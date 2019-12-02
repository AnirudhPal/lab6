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
#include <time.h>

/* Macro Vars */
#define BUF_LEN	1488
#define TCP_SERVER_ACCEPT	2
#define TCP_TERM 5

/* Global Vars */
struct sockaddr_in to;
struct sockaddr_in from;
unsigned char bufTCP[BUF_LEN];
unsigned char bufUDP[BUF_LEN];
int tcpSock;
int udpSock;
unsigned short udpServerPort;
unsigned short udpClientPort;
struct sockaddr_in toUDP;
struct sockaddr_in fromUDP;
unsigned int lambdA;
unsigned int mode;
unsigned int pldSize;
unsigned char logFileName[100];
unsigned char filePath[100] = "";
struct timespec tim;
unsigned int gammA;
unsigned int bufferOccupancy;
unsigned int tBufSize;
/* Helper Functions */

// Dat file paramters
int a = 1;
int delta = 0.5;
int epsilon = 0.1;
int beta = 0.5;


// Mode A
void apply_mode_A(unsigned int tBufSize,unsigned int bufferOccupancy){
	if (tBufSize > bufferOccupancy){
		lambdA += a;
	} else if (tBufSize < bufferOccupancy){
		lambdA -=a;
	}
}

// Mode B
void apply_mode_B(unsigned int tBufSize, unsigned int bufferOccupancy){
	if (tBufSize > bufferOccupancy){
		lambdA += a;
	} else if (tBufSize < bufferOccupancy){
		lambdA *= delta;
	}
}

// Mode C
void apply_mode_C(unsigned int tBufSize, unsigned int bufferOccupancy){
	lambdA += epsilon * (tBufSize - bufferOccupancy);
}

// Mode D
void apply_mode_D(unsigned int tBufSize,unsigned int bufferOccupancy,unsigned int gammA){
	lambdA += epsilon * (tBufSize - bufferOccupancy) - beta * (lambdA - gammA);
}



// I/O Handler
void ioHandler(int sig) {
	// Empty Buffer
	memset(bufUDP, 0, BUF_LEN);

	// Get from Sender
	int fromUDPLen = sizeof(fromUDP);
	if(recvfrom(udpSock, bufUDP, BUF_LEN, 0, (struct sockaddr *)&fromUDP, &fromUDPLen) < 0) {
		perror("ioHandler(): Error!");
		return;
	}

	//Read BufferOccupancy, TargetBufferSize,gammA 
	bufferOccupancy = (bufUDP[0] << 24) | (bufUDP[1] << 16) | (bufUDP[2] << 8) | bufUDP[3];
	tBufSize = (bufUDP[4] << 24) | (bufUDP[5] << 16) | (bufUDP[6] << 8) | bufUDP[7];
	gammA = (bufUDP[8] << 24) | (bufUDP[9] << 16) | (bufUDP[10] << 8) | bufUDP[11];

	// Got IO
	if (mode == 0)
  {
		apply_mode_A(tBufSize,bufferOccupancy);
	}
	else if  (mode == 1)
  {
		apply_mode_B(tBufSize,bufferOccupancy);
	}
	else if ( mode == 2)
	{
		apply_mode_C(tBufSize,bufferOccupancy);
	}
	else if (mode == 3)
	{
		apply_mode_D(tBufSize,bufferOccupancy,gammA);
	}

	printf("The lambda value is %d \n",lambdA);
	
	tim.tv_nsec = (1000/lambdA) * 100000;
}

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

	// Capture File Name
	strcat(filePath,bufTCP);

	// Check if File Exists
	int fd = open(filePath, O_RDONLY);
	if(fd == -1) {
		tcpTransmit(sock, "0");
		return -1;
	}
	close(fd);

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

	// Update IP
	toUDP.sin_addr.s_addr = to.sin_addr.s_addr;

	// Update Port
	toUDP.sin_port = htons(udpClientPort);

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

	// FD Control UDP
	if(fcntl(udpSock,F_SETOWN, getpid()) < 0) {	
		perror("setupUDP(): Error!");
		return -1;
	}
	if(fcntl(udpSock,F_SETFL, FASYNC) < 0) {	
		perror("setupUDP(): Error!");
		return -1;
	}

	// Update Port Variable
	udpServerPort = ntohs(toUDP.sin_port);

	// Return
	return 1;
}

// Extract Arguments
void grabArgs(char** args) {
	pldSize = atoi(args[2]);
	if(pldSize > BUF_LEN)
		pldSize = BUF_LEN;
	lambdA = atoi(args[3]);
	mode = atoi(args[4]);
	strcpy(logFileName, args[5]);
}

/* Main */
int main(int argc, char* argv[]) {
	// Handle Args
	if(argc != 6) {
		printf("usage: ./streamerd [Port of Server] [Payload Size] [Initial Lambda] [Mode] [Log File]\n");
		return -1;
	}

	// Grab Arguments
	grabArgs(argv);

	// Setup TCP Connection
	if(setupTCP(atoi(argv[1])) < 0)
		return -1;

	// Accept Clients
	while(1) {
		// Accept Connection
		int toLen = sizeof(to);
		int newConn = accept(tcpSock, (struct sockaddr*)&to, &toLen);

		// Send Audio (Fork)
		int pid = fork();
		if(pid == 0) {
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

			// Receive Client Port + IP
			if(tcpReceivePort(newConn) < 0)
				continue;

			// Enable SIGIO
			signal(SIGIO, ioHandler);

			// Diagnostic Print
			//fprintf(stderr, "IP Client: %s, Port Client: %d, Port Server: %d\n", inet_ntoa(toUDP.sin_addr), ntohs(toUDP.sin_port), udpServerPort);

			// Set Packet Spacing
			tim.tv_sec = 0;
			double nsec = (1000.0/ (double)lambdA) * 1000000.0;	 
			tim.tv_nsec = nsec;
			//printf("nsec: %ld\n", tim.tv_nsec);	

			// Open File
			int fd = open(filePath, O_RDONLY);

			// Empty Buffer
			memset(bufUDP, 0, BUF_LEN);

			// Send File
			int n;
			while((n = read(fd, bufUDP, pldSize)) > 0) {
				// Send
				if(sendto(udpSock, bufUDP, n, 0,(struct sockaddr *)&toUDP, sizeof(toUDP)) < 0) {
					perror("main(): Error!");
					return -1;	
				}

				// Empty Buffer
				memset(bufUDP, 0, BUF_LEN);

				// Sleep
				nanosleep(&tim, NULL);
			}

			// Close File
			close(fd);

			// Terminate with TCP
			char term[2];
			term[0] = TCP_TERM;
			term[1] = 0;
			for(int i = 0; i < 5; i++)
				tcpTransmit(newConn, term);

			// Exit Child Proc
			exit(1);
		}
	}
}
