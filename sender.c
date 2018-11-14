#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

enum {TIMEOUT, SENT, RECV, DUP_3, RETRANS, FIN};

#define BUFLEN 1400	//Max length of buffer
#define ACKLEN 21
#define INF 999999999
#define PORT 10080	//The port on which to send data
#define INT_DIGITS 19
#define DUP 3

int WIN;
float TIME;

typedef struct MSG{
	char fileName[BUFLEN + 1];
	char buf[BUFLEN + 1];
	int seq;
	int ret;
}MSG;
typedef struct ACK{
	char fileName[BUFLEN + 1];
	int ack;
}ACK;

char* itoa(int i)
{
	static char buf[INT_DIGITS + 2];
	char* p = buf + INT_DIGITS + 1;
	do
	{
		*--p = '0' + (i % 10);
		i /= 10;
	}while(i != 0);
	return p;
}

void write_log(char* logFileName, clock_t start, int num, int state, clock_t timeout_since)
{
	clock_t now = clock();
	double time = (double)((now - start)) / CLOCKS_PER_SEC;
	FILE* log = fopen(logFileName, "a");
	switch(state)
	{
		case SENT:
			fprintf(log, "%.3f pkt: %d	|	sent\n", time, num);
			break;
		case RETRANS:
			fprintf(log, "%.3f pkt: %d	|	retransmitted\n", time, num);
			break;
		case RECV:
			fprintf(log, "%.3f ack: %d	|	received\n", time, num);
			break;
		case TIMEOUT:
			fprintf(log, "%.3f pkt: %d	|	timeout since %.3f\n", time, num, (double)timeout_since / CLOCKS_PER_SEC);
			break;
		case DUP_3:
			fprintf(log, "%.3f pkt: %d	|	3 duplicated ACKs\n", time, num);
			break;
		case FIN:
			fprintf(log, "\nFile transfer is finished.\n");
			fprintf(log, "Throughput: %.2f pkts / sec\n", num / time);
			fprintf(log, "Goodput: %.2f pkts / sec\n", timeout_since / time);
			break;
	}
	fclose(log);
}
int slide_window(int ack, MSG **msg, int* msg_cnt)
{
	int i, j;
	int cnt = 0;
	MSG cur;
	for(i=0;i<WIN;i++)
	{
		if((*msg)[i].seq <= ack)
		{
			cnt++;
			for(j=i+1;j<WIN;j++)
			{
				(*msg)[j-1] = (*msg)[j];
			}
			(*msg)[WIN-1].seq = INF;
			i = -1;
		}
	}
	*msg_cnt -= cnt;
	return cnt;
}
int isOver(MSG* msg)
{
	return msg[0].seq == INF;
}
void print(MSG* msg)
{
	int i;
	for(i=0;i<WIN;i++)
	{
		if(msg[i].seq == INF) printf("INF: ");
		else printf("%d: ", msg[i].seq);
	}
		printf("\n---------------\n");
}
void do_fileSend(int send_sock, struct sockaddr_in recv_addr, int slen, char* fileName)
{
	int ret;
	int cnt = 0;
	int i, fp;
	MSG *msg = (struct MSG*)malloc(sizeof(struct MSG) * WIN);
	char buf[BUFLEN + 1];
	char ack[ACKLEN + 1];
	char tmp_ack[ACKLEN + 1];
	int msg_cnt = 0;
	int p_win = 0;
	int seq = 0;
	int flag = 0;
	clock_t start = clock();
	clock_t now;
	double last;
	clock_t very_start_time = clock();
	int prev_ack;
	int dup = 0;
	int throu_cnt = 0;
	int good_cnt = 0;
	char log[BUFLEN];
	strcpy(log, fileName);
	strcat(log, "_sending_log.txt");
	
	if((fp = open(fileName, O_RDONLY)) == -1)
	{
		perror("File open");
		exit(1);
	}
	int fd[2];
	if(pipe(fd) == -1)
	{
		perror("pipe");
		exit(1);
	}
	pid_t pid;
	if((pid = fork()) < 0) 
	{
		perror("fork");
		exit(1);
	}
	if(pid > 0) // parent
	{
		close(fd[1]);
		MSG *cur_msg = (struct MSG*)malloc(sizeof(struct MSG));
		while(1)
		{
printf("flag = %d\n", flag);
print(msg);
			if(flag == 0 || flag == 3)
			{
printf("msg_cnt = %d\n", msg_cnt);
				for(;msg_cnt < WIN;msg_cnt++)
				{
					memset(buf, 0, BUFLEN+1);
					if((ret = read(fp, buf, BUFLEN)) <= 0)
					{
						close(fp);
						flag = 1;
						break;
					}
					cur_msg = &(msg[msg_cnt]);
					strcpy(cur_msg->fileName, fileName);
					strcpy(cur_msg->buf, buf);
					cur_msg->ret = ret;
					cur_msg->seq = seq++;
print(msg);
printf("%s %d: %d\n", cur_msg->fileName, cur_msg->seq, cur_msg->ret);
					sendto(send_sock, cur_msg, sizeof(struct MSG), 0, (struct sockaddr*)&recv_addr, slen);
					throu_cnt++;
					good_cnt++;
					write_log(log, very_start_time, cur_msg->seq, SENT, 0);
				}
			}
			if(flag == 2 || flag == 4)
			{
printf("resending data in window...\n");
				int tmp_cnt = 0;
				for(i=0;i<WIN;i++)
				{
					cur_msg = &(msg[i]);
					if(cur_msg->seq == INF) break;
					sendto(send_sock,cur_msg, sizeof(struct MSG), 0, (struct sockaddr*)&recv_addr, slen);
					throu_cnt++;
					if(flag == 2)
					{
						if(tmp_cnt++ == 0)
							write_log(log, very_start_time, cur_msg->seq, TIMEOUT, start);
						write_log(log, very_start_time, cur_msg->seq, RETRANS, 0);
					}
					else if(flag == 4)
					{
						if(tmp_cnt++ == 0)
							write_log(log, very_start_time, cur_msg->seq, DUP_3, 0);
						write_log(log, very_start_time, cur_msg->seq, SENT, 0);
					}
				}
				flag = 0;	
			}
print(msg);
			if(flag == 1 && isOver(msg))
			{
printf("Bye!\n");
				kill(pid, SIGINT);
				write_log(log, very_start_time, throu_cnt, FIN, good_cnt);
				sendto(send_sock,cur_msg, sizeof(struct MSG), 0, (struct sockaddr*)&recv_addr, slen);
				return;
			}
			now = clock();
			last = (double)(now - start) / CLOCKS_PER_SEC;
printf("last: %f time: %f\n", last, TIME);
			if(last > TIME && flag == 3)
			{
printf("timer error\n");
				flag = 2;
				start = clock();
				continue;
			}
			else if(last > TIME)
			{
				flag = 3;
				start = clock();
			}
		
			while(1)
			{
				now = clock();
				last = (double)(now - start) / CLOCKS_PER_SEC;
				if(last > TIME) break;
				memset(ack, 0, ACKLEN+1);
printf("I am stuck here! 1\n");
				if(read(fd[0], ack, ACKLEN) == 0) continue;
				if(*ack != 0) break;
			}
printf("ack in parent: <%s>\n", ack);
			write_log(log, very_start_time, atoi(ack), RECV, 0);
		
			
			//if(*ack == 0) continue;
			if(atoi(ack) == prev_ack) 
			{
				if(++dup == DUP) 
				{
printf("3 dup error\n");
					flag = 4; // re-send data in window
					dup = 0;
				}
			}
			else dup = 0;
			prev_ack = atoi(ack);
printf("ack in parent: %s %d\n", ack, atoi(ack));
			ret = slide_window(atoi(ack), &msg, &msg_cnt);
			if(ret > 0) flag = 0; // window slided
		}
	}
	else if(pid == 0) // child
	{
		close(fd[0]);
		ACK *Ack = (ACK*)malloc(sizeof(ACK));
		char send_ack[ACKLEN + 1] = "0";
		char null_ack = 0;
		while(1)
		{
			
			memset(ack, 0, ACKLEN + 1);
			if(recvfrom(send_sock, Ack, sizeof(ACK), 0, (struct sockaddr*)&recv_addr, &slen) == -1)
				continue;
			usleep(3000);
			if(atoi(send_ack) < Ack->ack);
			strcpy(send_ack, itoa(Ack->ack));
printf("receiving...%s %s %s\n", fileName, Ack->fileName, send_ack);
			if(strcmp(fileName, Ack->fileName) != 0) 
			{
				write(fd[1], &null_ack, 1);
				continue; // ack for another file
			}
			write(fd[1], send_ack, sizeof(send_ack));
printf("ack in child: %s\n", send_ack);
		}
	}
}

