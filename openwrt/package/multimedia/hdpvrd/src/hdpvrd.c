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
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <signal.h>
#include <assert.h>
#include <syslog.h>

//size to read/write from hd pvr at.. 1316 is the size of one TS packet
#define MEMBUF_SIZE 1316

//port to listen on.
#define PORT 1101

//max no of streaming clients.. pi can handle about 5
#define MAX_BUFFERED_OUTPUTS 4

//thread prototype for the connected children.
void *SocketHandler(void *);    //handles accepted sockets
void *StreamHandler(void *);    //handles reading from hdpvr and writing to outputs

//data we'll share between all the threads.
struct shared {
        pthread_mutex_t sharedlock;     // used to protect the fd array, and recording count
	int outfds[MAX_BUFFERED_OUTPUTS];
        int recording;          //no of fd's we are writing to.
};

//data we'll give to each thread uniquely.
struct data {
        int csock;
        struct shared *data;
};

int main(int argv, char **argc)
{
	// Set up logging
        openlog("hdpvrd", LOG_PID|LOG_CONS|LOG_PERROR, LOG_DAEMON);
        syslog(LOG_INFO, "Starting");

        int host_port = PORT;

        struct sockaddr_in my_addr;

        int hsock;
        int *p_int;

        socklen_t addr_size = 0;
        struct sockaddr_in sadr;
        pthread_t thread_id = 0;


        //init the global data.
        struct shared *global = (struct shared *)malloc(sizeof(struct shared));
        global->recording = 0;

	//init the fd array
	int n = 0;
	for (n = 0; n < MAX_BUFFERED_OUTPUTS; n++) {
		global->outfds[n] = -1;
	}

	// init the shared lock
        pthread_mutex_init(&(global->sharedlock), NULL);

        //start the device read thread
        pthread_create(&thread_id, NULL, &StreamHandler, (void *)global);
        //pthread_detach(thread_id);

        struct data *datainst;

	// Socket stuff
        hsock = socket(AF_INET, SOCK_STREAM, 0);
        if (hsock == -1) {
                syslog(LOG_CRIT, "Error initializing socket %d", errno);
                goto FINISH;
        }

        p_int = (int *)malloc(sizeof(int));
        *p_int = 1;

        if ((setsockopt
             (hsock, SOL_SOCKET, SO_REUSEADDR, (char *)p_int,
              sizeof(int)) == -1)
            ||
            (setsockopt
             (hsock, SOL_SOCKET, SO_KEEPALIVE, (char *)p_int,
              sizeof(int)) == -1)) {
                syslog(LOG_CRIT, "Error setting socket options %d", errno);
                free(p_int);
                goto FINISH;
        }
        free(p_int);

        my_addr.sin_family = AF_INET;
        my_addr.sin_port = htons(host_port);

        memset(&(my_addr.sin_zero), 0, 8);
        my_addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(hsock, (struct sockaddr *)&my_addr, sizeof(my_addr)) == -1) {
                syslog(LOG_CRIT, "Error binding to socket, make sure nothing else is listening on this port %d", errno);
                goto FINISH;
        }

        if (listen(hsock, 10) == -1) {
                syslog(LOG_CRIT, "Error listening %d", errno);
                goto FINISH;
        }
        //Now lets do the server stuff
        addr_size = sizeof(struct sockaddr_in);
        syslog(LOG_INFO, "Waiting for a connection on port %d", PORT);
        while (1) {
                time_t timer;
                struct tm *tm_info;
                time(&timer);
                tm_info = localtime(&timer);
                //allocate the block we'll send to the thread
                datainst = (struct data *)malloc(sizeof(struct data));
                //hook up our global shared data struct..
                datainst->data = global;
                //get the socket, store into struct.
                if ((datainst->csock =
                     accept(hsock, (struct sockaddr *)&sadr, &addr_size)) != -1)
                {
                        time(&timer);
                        pthread_mutex_lock(&(global->sharedlock));
                        syslog(LOG_INFO, "Received connection from %s", inet_ntoa(sadr.sin_addr));
                        pthread_mutex_unlock(&(global->sharedlock));
                        pthread_create(&thread_id, NULL, &SocketHandler,
                                       (void *)datainst);
                        //pthread_detach(thread_id);
                } else {
                        syslog(LOG_ERR, "Error accepting connection %d\n", errno);
                }
        }

 FINISH:
        free(global);
        return 0;
}

int openDevice() {
        int devfd;
        /** open the hdpvr device **/
        if (-1 == (devfd = open("/dev/video0", O_RDONLY|O_NONBLOCK))) {
                syslog(LOG_CRIT, "Unable to open hdpvr device");
                exit(EXIT_FAILURE);
        }
	return devfd;
}


