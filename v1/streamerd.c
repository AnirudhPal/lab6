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

int lambda;
int fromIP;
struct timespec tim;

// Dat file paramters
int a = 1;
int delta = 0.5;
int mode;

// Mode A
void apply_mode_A(int cliTargetBufferSize, int cliBufferOcc){
	if (cliTargetBufferSize > cliBufferOcc){
		lambda += a;
	} else if (cliTargetBufferSize < cliBufferOcc){
		lambda -=a;
	}
}

// Mode B
void apply_mode_B(int cliTargetBufferSize, int cliBufferOcc){
	if (cliTargetBufferSize > cliBufferOcc){
		lambda += a;
	} else if (cliTargetBufferSize < cliBufferOcc){
		lambda *= delta;
	}
}

/* Helper Functions */
//IO Handler
void io_handler(int signal){

	//Receive the 12 bytes of the feedback packet
	to_len = sizeof(toUDP);
	int n = recvfrom(udpSock,bufUDP,sizeof(bufUDP),0, (struct sockaddr *)&toUDP,&to_len);

	// Variables from Client's feedback 
        int cliGammaVal, cliTargetBufferSize, cliBufferOcc;
	
	// Get values from feedback packet 
	cliBufferOcc =(bufUDP[0] << 24) | (bufUDP[1] << 16) | (bufUDP[2] << 8) | bufUDP[3];
	cliTargetBufferSize = (bufUDP[4] << 24) | (bufUDP[5] << 16) | (bufUDP[6] << 8) | bufUDP[7];
	cliGammaVal = (bufUDP[8] << 24) | (bufUDP[9] << 16) | (bufUDP[10] << 8) | bufUDP[11];

	if (mode == 0){
		apply_mode_A(cliTargetBufferSize, cliBufferOcc);
	} else if (mode == 1){
		apply_mode_B(cliTargetBufferSize, cliBufferOcc);
	}

	// Set new time for spacing between packets
	tim.tv_nsec = (1000/lambda) *1000000; 
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

	// Set ownership of socket to current process
	if (fcntl(udpSock,F_SETOWN, getpid()) < 0){
                perror("fcntl F_SETOWN");
                return -1;
        }
        // Allow receipt of asynchronous I/O signals   
        if (fcntl(udpSock,F_SETFL, O_NONBLOCK | FASYNC) < 0 ){
                perror("fcntl F_SETFL, O_NONBLOCK | FASYNC");
                return -1;
        }

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

	// Init Lambda
	lambda = atoi(argv[3]);

	// Set initial time spacing between packets
	tim.tv_sec = 0;
	tim.tv_nsec = (1000/lambda) *1000000;

	// Setup TCP Connection
	if(setupTCP(atoi(argv[1])) < 0)
		return -1;

	// Accept Clients
	while(1) {
		// Accept Connection
		int toLen = sizeof(to);
		int newConn = accept(tcpSock, (struct sockaddr*)&to, &toLen);
		fromIP = to.sin_addr.s_addr;

		// Waits for Receive
		if(tcpReceive(newConn) < 0)
			continue;

		// Setup UDP Connection
		if(setupUDP() < 0)
			continue;

		// Initialize IO signal handler
		signal(SIGIO, io_handler);

		// TCP Transmit UDP Port of Server
		unsigned char portStr[4];
		unsigned char* portP = (unsigned char*) &udpServerPort;
		portStr[0] = 2;
		portStr[1] = portP[0];
		portStr[2] = portP[1];
		portStr[3] = 0;

		if(tcpTransmit(newConn, portStr) < 0)
			continue;

		// Receive Client UDP Port
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

			// Size of the payload (also equals the size to be read from the audio file)
                        int sz = atoi(argv[2]);
			
			//create Buffer with size equal to payload size
		        audioBuf = (char *)malloc(sizeof(char)*sz);
		        memset(audioBuf, 0, sz);

			int n;
		        while ((n=read(fd, audioBuf, sz)) > 0) {
		        	//send payload of audio to the client and the client will start playing
		        	sendto(udpSock, audioBuf,n,0,(struct sockaddr *)&fromUDP,sizeof(fromUDP));

				// Nano Sleep for time spacing between packets
				nanosleep(&tim, NULL);

			}
			// Sending 5 five times to end the connection
			for(int j=0;j<5;j++)
				tcpTransmit(newConn, "5");
		}
	}
	close(tcpSock);
	close(udpSock);
}
