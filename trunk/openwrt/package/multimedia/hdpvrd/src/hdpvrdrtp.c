#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE
#define _GNU_SOURCE
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <netinet/in.h>
#include <resolv.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <assert.h>
#include <syslog.h>

unsigned short seqnumber;
unsigned long timestamp;
long long oldtimestamp;
unsigned int ssrc;

//size to read/write from hd pvr at.. 4k seems ok.
#define MEMBUF_INIT_SIZE 1328
#define MEMBUF_SIZE 1316

//port to listen on.
#define PORT 5555

#define GROUP "239.168.2.10"

int readPacket(int devfd, void *membuf) {

	int savedErrno;

	alarm(5);
	ssize_t nbytes = read(devfd, membuf, MEMBUF_SIZE);

	savedErrno = errno;                 /* In case alarm() changes it */
	alarm(0);                           /* Ensure timer is turned off */
	errno = savedErrno;

	if (0 > nbytes) {
		if (errno == EINTR)
			syslog(LOG_WARNING, "Read timed out");
		return nbytes;
	}

	// Are we aligned on a boundary?
	if (strncmp(membuf, "G", 1) == 0 && strncmp(membuf + 188, "G", 1) == 0)  {
			return nbytes;
	} else {
		// find a packet boundary
		// Don't fuss about trying to locate the start of the stream
		// in the buffer we've already read. Keep reading the stream
		// one byte at a time until we find it. This shouldn't be a
		// regular occurrence
		syslog(LOG_WARNING, "Alignment gone bad");
		int i = 0;
		while (0 < nbytes) {
			syslog(LOG_DEBUG, "Aligning to a packet boundary %d", i);
			nbytes = read(devfd, membuf, 1);
			if (0 > nbytes) return nbytes;
			if (strncmp(membuf, "G", 1) == 0) {
				syslog(LOG_DEBUG, "Found a G, checking alignment");
				nbytes = read(devfd, membuf + 1, 188);
				if (strncmp(membuf + 188, "G", 1) == 0)  {
					syslog(LOG_DEBUG, "aligned");
					nbytes = read(devfd, membuf + 189, MEMBUF_SIZE - 189);
					if (0 > nbytes) return nbytes;
					return nbytes + 189;
				}
			}
			i++;
		}
	}
}

long long current_timestamp() {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // caculate milliseconds
    return milliseconds;
}

int makeHeader(void *header) {
	int pointer = 0;

	unsigned char vpxcc = 0;
	vpxcc |= (2 << 6) & 0xC0;	// version	
	vpxcc |= (0 << 5) & 0x20;	// padding 0 = no, 1 = yes
	vpxcc |= (0 << 4) & 0x10;	// extension 0 = no, 1 = yes
	vpxcc |= (0 & 0x0F);		// number of CSRC
	memcpy(((char *)header) + pointer, &vpxcc, sizeof(unsigned char));
	pointer += sizeof(unsigned char);
	
	unsigned char mpt = 0;
	mpt |= (1 << 7) & 0x80;		// marker
	mpt |= (33 & 0x7F);		// payload type (MP2T)
	memcpy(((char *)header) + pointer, &mpt, sizeof(unsigned char));
	pointer += sizeof(unsigned char);

	unsigned short s = htons(seqnumber);
	memcpy(((char *)header) + pointer, &s, sizeof(unsigned short));
	pointer += sizeof(unsigned short);
	seqnumber++; 			// cannot overflow

	//long long curr = current_timestamp();
	//long diff = curr - oldtimestamp;
	//oldtimestamp = curr;
	//timestamp += diff;
	timestamp = 0;
	memcpy(((char *)header) + pointer, &timestamp, sizeof(unsigned long));
	pointer += sizeof(unsigned long);
	//timestamp++;

	unsigned long c = htonl(ssrc);
	memcpy(((char *)header) + pointer, &c, sizeof(unsigned long));

}


 static void handler(int sig) {
 }

int main(int argv, char **argc) {

	int devfd;
	char *ifname;
	ssize_t nbytes;
	int retval;
	struct sockaddr_in addr;
	int addrlen, sock, cnt;
	struct sigaction sa;
	void *membuf;

	// set up logging
	openlog("hdpvrd", LOG_PID|LOG_CONS|LOG_PERROR, LOG_DAEMON);

	// Set up the long read interrupt
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = handler;
	if (sigaction(SIGALRM, &sa, NULL) == -1)
		syslog(LOG_CRIT, "Can't create signal handler");

	// set up socket
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		syslog(LOG_CRIT, "Can't create multicast socket");
		exit(EXIT_FAILURE);
	}

	seqnumber = (unsigned short) rand();
	timestamp = (unsigned long)time(NULL);
	oldtimestamp = current_timestamp();
	ssrc = (unsigned int) rand();

	bzero((char *)&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(PORT);
	addrlen = sizeof(addr);
	addr.sin_addr.s_addr = inet_addr(GROUP);

	// ignore SIGPIPE
	signal(SIGPIPE, SIG_IGN);

	// set up file reader
	ifname = "/dev/video0";	//TODO: make this an arg 
	if (NULL == (membuf = malloc(MEMBUF_INIT_SIZE))) {
		syslog(LOG_CRIT, "Not enough memory for buffer");
		exit(EXIT_FAILURE);
	}

	if (-1 == (devfd = open(ifname, O_RDONLY))) {
		syslog(LOG_CRIT, "Unable to open hdpvr device");
		exit(EXIT_FAILURE);
	}

	// Let things settle
	usleep(5000);

	// start the recording loop
	syslog(LOG_INFO, "Starting to broadcast");
	int countdown = 0;
	while (1) {

			nbytes = readPacket(devfd, membuf + 12);
			if (0 > nbytes) {
				switch (errno) {
					syslog(LOG_CRIT, "errno response %d when reading device", errno);
					exit(EXIT_FAILURE);
				}
			} else {
				makeHeader(membuf);
				nbytes = sendto(sock, membuf, MEMBUF_INIT_SIZE, 0, (struct sockaddr *) &addr, addrlen);
				if (0 > nbytes) {
					syslog(LOG_CRIT, "Can't send data to multicast socket");
					exit(EXIT_FAILURE);
				}
			}
	}
}