//Device read thread.. pulls data from device, writes it to sockets
void *StreamHandler(void *lp)
{
        struct shared *global = (struct shared *)lp;
        int devfd;
        char *ifname;
        void *membuf;
        ssize_t nbytes;
        time_t timer;
        char buffer[80];
        struct tm *tm_info;
        int retval;

	struct timeval timeout;
	fd_set fds;
	FD_ZERO(&fds);


        ifname = "/dev/video0"; //TODO: make this an arg 
        if (NULL == (membuf = malloc(MEMBUF_SIZE))) {
                syslog(LOG_CRIT, "Not enough memory");
                exit(EXIT_FAILURE);
        }
        /** open the hdpvr device **/
	devfd = openDevice();
	FD_SET(devfd, &fds);


        /** start the recording loop **/
        while (1) {

		timeout.tv_sec = 5;
		timeout.tv_usec = 0;

		int ret = select(devfd + 1, &fds, NULL, NULL, &timeout);
		if (0 > ret) {
			syslog(LOG_CRIT, "Select failure %d", errno);
			exit(EXIT_FAILURE);
		} else if (0 == ret) {
			syslog(LOG_ERR, "Device not responding, restarting");
			FD_CLR(devfd, &fds);
			close(devfd);
			sleep(1);
			devfd = openDevice();
			sleep(2);
			FD_SET(devfd, &fds);
		} else if(FD_ISSET(devfd, &fds)) {

			nbytes = read(devfd, membuf, MEMBUF_SIZE);
			if (0 > nbytes)
				continue;

			// send to all output sockets
			// TODO use a write selector
			int qno;
			for (qno = 0; qno < (global->recording); qno++) {
				ret = write(global->outfds[qno], membuf, nbytes);
				if (ret == -1) {
					syslog(LOG_INFO, "player disconnected");
					pthread_mutex_lock(&(global->sharedlock));
					close(global->outfds[qno]);
					global->outfds[qno] = -1;
					global->recording--;
					pthread_mutex_unlock(&(global->sharedlock));
				}
			}
		}
        }
        /** clean up **/
        close(devfd);

        free(membuf);
        return 0;
}

int getIndex(char *needle, char *haystack)
{
        char *found = strstr(haystack, needle);
        if (found != NULL) {
                return found - haystack;
        } else {
                return -1;
        }
}

void return404(char *buffer, int csock, char *reason)
{
        char *eol = strstr(buffer, "\n");
        const int maxlen = 1024;
        char output[maxlen];
        memset(output, maxlen, 0);
        if (eol == NULL || ((eol - buffer) > (maxlen - 1))) {
                strncpy(output, buffer, maxlen - 1);
        } else {
                strncpy(output, buffer, (eol - buffer));
        }
        char *notfound =
            "HTTP/1.0 404 Not Found\nContent-Type: text/plain\n\nError: cannot handle request ";
        send(csock, notfound, strlen(notfound), 0);
        send(csock, output, strlen(output), 0);
        if (reason != NULL) {
                send(csock, "\n", 1, 0);
                send(csock, reason, strlen(reason), 0);
        }
        fsync(csock);
        close(csock);
}

void processVideoRequest(char *buffer, int csock, struct shared *global)
{
        char *text;
        if (global->recording < MAX_BUFFERED_OUTPUTS) {
                pthread_mutex_lock(&(global->sharedlock));
		// find a slot
		int i;
		int set = 0;
		for (i = 0; i < MAX_BUFFERED_OUTPUTS; i++) {
			if (global->outfds[i] == -1) { 
				global->outfds[i] = csock;
				global->recording++;
				set++;
				break;
			}
		}
                pthread_mutex_unlock(&(global->sharedlock));

		if (set != 0) {
			text = "HTTP/1.0 200 OK\nContent-Type: video/h264\nSync-Point: no\nPre-roll: no\nMedia-Start: invalid\nMedia-End: invalid\nStream-Start: invalid\nStream-End: invalid\n\n";
			send(csock, text, strlen(text), 0);
			fsync(csock);
			return;
		}

        }

	text = "HTTP/1.0 503 Service Unavailable\n\n";
       	send(csock, text, strlen(text), 0);
       	fsync(csock);
	close(csock);
}

void processHttpRequest(char *buffer, int csock, struct shared *global)
{
        if (getIndex("GET /video HTTP/1.1", buffer) == 0) {
                processVideoRequest(buffer, csock, global);
        } else {
                return404(buffer, csock, "Unknown request");
        }

}

void *SocketHandler(void *lp)
{
        struct data *datainst = (struct data *)lp;
        int csock = datainst->csock;    //(int*)lp;
        struct shared *global = datainst->data;

        const int buffer_len = 4096;
        char buffer[buffer_len];

        int bytecount;

        memset(buffer, 0, buffer_len);
        if ((bytecount = recv(csock, buffer, buffer_len - 1, 0)) == -1) {
                syslog(LOG_WARNING, "Error receiving data %d", errno);
                goto FINISH;
        }
        //request was read ok.. now figure out what it asked for.
        processHttpRequest(buffer, csock, global);

 FINISH:
        free(lp);
        return 0;
}
