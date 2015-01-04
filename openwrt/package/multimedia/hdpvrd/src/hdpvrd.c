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

//size to read/write from hd pvr at.. 4k seems ok.
#define MEMBUF_SIZE 4096

//how long to wait before giving up and declaring device needs a restart.
#define POLL_DELAY 2500

//port to listen on.
#define PORT 1101

//max buffers to use in buffered writer, each buffer is MEMBUF_SIZE bytes.
#define MAX_BUFFERS 8192 //4096 = 16mb, 8192 = 32mb, 12288 = 48mb

//max no of streaming clients.. pi can handle about 5
#define MAX_BUFFERED_OUTPUTS 4

//no of historical buffer counts to remember..
#define MAX_BUFFER_COUNT_HISTORY 300

//thread prototype for the connected children.
void *SocketHandler(void*); //handles accepted sockets
void *StreamHandler(void*); //handles reading from hdpvr and writing to outputs
void *IOHandler(void*); //buffered writer
void *BufferCleanUp(void*); //cleans up buffers if they remain free for 5 seconds

//data we'll share between all the threads.
struct shared {
    pthread_mutex_t sharedlock; // used to protect the fd array, and recording count
    int outfds[MAX_BUFFERED_OUTPUTS];
    char buffer[80];
    int recording;//no of fd's we are writing to.

    pthread_mutex_t diskiolock; //used to protect the data buffers and freebuffer arrays, and counts.
    int buffercount;
    int freecount;
    char **data;
    char **freebuffers;

    int historyFree[MAX_BUFFER_COUNT_HISTORY];
    int historyUsed[MAX_BUFFER_COUNT_HISTORY];
    int historyNowIndex;

};

//data we'll give to each thread uniquely.
struct data {
    int csock;
    struct shared *data; //pointer to shared data.
};

int main(int argv, char** argc) {
    signal(SIGPIPE, SIG_IGN);

    int host_port=PORT;

    struct sockaddr_in my_addr;

    int hsock;
    int * p_int ;

    socklen_t addr_size = 0;
    struct sockaddr_in sadr;
    pthread_t thread_id=0;

    //init the global data.
    struct shared *global = (struct shared*)malloc(sizeof(struct shared));
    global->recording=0;

    global->freebuffers = (char **)calloc(MAX_BUFFERS, sizeof(char *));
    global->data = (char **)calloc(MAX_BUFFERS, sizeof(char *));
    global->buffercount=0;
    global->freecount=0;
    memset(global->historyFree,0,MAX_BUFFER_COUNT_HISTORY*sizeof(int));
    memset(global->historyUsed,0,MAX_BUFFER_COUNT_HISTORY*sizeof(int));
    global->historyNowIndex=0;

    pthread_mutex_init(&(global->sharedlock), NULL);
    pthread_mutex_init(&(global->diskiolock), NULL);

    //start the IO thread, it will sleep until data is ready to write
    pthread_create(&thread_id,0,&IOHandler, (void*)global );
    pthread_detach(thread_id);

    //start the device read thread, it will sleep until there are clients to write to.
    pthread_create(&thread_id,0,&StreamHandler, (void*)global );
    pthread_detach(thread_id);

    //start the buffer cleanup thread.. it'll only kick in when the buffers exceed > 1024
    pthread_create(&thread_id,0,&BufferCleanUp, (void*)global );
    pthread_detach(thread_id);

    struct data *datainst;

    hsock = socket(AF_INET, SOCK_STREAM, 0);
    if(hsock == -1) {
        printf("Error initializing socket %d\n", errno);
        goto FINISH;
    }

    p_int = (int*)malloc(sizeof(int));
    *p_int = 1;

    if( (setsockopt(hsock, SOL_SOCKET, SO_REUSEADDR, (char*)p_int, sizeof(int)) == -1 )||
            (setsockopt(hsock, SOL_SOCKET, SO_KEEPALIVE, (char*)p_int, sizeof(int)) == -1 ) ) {
        printf("Error setting options %d\n", errno);
        free(p_int);
        goto FINISH;
    }
    free(p_int);

    my_addr.sin_family = AF_INET ;
    my_addr.sin_port = htons(host_port);

    memset(&(my_addr.sin_zero), 0, 8);
    my_addr.sin_addr.s_addr = INADDR_ANY ;

    if( bind( hsock, (struct sockaddr*)&my_addr, sizeof(my_addr)) == -1 ) {
        fprintf(stderr,"Error binding to socket, make sure nothing else is listening on this port %d\n",errno);
        goto FINISH;
    }

    if(listen( hsock, 10) == -1 ) {
        fprintf(stderr, "Error listening %d\n",errno);
        goto FINISH;
    }

    //Now lets do the server stuff
    addr_size = sizeof(struct sockaddr_in);
    while(1) {
        char tbuf[80];
        time_t timer;
        struct tm* tm_info;
        time(&timer);
        tm_info = localtime(&timer);
        strftime(tbuf,80,"%Y-%m-%d %H:%M:%S", tm_info);
        printf("%s Waiting for a connection on port %d \n", tbuf, PORT);
        //allocate the block we'll send to the thread
        datainst = (struct data *)malloc(sizeof(struct data));
        //hook up our global shared data struct..
        datainst->data = global;
        //get the socket, store into struct.
        if((datainst->csock = accept( hsock, (struct sockaddr*)&sadr, &addr_size))!= -1) {
            time(&timer);
            tm_info = localtime(&timer);
            strftime(tbuf,80,"%Y-%m-%d %H:%M:%S", tm_info);
            pthread_mutex_lock(&(global->sharedlock));
            printf("%s Received connection from %s\n",tbuf,inet_ntoa(sadr.sin_addr));
            pthread_mutex_unlock(&(global->sharedlock));
            pthread_create(&thread_id,0,&SocketHandler, (void*)datainst );
            pthread_detach(thread_id);
        } else {
            fprintf(stderr, "Error accepting %d\n", errno);
        }
    }

FINISH:
    free(global);
    return 0;
}

