#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUFLEN 1400	//Max length of buffer
#define ACKLEN 21
#define INF 999999999
#define PORT 10080	//The port on which data is sent
#define INT_DIGITS 19
#define DUP 3

int WIN;
float TIME;
double throu = 0;
double good = 0;
double avgRTT = 0.05;
double sampleRTT, devRTT;

typedef struct MSG{
	char buf[BUFLEN + 1];
	int seq;
	double sent_time;
}MSG;
typedef struct ACK{
	int portNum;
	int ack;
	int loc;
	double start;
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
double get_time()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	double t = tv.tv_sec;
	t += tv.tv_usec / 1000000.0;
	return t;
}
void write_log(char* logFileName, double start, double t1, double t2)
{
	double now = get_time();
	double time = now - start;
	double time2 = t1 - t2;
	FILE* log = fopen(logFileName, "a");
	fprintf(log, "%.3lf	|	%.3lf	|	%.3lf	|%.3lf	|\n", time, avgRTT, throu/time2, good/time2);
	fclose(log);
}
void update_timer(double sent_time)
{
	sampleRTT = get_time() - sent_time;
	avgRTT = (1 - 0.125) * avgRTT + 0.125 * sampleRTT;
	devRTT = (1 - 0.25) * devRTT + 0.25 * (sampleRTT - avgRTT);
	TIME = avgRTT + 4 * devRTT;
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
			update_timer((*msg)[i].sent_time);
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


void do_fileSend(int send_sock, struct sockaddr_in recv_addr, int slen, char* FileName)
{
	double very_start_time = get_time();
	int ret;
	int cnt = 0;
	int i, fp;
	MSG *msg = (struct MSG*)malloc(sizeof(struct MSG) * WIN);
	char buf[BUFLEN + 1];
	char ack[ACKLEN + 1];
	char tmp_ack[ACKLEN + 1];
	int msg_cnt = 0;
	int seq = 0;
	int flag = 0;
	double start = get_time();
	double now;
	double two_sec_before = get_time();
	double last;
	
	int prev_ack = -1;
	int dup = 0;

	char m_one[] = "-1";


	for(i=0;i<WIN;i++)
		msg[i].seq = INF;
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
			now = get_time();
			if(now >= two_sec_before + 2)
			{
				write_log(FileName, very_start_time, now, two_sec_before);
				two_sec_before = now; throu = 0; good = 0;
			}
			if(flag == 0) // read file & send it
			{
				for(;msg_cnt < WIN;msg_cnt++)
				{
					cur_msg = &(msg[msg_cnt]);
					cur_msg->seq = seq++;
					strcpy(cur_msg->buf, itoa(cur_msg->seq));
					cur_msg->sent_time = get_time();
					sendto(send_sock, cur_msg, sizeof(struct MSG), 0, (struct sockaddr*)&recv_addr, slen);
printf("msg: %d\n", cur_msg->seq);
					throu++;
				}
			}
			else if(flag == 4)  //3 dup
			{
				/* AIMD or CUBIC -> shrink window size */
				cur_msg = &(msg[0]);
printf("3 dup ack\n");
printf("msg: %d\n", cur_msg->seq);
				sendto(send_sock,cur_msg, sizeof(struct MSG), 0, (struct sockaddr*)&recv_addr, slen);
				flag = 0;
				throu++;	
			}
			now = get_time();
			last = now - start;
			if(last > TIME)
			{
				start = get_time();
				dup = 0;
			}
			if((pid_time = fork()) < 0)
			{
				perror("fork");
				exit(1);
			}
			struct sigaction sa;
			sa.sa_handler = SIG_IGN;
			sa.sa_flags = 0;
			sigaction(SIGCHLD, &sa, NULL);
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
printf("ACK: %s\n", ack);
				good++;
				kill(pid_time, SIGINT);
			}
			else if(pid_time == 0) // timer
			{
				flag = 0;
				while(1)
				{
					now = get_time();
					last = now - start;
					if(last > TIME)
					{
						if(flag == 2)
						{		
							cur_msg = &(msg[0]);
							/* AIMD or CUBIC -> shrink window size */
printf("time out\n");
printf("msg: %d\n", cur_msg->seq);
							sendto(send_sock,cur_msg, sizeof(struct MSG), 0, (struct sockaddr*)&recv_addr, slen);
							throu++;
							break;
						}
						else 
						{
							flag = 2;
							start = get_time();
						}
					}
				}
				return;
			}
			now = get_time();
			last = now - start;
			if(last > TIME) dup = 0;
		
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
printf("child ACK: %d\n", Ack->ack);
			strcpy(send_ack, itoa(Ack->ack));
			write(fd[1], send_ack, strlen(send_ack));
		}
	}
}

int main(void)
{
	struct sockaddr_in recv_addr;
	struct sockaddr_in send_addr;
	int send_sock, i, slen=sizeof(struct sockaddr_in);
	int send_len = sizeof(send_addr);
	char message[BUFLEN];
	char IP[20], ret;
	char fileName[100];

	if ((send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		perror("socket errors");
		exit(1);
	}

	char command[10];
	printf("Receiver IP address: ");
	ret = scanf("%s", IP);


	memset((char *)&recv_addr, 0, sizeof(recv_addr));
	recv_addr.sin_family = AF_INET;
	recv_addr.sin_port = htons(PORT);
	recv_addr.sin_addr.s_addr= inet_addr(IP);
	
	if (inet_aton(IP, &(recv_addr.sin_addr)) == 0) 
	{
		perror("inet_aton() error");
		exit(1);
	}
//	getsockname(send_sock, (struct sockaddr *)&send_addr, &send_len);
	unsigned int portNum = 0;
//	portNum = ntohs(send_addr.sin_port);
//printf("port Nmber: %d\n", portNum);
//	strcpy(fileName, itoa(portNum));
//	strcat(fileName, "_log.txt");
	int pid = 0;

	printf("command>> ");
	ret = scanf("%s %d", command, &WIN);
	TIME = 0.05;
char TMP[] = "Hello sender";
//slen = sizeof(struct sockaddr_in);
if(sendto(send_sock, TMP, sizeof(TMP), 0, (struct sockaddr*)&recv_addr, slen) < 0)
{
	perror("send");
	exit(1);
}
printf("sent\n");
	while(1)
	{
		if(strcmp(command, "stop") == 0) kill(pid, SIGINT);
		
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
		printf("command>> ");
		scanf("%s", command);
	}
	close(send_sock);
	return 0;
}

