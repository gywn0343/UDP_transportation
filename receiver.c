#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUFLEN 1400
#define PORT 10080
#define SOCK_BUF_SIZE 10000000
#define RECV_BUF_SIZE 200
#define PORT_NUM 50
#define INT_DIGITS 19
#define INF 999999999

enum {FIN, DROP, SEND, RECV};
typedef struct ACK{
	int portNum;
	int ack;
	int loc;
	double start;
}ACK;
ACK ack[PORT_NUM];
typedef struct MSG{
	char buf[BUFLEN + 1];
	int seq;
	double time;
}MSG;
MSG msg[PORT_NUM][RECV_BUF_SIZE];

double get_time()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	double t = tv.tv_sec;
	t += tv.tv_usec / 1000000.0;
	return t;
}
char* itoa(int i)
{
	static char buf[INT_DIGITS + 2];
	char* p = buf + INT_DIGITS + 1;
	if(i >= 0)
	{
		do
		{
			*--p = '0' + (i % 10);
			i /= 10;
		} while(i != 0);
		return p;
	}
	else
	{
		do
		{
			*--p = '0' - (i % 10);
			i /= 10;
		}while(1 != 0);
		*--p = '-';
		return p;
	}
}
ACK receive_file(MSG* tmp_msg, int portNum, int seq)
{
	int i, loc, j;
	int flag = 0;
	for(i=0;i<PORT_NUM;i++)
	{
		if(ack[i].portNum == portNum)
		{
			if(ack[i].ack + 1 == seq)   // sequential input
			{
				ack[i].ack = seq; 
			}
			else if(ack[i].ack >= seq)
			{
				flag = 1;
			}
			else  // something is dropped
			{
				flag = 1;
				for(j=0;j<ack[i].loc;j++)
				{
					if(seq == msg[i][j].seq) return ack[i];
				}
				msg[i][(ack[i].loc)++] = *tmp_msg;
			}
			break;
		}
		else if(ack[i].portNum == -1 && seq == 0)
		{
			ack[i].ack = seq;
			ack[i].portNum = portNum;
		}
	}


	if(flag == 1) return ack[i]; // redundent sequence || some packets are missing
	else
	{
		while(1)
		{
			if(msg[i][0].seq == seq + 1)
			{
				ack[i].ack = msg[i][0].seq;
				ack[i].loc--;
				for(j=0;msg[i][j].seq != -2;j++)
				{
					msg[i][j] = msg[i][j+1];
				}
				seq++;
			}
			else break;
		}
	}
	
	return ack[i];
}

int erase_fileAck(int portNum)
{
	int i, j;
	for(i=0;ack[i].ack != -1;i++)
	{
		if(ack[i].portNum == portNum)
		{
			for(j=i;ack[j].ack != -1;j++)
			{
				ack[j] = ack[j+1];
			}
			for(j=0;j<110;j++)
			{
				msg[i][j].seq = -2;
			}
		}
	}
	return i;
}


int main(void)
{
	struct sockaddr_in recv_addr, send_addr;
	pid_t pid;	
	int recv_sock, i, slen = sizeof(send_addr);	
	int recv_len = sizeof(send_addr);	
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
	printf("socket recv buffer size: %d\n", sock_buf_size);

	if(setsockopt(recv_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&bf, (int)sizeof(bf)) < 0)
	{
		perror("setsockopt error(SO_REUSEADDR)");
		exit(1);
	}
	if(sock_buf_size < SOCK_BUF_SIZE)
	{
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
	int j;
	for(i=0;i<PORT_NUM;i++)
	{
		ack[i].ack = -1;
		ack[i].portNum = -1;
		for(j=0;j<RECV_BUF_SIZE;j++)
		{
			msg[i][j].seq = -2;
		}
	}
	MSG* tmp_msg = (MSG*)malloc(sizeof(MSG));
	char logFileName[BUFLEN];
	int loc;

	unsigned int portNum;
	while(1)
	{
		if (recvfrom(recv_sock, tmp_msg, sizeof(struct MSG), 0, (struct sockaddr *) &send_addr, &slen) < 0)
		{
			perror("recv");
			exit(1);
		}
		getsockname(recv_sock, (struct sockaddr *)&recv_addr, &recv_len);
		portNum = ntohs(send_addr.sin_port);
printf("%d: %d\n", portNum, tmp_msg->seq);
		if(tmp_msg->seq == INF)
		{
			erase_fileAck(portNum);
			continue;
		}

		ACK tmp = receive_file(tmp_msg, portNum, tmp_msg->seq);
printf("ACK: %d (%d)\n", tmp.ack, tmp.portNum);
		if (sendto(recv_sock, &tmp, sizeof(tmp), 0, (struct sockaddr*) &send_addr, slen) == -1)
		{
			perror("sendto");
			exit(1);
		}
	}
		
	

	close(recv_sock);
	return 0;
}