void *BufferCleanUp(void* lp) {
    struct shared *global = (struct shared *)lp;

    int freeloop=0;
    int freecounts[8] = {0,0,0,0,
    		             0,0,0,0};
    int currentFreeCount=0;

    //obtain the initial counts via the appropriate locks.
    pthread_mutex_lock(&(global->diskiolock));
    freecounts[0] = global->freecount;
    pthread_mutex_unlock(&(global->diskiolock));

    while(1){
        usleep(50000);
        currentFreeCount++;
        if(currentFreeCount>7){
            currentFreeCount=0;
        }
        global->historyNowIndex++;
        if(global->historyNowIndex >= MAX_BUFFER_COUNT_HISTORY){
            global->historyNowIndex = 0;
        }

        //obtain the initial counts via the appropriate locks.
        pthread_mutex_lock(&(global->diskiolock));
        freecounts[currentFreeCount] = global->freecount;
        global->historyFree[global->historyNowIndex] = global->freecount;
        global->historyUsed[global->historyNowIndex] = global->buffercount;

        //cheap way of checking all counts are over 1024.
        if(  (freecounts[0] >> 10)
           &&(freecounts[1] >> 10)
           &&(freecounts[2] >> 10)
           &&(freecounts[3] >> 10)
           &&(freecounts[4] >> 10)
           &&(freecounts[5] >> 10)
           &&(freecounts[6] >> 10)
           &&(freecounts[7] >> 10) ){
                //if we've been over 1024 for the last 8 loops.. we remove 512 buffers.
                //we'll then keep dropping 512 every loop while the count remains above 1024
                for(freeloop=0; freeloop<512; freeloop++){
                    char *buf = global->freebuffers[global->freecount-1];
                    free(buf);
                    global->freecount--;
                }
        }
        pthread_mutex_unlock(&(global->diskiolock));
    }
    return 0;
}

