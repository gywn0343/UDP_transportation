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
void slide_window(int ack, MSG **msg, int* msg_cnt)
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
	int i, fp;
	MSG *msg = (struct MSG*)malloc(sizeof(struct MSG) * WIN);
	char buf[BUFLEN + 1];
	char ack[ACKLEN + 1];
	int msg_cnt = 0;
	int p_win = 0;
	int seq = 0;
	int flag = 0;
	clock_t start = clock();
	clock_t now;
	float last;
	int prev_ack;
	int dup = 0;
	if((fp = open(fileName, O_RDONLY)) == -1)
	{
		perror("File open");
		exit(1);
	}
	int fd[2];
	int fd1[2];
	if(pipe(fd) == -1 || pipe(fd1) == -1)
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
	//	close(fd1[0]);
		MSG *cur_msg = (struct MSG*)malloc(sizeof(struct MSG));
		while(1)
		{
printf("flag = %d\n", flag);
print(msg);
			if(flag == 0)
			{
				for(;msg_cnt < WIN;msg_cnt++)
				{
					memset(buf, 0, BUFLEN+1);
					if((ret = read(fp, buf, BUFLEN)) <= 0)
					{
						close(fp);
						flag = 1;
						break;
					}
printf("buf size: %d\n", ret);
					cur_msg = &(msg[msg_cnt]);
					strcpy(cur_msg->fileName, fileName);
					strcpy(cur_msg->buf, buf);
					cur_msg->ret = ret;
					cur_msg->seq = seq++;
printf("%s %d: %d\n", cur_msg->fileName, cur_msg->seq, cur_msg->ret);
					sendto(send_sock, cur_msg, sizeof(struct MSG), 0, (struct sockaddr*)&recv_addr, slen);
				}
			}
			if(flag == 2)
			{
printf("resending data in window...\n");
				for(i=0;i<WIN;i++)
				{
					cur_msg = &(msg[i]);
					if(cur_msg->seq == INF) break;
					sendto(send_sock, cur_msg, sizeof(struct MSG), 0, (struct sockaddr*)&recv_addr, slen);
				}
				flag = 0;	
			}
print(msg);
			if(flag == 1 && isOver(msg))
			{
printf("Bye!\n");
				return;
			}
			now = clock();
			last = now - start;
printf("last: %f time: %f\n", last, TIME);
			if(last > TIME)
			{
printf("timer error\n");
				flag = 2;
				start = clock();
				continue;
			}
			memset(ack, 0, ACKLEN+1);
			if(read(fd[0], ack, ACKLEN) == 0) continue;
			if(*ack == 0) continue;
			if(atoi(ack) == prev_ack) 
			{
printf("3 dup error\n");
				if(++dup == DUP) 
				{
					flag = 2; // re-send data in window
					dup = 0;
				}
			}
			else dup = 0;
			prev_ack = atoi(ack);
printf("ack in parent: %s %d\n", ack, atoi(ack));
			slide_window(atoi(ack), &msg, &msg_cnt);
		}
	}
	else if(pid == 0) // child
	{
		close(fd[0]);
	//	close(fd1[1]);
		ACK *Ack = (ACK*)malloc(sizeof(ACK));
		char null_ack = 0;
		while(1)
		{
			
			memset(ack, 0, ACKLEN + 1);
			if(recvfrom(send_sock, Ack, sizeof(ACK), 0, (struct sockaddr*)&recv_addr, &slen) == -1)
				continue;
			//usleep(3000);
printf("%s\n", Ack->fileName);
			if(strcmp(fileName, Ack->fileName) != 0) 
			{
				write(fd[1], &null_ack, 1);
				continue; // ack for another file
			}
			strcpy(ack, itoa(Ack->ack));
			write(fd[1], ack, sizeof(ack));
printf("ack in child: %s\n", ack);
		//	if(read(fd1[0], get_flag, 7) == 0) continue;
		//	if(strcmp(get_flag, "close!") == 0) return;
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
	TIME *= 1000;

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
		//send file name
		if (sendto(send_sock, fileName, strlen(fileName), 0, (struct sockaddr*)&recv_addr, slen)==-1)
		{
			perror("sendto error");
			exit(1);
		}
		
		if((pid = fork()) < 0)
		{
			puts("fork error");
			return -1;
		}
		if(pid == 0)  // child
		{
			do_fileSend(send_sock, recv_addr, slen, fileName);
		}

		
		//memset(buf,'\0', BUFLEN);
		//try to receive some data, this is a blocking call
		/*if (recvfrom(send_sock, buf, BUFLEN, 0, (struct sockaddr*)&recv_addr, &slen) == -1)
		{
			perror("recvfrom error");
			exit(1);
		}
		
		puts(buf);*/
	}
	close(send_sock);
	return 0;
}

