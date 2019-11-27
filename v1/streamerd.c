// Audio Server
/* Import Libs */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>

/* Macro Vars */
#define BUF_LEN	50
#define TCP_SERVER_ACCEPT 2

/* Global Vars */
struct sockaddr_in to;
struct sockaddr_in from;
unsigned char bufTCP[BUF_LEN];
char bufUDP[BUF_LEN];
int tcpSock;
int udpSock;
int fd;
socklen_t from_len, to_len;
unsigned short udpServerPort;
unsigned short udpClientPort;
struct sockaddr_in toUDP;
struct sockaddr_in fromUDP;
unsigned char *audioBuf;
char filePath[100] = "/tmp/";
int gammaVal,targetBufferSize,remainingBufferSize;
int fromIP;

/* Helper Functions */
// Setup TCP Network Connection
int setupTCP(unsigned short port) {
	// Set Up Struct to
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_addr.s_addr = INADDR_ANY;
	to.sin_port = htons(port);

	// Set Up Struct from
	memset(&from, 0, sizeof(from));
	from.sin_family = AF_INET;
	from.sin_addr.s_addr = INADDR_ANY;
	from.sin_port = htons((u_short)0);

	// Allocate Socket
	tcpSock =  socket(AF_INET, SOCK_STREAM, 0);
	if (tcpSock < 0) {
		perror("setupTCP(): Error!");
		return -1;
	}

	// Bind Socket and IP+PORT
	if(bind(tcpSock, (struct sockaddr*)&to, sizeof(to))) {
		perror("setupTCP(): Error!");
		return -1;
	}

	// Make Port Passive Listner
	if(listen(tcpSock, 5) < 0) {
		perror("setupTCP(): Error!");
		return -1;
	}

	// Return
	return 1;
}

// TCP Transmit Filename
int tcpTransmit(int sock, unsigned char* res) {
	// Clean Buffer
	memset(bufTCP, 0, BUF_LEN);

	// Add to Buffer
	strcpy(bufTCP, res);

	// Send to Server
	if(write(sock, bufTCP, BUF_LEN) < 0) {
		perror("tcpTransmit(): Error!");
		return -1;
	}

	// Clean Buffer
	memset(bufTCP, 0, BUF_LEN);

	// Return
	return 1;
}

// TCP Receive
int tcpReceive(int sock) {
	// Clean Buffer
	memset(bufTCP, 0, BUF_LEN);

	// Get from Sender
	if(read(sock, bufTCP, BUF_LEN) < 0) {
		perror("tcpReceive(): Error!");
		return -1;
	}

	// Check if File Exists
	strcat(filePath,bufTCP);
	fd = open(filePath, O_RDONLY);
	if(fd == -1) {
		tcpTransmit(sock, "0");
		return -1;
	}

	// Clean Buffer
	memset(bufTCP, 0, BUF_LEN);

	// Return
	return 1;
}

// TCP Receive Port
int tcpReceivePort(int sock) {
	// Clean Buffer
	memset(bufTCP, 0, BUF_LEN);

	// Get from Sender
	if(read(sock, bufTCP, BUF_LEN) < 0) {
		perror("tcpReceive(): Error!");
		return -1;
	}

	// Set Port
	unsigned char* portP = (unsigned char*) &udpClientPort;
	portP[0] = bufTCP[0];
	portP[1] = bufTCP[1];

	// Clean Buffer
	memset(bufTCP, 0, BUF_LEN);

	// Return
	return 1;
}


// Setup UDP Network Connection
int setupUDP() {
	// Set Up Struct to
	memset(&toUDP, 0, sizeof(toUDP));
	toUDP.sin_family = AF_INET;
	toUDP.sin_addr.s_addr = INADDR_ANY;
	toUDP.sin_port = htons((u_short)0);


	// Allocate Socket
	udpSock =  socket(AF_INET, SOCK_DGRAM, 0);
	if (udpSock < 0) {
		perror("setupUDP(): Error!");
		return -1;
	}

	// Bind Socket
	if(bind(udpSock, (struct sockaddr*)&toUDP, sizeof(toUDP)) < 0) {
		perror("setupUDP(): Error!");
		return -1;
	}

	// Get Port
	int toLen = sizeof(toUDP);
	getsockname(udpSock, (struct sockaddr *)&toUDP, &toLen);

	// Update Port Variable
	udpServerPort = ntohs(toUDP.sin_port);

	// Return
	return 1;
}