//ONLY CALL WHILE HOLDING global->sharedLock
void collapseFdArrays(struct shared *global){
	int outfdcount;
    int writepos=0;
    int currentmax=global->recording;
    for(outfdcount=0; outfdcount<currentmax; outfdcount++) {
        if(global->outfds[outfdcount] != -1) {
            if(writepos!=outfdcount) {
                //move the data back to the writepos slot, and set self to -1..
                global->outfds[writepos] = global->outfds[outfdcount];
                global->outfds[outfdcount] = -1;
            }
            writepos++;
        } else {
            global->recording--;
        }
    }
}

void sendBuffersToFds(int buffersToWrite, struct shared *global){
    pthread_mutex_lock(&(global->sharedlock));
    int outfdcount;
    int buffercount;
    //iterate over the output fd's.. set them to -1 if they fail to write.
    for(outfdcount=0; outfdcount<(global->recording); outfdcount++) {
        if(global->outfds[outfdcount]!=-1) {
                for(buffercount=0; buffercount<buffersToWrite; buffercount++){
                                ssize_t written = write(global->outfds[outfdcount], global->data[buffercount], MEMBUF_SIZE);
                                if(written==-1) {
                                        global->outfds[outfdcount]=-1;
                                }
                }
        }
    }
    //now issue the flushes.
    for(outfdcount=0; outfdcount<(global->recording); outfdcount++) {
        if(global->outfds[outfdcount]!=-1) {
                        fsync(global->outfds[outfdcount]);
        }
    }

    //we're still holding the global lock.. so we can manipulate the recording count,
    //and move the contents of the outfds array around without fear of a new client corrupting us.

    //iterate over the outputfd's.. collapsing the array to move the valids to the front.
    collapseFdArrays(global);

    pthread_mutex_unlock(&(global->sharedlock));
}

void *IOHandler(void* lp) {
    struct shared *global = (struct shared *)lp;

    int bufferstowrite=0;
    int enabled=0;

    //obtain the initial counts via the appropriate locks.
    pthread_mutex_lock(&(global->diskiolock));
    bufferstowrite = global->buffercount;
    pthread_mutex_unlock(&(global->diskiolock));

    pthread_mutex_lock(&(global->sharedlock));
    enabled = global->recording;
    pthread_mutex_unlock(&(global->sharedlock));

    while(1){
        //if we're not enabled yet, sleep a while, then reget the flag inside the appropriate lock.
        while(!enabled) {
            usleep(100);

            pthread_mutex_lock(&(global->diskiolock));
            bufferstowrite = global->buffercount;
            pthread_mutex_unlock(&(global->diskiolock));

            pthread_mutex_lock(&(global->sharedlock));
            enabled = global->recording;
            pthread_mutex_unlock(&(global->sharedlock));
        }

        //we're now enabled.. until we're not ;p
        while(enabled || bufferstowrite>0){
            //update the enabled flag from inside the lock
            pthread_mutex_lock(&(global->sharedlock));
            enabled = global->recording;
            pthread_mutex_unlock(&(global->sharedlock));

            //grab the current buffers to write value.
            //we release the lock, as only we ever remove buffers,
            //so we can allow new buffers to be added while we write the ones we have.
            pthread_mutex_lock(&(global->diskiolock));
            bufferstowrite = global->buffercount;
            pthread_mutex_unlock(&(global->diskiolock));

            //write thread might get ahead of read thread..
            if(bufferstowrite==0){
                if(enabled){
                    //just means we're still enabled, but no buffers have been added yet..
                    //wait a bit.. maybe more buffers will come =)
                    sleep(1);
                }
            }else{
                //we have buffers to write.. write out as many as we know there are
                //(there may be more to write by now.. if so we deal with them next loop)
                sendBuffersToFds(bufferstowrite,global);

                //we've written buffers..
                // so now we move the buffers still to write to the front of the write array
                // and move the buffers we've written across to the end of the free array

                //lock to protect the arrays while we nobble the data.
                pthread_mutex_lock(&(global->diskiolock));

                //reduce by no of buffers written
                global->buffercount -= bufferstowrite;

                //move the current buffers onto the end of the free array where they can be reused.
                memcpy(&(global->freebuffers[global->freecount]), global->data, (bufferstowrite * sizeof(char *)));
                global->freecount += bufferstowrite;

                //if buffers were incremented while we were writing.. move the pointers along a bit
                //so the reads can process from the start again..
                if(global->buffercount >0 ){
                    memcpy(global->data, &(global->data[bufferstowrite]), global->buffercount * sizeof(char *) );
                }

                //all done =) free array is added to, write array removed from, counts adjusted, release lock.
                pthread_mutex_unlock(&(global->diskiolock));
            }
        }
    }
    return 0;
}

