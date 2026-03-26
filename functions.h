#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include "main.h"

/* Signal handlers */
void sigintHandler(int sig);
void sigchldHandler(int sig);
void install_sigchldHandler(void);

/* Server helpers */
http_request_t parse_request(char *msg, int client_desc);
void handle_client(int client_desc);
int get_content_length(const char *buf);
void handle_request(int client_desc, http_request_t req);
void *file_to_buffer(FILE *fp, size_t *out_size);
void send_response(int client_desc, const void *body, size_t body_len, char *status, http_request_t req, char* cont_type);
char *get_file_ext(const char *filename);
char *get_content_type(const char *ext);

#endif // FUNCTIONS_H
