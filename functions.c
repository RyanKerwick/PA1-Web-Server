#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#include "functions.h"
#include "main.h"

#define MAX_REQUEST_SIZE 65536

// Helper functions
void sigintHandler(int sig) {
    (void)sig;
    running = 0;
    if (sock_desc != -1) {
        close(sock_desc);
        sock_desc = -1;
    }
}

http_request_t parse_request(char *msg, int client_desc) {
    http_request_t parsed_request;
    memset(&parsed_request, 0, sizeof(parsed_request));

    // Separate headers and body of request
    char *header_end = strstr(msg, "\r\n\r\n");
    if (!header_end) {
        send_response(client_desc, "400 Bad Request\n", strlen("400 Bad Request\n"), "400 Bad Request", parsed_request, NULL);
        return parsed_request;
    }

    *header_end = '\0'; // terminate the headers
    char *body = header_end + 4; // Skip delimiter
    printf("Body: %s\n", body);

    // Separate request line and method, url, and version
    char *request_line = msg;
    char *line_end = strstr(request_line, "\r\n");
    if (!line_end) {
        send_response(client_desc, "400 Bad Request\n", strlen("400 Bad Request\n"), "400 Bad Request", parsed_request, NULL);
        return parsed_request;
    }
    *line_end = '\0'; // terminate the request line

    char *method = strtok(request_line, " ");
    char *url = strtok(NULL, " ");
    char *version = strtok(NULL, " ");

    if (!method || !url || !version) {
        send_response(client_desc, "400 Bad Request\n", strlen("400 Bad Request\n"), "400 Bad Request", parsed_request, NULL);
        return parsed_request;
    }
    printf("Method: %s\nURL: %s\nVersion: %s\n", method, url, version);

    parsed_request.body = body;

    strncpy(parsed_request.method, method, sizeof(parsed_request.method) - 1);
    strncpy(parsed_request.url, url, sizeof(parsed_request.url) - 1);
    strncpy(parsed_request.version, version, sizeof(parsed_request.version) - 1);

    if (! (strcmp(version, "HTTP/1.1") == 0 || strcmp(version, "HTTP/1.0") == 0) ) {
        send_response(client_desc, "505 HTTP Version Not Supported\n", strlen("505 HTTP Version Not Supported\n"), "505 HTTP Versioin Not Supported", parsed_request, NULL);
        return parsed_request;
    }

    char *headers = line_end + 2; // skip delimiter

    char *line = strtok(headers, "\r\n");
    while (line) {
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char *key = line;
            char *value = colon + 1;

            while (*value == ' ') value++;  // trim spaces

            printf("Header: %s = %s\n", key, value);

            
            if (strcasecmp(key, "Host") == 0) {
                strncpy(parsed_request.host, value, sizeof(parsed_request.host) - 1);
            }
            else if (strcasecmp(key, "Content-Length") == 0) {
                parsed_request.content_length = atoi(value);
            }
            else if (strcasecmp(key, "Content-Type") == 0) {
                strncpy(parsed_request.content_type, value, sizeof(parsed_request.content_type) - 1);
            }
            else if (strcasecmp(key, "Connection") == 0) {
                strncpy(parsed_request.connection, value, sizeof(parsed_request.connection) - 1);
            }
        }
        line = strtok(NULL, "\r\n");
    }

    return parsed_request;
}

void sigchldHandler(int sig) {
    (void)sig;
    int saved_errno = errno;

    while (waitpid(-1, NULL, WNOHANG) > 0) {
        // Do nothing
    }

    errno = saved_errno;
}

void install_sigchldHandler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchldHandler;
    sigemptyset(&sa.sa_mask);

    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        perror("sigaction(SIGCHLD)");
        exit(1);
    }
}

int get_content_length(const char *buf) {
    const char *cur = buf;

    while ((cur = strstr(cur, "\r\n")) != NULL) {
        cur += 2;
        if (strncasecmp(cur, "Content-Length:", 15) == 0) {
            cur += 15;
            while (*cur == ' ') cur++;
            return atoi(cur);
        }
        if (strncmp(cur, "\r\n", 2) == 0) {
            break; // end of headers
        }
    }

    return 0;
}

