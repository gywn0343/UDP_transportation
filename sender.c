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

char *itoa(int i)
{
	static char buf[INT_DIGITS + 2];
	char *p = buf + INT_DIGITS + 1; 
	if (i >= 0) 
	{
		do {
			*--p = '0' + (i % 10);
			i /= 10;
		} while (i != 0);
		return p;
	}
	else 
	{ 
		do {
			*--p = '0' - (i % 10);
			i /= 10;
		} while (i != 0);
		*--p = '-';
	}
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
	int prev_ack = -1;
	int dup = 0;
	int throu_cnt = 0;
	int good_cnt = 0;
	char log[BUFLEN];
	char m_one[] = "-1";
	strcpy(log, fileName);
	strcat(log, "_sending_log.txt");

	for(i=0;i<WIN;i++)
		msg[i].seq = INF;
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
	pid_t pid_time;
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
			if(flag == 0) // read file & send it
			{
				for(;msg_cnt < WIN;msg_cnt++)
				{
					cur_msg = &(msg[msg_cnt]);
					memset(cur_msg->buf, 0, BUFLEN+1);
					if((ret = read(fp, cur_msg->buf, BUFLEN)) <= 0)
					{
						close(fp);
						flag = 1;
						break;
					}
					strcpy(cur_msg->fileName, fileName);
					cur_msg->ret = ret;
					cur_msg->seq = seq++;
					sendto(send_sock, cur_msg, sizeof(struct MSG), 0, (struct sockaddr*)&recv_addr, slen);
					throu_cnt++;
					good_cnt++;
					write_log(log, very_start_time, cur_msg->seq, SENT, 1);
				}
			}
			else if(flag == 4)
			{
				cur_msg = &(msg[0]);
				sendto(send_sock,cur_msg, sizeof(struct MSG), 0, (struct sockaddr*)&recv_addr, slen);
				throu_cnt++;
				write_log(log, very_start_time, cur_msg->seq, DUP_3, 0);
				write_log(log, very_start_time, cur_msg->seq, SENT, 2);
				flag = 0;	
			}
			if(flag == 1 && isOver(msg))
			{
				kill(pid, SIGINT);
				write_log(log, very_start_time, throu_cnt, FIN, good_cnt);
				sendto(send_sock,cur_msg, sizeof(struct MSG), 0, (struct sockaddr*)&recv_addr, slen);
				close(fp);
				return;
			}
			now = clock();
			last = (double)(now - start) / CLOCKS_PER_SEC;
			if(last > TIME)
			{
				start = clock();
			}

			if((pid_time = fork()) < 0)
			{
				perror("fork");
				exit(1);
			}
			if(pid_time > 0) // waits for ack
			{
				while(1)
				{
					memset(ack, 0, ACKLEN+1);
					read(fd[0], ack, ACKLEN);
					if(*ack == 0 || *ack == '*') continue;
					for(i=0;i<strlen(ack);i++)
					{
						if(ack[i] == '-') 
						{
							memset(ack, 0, ACKLEN + 1); strcpy(ack, m_one);			
							break;
						}
					}
					if(strcmp(ack, m_one) == 0) break;
					else if(strlen(ack) > strlen(itoa(prev_ack)))
					{
						for(i=0;i<WIN;i++)
						{
							if(msg[i].seq == atoi(ack)) break;
						}
						if(i == WIN)
						{
							memset(ack, 0, ACKLEN + 1);
							strcpy(ack, itoa(prev_ack));
						}
						
					}
					break;
				}
				kill(pid_time, SIGINT);
			}
			else if(pid_time == 0) // timer
			{
				flag = 0;
				while(1)
				{
					now = clock();
					last = (double)(now - start) / CLOCKS_PER_SEC;
					if(last > TIME)
					{
						if(flag == 2)
						{		
							cur_msg = &(msg[0]);
							sendto(send_sock,cur_msg, sizeof(struct MSG), 0, (struct sockaddr*)&recv_addr, slen);
							throu_cnt++;

							write_log(log, very_start_time, cur_msg->seq, TIMEOUT, start);
							write_log(log, very_start_time, cur_msg->seq, RETRANS, 0);
							break;
						}
						else 
						{
							flag = 2;
							start = clock();
						}
					}
				}
				return;
			}
			now = clock();
			last = (double) (now - start) / CLOCKS_PER_SEC;
			if(last > TIME) dup = 0;
			write_log(log, very_start_time, atoi(ack), RECV, 0);
		
			
			if(atoi(ack) == prev_ack) 
			{
				if(++dup == DUP) 
				{
					flag = 4; // re-send data in window
				}
			}
			else dup = 0;
			prev_ack = atoi(ack);
			ret = slide_window(atoi(ack), &msg, &msg_cnt);
			if(ret > 0) flag = 0; // window slided
		}
	}
	else if(pid == 0) // child
	{
		close(fd[0]);
		ACK *Ack = (ACK*)malloc(sizeof(ACK));
		char null_ack = '*';
		char send_ack[ACKLEN+1];
		while(1)
		{
			
			memset(ack, 0, ACKLEN + 1);
			if(recvfrom(send_sock, Ack, sizeof(ACK), 0, (struct sockaddr*)&recv_addr, &slen) == -1)
				continue;
			usleep(300);
			strcpy(send_ack, itoa(Ack->ack));
			if(strcmp(fileName, Ack->fileName) != 0) 
			{
				write(fd[1], &null_ack, 1);
				continue; // ack for another file
			}
			write(fd[1], send_ack, strlen(send_ack));
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
	int pid = 0;
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
			return 0;
		}
	}
	close(send_sock);
	return 0;
}

