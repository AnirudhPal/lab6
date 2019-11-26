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
#include <alsa/asoundlib.h>

/* Macro Vars */
#define BUF_LEN	50
#define TCP_SERVER_ACCEPT 2
#define mulawwrite(x) snd_pcm_writei(mulawdev, x, mulawfrms)

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
socklen_t from_len;
unsigned char* audioBUF;
unsigned char* recvBUF;
unsigned char sendBUF[12];
int gamma_val, target_buffer, buffer_size;

static snd_pcm_t *mulawdev; 
static snd_pcm_uframes_t mulawfrms;

/* Helper Functions */
// IO Handler
void io_handler(int signal){
	from_len = sizeof(fromUDP);
        memset(recvBUF, 0, sizeof(recvBUF));
        int n = recvfrom(udpSock, recvBUF, sizeof(recvBUF), 0, (struct sockaddr *)&fromUDP, &from_len);
        memcpy(audioBUF, recvBUF+4, n - 4);
      	
        fprintf(stderr, "Size of audio buffer is %ld", sizeof(audioBUF));

        memset(sendBUF, 0, sizeof(sendBUF));
	int abufsize = sizeof(audioBUF) - n;
	
	sendBUF[0] = (abufsize >> 24) & 0xFF;
	sendBUF[1] = (abufsize >> 16) & 0xFF;
	sendBUF[2] = (abufsize >> 8) & 0xFF;
	sendBUF[3] = (abufsize) & 0xFF;

	sendBUF[4] = (target_buffer >> 24) & 0xFF;
	sendBUF[5] = (target_buffer >> 16) & 0xFF;
	sendBUF[6] = (target_buffer >> 8) & 0xFF;
	sendBUF[7] = (target_buffer) & 0xFF;

	sendBUF[8] = (gamma_val >> 24) & 0xFF;
	sendBUF[9] = (gamma_val >> 16) & 0xFF;
	sendBUF[10] = (gamma_val >> 8) & 0xFF;
	sendBUF[11] = (gamma_val) & 0xFF;
	sendto(udpSock, sendBUF, sizeof(sendBUF), 0, (struct sockaddr *)&toUDP, sizeof(toUDP));

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

// UDP Network Connection
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
 	if (fcntl(udpSock,F_SETOWN, getpid()) < 0){
        	perror("fcntl F_SETOWN");         
		return -1;   
	}   
	 // third: allow receipt of asynchronous I/O signals   
	if (fcntl(udpSock,F_SETFL, O_NONBLOCK | FASYNC) < 0 ){ 
	        perror("fcntl F_SETFL, O_NONBLOCK | FASYNC");
	        return -1;
	}
	// Update Port Variable
	udpClientPort = ntohs(toUDP.sin_port);

	return 1;
}

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

void mulawclose(void) {
        snd_pcm_drain(mulawdev);         
	snd_pcm_close(mulawdev); 
}

/* Main */
int main(int argc, char* argv[]) {
	// Handle Args
	if(argc != 9) {
		printf("usage: ./playaudio [IP of Server] [Port of Server] [Audio File] [Block Size] [Gammma] [Buffer Size] [Target Buffer] [Log File]\n");
		return -1;
	}
	gamma_val = atoi(argv[5]);
	target_buffer = atoi(argv[7]);
	buffer_size = atoi(argv[6]);
        size_t bs = (size_t)buffer_size;
	mulawopen(&bs);             // initialize audio codec
        // Set size of audio buffer
        audioBUF = (unsigned char *)malloc(target_buffer);   // audio buffer
        
        // Set size of tmp buffer to target
        recvBUF = (unsigned char *)malloc(buffer_size);

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

        // Init signal handler
        signal(SIGIO,io_handler);

	// TCP Transmit UDP Port of Client
	unsigned char portStr[3];
	unsigned char* portP = (unsigned char*) &udpClientPort;
	portStr[0] = portP[0];
	portStr[1] = portP[1];
	portStr[2] = 0;
	if(tcpTransmit(portStr) < 0)
		return -1;

	// Play Audio
        while(1){
        	if (sizeof(audioBUF) >= target_buffer){
			mulawwrite(audioBUF);
		}
	}
	free(audioBUF);
}
