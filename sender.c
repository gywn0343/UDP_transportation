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

int WIN;
int TIME;

typedef struct MSG{
	char fileName[BUFLEN];
	char buf[BUFLEN];
	int seq;
}MSG;

void slide_window(int ack, MSG **msg, int* msg_cnt, int* p_msg)
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
		}
	}
	*msg_cnt -= cnt;
	*p_msg = WIN - 1 - cnt;
}
int isOver(MSG* msg)
{
	return msg[0].seq == INF;
}
void do_fileSend(int send_sock, struct sockaddr_in recv_addr, int slen, char* fileName)
{
	int ret;
	int i, fp;
	MSG *msg = (struct MSG*)malloc(sizeof(struct MSG) * WIN);
	char buf[BUFLEN + 1];
	char ack[ACKLEN + 1];
	int p_msg = 0;
	int msg_cnt = 0;
	int p_win = 0;
	int seq = 0;
	clock_t start = clock();
	clock_t now;
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
	pid_t pid = fork();
	if(pid < 0) 
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
			for(;msg_cnt < WIN;msg_cnt++)
			{
printf("%d %d\n", msg_cnt, WIN);
				if(read(fp, buf, BUFLEN) == 0)
				{
					close(fp);
					break;
				}
				cur_msg = &(msg[p_msg + msg_cnt]);
				strcpy(cur_msg->fileName, fileName);
				strcpy(cur_msg->buf, buf);
				cur_msg->seq = seq++;
printf("%s: %d\n", msg[p_msg + msg_cnt].fileName, msg[p_msg + msg_cnt].seq);
printf("%s: %d\n", cur_msg->fileName, cur_msg->seq);
				sendto(send_sock, cur_msg, sizeof(struct MSG), 0, (struct sockaddr*)&recv_addr, slen);
			}	
printf("window...\n");
for(i=0;i<WIN;i++)
{
	printf("%s", msg[i].buf);
}
			read(fd[0], ack, ACKLEN);
printf("ack in parent: %s\n", ack);
			slide_window(atoi(ack), &msg, &msg_cnt, &p_msg);
			if(isOver(msg))
			{
				close(fp);
				return;
			}
		}
	}
	else if(pid == 0) // child
	{
		close(fd[0]);
		while(1)
		{
			if(recvfrom(send_sock, ack, ACKLEN, 0, (struct sockaddr*)&recv_addr, &slen) == -1)
				continue;
			write(fd[1], ack, sizeof(ack));
printf("ack in child: %s\n", ack);
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
	ret = scanf("%d", &TIME);

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

