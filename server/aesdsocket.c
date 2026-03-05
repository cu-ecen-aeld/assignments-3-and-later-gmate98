#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <time.h>

#include "queue.h"

#define PORT_NUM "9000"
#define IN_BUFFER_SIZE 4096
const char* packets_fp = "/var/tmp/aesdsocketdata";

// Buffer for the incoming packets (dynamic)
typedef struct {
    char* data;
    size_t data_len;
} input_buffer;
struct send_recv_thread_data {
    pthread_mutex_t* fp_mutex;
    struct sockaddr_in inbound_address;
    bool fininshed;
    int client_fd;
};

typedef struct slist_threads_s {
    pthread_t thread_id;
    struct send_recv_thread_data* tdata;
    SLIST_ENTRY(slist_threads_s) threads;
} slist_threads_t;

SLIST_HEAD(slisthead, slist_threads_s) head;

static input_buffer in_buf;
static atomic_int terminate = 0;
atomic_int exit_signal = 0;

void init_in_buf() {
    in_buf.data = (char *)malloc(IN_BUFFER_SIZE*sizeof(char));
    in_buf.data_len = 0;
}

void init_threads_slist() {
    SLIST_INIT(&head);
}

void signalHandler(int sig) {
    int errno_saved = errno;
    terminate = 1;
    exit_signal = sig;
    errno = errno_saved;
}

// Server structs
int server_fd, client_fd;
struct addrinfo hints;
struct addrinfo *gai_result;
struct sockaddr_in inbound_address;
socklen_t inbound_address_len = sizeof(struct sockaddr);

void receiveDataFromSocket(int sockfd, FILE* file, bool* err_flag);
void sendDataBackToSocket(int sockfd, FILE* file, bool* err_flag);
void* sendAndReceiveFunc(void* thread_params);
void insertThreadIntoSlist(pthread_t tid, struct send_recv_thread_data* tdata);
void cleanupFinishedThreads();
void generateTimeStamp(union sigval sigval);
void setUpTimer(pthread_mutex_t* mutex);

extern void create_daemon();

