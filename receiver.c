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

int income=0;
int forward=0;
int avgQueue=0;
int avgCnt=0;
int throu[PORT_NUM];
int portCnt=0;

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
typedef struct MSG2RM{
	int seq;
	struct sockaddr_in send_addr;
}MSG2RM;
//MSG msg[PORT_NUM][RECV_BUF_SIZE];
int flag = -1;

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

int isFirstConnection(int portNum)
{
	int i;
	for(i=0;i<portCnt;i++)
	{
		if(ack[i].portNum == portNum) return 0;
	}
	ack[portCnt].portNum = portNum;
	return 1;
}
ACK receive_file(MSG* tmp_msg, int portNum, int seq)
{
	int i, loc, j;
	int flag = 0;
	for(i=0;i<portCnt;i++)
	{
		if(ack[i].portNum == portNum)
		{
			if(ack[i].ack + 1 == seq)   // sequential input
			{
				ack[i].ack = seq; 
			}
			else if(ack[i].ack >= seq)
			{
				//flag = 1;
			}
			else  // something is dropped
			{
				//flag = 1;
				/*for(j=0;j<ack[i].loc;j++)
				{
					if(seq == msg[i][j].seq) return ack[i];
				}
				msg[i][(ack[i].loc)++] = *tmp_msg;*/
			}
			break;
		}
	}


	/*if(flag == 1) return ack[i]; // redundent sequence || some packets are missing
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
	}*/
	
	return ack[i];
}

void erase_port(int n)
{
	int j;
printf("erasing %d\n", ack[n].portNum);
	for(j=n;ack[j].ack != -1;j++)
	{
		ack[j] = ack[j+1];
		throu[j] = throu[j+1];
	}
	portCnt--;
printf("portCnt: %d\n", portCnt);
	flag = -1;
}