/* Main */
int main(int argc, char* argv[]) {
	// Handle Args
	if(argc != 6) {
		printf("usage: ./streamerd [Port of Server] [Payload Size] [Initial Lambda] [Mode] [Log File]\n");
		return -1;
	}

	// Setup TCP Connection
	if(setupTCP(atoi(argv[1])) < 0)
		return -1;

	// Accept Clients
	while(1) {
		// Accept Connection
		int toLen = sizeof(to);
		int newConn = accept(tcpSock, (struct sockaddr*)&to, &toLen);
		//fprintf(stderr, "Received udp info: to %s %d\n", inet_ntoa(to.sin_addr), htons(to.sin_port));
		fromIP = to.sin_addr.s_addr;

		// Waits for Receive
		if(tcpReceive(newConn) < 0)
			continue;

		// Setup UDP Connection
		if(setupUDP() < 0)
			continue;
		
		// TCP Transmit UDP Port of Server
		unsigned char portStr[4];
		unsigned char* portP = (unsigned char*) &udpServerPort;
		portStr[0] = 2;
		portStr[1] = portP[0];
		portStr[2] = portP[1];
		portStr[3] = 0;
		if(tcpTransmit(newConn, portStr) < 0)
			return -1;

		// Receive Client Port
		if(tcpReceivePort(newConn) < 0)
			continue;

		// Send Audio (Fork)
		int pid = fork();
		if(pid == 0) {	
	                // Set Up Struct from
	                memset(&fromUDP, 0, sizeof(fromUDP));
	                fromUDP.sin_family = AF_INET;
	                fromUDP.sin_addr.s_addr = fromIP;
	                fromUDP.sin_port = htons(udpClientPort);
                        int sz = atoi(argv[2]);
			//fprintf(stderr, "Port Client: %d, Port Server: %d\n", udpClientPort, udpServerPort);
				//create Buffer with size equal to payload size from args
		        audioBuf = (char *)malloc(sizeof(char)*sz);
		        memset(audioBuf, 0, sizeof(audioBuf));
			int n;
		        while ((n=read(fd, audioBuf, sz)) > 0) {
		        	//send payload of audio to the client and the client will start playing
		                //fprintf(stderr, "Sending audio %s of size %ld to %s %d\n", audioBuf, n, inet_ntoa(fromUDP.sin_addr), htons(fromUDP.sin_port));
		                sleep(1);
		        	sendto(udpSock, audioBuf,n,0,(struct sockaddr *)&fromUDP,sizeof(fromUDP));
		        	//recv the 12 bytes of the feedback packet
		        	to_len = sizeof(toUDP);
		        	n = recvfrom(udpSock,bufUDP,sizeof(bufUDP),0, (struct sockaddr *)&toUDP,&to_len);
		                fprintf(stderr, "Received feedback %s of size %ld to %s %d\n", bufUDP, n, inet_ntoa(toUDP.sin_addr), htons(toUDP.sin_port));
		        	//do the logic to get the number of remaining bytes
		        	remainingBufferSize =(bufUDP[0] << 24) | (bufUDP[1] << 16) | (bufUDP[2] << 8) | bufUDP[3];
		        	targetBufferSize = (bufUDP[4] << 24) | (bufUDP[5] << 16) | (bufUDP[6] << 8) | bufUDP[7];
		        	gammaVal = (bufUDP[8] << 24) | (bufUDP[9] << 16) | (bufUDP[10] << 8) | bufUDP[11];

			}
		}
	}
	close(tcpSock);
	close(udpSock);
}