//Device read thread.. pulls data from device, writes it to sockets
void *StreamHandler(void* lp) {
    struct shared *global = (struct shared *)lp;
    int devfd;
    char *ifname;
    struct pollfd fds[2];
    void *membuf;
    ssize_t nbytes;
    time_t timer;
    char buffer[80];
    struct tm* tm_info;
    int retval;

    ifname = "/dev/video0"; //TODO: make this an arg 
    if(NULL == (membuf = malloc(MEMBUF_SIZE))) {
        printf("Not enough memory to allocate buffer\n");
        fprintf(stderr, "Not enough memory\n");
        exit(EXIT_FAILURE);
    }
    while(1) {
        int enabled=0;
        pthread_mutex_lock(&(global->sharedlock));
        enabled = global->recording;
        pthread_mutex_unlock(&(global->sharedlock));
	// Wait for someone to connect
        while(!enabled) {
            usleep(500);
            pthread_mutex_lock(&(global->sharedlock));
            enabled = global->recording;
            pthread_mutex_unlock(&(global->sharedlock));
        }

        //someone enabled the datarelay, we'd better setup & start reading data.
        /** open the device **/
        if(-1 == (devfd = open(ifname, O_RDWR | O_NONBLOCK))) {
            perror("Unable to open device");
            exit(EXIT_FAILURE);
        }
        usleep(5000);

        /** setup descriptors for event polling **/
        fds[0].fd = devfd;
        fds[0].events = POLLIN;

        /** start the recording loop **/
        int countdown=0;
        while(enabled) {
            pthread_mutex_lock(&(global->sharedlock));
            enabled = global->recording;
            pthread_mutex_unlock(&(global->sharedlock));

            while(countdown>0) {
                retval = poll(fds, 2, POLL_DELAY);
                if(0 == retval) {
                    time(&timer);
                    tm_info = localtime(&timer);
                    strftime(buffer,80,"%Y-%m-%d %H:%M:%S", tm_info);
                    fprintf(stderr, "%s  Waiting for ready (%d)...\n", buffer, countdown);
                    usleep(100);
                    countdown--;
                } else {
                    countdown=0;
                }
            }
            retval = poll(fds, 2, POLL_DELAY);
            if(0 == retval) {
                time(&timer);
                tm_info = localtime(&timer);
                strftime(buffer,80,"%Y-%m-%d %H:%M:%S", tm_info);
                fprintf(stderr, "%s  Lost signal, restarting device...\n",buffer);
                close(devfd);
                countdown=5;
                if(-1 == (devfd = open(ifname, O_RDWR | O_NONBLOCK))) {
                    perror("Unable to reopen the device");
                    exit(EXIT_FAILURE);
                } else {
                    fds[0].fd = devfd;
                    fds[0].events = POLLIN;
                    fprintf(stderr,"%s Device reaquired. Wait for data\n",buffer);
                    usleep(500);
                    continue;
                }
            } else if(-1 == retval) {
                printf("polling failed\n");
                perror("Polling failed");
                break;
            } else if(fds[0].revents & POLLIN) {
                nbytes = read(devfd, membuf, MEMBUF_SIZE);
                if(0 > nbytes) {
                    switch(errno) {
                    case EINTR:
                    case EAGAIN:{
                        usleep(2500);
                        continue;
                    }
                    default:
                        printf("Unknown errno response %d when reading device\n",errno);
                        perror("Unknown");
                        exit(EXIT_FAILURE);
                    }
                } else if(MEMBUF_SIZE == nbytes) {
                    int recording=0;
                    pthread_mutex_lock(&(global->sharedlock));
                    recording=global->recording;
                    pthread_mutex_unlock(&(global->sharedlock));

                    //if recording.. write out to outfd.
                    if(recording){
                        //we're about to alter the write/free buffer arrays, so we need this lock.
                        pthread_mutex_lock(&(global->diskiolock));

                        char *bufToUse = NULL;
                        //do we have space?
                        if(!(global->buffercount<MAX_BUFFERS)){
                            //this will be a file corruption scenario, as the buffer MAY already be being written from.
                            //plus the data in this last buffer is overwritten and lost forever.
                            //in an ideal world, the write thread will catch up, and start freeing buffers,
                            //and we'll just lose a chunk of the ts.
                            perror("Out of write buffers!! - reusing last buffer.. ");
                            bufToUse = global->data[(global->buffercount)-1];
                            //reduce by 1, as later we re-increment it.
                            global->buffercount--;
                        }

                        //any buffers we can reuse?
                        if(bufToUse==NULL && global->freecount>0){
                            global->data[global->buffercount] = global->freebuffers[global->freecount -1];
                            bufToUse = global->data[global->buffercount];
                            //this buffer will no longer be free..
                            global->freecount--;
                        }

                        //no buffer yet, but if we have space, we can make one..
                        if(bufToUse==NULL && (global->buffercount + global->freecount)<MAX_BUFFERS){
                            if(NULL == (bufToUse = malloc(MEMBUF_SIZE))) {
                                printf("Not enough memory to allocate buffer (%d)\n",(global->freecount+global->buffercount));
                                fprintf(stderr, "Not enough memory\n");
                                exit(EXIT_FAILURE);
                            }
                            global->data[global->buffercount] = bufToUse;
                        }

                        //at this stage, we pretty much have to have a buffer.. right?
                        assert(bufToUse!=NULL);

                        //copy the data from the read buffer to the selected write buffer.
                        memcpy(bufToUse, membuf, MEMBUF_SIZE);

                        //bump the write counter, to say theres a new buffer to write.
                        global->buffercount++;

                        //all done playing with the buffers.. release the lock.
                        pthread_mutex_unlock(&(global->diskiolock));
                    }
                    continue;
                } else {
                    printf("Short read\n");
                    perror("Short read");
                    exit(EXIT_FAILURE);
                }
            } else if(fds[0].revents & POLLERR) {
                printf("Pollerr\n");
                perror("pollerr");
                exit(EXIT_FAILURE);
                break;
            }
        }
        /** clean up **/
        close(devfd);
    }

    free(membuf);
    return 0;
}

