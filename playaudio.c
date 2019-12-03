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
#include <time.h>
#include <alsa/asoundlib.h>
#include <semaphore.h>
#include <pthread.h>

/* Macro Vars */
#define BUF_LEN	1488
#define TCP_SERVER_ACCEPT	2
#define mulawwrite(x) snd_pcm_writei(mulawdev, x, mulawfrms)
#define R_BUF_SIZE	1488
#define A_BUF_SIZE	4* 1024
#define INSERT_TEST 0
#define REMOVE_TEST 0
#define OUT_AUDIO 0
#define FEEDBACK 1
#define TERM_TEST 0

/* Global Vars */
struct sockaddr_in to;
struct sockaddr_in from;
unsigned char bufTCP[BUF_LEN];
unsigned char bufUDP[BUF_LEN];
int tcpSock;
int udpSock;
int feedSock;
unsigned short udpServerPort;
unsigned short udpClientPort;
struct sockaddr_in toUDP;
struct sockaddr_in fromUDP;
unsigned int gammA;
unsigned int tBufSize;
unsigned int blkSize;
unsigned int C_BUF_SIZE;
unsigned char logFileName[100];
fd_set fds;
unsigned int loop = 1;
unsigned int softStart = 1;
struct timespec tim;
static snd_pcm_t *mulawdev;  
static snd_pcm_uframes_t mulawfrms;
unsigned char* c_buf;
unsigned char r_buf[R_BUF_SIZE];
unsigned char a_buf[A_BUF_SIZE];
int c_buf_head;
int c_buf_tail;
sem_t mutex;
sem_t full;
sem_t empty;
unsigned int bufferOccupancy;
unsigned char feedBack[12];

/* Func Defs */
void insertCircBuffer(void);

/* Helper Functions */
// IO Handler
void ioHandler(int sig) {
	// Set FD Set Empty
	FD_ZERO(&fds);

	// Add TCP Socket
	FD_SET(tcpSock, &fds);

	// Add UDP Socket
	FD_SET(udpSock, &fds);

	// Get MaxSock
	int maxSock = tcpSock;
	if(udpSock > maxSock)
		maxSock = udpSock;

	// Select
	if(select(maxSock + 1, &fds, NULL, NULL, NULL) < 0) {
		perror("I/O Handler: Error!");
		return;
	}

	// UDP
	if(FD_ISSET(udpSock, &fds)) {
		// Empty Buffer
		memset(bufUDP, 0, BUF_LEN);

		// Get from Sender
		insertCircBuffer();
	}

	// TCP
	if(FD_ISSET(tcpSock, &fds)) {
		// Get from Sender
		char ch;
		if(read(tcpSock, &ch, 1) < 0) {
			perror("ioHandler(): Error!");
			return;
		}

		// Terminate ?
		if(ch == 5) {
			// Cancel Loop
			loop = 0;

			// Flush Sem
			for(int i = 0; i < tBufSize; i++)
				sem_post(&full);

			// Diahnostic
			if(TERM_TEST)
				printf("Terminate\n");
		}
	}
}

// Initializer
void initCircBuffer() {
	// Allocate Memory
	c_buf = (unsigned char*) malloc(C_BUF_SIZE * sizeof(unsigned char));

	// Empty Buffer
	memset(c_buf, 0, C_BUF_SIZE);

	// Set Head and Tail
	c_buf_head = 0;
	c_buf_tail = 0;

	// Set Semaphores
	sem_init(&mutex, 1, 1);	
	sem_init(&full, 1, 0);	
	sem_init(&empty, 1, C_BUF_SIZE);	
}