int main(int argc, char const *argv[])
{
    int run_as_deamon = 0;
    pthread_mutex_t fp_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_t thread_id;

    // Parse input args
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            run_as_deamon = 1;
        } else {
            printf("Usage: %s [-d]\n", argv[0]);
            printf("  -d    Run as deamon.\n");
            printf("\nInvalid argument: %s\n", argv[i]);
            return -1;
        }
    }
    
    // Open connection to syslog 
    if (run_as_deamon == 0) {
        openlog("aesdsocket",LOG_PID,LOG_USER);
        syslog(LOG_INFO, "Started aesdsocket.");
    }
    // Init in buffer for incoming packets, init threads singly linked list
    init_in_buf();
    init_threads_slist();
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM; /* Datagram socket */
    hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */
    hints.ai_protocol = 0;          /* Any protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
    
    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        syslog(LOG_ERR, "socket: %d (%s) creating socket!\n", errno, strerror(errno));
        closelog();
        return -1;
    }
    
    int optval = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    // Setting up socket address
    if (getaddrinfo(NULL, PORT_NUM, &hints, &gai_result) < 0) {
        syslog(LOG_ERR, "getaddrinfo: %d (%s)\n", errno, gai_strerror(errno));
        closelog();
        return -1;
    }
    
    // Bind to port
    if (bind(server_fd, gai_result->ai_addr, gai_result->ai_addrlen) < 0) {
        syslog(LOG_ERR, "bind: %d (%s)\n", errno, strerror(errno));
        closelog();
        freeaddrinfo(gai_result);
        return -1;
    };

    if (run_as_deamon) {
        create_daemon();
    }

    // Let's listen (only allow max 3 connections)
    if (listen(server_fd, 3) < 0) {
        syslog(LOG_ERR, "listen: %d (%s)\n", errno, strerror(errno));
        closelog();
        return -1;
    }

    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    //Init signal handlers
    struct sigaction new_action;

    memset(&new_action,0,sizeof(struct sigaction));
    new_action.sa_handler=signalHandler;
    if( sigaction(SIGTERM, &new_action, NULL) != 0 ) {
        syslog(LOG_ERR,"Error %d (%s) registering for SIGTERM",errno,strerror(errno));
        return -1;
    }
    if( sigaction(SIGINT, &new_action, NULL) ) {
        syslog(LOG_ERR,"Error %d (%s) registering for SIGINT",errno,strerror(errno));
        return -1;
    }

    // Set up the timer for the timestamping into /var/tmp/aesdsocket every 10s
    setUpTimer(&fp_mutex);
    
    while(true) {
        cleanupFinishedThreads();

        if (atomic_load(&terminate)) {
            if (access(packets_fp, F_OK) == 0) {
                if (remove(packets_fp) != 0) syslog(LOG_ERR, "remove failed: %s", strerror(errno));
            } else {
                syslog(LOG_INFO, "The TCP echo file: %s does not exist.", packets_fp);
            }

            // Log to syslog that we are exiting
            syslog(LOG_INFO, "Caught signal, exiting\n");
            if (in_buf.data != NULL) {
                free(in_buf.data);
                in_buf.data = NULL;
            }
            freeaddrinfo(gai_result);
            closelog();
            _exit(exit_signal);
        }

        if ((client_fd = accept(server_fd, (struct sockaddr *)&inbound_address, &inbound_address_len)) < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            syslog(LOG_ERR, "accept: %d (%s)\n", errno, strerror(errno));
            closelog();
            return -1;
        }    


        struct send_recv_thread_data* tdata = calloc(1,sizeof(struct send_recv_thread_data));
        if(tdata == NULL){
            syslog(LOG_ERR, "calloc: %d (%s)\n", errno, strerror(errno));
            return -1;
        }

        // Initilaize thread_data
        tdata->fp_mutex = &fp_mutex;
        tdata->client_fd = client_fd;
        tdata->inbound_address = inbound_address;
        tdata->fininshed = false;

        // Create thread and pass thread parameters
        if(pthread_create(&thread_id, NULL, sendAndReceiveFunc, (void *)tdata)) {
            free(tdata);
            syslog(LOG_ERR, "pthread_create: %d (%s)\n", errno, strerror(errno));
            return -1;
        }

        // Insert the newly created thread into the list
        insertThreadIntoSlist(thread_id, tdata);
    }
    // Ideally we should never get here
    return -1;
}

void sendDataBackToSocket(int sockfd, FILE* file, bool* err_flag) {
    char send_buf[4096];
    ssize_t n_read, n_sent;

    rewind(file);

    // Send file content in chunks
    while ((n_read = fread(send_buf, sizeof(char), sizeof(send_buf), file)) > 0) {
        char *start_ptr = send_buf;
        size_t remaining = n_read;
        
        while (remaining > 0) {
            n_sent = send(sockfd, start_ptr, remaining, 0);
            if (n_sent <= 0) {
                syslog(LOG_ERR, "send failed: %s", strerror(errno));
                *err_flag = 1;
                return;
            }
            start_ptr += n_sent;
            remaining -= n_sent;
        }
    }
    
    if (n_read < 0) {
        syslog(LOG_ERR, "fread failed: %s", strerror(errno));
        *err_flag = 1;
    }
}

void receiveDataFromSocket(int sockfd, FILE* file, bool* err_flag) {
    char tmp_buf[IN_BUFFER_SIZE] = {};
    ssize_t n_received;
    char* nl = NULL;

    if(file == NULL) {
        syslog(LOG_ERR, "The TCP echo file %s could not be created! Errno: %s\n", packets_fp, strerror(errno));
        *err_flag = 1;
        return;
    }

    while(true) {
        n_received = recv(sockfd,tmp_buf,sizeof(tmp_buf),0);
        if(n_received == 0) {
            free(in_buf.data);
            in_buf.data = NULL;
            in_buf.data_len = 0;
            return;
        };

        if(in_buf.data == NULL) {
            *err_flag = 1;
            return;
        }
        char* tmp = realloc(in_buf.data,in_buf.data_len + n_received);

        if (!tmp) {
            syslog(LOG_ERR, "Realloc failed for packet! Error: %s\n", strerror(errno));
            free(in_buf.data);
            in_buf.data = NULL;
            in_buf.data_len = 0;
            *err_flag = 1;
            return;  // discard over-length
        }
        in_buf.data = tmp;
        memcpy(in_buf.data + in_buf.data_len, tmp_buf, n_received);
        in_buf.data_len += n_received;

        nl = memchr(in_buf.data, '\n', in_buf.data_len);

        if(nl) {
            size_t packet_len = nl - in_buf.data + 1;
            
            size_t ret = fwrite(in_buf.data, sizeof(char), packet_len, file);
            if(ret < packet_len) {
                syslog(LOG_ERR, "Writing the TCP echo file %s failed! Errno: %s\n", packets_fp, strerror(errno));
                *err_flag = 1;
                return;
            }

            int ret_f = fflush(file);
            if(ret_f != 0) {
                syslog(LOG_ERR, "Could not flush to TCP echo file %s failed! Errno: %s\n", packets_fp, strerror(errno));
                *err_flag = 1;
                return;
            }

            memmove(in_buf.data, nl + 1, in_buf.data_len - packet_len);
            in_buf.data_len -= packet_len;
            return;
        }
    }
}

void* sendAndReceiveFunc(void* thread_params) {
        char ip_str[INET_ADDRSTRLEN];
        struct send_recv_thread_data* thread_func_args = (struct send_recv_thread_data *) thread_params;
        bool err = false;

        // Get the IP of the inbound connection and print it to syslog
        inet_ntop(AF_INET, &thread_func_args->inbound_address.sin_addr, ip_str, INET_ADDRSTRLEN);
        syslog(LOG_INFO,"Accepted connection from: %s\n", ip_str);

        // Create file to write incoming packets to
        FILE* fp = fopen(packets_fp, "ab+");

        do {
            // Acquire and lock mutex before writing to packets_fp
            if(pthread_mutex_lock(thread_func_args->fp_mutex)) {
                syslog(LOG_ERR, "pthread_mutex_lock: %d (%s)\n", errno, strerror(errno));
                err = true;
                break;
            }

            // Check if signal was received
            if (atomic_load(&terminate)) break;
    
            // Receive incoming packets, write them to file then send them back
            receiveDataFromSocket(thread_func_args->client_fd, fp, &err);

            // Check if signal was received
            if (atomic_load(&terminate)) break;

            sendDataBackToSocket(thread_func_args->client_fd, fp, &err);
    
            // Unlock mutex before exiting
            if(pthread_mutex_unlock(thread_func_args->fp_mutex)) {
                syslog(LOG_ERR, "pthread_mutex_unlock: %d (%s)\n", errno, strerror(errno));
                err = true;
                break;
            }
        } while (0);
        
        // Cleanup
        fclose(fp);
        close(thread_func_args->client_fd);
        if (err) {
            syslog(LOG_INFO,"An error occured, closed connection from: %s\n", ip_str);
            thread_func_args->fininshed = true;
            return NULL;
        } else {
            syslog(LOG_INFO,"Closed connection from: %s\n", ip_str);
            thread_func_args->fininshed = true;
            return NULL;
        }
}

void insertThreadIntoSlist(pthread_t tid, struct send_recv_thread_data* tdata) {
    slist_threads_t *threadp = malloc(sizeof(slist_threads_t));
    threadp->thread_id = tid;
    threadp->tdata = tdata;
    SLIST_INSERT_HEAD(&head, threadp, threads);
}

void cleanupFinishedThreads() {
    slist_threads_t *n, *tmp;
    SLIST_FOREACH_SAFE(n, &head, threads, tmp) {
        if (n->tdata->fininshed == true) {
            pthread_join(n->thread_id, NULL);
            SLIST_REMOVE(&head, n, slist_threads_s ,threads);
            free(n->tdata);
            free(n);
        }
    }
}

static inline void timespec_add( struct timespec *result,
                        const struct timespec *ts_1, const struct timespec *ts_2)
{
    result->tv_sec = ts_1->tv_sec + ts_2->tv_sec;
    result->tv_nsec = ts_1->tv_nsec + ts_2->tv_nsec;
    if( result->tv_nsec > 1000000000L ) {
        result->tv_nsec -= 1000000000L;
        result->tv_sec ++;
    }
}

void setUpTimer(pthread_mutex_t* mutex) {
    struct sigevent sev;
    timer_t timerid;
    memset(&sev,0,sizeof(struct sigevent));
    int clock_id = CLOCK_MONOTONIC;
    struct timespec start_time;

    /**
    * Setup a call to timer_thread passing in the td structure as the sigev_value
    * argument
    */
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_value.sival_ptr = mutex;
    sev.sigev_notify_function = generateTimeStamp;
    if ( timer_create(clock_id,&sev,&timerid) != 0 ) {
        syslog(LOG_ERR, "timer_create: %s", strerror(errno));
        return;
    }

    if ( clock_gettime(clock_id,&start_time) != 0 ) {
        syslog(LOG_ERR, "clock_gettime: %s", strerror(errno));
        return;
    } else {
        struct itimerspec its;
        memset(&its, 0, sizeof(struct itimerspec));
        its.it_interval.tv_sec = 10;
        its.it_interval.tv_nsec = 0;
        timespec_add(&its.it_value,&start_time,&its.it_interval);
        if( timer_settime(timerid, TIMER_ABSTIME, &its, NULL ) != 0 ) {
            syslog(LOG_ERR, "timer_settime: %s", strerror(errno));
        }
        return;
    }
}