http_request_t recv_request(int client_desc) {
    char buffer[MAX_REQUEST_SIZE];
    size_t total_recv = 0;
    ssize_t bytes;

    int content_length = 0;
    http_request_t full_req = {0};

    while (1) {
        bytes = recv(client_desc, buffer + total_recv, sizeof(buffer) - total_recv, 0);

        if (bytes < 0) {
            if (errno == EINTR) continue;
            perror("recv");
            return full_req;
        }
        if (bytes == 0){
            // Client closed connection
            return full_req;
        }

        total_recv += bytes;

        buffer[total_recv] = '\0';
        if (total_recv >= 4 && strstr(buffer, "\r\n\r\n")) {
            break; // All headers have been received.
        }

        if (total_recv >= sizeof(buffer)) {
            send_response(client_desc, "500 Internal Server Error\n", strlen("500 Internal Server Error\n"), "500 Internal Server Error", full_req, NULL);
            return full_req;
        }
    }
    buffer[total_recv] = '\0';
    content_length = get_content_length(buffer);

    char *header_end = strstr(buffer, "\r\n\r\n");
    size_t header_bytes = (header_end + 4) - buffer;
    size_t body_bytes_received = total_recv - header_bytes;

    while (body_bytes_received < (size_t)content_length) {
        bytes = recv(client_desc, buffer + total_recv, sizeof(buffer) - total_recv, 0);

        if (bytes < 0) {
            if (errno == EINTR) continue;
            perror("recv");
            return full_req;
        }
        if (bytes == 0){
            // Client closed connection
            return full_req;
        }

        total_recv += bytes;
        body_bytes_received += bytes;

        if (total_recv >= sizeof(buffer)) {
            send_response(client_desc, "500 Internal Server Error\n", strlen("500 Internal Server Error\n"), "500 Internal Server Error", full_req, NULL);
            return full_req;
        }
    }

    buffer[total_recv] = '\0';
    full_req = parse_request(buffer, client_desc);

    return full_req;
} 

void handle_client(int client_desc) {

    http_request_t request = {0};

    printf("Client connected\n");
    
    request = recv_request(client_desc);
    handle_request(client_desc, request);

    memset(&request, 0, sizeof(request));
    printf("Client disconnected.\n");
}

void handle_request(int client_desc, http_request_t req) {
    // request.url will be /directory/directory/file
    char full_url[512];

    if (strcasecmp(req.url, "/") == 0 || strcasecmp(req.url, "/inside/") == 0) {
        // return index.html or index.htm
        snprintf(full_url, sizeof(full_url), "./www%s", "/index.html");
    }
    else {
        snprintf(full_url, sizeof(full_url), "./www%s", req.url);
    }


    if (strcasecmp(req.method, "GET") == 0) {
        FILE *fptr = fopen(full_url, "rb");
        if (!fptr) {
            send_response(client_desc, "404 Not Found\n", strlen("404 Not Found\n"), "404 Not Found", req, NULL);
            return;
        }

        
        size_t file_size = 0;
        void *file_buf = file_to_buffer(fptr, &file_size);
        fclose(fptr);

        if (!file_buf) {
            send_response(client_desc, "500 Internal Server Error\n", strlen("500 Internal Server Error\n"), "500 Internal Server Error", req, NULL);
            return;
        }

        send_response(client_desc, file_buf, file_size, "200 OK", req, get_content_type(get_file_ext(full_url)));

        free(file_buf);
        return;
    }
    send_response(client_desc, "405 Method Not Allowed\n", strlen("405 Method Not Allowed\n"), "405 Method Not Allowed", req, NULL);
    return;
}

void send_response(int client_desc, const void *body, size_t body_len, char *status, http_request_t req, char* cont_type) {
    char header[256];

    snprintf(header, sizeof(header),
             "%s %s\r\n"
             "Content-Length: %zu\r\n"
             "Content-Type: %s\r\n"
             "Connection: close\r\n"
             "\r\n",
             req.version[0] ? req.version : "HTTP/1.1",
             status,
             body_len,
             (cont_type && cont_type[0]) ? cont_type : "text/html");

    send(client_desc, header, strlen(header), 0);

    if (body_len > 0) {
        send(client_desc, body, body_len, 0);
    }
}

void *file_to_buffer(FILE *fp, size_t *out_size) {
    if (!fp || !out_size) return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    if (size < 0) return NULL;

    void *buffer = malloc(size);
    if (!buffer) return NULL;

    size_t read = fread(buffer, 1, size, fp);
    if (read != (size_t)size) {
        free(buffer);
        return NULL;
    }

    *out_size = size;
    return buffer;
}



char *get_file_ext(const char *filename) {
    char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        return ""; // Return an empty string if no dot found or the dot is the first character
    }
    return dot + 1; // Return the substring after the last dot
}

char *get_content_type(const char *ext) {
    if (!ext) return "text/html";

    if (strcasecmp(ext, "html") == 0) return "text/html";
    if (strcasecmp(ext, "txt")  == 0) return "text/plain";
    if (strcasecmp(ext, "png")  == 0) return "image/png";
    if (strcasecmp(ext, "gif")  == 0) return "image/gif";
    if (strcasecmp(ext, "jpg")  == 0) return "image/jpg";
    if (strcasecmp(ext, "ico")  == 0) return "image/x-icon";
    if (strcasecmp(ext, "css")  == 0) return "text/css";
    if (strcasecmp(ext, "js")   == 0) return "application/javascript";

    return "text/html";  // fallback
}
