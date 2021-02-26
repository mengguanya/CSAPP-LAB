#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *proxy_conn_hdr = "Proxy-Connection: close\r\n";

static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key= "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";

void doit(int connfd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void build_http_hdr(char *endserver_httphdr, char *hostname, char *path, int port, rio_t *from_clien);

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    /* 代理服务器开始监听客户端的请求 */
    listenfd = Open_listenfd(argv[1]);

    /* 无限循环：接收客户端请求，并把请求转发给目的服务器 */
    while (1)
    {
        /* 开始等待下一个客户端连接请求，在此之前处于阻塞状态 */
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept

        /* 打印请求信息 */
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        /* 依次序列化处理客户端的连接请求 */
        doit(connfd); //line:netp:tiny:doit

        /* 关闭该请求，然后进入下一个循环等待 */
        Close(connfd); //line:netp:tiny:close
    }
    return 0;
}

void doit(int connfd)
{
    int endserver_fd;
    char endserver_httphdr[MAX_OBJECT_SIZE];

    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    rio_t from_client, to_endserver;

    char hostname[MAXLINE], path[MAXLINE];
    int port;

    Rio_readinitb(&from_client, connfd);
    Rio_readlineb(&from_client, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET"))
    {
        clienterror(connfd, method, "501", "Not Implemented",
                    "Proxy Server does not implement this method");
        return;
    }

    /*parse the uri to get hostname,file path ,port*/
    parse_uri(uri, hostname, path, &port);

    char port_str[10];
    sprintf(port_str, "%d", port);
    endserver_fd = Open_clientfd(hostname, port_str);
    if (endserver_fd < 0)
    {
        printf("connection failed\n");
        return;
    }

    /*build the http header which will send to the end server*/
    build_http_hdr(endserver_httphdr, hostname, path, port, &from_client);

    Rio_readinitb(&to_endserver, endserver_fd);
    /*write the http header to endserver*/
    Rio_writen(endserver_fd, endserver_httphdr, strlen(endserver_httphdr));

    /*receive message from end server and send to the client*/
    size_t n;
    while ((n = Rio_readlineb(&to_endserver, buf, MAXLINE)) != 0)
    {
        printf("proxy received %ld bytes,then send\n", n);
        Rio_writen(connfd, buf, n);
    }
    Close(endserver_fd);
}

/* parse the uri to get hostname,file path ,port */
void parse_uri(char *uri, char *hostname, char *path, int *port)
{
    *port = 80;
    char *pos = strstr(uri, "//");
    /* 判断是否有"http//" */
    pos = pos != NULL ? pos + 2 : uri;

    /* 判断是否指定了端口号 */
    char *pos2 = strstr(pos, ":");
    /* 如果指定了端口号 */
    if (pos2 != NULL)
    {
        *pos2 = '\0';
        sscanf(pos, "%s", hostname);
        sscanf(pos2 + 1, "%d%s", port, path);
        *pos2 = ':';
    }
    /* 如果没有指定端口号 */
    else
    {
        pos2 = strstr(pos, "/");
        if (pos2 != NULL)
        {
            *pos2 = '\0';
            sscanf(pos, "%s", hostname);
            *pos2 = '/';
            sscanf(pos2, "%s", path);
        }
        else
        {
            sscanf(pos, "%s", hostname);
        }
    }
    return;
}

void build_http_hdr(char *endserver_httphdr, char *hostname, char *path, int port, rio_t *from_client)
{
    char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];
    /*request line*/
    sprintf(request_hdr, requestlint_hdr_format, path);
    /*get other request header for client rio and change it */
    while (Rio_readlineb(from_client, buf, MAXLINE) > 0)
    {
        if (strcmp(buf, endof_hdr) == 0)
            break; /*EOF*/

        if (!strncasecmp(buf, host_key, strlen(host_key))) /*Host:*/
        {
            strcpy(host_hdr, buf);
            continue;
        }

        if (!strncasecmp(buf, connection_key, strlen(connection_key)) && !strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key)) && !strncasecmp(buf, user_agent_key, strlen(user_agent_key)))
        {
            strcat(other_hdr, buf);
        }
    }
    if (strlen(host_hdr) == 0)
    {
        sprintf(host_hdr, host_hdr_format, hostname);
    }
    sprintf(endserver_httphdr, "%s%s%s%s%s%s%s",
            request_hdr,
            host_hdr,
            conn_hdr,
            proxy_conn_hdr,
            user_agent_hdr,
            other_hdr,
            endof_hdr);
    return;
}

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor="
                  "ffffff"
                  ">\r\n",
            body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