void generateTimeStamp(union sigval sigval) {
    pthread_mutex_t* mutex = (pthread_mutex_t *) sigval.sival_ptr;
    const char label[] = "timestamp:";
    char timestamp[100] = {};
    memcpy(timestamp, label, sizeof(label));

    FILE* fp = fopen(packets_fp, "a+");

    if ( pthread_mutex_lock(mutex) != 0 ) {
        syslog(LOG_ERR, "pthread_mutex_lock: %s", strerror(errno));
        return;
    }

    time_t t = time(NULL);
    struct tm *tmp = localtime(&t);

    if (tmp == NULL) {
        syslog(LOG_ERR, "localtime: %s", strerror(errno));
        return;
    }

    size_t len = strftime(timestamp+sizeof(label), sizeof(timestamp)-sizeof(label), "%a, %d %b %Y %H:%M:%S %z \n", tmp);
    if (len == 0) {
        syslog(LOG_ERR, "strftime: %s", strerror(errno));
        return;
    } else if (len > sizeof(timestamp)) {
        syslog(LOG_ERR, "Timestamp too long!");
    }

    size_t ret = fwrite(timestamp, sizeof(char), len+sizeof(label), fp);
    if (ret < len) {
        syslog(LOG_ERR, "Unable to write timestamp!");
    }
    fclose(fp);

    if ( pthread_mutex_unlock(mutex) != 0 ) {
        syslog(LOG_ERR, "pthread_mutex_unlock: %s", strerror(errno));
    }
}

