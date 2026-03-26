// PA1/main.c

/**
* TCP-based web server that handles multiple simultaneous requests from users
*/



/**
* The HTTP Request
*   request method: "POST" | "GET" | "HEAD" | ...
*   request URL
*   request version: HTTP/x.y ex. HTTP/1.1
*/

/**
* The HTTP Response
*   HTTP/1.1 200 OK (Status)
*   Headers: ex. Content-Type, Content-Length
*   File contents
*
* NOTE: each response Header must be delimited by a terminating CLRF (\r\n)
*   And, the file content should be separated by \r\n\r\n
*/

/**
* The Web Server
*   Command line: ./server <port number>
*   Should run in a loop, and exit gracefully upon Ctrl+C
*   Document Root
*       The base directory which files exist, should be accessed as GET /directory_name/file_name HTTP/1.1. The Document Root here is directory_name and file_name exists in that directory.
*/

/**
* Handling Multiple Connections
*   Use either a multi-threaded or multi-process implementation to service requests.
*/

/**
* Testing the Web Server
*   Use a browser, netcat, or wget, curl, or aria2c
*   Command line test:
*   (echo -en "GET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: Keep-alive\r\n\r\n"; sleep 10; echo -en "GET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: Keep-alive\r\n\r\n"; sleep 10;  echo -en "GET /index.html HTTP/1.1\r\nHost: localhost\r\nConnection: Close\r\n\r\n") | nc 127.0.0.1 9000
*/

/**
* Compile with `gcc main.c -o server`
* Run with `./server`
*/


/*
    The model:

    Parent process:
        socket()
        bind()
        listen()

    loop:
        client_fd = accept()
        fork()

        Parent:
            close(client_fd)
            continue accepting

        Child:
            close(listen_fd)
            handle client using client_fd
            exit()
*/

// Libraries
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/wait.h>
#include <pthread.h>

#include "main.h"
#include "functions.h"

// Global variables
volatile sig_atomic_t running = 1;
int sock_desc = -1;


// void *connection_handler(void *socket_desc) {
//     int sock = *(int*)socket_desc;
//     handle_client(sock);
//     return NULL;
// }


// Program entry point
int main(int argc, char *argv[]) {
    long port = 0;
    char *endptr = NULL;
    errno = 0;

    uint16_t port_net = 0;

    struct sockaddr_in sock_addr;
    memset(&sock_addr, 0, sizeof(sock_addr));

    int client_desc = 0;

    // Parse command line argument port number
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        return 1;
    }
    
    port = strtol(argv[1], &endptr, 10);
    if (errno != 0 || *endptr != '\0' || port < 1 || port > 65535) {
        fprintf(stderr, "Invalid port number\n");
        return 1;
    }

    printf("Starting server on port %ld\n", port);

    install_sigchldHandler();

    if (signal(SIGINT, sigintHandler) == SIG_ERR) {
        perror("signal");
        return 1;
    }

    // Initialize socket
    sock_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_desc < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(sock_desc, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        return 1;
    }

    port_net = htons((uint16_t)port);
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = port_net;
    sock_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock_desc, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(sock_desc, SOMAXCONN) < 0) {
        perror("listen");
        return 1;
    }

    while (running) {
        printf("Waiting for a connection...\n");

        // Receive connection from client
        client_desc = accept(sock_desc, NULL, NULL);
        if (client_desc < 0) {
            if (!running) {
                break; 
            }
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }

        // pthread_t sniffer_thread;
        // int* new_sock = malloc(1);
        // *new_sock = client_desc;

        // if (pthread_create(&sniffer_thread, NULL, connection_handler, (void*) new_sock) < 0) {
        //     perror("pthread_create");
        //     return 1;
        // }

        // pthread_join(sniffer_thread, NULL);

        pid_t pid = fork(); // Create the child process

        if(pid < 0){
            perror("fork fail");
            close(client_desc);     // Too many processes running, drop the client
            continue;
        }
        // child process because return value zero
        else if ( pid == 0) {
            signal(SIGINT, SIG_IGN); // Ignore Ctrl+C

            close(sock_desc);
            handle_client(client_desc);

            close(client_desc);

            exit(0);
        }

        // parent process because return value non-zero.
        else {

            close(client_desc);

        }
    }

    printf("\nCtrl+C caught, exiting program.\n");

    close(sock_desc);
    return 0;
}