// Producer
void insertCircBuffer() {
	// Compute Buffer Occupancy
	int tail = c_buf_tail;
	int size = 0;
	while(tail != c_buf_head) {
			tail = (tail + 1) % C_BUF_SIZE;
			size++;
	}
	bufferOccupancy = size;

	// Empty Buffer
	memset(r_buf, 0, R_BUF_SIZE);

	// Populate Buffer
	int fromUDPLen = sizeof(fromUDP);
	int n = recvfrom(udpSock, r_buf, R_BUF_SIZE, 0, (struct sockaddr *)&fromUDP, &fromUDPLen);
	if(n < 0) {
		perror("insertCircBuffer(): Error!");
		return;
	}

	// Wait on Semaphores
	for(int i = 0; i < n; i++)
		sem_wait(&empty);
	sem_wait(&mutex);

	/* Sensitive Start */
	// Split
	if((c_buf_head + n) > C_BUF_SIZE) {
		// First Half Copy
		memcpy((c_buf + c_buf_head), r_buf, C_BUF_SIZE - c_buf_head);
		// Second Half Copy
		memcpy(c_buf, (r_buf + (C_BUF_SIZE - c_buf_head)), (n - (C_BUF_SIZE - c_buf_head))); 
	}
	// No Split
	else {
		// Entire Copy
		memcpy((c_buf + c_buf_head), r_buf, n);
	}

	// Increment Head
	c_buf_head = (c_buf_head + n) % C_BUF_SIZE;

	// Testing
	if(INSERT_TEST) {
		int tail = c_buf_tail;
		int size = 0;
		while(tail != c_buf_head) {
			tail = (tail + 1) % C_BUF_SIZE;
			size++;
		}
		printf("insert() -> C_BUF Usage: %d / %d\n", size, C_BUF_SIZE);
	}

	/* Sensitive End */

	// Signal on Semaphores
	sem_post(&mutex);
	for(int i = 0; i < n; i++)
		sem_post(&full);
}

// Consumer
void removeCircBuffer() {
	// Empty Buffer
	memset(a_buf, 0, A_BUF_SIZE);

	// Soft Start
	if(softStart) {
		// Get Size
		int tail = c_buf_tail;
		int size = 0;
		while(tail != c_buf_head) {
			tail = (tail + 1) % C_BUF_SIZE;
			size++;
		}

		// Skip if not 24
		if(size < tBufSize)
			return;

		// Else no Soft Start
		softStart = 0;
	}

	// Wait on Semaphores
	for(int i = 0; i < A_BUF_SIZE; i++)
		sem_wait(&full);
	sem_wait(&mutex);

	/* Sensitive Start */
	// Split
	if((c_buf_tail + A_BUF_SIZE) > C_BUF_SIZE) {
		// First Half Copy
		memcpy(a_buf, (c_buf + c_buf_tail), (C_BUF_SIZE - c_buf_tail));
		// Second Half Copy
		memcpy((a_buf + (C_BUF_SIZE - c_buf_tail)), c_buf, (A_BUF_SIZE - (C_BUF_SIZE - c_buf_tail))); 
	}
	// No Split
	else {
		// Entire Copy
		memcpy(a_buf, (c_buf + c_buf_tail), A_BUF_SIZE);
	}

	// Increment Tail
	c_buf_tail = (c_buf_tail + A_BUF_SIZE) % C_BUF_SIZE;

	// Testing
	if(REMOVE_TEST) {
		int tail = c_buf_tail;
		int size = 0;
		while(tail != c_buf_head) {
			tail = (tail + 1) % C_BUF_SIZE;
			size++;
		}
		printf("remove() -> C_BUF Usage: %d / %d\n", size, C_BUF_SIZE);
	}

	/* Sensitive End */

	// Signal on Semaphores
	sem_post(&mutex);
	for(int i = 0; i < A_BUF_SIZE; i++)
		sem_post(&empty);

	// MulaWrite
	if(OUT_AUDIO)
		printf("%s\n\n", a_buf);
	mulawwrite(a_buf);

	// Send to Sender
	if(FEEDBACK) {
		// Build Packet	
		feedBack[0] = (bufferOccupancy >> 24) & 0xFF;
    		feedBack[1] = (bufferOccupancy >> 16) & 0xFF;
		feedBack[2] = (bufferOccupancy >> 8) & 0xFF;
    		feedBack[3] = (bufferOccupancy) & 0xFF;
    		feedBack[4] = (tBufSize >> 24) & 0xFF;
		feedBack[5] = (tBufSize >> 16) & 0xFF;
		feedBack[6] = (tBufSize >> 8) & 0xFF;
    		feedBack[7] = (tBufSize) & 0xFF;
    		feedBack[8] = (gammA >>24) & 0xFF;
	  	feedBack[9] = (gammA >>16) & 0xFF;
	  	feedBack[10] = (gammA >>8) & 0xFF;
		feedBack[11] = (gammA) & 0xFF;
    
		// Send
		if(sendto(udpSock, feedBack, 12, 0, (struct sockaddr *)&fromUDP, sizeof(fromUDP)) < 0) {
			perror("ioHandler(): Error!");
			return;
		}
	}
}

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

	/**
	// FD Control UDP
	if(fcntl(udpSock,F_SETOWN, getpid()) < 0) {
		perror("setupUDP(): Error!");
		return -1;
	}
	if(fcntl(udpSock,F_SETFL, FASYNC) < 0 ) {	
		perror("setupUDP(): Error!");
		return -1;
	}

	// FD Control TCP
	if(fcntl(tcpSock,F_SETOWN, getpid()) < 0) {
		perror("setupUDP(): Error!");
		return -1;
	}
	if(fcntl(tcpSock,F_SETFL, FASYNC) < 0 ) {	
		perror("setupUDP(): Error!");
		return -1;
	}**/

	// Return
	return 1;
}

