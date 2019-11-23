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
#define TCP_SERVER_ACCEPT	50

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
	int fd = open(bufTCP, O_RDONLY);
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
			fprintf(stderr, "Port Client: %d, Port Server: %d\n", udpClientPort, udpServerPort);
			while(1) {}
		}
	}
}
