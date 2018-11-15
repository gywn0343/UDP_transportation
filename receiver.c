#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUFLEN 1400
#define PACKET_LOSS_PROB 0
#define PORT 10080
#define SOCK_BUF_SIZE 10000000
#define FILE_NUM 10000
#define INT_DIGITS 19
#define INF 999999999

enum {FIN, DROP, SEND, RECV};

typedef struct ACK{
	char fileName[BUFLEN + 1];
	int ack;
	clock_t start;
}ACK;
ACK ack[FILE_NUM];
typedef struct MSG{
	char fileName[BUFLEN + 1];
	char buf[BUFLEN + 1];
	int seq;
	int ret;
}MSG;

void print()
{
	int i;
	printf("----------------\n");
	for(i=0;ack[i].ack != -1;i++)
	{
		printf("%s: %d\n", ack[i].fileName, ack[i].ack);
	}
	printf("----------------\n");
}
char* itoa(int i)
{
	static char buf[INT_DIGITS + 2];
	char* p = buf + INT_DIGITS + 1;
	do
	{
		*--p = '0' + (i % 10);
		i /= 10;
	} while(i != 0);
	return p;
}
ACK receive_file(char *fileName, char *buf, int seq, int length)
{
	int i, loc;
	int flag = 0;
	//printf("%s:\n%s\nseq: %d\n", fileName, buf, seq);
	printf("%s %d: %d %ld\n", fileName, seq, length, strlen(buf));
	for(i=0;i<FILE_NUM;i++)
	{
		if(strcmp(ack[i].fileName, fileName) == 0)
		{
			if(ack[i].ack + 1 == seq) ack[i].ack = seq; // is it sequential?
			else flag = 1;
			break;
		}
	}

	int fp;
	if(flag == 1) return ack[i]; // redundent sequence || some packets are missing
	if(seq == 0)
	{
		if((fp = open(fileName, O_RDWR | O_CREAT, 0755)) == -1)
		{
			perror("file create");
			exit(1);
		}
	}
	else
	{
		if((fp = open(fileName, O_RDWR | O_APPEND)) == -1)
		{
			perror("file open");
			exit(1);
		}
	}

	write(fp, buf, length);
	
	close(fp);
printf("%s %d\n", ack[i].fileName, ack[i].ack);
	return ack[i];
}

int isDrop()
{
	srand(clock());

	int ret = rand() % 100;
	printf("!!!!!!!!!%d %d!!!!!!!!11\n", ret, PACKET_LOSS_PROB);
	if(ret < PACKET_LOSS_PROB) return 1;
	return 0;
}
void erase_fileAck(char* fileName)
{
	int i, j;
	for(i=0;ack[i].ack != -1;i++)
	{
		if(strcmp(ack[i].fileName, fileName) == 0)
		{
			for(j=i;ack[j].ack != -1;j++)
			{
				ack[j] = ack[j+1];
			}
		}
	}
	return;
}
void write_log(char* logFileName, char* fileName, int num, int state)
{
	int i;
	for(i=0;ack[i].ack != -1;i++)
	{
		if(strcmp(ack[i].fileName, fileName) == 0) break;
	}
	if(ack[i].ack == -1)
	{
		ack[i].start = clock();
		strcpy(ack[i].fileName, fileName);
	}
	FILE* log = fopen(logFileName, "a");
	clock_t now = clock();
	double time = (double)(now - ack[i].start) / (CLOCKS_PER_SEC);
	if(state == DROP) 
		fprintf(log, "%.3f pkt: %d	|	dropped\n", time, num);
	else if(state == RECV) 	
		fprintf(log, "%.3f pkt: %d	|	received\n", time, num);
	else if(state == SEND)
		fprintf(log, "%.3f ACK: %d	|	sent\n", time, num);
	else
	{
		fprintf(log, "\nFile transfer is finished.\n");
	}
	fclose(log);
}

int main(void)
{
	struct sockaddr_in recv_addr, send_addr;
	pid_t pid;	
	int recv_sock, i, slen = sizeof(send_addr);
	char buf[BUFLEN + 1];
	int ret;
	int sock_buf_size;
	int new_sock_buf_size = SOCK_BUF_SIZE;
	socklen_t len;
	int sock_buf_len = sizeof(sock_buf_size);
	int bf = 1;
	
	//create a UDP socket
	if ((recv_sock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		perror("socket");
		exit(1);
	}
	if(getsockopt(recv_sock, SOL_SOCKET, SO_RCVBUF, (char*)&sock_buf_size, &sock_buf_len) < 0)
	{
		perror("getsockopt error");
		exit(1);
	}
	printf("packet loss probability: %.2f\n", PACKET_LOSS_PROB / 100.0);
	printf("socket recv buffer size: %d\n", sock_buf_size);

	if(setsockopt(recv_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&bf, (int)sizeof(bf)) < 0)
	{
		perror("setsockopt error(SO_REUSEADDR)");
		exit(1);
	}
	if(sock_buf_size < SOCK_BUF_SIZE)
	{
	printf("%d\n", new_sock_buf_size);
		if(setsockopt(recv_sock, SOL_SOCKET, SO_RCVBUF, (char*)&new_sock_buf_size, sizeof(new_sock_buf_size)) < 0)
		{
			perror("setsockopt error(SO_RCVBUF)");
			exit(1);
		}
	}
	if(getsockopt(recv_sock, SOL_SOCKET, SO_RCVBUF, (char*)&sock_buf_size, &sock_buf_len) < 0)
	{
		perror("getsockopt error");
		exit(1);
	}
	printf("socket recv buffer size updated: %d\n", sock_buf_size);
	memset((char *) &recv_addr, 0, sizeof(recv_addr));
	
	recv_addr.sin_family = AF_INET;
	recv_addr.sin_port = htons(PORT);
	recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	if( bind(recv_sock, (struct sockaddr*)&recv_addr, sizeof(recv_addr)) == -1)
	{
		perror("bind");
		exit(1);
	}
	printf("\nReceiver program started...\n");
	for(i=0;i<FILE_NUM;i++)
		ack[i].ack = -1;
	MSG* msg = (MSG*)malloc(sizeof(MSG));
	char logFileName[BUFLEN];

	while(1)
	{
		if (recvfrom(recv_sock, msg, sizeof(struct MSG), 0, (struct sockaddr *) &send_addr, &slen) == -1)
		{
			continue;
		}
		if(msg->ret == 0)
			continue;

		strcpy(logFileName, msg->fileName);
		strcat(logFileName, "_receiving_log.txt");
		if(msg->seq == INF)
		{
			erase_fileAck(msg->fileName);
			write_log(logFileName, msg->fileName, 0, FIN);
			continue;
		}
		else if(isDrop())
		{
			write_log(logFileName, msg->fileName, msg->seq, DROP);
			continue;
		}
		else
			write_log(logFileName, msg->fileName, msg->seq, RECV);
			
		ACK tmp = receive_file(msg->fileName, msg->buf, msg->seq, msg->ret);
print();
printf("sending...%s %d\n", tmp.fileName, tmp.ack);

		if (sendto(recv_sock, &tmp, sizeof(tmp), 0, (struct sockaddr*) &send_addr, slen) == -1)
		{
			perror("sendto");
			exit(1);
		}
		write_log(logFileName, tmp.fileName, tmp.ack, SEND);
	}
		
	

	close(recv_sock);
	return 0;
}

