
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
#include<semaphore.h>
#include<time.h>

/* Macro Vars */
#define BUF_LEN	50
#define TCP_SERVER_ACCEPT 2
#define TCP_SERVER_END 5
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
char* audioBUF;
char* recvBUF;
char* audioBlock;
unsigned char sendBUF[12];
int gammaVal, targetBUF, bufferSize;
static int blockSize;
sem_t mutex;
static snd_pcm_t *mulawdev; 
static snd_pcm_uframes_t mulawfrms;
struct timespec tim;

// Volatie vars
volatile sig_atomic_t audioBUFSize = 0;
volatile sig_atomic_t audioBUFRead = 0;
volatile sig_atomic_t audioBUFWrite = 0;

/* Helper Functions */
// IO Handler
void io_handler(int signal){
	from_len = sizeof(fromUDP);
	
	//reset the receive audio packet
        
	memset(recvBUF, 0, bufferSize*1024);
	
	//receive the audio packet
        
	int n = recvfrom(udpSock, recvBUF, bufferSize * 1024, 0, (struct sockaddr *)&fromUDP, &from_len);
	
	//Block the play till the audiobuffer is filled with latest packet
	
	sem_wait(&mutex);
	
	//write to audio buffer from the receive buffer
	
	for (int i=0; i<n;i++){
		audioBUF[audioBUFWrite++] = recvBUF[i];
		
		//Reset to 0 if buffer size is filled
		if (audioBUFWrite ==( (bufferSize * 1024) -1)){
			audioBUFWrite = 0;
		}
	}
	audioBUFSize += n;
        
        memset(sendBUF, 0, sizeof(sendBUF));
	int abufsize =  audioBUFSize- n;
      	sem_post(&mutex);
	
	//Buffer Occupancy size
	sendBUF[0] = (abufsize >> 24) & 0xFF;
	sendBUF[1] = (abufsize >> 16) & 0xFF;
	sendBUF[2] = (abufsize >> 8) & 0xFF;
	sendBUF[3] = (abufsize) & 0xFF;
	
	//Target Buffer Occupancy size
	sendBUF[4] = (targetBUF >> 24) & 0xFF;
	sendBUF[5] = (targetBUF >> 16) & 0xFF;
	sendBUF[6] = (targetBUF >> 8) & 0xFF;
	sendBUF[7] = (targetBUF) & 0xFF;
	
	//Gamma val for the nanosleep update
	sendBUF[8] = (gammaVal >> 24) & 0xFF;
	sendBUF[9] = (gammaVal >> 16) & 0xFF;
	sendBUF[10] = (gammaVal >> 8) & 0xFF;
	sendBUF[11] = (gammaVal) & 0xFF;

	//Send to the server the feedback packet
	toUDP.sin_addr.s_addr = fromUDP.sin_addr.s_addr;
	
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
		//perror("tcpReceive(): Error!");
		return -1;
	}
	//Check exit condition
	if(bufTCP[0] == TCP_SERVER_END)
	{
		fprintf(stderr,"End of Pseudo streaming");
		return 2;
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
	toUDP.sin_port = htons(udpServerPort);

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
	fprintf(stderr,"Port of client %d %s",htons(toUDP.sin_port),inet_ntoa(toUDP.sin_addr));
 	if (fcntl(udpSock,F_SETOWN, getpid()) < 0){
        	perror("fcntl F_SETOWN");         
		return -1;   
	}   
	 // third: allow receipt of asynchronous I/O signals   
	if (fcntl(udpSock,F_SETFL, O_NONBLOCK | FASYNC) < 0 ){ 
	        perror("fcntl F_SETFL, O_NONBLOCK | FASYNC");
	        return -1;
	}
	if (fcntl(tcpSock,F_SETOWN, getpid()) < 0){
                perror("fcntl F_SETOWN");
                return -1;
        }

	if (fcntl(tcpSock,F_SETFL, O_NONBLOCK | FASYNC) < 0 ){
                perror("fcntl F_SETFL, O_NONBLOCK | FASYNC");
                return -1;
        }

	// Update Port Variable
	udpClientPort = ntohs(toUDP.sin_port);

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
	gammaVal = atoi(argv[5]);
	targetBUF = atoi(argv[7]);
	bufferSize = atoi(argv[6]);
	blockSize = atoi(argv[4]);
        tim.tv_sec = 0;
	tim.tv_nsec = 90000000;
	size_t bs;
	mulawopen(&bs);         	// initialize audio codec
        // Set size of audio buffer
        audioBUF = (char *)malloc(bufferSize * 1024);   // audio buffer
        
        // Set size of tmp buffer to target
        recvBUF = (char *)malloc(bufferSize * 1024);

        // Set size of block buffer to target
        audioBlock = (char *)malloc(bs);

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
	// Semaphore initialize
	sem_init(&mutex,0,1);
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
		if(tcpReceive() == 2)
		{
			fprintf(stderr,"End of the reception \n"); 
			break;
		}
		nanosleep(&tim,NULL);
                sem_wait(&mutex);
		
		//check if target buf is greater than 24KB

        	if (audioBUFSize >= (targetBUF * 1024)){
			fprintf(stderr,"Inside the mulawrite\n");
			int iter = (int)(audioBUFSize/blockSize);
		        
			// For each iteration copy 4 KB of block to be pase to mulawwrite and then remove those 4 KB from audio buffer 
			for(int i=0; i<iter;i++){
                            memset(audioBlock, 0, blockSize);
			    //Copy audio buffer to audioBlock in size of 4096 Bytes and send to 
			    for(int j=0; j<blockSize;j++){
				    audioBlock[j] = audioBUF[audioBUFRead++];
				    if(audioBUFRead == ((bufferSize*1024) - 1)){
					    audioBUFRead = 0;
				    }
			    }
			    
			    mulawwrite(audioBlock);
			    audioBUFSize -= blockSize;
			}
		}
      	        sem_post(&mutex);
	}
	free(audioBlock);
	sem_destroy(&mutex);
}
