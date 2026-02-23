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
#include <poll.h>
#include <fcntl.h>

#define PORT_NUM "9000"
#define IN_BUFFER_SIZE 4096
const char* packets_fp = "/var/tmp/aesdsocketdata";

// Buffer for the incoming packets (dynamic)
typedef struct {
    char* data;
    size_t data_len;
} input_buffer;

static input_buffer in_buf;
int terminate = 0;
int exit_signal = 0;

void init_in_buf() {
    in_buf.data = (char *)malloc(IN_BUFFER_SIZE*sizeof(char));
    in_buf.data_len = 0;
}

// Server structs
int server_fd, client_fd;
struct addrinfo hints;
struct addrinfo *gai_result;
struct sockaddr_in inbound_address;
socklen_t inbound_address_len = sizeof(struct sockaddr);

char ip_str[INET_ADDRSTRLEN];

void receiveDataFromSocket(int sockfd, FILE* file);
void sendDataBackToSocket(int sockfd, FILE* file);
extern void create_daemon();

void signalHandler(int sig) {
    int errno_saved = errno;
    terminate = 1;
    exit_signal = sig;
    errno = errno_saved;
}


int main(int argc, char const *argv[])
{
    int run_as_deamon = 0;

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
    }
    // Init in buffer for incoming packets
    init_in_buf();
    
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
    
    while(true) {
        struct pollfd pfd = {
            .fd = server_fd,
            .events = POLLIN
        };

        int ret = poll(&pfd, 1, 100); 
        if (ret < 0 && (errno != EINTR)) {
            syslog(LOG_ERR, "poll failed: %d (%s)\n", errno, strerror(errno));
            return -1;
        } else if ( ret < 0) {
            continue;
        } else if (ret == 0) {
            if (terminate) {
                if (access(packets_fp, F_OK) == 0) {
                    if (remove(packets_fp) != 0) {
                        syslog(LOG_ERR, "remove failed: %s", strerror(errno));
                    }
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
            continue;
        }

        if (pfd.revents & POLLIN) {
            if ((client_fd = accept(server_fd, (struct sockaddr *)&inbound_address, &inbound_address_len)) < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                syslog(LOG_ERR, "accept: %d (%s)\n", errno, strerror(errno));
                closelog();
                return -1;
            }   
        }

        // Get the IP of the inbound connection and print it to syslog
        inet_ntop(AF_INET, &inbound_address.sin_addr, ip_str, INET_ADDRSTRLEN);
        syslog(LOG_INFO,"Accepted connection from: %s\n", ip_str);

        // Create file to write incoming packets to
        FILE* fp = fopen(packets_fp, "ab+");

        // Receive incoming packets, write them to file then send them back
        receiveDataFromSocket(client_fd, fp);
        sendDataBackToSocket(client_fd, fp);
        
        syslog(LOG_INFO,"Closed connection from: %s\n", ip_str);
        fclose(fp);
        close(client_fd);
    }
    // Ideally we should never get here
    return -1;
}

void sendDataBackToSocket(int sockfd, FILE* file) {
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
                return;
            }
            start_ptr += n_sent;
            remaining -= n_sent;
        }
    }
    
    if (n_read < 0) {
        syslog(LOG_ERR, "fread failed: %s", strerror(errno));
    }
}

void receiveDataFromSocket(int sockfd, FILE* file) {
    char tmp_buf[IN_BUFFER_SIZE] = {};
    ssize_t n_received;
    char* nl = NULL;

    if(file == NULL) {
        syslog(LOG_ERR, "The TCP echo file %s could not be created! Errno: %s\n", packets_fp, strerror(errno));
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
            return;
        }
        char* tmp = realloc(in_buf.data,in_buf.data_len + n_received);

        if (!tmp) {
            syslog(LOG_ERR, "Realloc failed for packet! Error: %s\n", strerror(errno));
            free(in_buf.data);
            in_buf.data = NULL;
            in_buf.data_len = 0;
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
                return;
            }

            int ret_f = fflush(file);
            if(ret_f != 0) {
                syslog(LOG_ERR, "Could not flush to TCP echo file %s failed! Errno: %s\n", packets_fp, strerror(errno));
                return;
            }

            memmove(in_buf.data, nl + 1, in_buf.data_len - packet_len);
            in_buf.data_len -= packet_len;
            return;
        }
    }
}
