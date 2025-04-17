/*
 * Starter code for proxy lab.
 * @author Tram Tran <tntran@andrew.cmu.edu>
 * Feel free to modify this code in whatever way you wish.
 */

/* Some useful includes to help you get started */

#include "csapp.h"

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
 * Code reference: Textbook
 */
int main(int argc, char **argv) {
    int listenfd;
    int *connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socketlen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    if (arg != 2) {
        fprintf(stderr, "usage: %s <domain name>\n", argv[0]);
        exit(1);
    }
    
    /* Ignore SIGPIPE to prevent crashes when client closes connection early */
    Signal(SIGPIPE, ISG_IGN);

    /* argv[1] is port number */
    listenfd = open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Malloc(sizeof(int));
        *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        if (pthread_create(&tid, NULL, thread, connfd) != 0) {
            perror("Pthread create error");
            close(*connfd);
            Free(connfd);
        }
    }
    return 0;
}

/**
 * @brief
 */
void *thread (void *vargp) {
    int connfd = *((int*) vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
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
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char path[MAXLINE], hostname[MAXLINE];

    rio_t rio;
    rio_t rio_end;
    parser_t *parser = parser_new();
    parser_state parse_state;

    if (!parser) {
        clienterror(clientfd, "400", "Bad request", "Parser Failed!");
        return;
    }

    rio_readinitb(&rio, client_fd);

    /* Reach EOF or error occured */
    if (!rio_readlineb(&rio, buf, MAXLINE)) {
        parser_free(parser);
        return;
    }

    /* Parsing */
    parse_state = parser_parse_line(parser, buf);

    if (parse_state != REQUEST) {
        parser_free(parser);
        clienterror(clientfd, "400", "Bad request", "Parse fail");
        return;
    }

    
    
    
    /* Invalid request */
    if (strcasecmp(method, "GET")) {
        clienterror(client_fd, method, "501", "Not implemented", "Tiny does not support this method");
        return;
    }

}