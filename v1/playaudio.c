// Client Audio Player
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
#define TCP_SERVER_ACCEPT	2

/* Global Vars */
struct sockaddr_in to;
struct sockaddr_in from;
unsigned char bufTCP[BUF_LEN];
int tcpSock;
int udpSock;
unsigned short udpServerPort;
unsigned short udpClientPort;
struct sockaddr_in toUDP;
struct sockaddr_in fromUDP;

/* Helper Functions */
// Setup TCP Network Connection
int setupTCP(char* ipStr, unsigned short port) {
	// Set Up Struct to
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_addr.s_addr = inet_addr(ipStr);
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

	// Open Connection
	if(connect(tcpSock, (struct sockaddr*)&to, sizeof(to)) < 0) {
		perror("setupTCP(): Error!");
		return -1;
	}

	// Return
	return 1;
}

// TCP Transmit Filename
int tcpTransmit(unsigned char* filename) {
	// Clean Buffer
	memset(bufTCP, 0, BUF_LEN);

	// Add to Buffer
	strcpy(bufTCP, filename);

	// Send to Server
	if(write(tcpSock, bufTCP, BUF_LEN) < 0) {
		perror("tcpTransmit(): Error!");
		return -1;
	}

	// Clean Buffer
	memset(bufTCP, 0, BUF_LEN);

	// Return
	return 1;
}

// TCP Receive
int tcpReceive() {
	// Clean Buffer
	memset(bufTCP, 0, BUF_LEN);

	// Get from Sender
	if(read(tcpSock, bufTCP, BUF_LEN) < 0) {
		perror("tcpReceive(): Error!");
		return -1;
	}

	// Check if Accept
	if(bufTCP[0] == TCP_SERVER_ACCEPT) {
		// Extract Port
		unsigned char* portP = (unsigned char*)&udpServerPort;
		portP[0] = bufTCP[1];
		portP[1] = bufTCP[2];

		// Clean Buffer
		memset(bufTCP, 0, BUF_LEN);

		// Accept
		return 1;
	}

	// Clean Buffer
	memset(bufTCP, 0, BUF_LEN);

	// Reject
	return -1;
}

// Setup UDP Network Connection
int setupUDP() {
	// Set Up Struct to
	memset(&toUDP, 0, sizeof(toUDP));
	toUDP.sin_family = AF_INET;
	toUDP.sin_addr.s_addr = INADDR_ANY;
	toUDP.sin_port = htons((u_short)0);

	// Set Up Struct from
	memset(&fromUDP, 0, sizeof(fromUDP));
	fromUDP.sin_family = AF_INET;
	fromUDP.sin_addr.s_addr = INADDR_ANY;
	fromUDP.sin_port = htons((u_short)0);

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
	udpClientPort = ntohs(toUDP.sin_port);

	// Return
	return 1;
}

/* Main */
int main(int argc, char* argv[]) {
	// Handle Args
	if(argc != 9) {
		printf("usage: ./playaudio [IP of Server] [Port of Server] [Audio File] [Block Size] [Gammma] [Buffer Size] [Target Buffer] [Log File]\n");
		return -1;
	}

	// Setup TCP Connection
	if(setupTCP(argv[1], atoi(argv[2])) < 0)
		return -1;

	// TCP Transmit
	if(tcpTransmit(argv[3]) < 0)
		return -1;

	// Waits for Receive
	if(tcpReceive() < 0)
		return -1;

	// Setup UDP Connection
	if(setupUDP() < 0)
		return -1;

	// TCP Transmit UDP Port of Client
	unsigned char portStr[3];
	unsigned char* portP = (unsigned char*) &udpClientPort;
	portStr[0] = portP[0];
	portStr[1] = portP[1];
	portStr[2] = 0;
	if(tcpTransmit(portStr) < 0)
		return -1;

	// Play Audio
}
