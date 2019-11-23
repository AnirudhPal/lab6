// Server Program File Server - Anirudh Pal
/* Import Libs */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define MAXBUFSZM 10

/* Main */
int main(int argc, char* argv[]) {
	/* Local Vars */
	char buf[MAXBUFSZM];														// Input Buffer
	struct sockaddr_in from;												// To Server
	struct sockaddr_in srvAddr; 										// This Client
	int blkSize = 0;

	// Handle Args
	if(argc != 3) {
		printf("usage: ./myfetchfiled [Blocksize] [Port of Server]\n");
		return -1;
	}

	// Get Block Size
	blkSize = atoi(argv[1]);
	if(blkSize > MAXBUFSZM)
		blkSize = MAXBUFSZM;

	// Set Up Struct (From)
	memset(&from, 0, sizeof(from));

	// Set Up Struct 
	memset(&srvAddr, 0, sizeof(srvAddr));
	// IPv4 Family
	srvAddr.sin_family = AF_INET;
	// Allow binding to IP+PORT
	srvAddr.sin_addr.s_addr = INADDR_ANY;
	// Get Port Automatically
	srvAddr.sin_port = htons(atoi(argv[2]));

	// Allocate Socket
	int srvSock =  socket(AF_INET, SOCK_STREAM, 0);
	if (srvSock < 0) {
		perror("socket");
		return -1;
	}

	// Bind Socket and IP+PORT
	if(bind(srvSock, (struct sockaddr*)&srvAddr, sizeof(srvAddr))) {
		perror("bind");
		return -1;
	}

	// Make Port Passive Listner
	if(listen(srvSock, 5) < 0) {
		perror("listen");
		return -1;
	}

	// Loop for more Commands
	while(1) {
		// Accept Connection
		int srvAddrLen = sizeof(srvAddr);
		int newSrvSock = accept(srvSock, (struct sockaddr*)&srvAddr, &srvAddrLen);
		if(newSrvSock < 0) {
			perror("accept");
			return -1;
		}

		// Fork a Child
		int k = fork();

		// Child 
		if (k==0) {
			// Empty Buffer
			memset(buf, '\0', MAXBUFSZM);
			
			// Get Filename
			read(newSrvSock, buf, MAXBUFSZM);
			
			// Open File
			int fd = open(buf, O_RDONLY);

			// File Doesnt Exist
			if(fd == -1) {
				write(newSrvSock, "0", 1);
				close(newSrvSock);
				exit(1);
			}

			// Get First Read
			int rF = read(fd, buf, blkSize);
			if(rF == -1) {
				perror("read");
				return -1;
			}

			// Empty File
			if(rF == 0) {
				write(newSrvSock, "1", 1);
				close(newSrvSock);
				exit(1);
			}
			else {
				write(newSrvSock, "2", 1);
			}

			// Loop
			while(rF > 0) {
				// Write to File
				write(newSrvSock, buf, rF);

				// Read Next
				rF = read(fd, buf, blkSize);
				if(rF == -1) {
					perror("read");
					return -1;
				}	
			}

			// Close
			shutdown(newSrvSock, SHUT_WR);
			close(fd);
			close(newSrvSock);
		}
	}
}