int getIndex(char *needle, char *haystack){
    char *found = strstr(haystack,needle);
    if(found!=NULL){
        return found-haystack;
    }else{
        return -1;
    }
}

void return404(char *buffer, int csock, char *reason){
    char *eol = strstr(buffer,"\n");
    const int maxlen=1024;
    char output[maxlen];
    memset(output,maxlen,0);
    if(eol==NULL || ((eol-buffer)>(maxlen-1))){
        strncpy(output,buffer,maxlen-1);
    }else{
        strncpy(output,buffer,(eol-buffer));
    }
    char *notfound = "HTTP/1.0 404 Not Found\nContent-Type: text/plain\n\nError: cannot handle request ";
    send(csock,notfound,strlen(notfound),0);
    send(csock,output,strlen(output),0);
    if(reason!=NULL){
        send(csock,"\n",1,0);
        send(csock,reason,strlen(reason),0);
    }
    fsync(csock);
    close(csock);
}

void processStatusRequest(char *buffer, int csock, struct shared *global){
        char *text="HTTP/1.0 200 OK\nContent-Type: text/plain\n\nStatus at : ";
        send(csock,text,strlen(text),0);
        int idx;
        char tbuf[80];
        time_t timer;
        struct tm* tm_info;
        time(&timer);
        tm_info = localtime(&timer);
        strftime(tbuf,80,"%Y-%m-%d %H:%M:%S", tm_info);
        send(csock,tbuf,strlen(tbuf),0);

        printf("%s Status request...\n",tbuf);

        int freebuffers = 0;
        int databuffers = 0;
        int noofconnections = 0;
        int recording = 0;
        pthread_mutex_lock(&(global->sharedlock));
        noofconnections = global->recording;
        recording = 0;
        for(idx=0; idx<global->recording; idx++){
        	recording = 1;
        }
        pthread_mutex_unlock(&(global->sharedlock));
        pthread_mutex_lock(&(global->diskiolock));
        freebuffers = global->freecount;
        databuffers = global->buffercount;
        pthread_mutex_unlock(&(global->diskiolock));

        snprintf(tbuf,80,"\nNo of Connections : %d\n",noofconnections);
        send(csock,tbuf,strlen(tbuf),0);
        snprintf(tbuf,80,  "Recording?        : %d\n",recording);
        send(csock,tbuf,strlen(tbuf),0);
        snprintf(tbuf,80,  "Free Buffer Count : %d\n",freebuffers);
        send(csock,tbuf,strlen(tbuf),0);
        snprintf(tbuf,80,  "Data Buffer Count : %d\n",databuffers);
        send(csock,tbuf,strlen(tbuf),0);
        snprintf(tbuf,80,  "Total Buffer Usage: %d\n",databuffers+freebuffers);
        send(csock,tbuf,strlen(tbuf),0);

        fsync(csock);
        close(csock);
}

