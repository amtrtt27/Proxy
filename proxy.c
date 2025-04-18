/*
 * Starter code for proxy lab.
 * @author Tram Tran <tntran@andrew.cmu.edu>
 * 
 * This proxy is responsible for:
 * - Accept and parse incoming requests from clients.
 * - Forward these requests to the end server.
 * - Transfer the data from the end server back to the client.
 * 
 * To handle multiple requests at once, the proxy uses threads.
 * It listens for client connections, and upon accepting one, it creates a
 * separate connection file for each client socket and does the work.
 * 
 * Reference: Textbook
 */

#include "csapp.h"
#include "http_parser.h"
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

/*
 * Debug macros, which can be enabled by adding -DDEBUG in the Makefile
 * Use these if you find them useful, or delete them if not
 */
#ifdef DEBUG
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define dbg_assert(...)
#define dbg_printf(...)
#endif

/* Global Variable */
#define DEFAULT_PORT "80"
#define SA struct sockaddr

void clienterror(int fd, const char *errnum, const char *shortmsg, const char *longmsg);
int main(int argc, char **argv);
void *thread(void *vargp);
void doit(int cliend_fd);
/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20250408 Firefox/63.0.1";

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, const char *errnum, const char *shortmsg,
                 const char *longmsg) {
    char buf[MAXLINE];
    char body[MAXBUF];
    size_t buflen;
    size_t bodylen;

    /* Build the HTTP response body */
    bodylen = snprintf(body, MAXBUF,
            "<!DOCTYPE html>\r\n" \
            "<html>\r\n" \
            "<head><title>Tiny Error</title></head>\r\n" \
            "<body bgcolor=\"ffffff\">\r\n" \
            "<h1>%s: %s</h1>\r\n" \
            "<p>%s</p>\r\n" \
            "<hr /><em>The Tiny Web server</em>\r\n" \
            "</body></html>\r\n", \
            errnum, shortmsg, longmsg);
    if (bodylen >= MAXBUF) {
        return; // Overflow!
    }

    /* Build the HTTP response headers */
    buflen = snprintf(buf, MAXLINE,
            "HTTP/1.0 %s %s\r\n" \
            "Content-Type: text/html\r\n" \
            "Content-Length: %zu\r\n\r\n", \
            errnum, shortmsg, bodylen);
    if (buflen >= MAXLINE) {
        return; // Overflow!
    }

    /* Write the headers */
    if (rio_writen(fd, buf, buflen) < 0) {
        fprintf(stderr, "Error writing error response headers to client\n");
        return;
    }

    /* Write the body */
    if (rio_writen(fd, body, bodylen) < 0) {
        fprintf(stderr, "Error writing error response body to client\n");
        return;
    }
}

/**
 * @brief handles primary workflow of the proxy:
 * - Acts as a server: create a listening socket to accept HTTP requests from 
 * the browser.
 * - Acts as a client: forward those requests to the end server.
 */
int main(int argc, char **argv) {
    int listenfd;
    int *connfd;
    char hostname[MAXLINE], client_port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    

    if (argc != 2) {
        fprintf(stderr, "usage: %s <domain name>\n", argv[0]);
        exit(1);
    }
    
    /* Ignore SIGPIPE to prevent crashes when client closes connection early */
    Signal(SIGPIPE, SIG_IGN);

    /* port number */
    listenfd = open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Malloc(sizeof(int));
        *connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);

        getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, client_port);
        pthread_t tid;
        if (pthread_create(&tid, NULL, thread, connfd) != 0) {
            perror("Pthread create error");
            close(*connfd);
            Free(connfd);
        }
    }
    return 0;
}

/**
 * @brief handles each connection using a separate thread
 */
void *thread (void *vargp) {
    int connfd = *((int*) vargp);
    pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    close(connfd);
    return NULL;
}

/**
 * @brief 
 * - Reads and parses the client's HTTP request
 * - Connects to the target server
 * - Builds a valid HTTP request to send to that server
 * - Sends the request to the end server
 * - Forwards the end server's response back to the client
 */
void doit (int client_fd) {
    char buf[MAXLINE];
    parser_t *parser = parser_new();
    rio_t client_rio, server_rio;
    const char *method, *path, *hostname, *port, *uri;

    int server_fd;
    parser_state parse_state;

    if (parser == NULL) {
        clienterror(client_fd, "400", "Bad request", "Parser Failed!");
        return;
    }

    rio_readinitb(&client_rio, client_fd);

    /* Reach EOF or error occured */
    if (!rio_readlineb(&client_rio, buf, MAXLINE)) {
        parser_free(parser);
        return;
    }

    parse_state = parser_parse_line(parser, buf);

    if (parse_state != REQUEST) {
        parser_free(parser);
        clienterror(client_fd, "400", "Bad request", "Parse fail");
        return;
    }
    
    parser_retrieve(parser, METHOD, &method);
    parser_retrieve(parser, URI, &uri);
    parser_retrieve(parser, HOST, &hostname);
    parser_retrieve(parser, PORT, &port);
    parser_retrieve(parser, PATH, &path);
    
    
    if (!port) port = DEFAULT_PORT;
    
    if (method == NULL || path == NULL || hostname == NULL) {
        clienterror(client_fd, "400", "Bad request", "Invalid header");
        parser_free(parser);
        return;
    }
    /* Invalid request */
    if (strcasecmp(method, "GET")) {
        parser_free(parser);
        clienterror(client_fd, "501", "Not implemented", "Tiny does not support this method");
        return;
    }

    /* Connect to end server */
    server_fd = open_clientfd(hostname, port);

    snprintf(buf, sizeof(buf), "GET %s HTTP/1.0\r\n", path);
    rio_writen(server_fd, buf, strlen(buf));

    while (rio_readlineb(&client_rio, buf, sizeof(buf)) > 0) {
        if (strcmp(buf, "\r\n") == 0) break;

        if (strncasecmp(buf, "Host:", 5) == 0) {
            rio_writen(server_fd, buf, strlen(buf));
            hostname = NULL;
            continue;
        }

        if (strncasecmp(buf, "Connection:", 11) == 0 || strncasecmp(buf, "Proxy-Connection:", 17) == 0 || strncasecmp(buf, "User-Agent:", 11) == 0) continue;

        rio_writen(server_fd, buf, strlen(buf));
    }

    if (hostname) {
        snprintf(buf, sizeof(buf), "Host: %s:%s\r\n", hostname, port);
        rio_writen(server_fd, buf, strlen(buf));
    }
    rio_readinitb(&server_rio, server_fd);
    snprintf(buf, sizeof(buf), "User-Agent: %s\r\n", header_user_agent);
    rio_writen(server_fd, buf, strlen(buf));
    snprintf(buf, sizeof(buf), "Connection: close\r\n");
    rio_writen(server_fd, buf, strlen(buf));
    snprintf(buf, sizeof(buf), "Proxy-Connection: close\r\n");
    rio_writen(server_fd, buf, strlen(buf));

    snprintf(buf, sizeof(buf), "\r\n");

    rio_readinitb(&server_rio, server_fd);
    ssize_t num_bytes;
    while ((num_bytes = rio_readnb(&server_rio, buf, sizeof(buf))) > 0) {
        rio_writen(client_fd, buf, num_bytes);
    }
    close(server_fd);
    parser_free(parser);
}