//Mulawopen to init Audio Buffer
void mulawopen(size_t *bufsiz) {
	snd_pcm_hw_params_t *p;
	unsigned int rate = 8000;
	snd_pcm_open(&mulawdev, "default", SND_PCM_STREAM_PLAYBACK, 0);
	snd_pcm_hw_params_alloca(&p);
	snd_pcm_hw_params_any(mulawdev, p);
	snd_pcm_hw_params_set_access(mulawdev, p, SND_PCM_ACCESS_RW_INTERLEAVED); 
	snd_pcm_hw_params_set_format(mulawdev, p, SND_PCM_FORMAT_MU_LAW);
	snd_pcm_hw_params_set_channels(mulawdev, p, 1);
	snd_pcm_hw_params_set_rate_near(mulawdev, p, &rate, 0);
	snd_pcm_hw_params(mulawdev, p);
	snd_pcm_hw_params_get_period_size(p, &mulawfrms, 0);
	*bufsiz = (size_t)mulawfrms;
	return; 
}  

//Close the audio buffer in driver
void mulawclose(void) {
	snd_pcm_drain(mulawdev);         
	snd_pcm_close(mulawdev); 
}

// Extract Args
void grabArgs(char** args) {
	gammA = atoi(args[5]);
	tBufSize = atoi(args[7]) * 1024;
	blkSize = atoi(args[4]);
	C_BUF_SIZE = atoi(args[6]) * 1024;
	strcpy(logFileName, args[8]);
}

void ioLoop() {
	while(1)
		ioHandler(0);
}
void* ioLoopP = &ioLoop;

/* Main */
int main(int argc, char* argv[]) {
	// Handle Args
	if(argc != 9) {
		printf("usage: ./playaudio [IP of Server] [Port of Server] [Audio File] [Block Size] [Gammma] [Buffer Size] [Target Buffer] [Log File]\n");
		return -1;
	}

	// Grab Arguments
	grabArgs(argv);

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

	// Enable SIGIO
	//signal(SIGIO, ioHandler);

	// Set Time
	tim.tv_sec = 0;
	tim.tv_nsec = gammA * 1000000;
	//printf("Gamma: %ld\n", tim.tv_nsec);

	// Initialize Circular Buffer
	initCircBuffer();

	// Mula Open (Check with TA)
	size_t bs;
	mulawopen(&bs);
	//printf("BS: %ld\n", bs);
	

	// Create Thread
	pthread_t io_thread;
	if(pthread_create(&io_thread, NULL, ioLoopP, NULL))
		perror("main():");

	// Play Audio
	while(loop) {
		// Sleep
		if(nanosleep(&tim, NULL) < 0)
			perror("Fail");

		// Consume
		removeCircBuffer();
	}

	// Mula Close (Check with TA)
	mulawclose();
}