int main(void)
{
	struct sockaddr_in recv_addr;
	int send_sock, i, slen=sizeof(recv_addr);
	char message[BUFLEN];
	char IP[20], ret;

	if ((send_sock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		perror("socket errors");
		exit(1);
	}

	printf("Receiver IP address: ");
	ret = scanf("%s", IP);
	printf("window size: ");
	ret = scanf("%d", &WIN);
	printf("timeout (sec): ");
	ret = scanf("%f", &TIME);

	memset((char *)&recv_addr, 0, sizeof(recv_addr));
	recv_addr.sin_family = AF_INET;
	recv_addr.sin_port = htons(PORT);
	recv_addr.sin_addr.s_addr= inet_addr(IP);
	
	if (inet_aton(IP, &recv_addr.sin_addr) == 0) 
	{
		perror("inet_aton() error");
		exit(1);
	}

	char* fileName = (char*)malloc(sizeof(char)*BUFLEN);
	int pid;
	while(1)
	{
		memset(fileName, 0, BUFLEN);
		printf("file name: ");
		scanf("%s", fileName);
		
		if((pid = fork()) < 0)
		{
			puts("fork error");
			return -1;
		}
		if(pid == 0)  // child
		{
			do_fileSend(send_sock, recv_addr, slen, fileName);
		}
	}
	close(send_sock);
	return 0;
}

