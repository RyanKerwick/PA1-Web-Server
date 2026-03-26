#ifndef MAIN_H
#define MAIN_H

#include <signal.h>
#include <sys/types.h>

// Global variables
extern volatile sig_atomic_t running;
extern int sock_desc;

// Structs
typedef struct {
    char method[8];
    char url[256];
    char version[16];

    int content_length;
    char content_type[64];
    char host[256];
    char connection[64];

    char *body;
} http_request_t;

#endif