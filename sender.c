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

#define BUFLEN 1400	//Max length of buffer
#define PORT 10080	//The port on which to send data

int WIN;
int TIME;

int main(void)
{
	struct sockaddr_in recv_addr;
	int send_sock, i, slen=sizeof(recv_addr);
	char buf[BUFLEN];
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
		
		memset(buf,'\0', BUFLEN);
		//try to receive some data, this is a blocking call
		if (recvfrom(send_sock, buf, BUFLEN, 0, (struct sockaddr*)&recv_addr, &slen) == -1)
		{
			perror("recvfrom error");
			exit(1);
		}
		
		puts(buf);
	}

	close(send_sock);
	return 0;
}