void processVideoRequest(char *buffer, int csock, struct shared *global){
    char *text;
    if(global->recording < MAX_BUFFERED_OUTPUTS) {
            text="HTTP/1.0 200 OK\nContent-Type: video/h264\nSync-Point: no\nPre-roll: no\nMedia-Start: invalid\nMedia-End: invalid\nStream-Start: invalid\nStream-End: invalid\n\n";
    } else {
            text="HTTP/1.0 503 Service Unavailable\n\n";
    }
    send(csock,text,strlen(text),0);
    fsync(csock);

    if(global->recording < MAX_BUFFERED_OUTPUTS) {
	    pthread_mutex_lock(&(global->sharedlock));
	    global->outfds[global->recording]=csock;
	    global->recording++;
	    pthread_mutex_unlock(&(global->sharedlock));
    }
}

void processHttpRequest(char *buffer, int csock, struct shared *global){
    if(getIndex("GET /video HTTP/1.1",buffer)==0){
        processVideoRequest(buffer,csock,global);
    }else if(getIndex("GET /status HTTP/1.1",buffer)==0){
        processStatusRequest(buffer,csock,global);
    }else if(getIndex("GET /favicon.ico HTTP/1.",buffer)==0){
        return404(buffer,csock,"We don't do Favicons =)");
    }else{
        return404(buffer,csock,"Unknown request");
    }

}

void *SocketHandler(void* lp) {
    struct data *datainst = (struct data *)lp;
    int csock = datainst->csock; //(int*)lp;
    struct shared *global = datainst->data;

    const int buffer_len = 4096;
    char buffer[buffer_len];

    int bytecount;

    memset(buffer, 0, buffer_len);
    if((bytecount = recv(csock, buffer, buffer_len-1, 0))== -1) {
        fprintf(stderr, "Error receiving data %d\n", errno);
        goto FINISH;
    }

    //request was read ok.. now figure out what it asked for.
    processHttpRequest(buffer,csock,global);

FINISH:
    free(lp);
    return 0;
}