MSG2RM *queue;
int q_cnt = 0;
int BLR, queue_size;
int flush_queue(int fd)
{
	int i;
	for(i=0;i<q_cnt;i++)
	{
		write(fd, queue + i, sizeof(struct MSG2RM));
	}
	int ret = q_cnt;
	q_cnt = 0;
	return ret;
}
void check_queue(MSG* tmp_msg, struct sockaddr_in send_addr)
{
//printf("seq in queue check..%d\n", tmp_msg->seq);
	int drop_rate;
	double u_rate = (double)q_cnt / (double)queue_size * 100; 
	drop_rate = u_rate;
	

	int ret = rand() % 100;
	if(ret < drop_rate) // drop
		return;
	else
	{
		queue[q_cnt].seq = tmp_msg->seq;
		queue[q_cnt++].send_addr = send_addr;
	}

}
void write_log_NEM(double start, double t1, double t2)
{
	double time2 = t1 - t2; // almost 2sec
	double time = t1 - start;
	
	FILE* log = fopen("NEM.log", "a");
	fprintf(log, "%.3lf	|	%.3lf	|	%.3lf	|	%.3lf	\n", 
				time, 
				income/time2, 
				forward/time2, 
				(double)avgQueue/avgCnt/100);
	fclose(log);
}
void write_log_RM(double start, double t1, double t2)
{
	double jain=0;
	double time = t1 - t2;
	int sum = 0, sum2 = 0;
	int i;
	FILE* log = fopen("RM.log", "a");
	if(portCnt > 0)
	{
		for(i=0;i<portCnt;i++)
		{
			if(throu[i] == 0) erase_port(i);
		}
		for(i=0;i<portCnt;i++)	
		{
			sum += throu[i];
		}
		for(i=0;i<portCnt;i++)
		{
			sum2 += throu[i] * throu[i];
		}
		jain = (double)sum * sum / (portCnt * sum2);
	}
	
	fprintf(log, "%.3lf	|	%.3lf\n", t1 - start, jain);
	for(i=0;i<portCnt;i++)
	{
		fprintf(log, "		|	%d	|	%.3lf\n", /*ip,*/ ack[i].portNum, throu[i] / time);
	}
	fprintf(log, "-----------------------\n");
	fclose(log);
}
int main(void)
{
	struct sockaddr_in recv_addr, send_addr;
	pid_t pid;	
	srand((unsigned)time(NULL));
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
		//for(j=0;j<RECV_BUF_SIZE;j++)
		//{
		//	msg[i][j].seq = -2;
		//}
	}
	MSG* tmp_msg = (MSG*)malloc(sizeof(MSG));
	char logFileName[BUFLEN];
	int loc;

	unsigned int portNum;

	double now_time;

	printf("configure>>");
	scanf("%d %d", &BLR, &queue_size);
	queue = (MSG2RM*)malloc(sizeof(MSG2RM)*queue_size);
	char myIP[16];
	int fd[2];
	MSG2RM msg2rm;
	if(pipe(fd) == -1)
	{
		perror("pipe");
		exit(1);
	}

	if((pid = fork()) < 0) 
	{
		perror("fork");
		exit(1);
	}
	else if(pid > 0) // RM
	{
		close(fd[1]);
		double very_start_time = get_time();
		double start_time_2sec = get_time();
		while(1)
		{

			read(fd[0], &msg2rm, sizeof(struct MSG2RM));
			
//printf("msg from NEM: %d\n", msg2rm.seq);
			send_addr = msg2rm.send_addr;
			//getsockname(recv_sock, (struct sockaddr *)&recv_addr, &recv_len);
			portNum = ntohs(send_addr.sin_port);
			if(isFirstConnection(portNum))
			{
				portCnt++;
			}
			for(i=0;i<PORT_NUM;i++)
			{
//printf("ack[%d].portNum = %d\n", i, ack[i].portNum);
				if(ack[i].portNum == portNum)
				{
//printf("throu[%d] = %d\n", i, throu[i]);
					throu[i] += 1;
//printf("throu[%d] = %d\n", i, throu[i]);
					break;

				}
			}

			ACK tmp = receive_file(tmp_msg, portNum, msg2rm.seq);
//printf("ACK: %d (%d)\n", tmp.ack, tmp.portNum);
			if (sendto(recv_sock, &tmp, sizeof(tmp), 0, (struct sockaddr*) &send_addr, slen) == -1)
			{
				perror("sendto");
				exit(1);
			}
			now_time = get_time();
			if(now_time >= start_time_2sec + 2)
			{
//printf("hear!\n");
				write_log_RM(very_start_time, now_time, start_time_2sec);
				start_time_2sec = get_time();
				for(i=0;i<portCnt;i++)
					throu[i] = 0;
			}
		}
	}
	else // NEM
	{
		double very_start_time = get_time();
		double start_time_1sec = get_time();
		double start_time_2sec = get_time();
		double start_time_100m = get_time();
		close(fd[0]);	
		int CNT = 0;
		FILE* fp = fopen("NEM.log", "w");
		fprintf(fp, "time	|	income	|	forward	|	avg_queue_utilization\n");
		fclose(fp);
		while(1)
		{


			if (recvfrom(recv_sock, tmp_msg, sizeof(struct MSG), 0, (struct sockaddr *) &send_addr, &slen) < 0)
			{
				perror("recv");
				exit(1);
			}
			income++;
			CNT++;
			if(CNT > BLR)
			{
				check_queue(tmp_msg, send_addr);
			}
			else
			{
				msg2rm.seq = tmp_msg->seq;
				msg2rm.send_addr = send_addr;
				write(fd[1], &msg2rm, sizeof(struct MSG2RM));
				forward++;
				usleep(300);
			}
			now_time = get_time();
			if(now_time >= start_time_100m + 0.1)
			{
				avgQueue += q_cnt;
				avgCnt++;
				start_time_100m = get_time();
			}
			if(now_time >= start_time_1sec + 1) // 1 sec later..
			{
				start_time_1sec = get_time();
				CNT = 0;
				CNT += flush_queue(fd[1]);
			}
			if(now_time >= start_time_2sec + 2)
			{
				write_log_NEM(very_start_time, now_time, start_time_2sec);
				start_time_2sec = get_time();				
				income = 0; forward = 0; avgQueue = 0; avgCnt = 0;
			}
		}
	}


	close(recv_sock);
	return 0;
}

