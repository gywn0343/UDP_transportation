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
#define PACKET_LOSS_PROB 0.02
#define PORT 10080


int main(void)
{
	struct sockaddr_in recv_addr, send_addr;
	
	int recv_sock, i, slen = sizeof(send_addr);
	char buf[BUFLEN];
	int ret;
	int sock_buf_size;
	socklen_t len;
	int bf = 1;
	
	//create a UDP socket
	if ((recv_sock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		perror("socket");
		exit(1);
	}
	if(getsockopt(recv_sock, SOL_SOCKET, SO_SNDBUF, &sock_buf_size, &len))
	{
		perror("getsockopt error");
		exit(1);
	}
	if(setsockopt(recv_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&bf, (int)sizeof(bf)) == SO_ERROR)
	{
		perror("setsockopt error");
		exit(1);
	}

	memset((char *) &recv_addr, 0, sizeof(recv_addr));
	
	recv_addr.sin_family = AF_INET;
	recv_addr.sin_port = htons(PORT);
	recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	printf("packet loss probability: %.2f\n", PACKET_LOSS_PROB);
	if( bind(recv_sock, (struct sockaddr*)&recv_addr, sizeof(recv_addr)) == -1)
	{
		perror("bind");
		exit(1);
	}
	printf("socket buffer size: %dB\n", sock_buf_size);
	while(1)
	{
		//try to receive some data, this is a blocking call
		if ((ret = recvfrom(recv_sock, buf, BUFLEN, 0, (struct sockaddr *) &send_addr, &slen)) == -1)
		{
			continue;
		}
		printf("fileName: %s\n", buf);
		//print details of the client/peer and the data received

		
		//now reply the client with the same data
		if (sendto(recv_sock, buf, ret, 0, (struct sockaddr*) &send_addr, slen) == -1)
		{
		}
	}

	close(recv_sock);
	return 0;
}

