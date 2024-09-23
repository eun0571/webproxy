#include <stdio.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

#include "csapp.h"

#define NTHREADS 4
#define SBUFSIZE 16

typedef struct {
    int *buf;
    int n;
    int front;
    int rear;
    sem_t mutex;
    sem_t slots;
    sem_t items;
} sbuf_t;

sbuf_t sbuf;

void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);
void doit(int fd);
void read_requesthdrs(rio_t *rp, char *forward_buf);
void parse_url(char *url, char *host, char *port, char *uri);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
void *thread(void *vargp);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen = sizeof(SA);
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\r\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);

  sbuf_init(&sbuf, SBUFSIZE);
  for (int i = 0; i < NTHREADS; i++) {
    Pthread_create(&tid, NULL, thread, NULL);
    printf("Thread %d is created for client service", tid);
  }

  while (1) {
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\r\n", hostname, port);
    sbuf_insert(&sbuf, connfd);
  }
}

void *thread(void *vargp) {
  Pthread_detach(pthread_self());
  while (1) {
    int connfd = sbuf_remove(&sbuf);
    doit(connfd);   // line:netp:tiny:doit
    Close(connfd);  // line:netp:tiny:close
  }
}

void doit(int fd){
  char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
  char host[MAXLINE], port[MAXLINE], uri[MAXLINE], proxy_request[MAXLINE], proxy_buf[MAXLINE], forward_buf[MAXLINE];
  int proxy_fd, n;
  rio_t rio, proxy_rio;
  char *p, filesize[MAXLINE];

  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\r\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, url, version);

  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented", "그런 method 지원 안한다.");
    return;
  }
  read_requesthdrs(&rio, forward_buf);
  
  parse_url(url, host, port, uri);
  
  proxy_fd = Open_clientfd(host, port);
  Rio_readinitb(&proxy_rio, proxy_fd);

  sprintf(proxy_request, "GET %s HTTP/1.0\r\n", uri);
  sprintf(proxy_request, "%sHost: %s\r\n", proxy_request, host);
  sprintf(proxy_request, "%s%s", proxy_request, user_agent_hdr);
  sprintf(proxy_request, "%sConnection: close\r\n", proxy_request);
  sprintf(proxy_request, "%sProxy-Connection: close\r\n", proxy_request);
  sprintf(proxy_request, "%s%s", proxy_request, forward_buf);
  sprintf(proxy_request, "%s%s", proxy_request, "\r\n");
  Rio_writen(proxy_fd, proxy_request, (int)strlen(proxy_request));
  printf("Proxy Request headers:\r\n");
  printf("%s", proxy_request);

  printf("Forwarding Response headers:\r\n");
  while (n = Rio_readlineb(&proxy_rio, proxy_buf, MAXLINE)) {
    Rio_writen(fd, proxy_buf, n);
    printf("%s", proxy_buf);
    if (!strcmp(proxy_buf, "\r\n")) {
      break;
    }
  }
  printf("Forwarding Response body\r\n\r\n");
  while (n = Rio_readlineb(&proxy_rio, proxy_buf, MAXLINE)) {
    Rio_writen(fd, proxy_buf, n);
  }
  printf("Body forwarded successfully\r\n\r\n");
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXLINE];
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, (int)strlen(body));
}

void read_requesthdrs(rio_t *rp, char *forward_buf) {
  char buf[MAXLINE], *preempted_header[] = {"Host:", "User-Agent:", "Connection:", "Proxy-Connection:", NULL};
  int n;
  while (n = Rio_readlineb(rp, buf, MAXLINE)) {
    // printf("%s", buf);
    int should_skip = 0;  // 헤더를 스킵할지 여부
    for (int i = 0; preempted_header[i] != NULL; i++) {
        if (strncmp(buf, preempted_header[i], strlen(preempted_header[i])) == 0) {
            should_skip = 1;  // 스킵해야 할 헤더임
            break;
        }
    }
    // 스킵하지 않을 경우에만 forward_buf에 추가
    if (!should_skip) {
        sprintf(forward_buf, "%s%s", forward_buf, buf);
    }
    if (!strcmp(buf, "\r\n")) {
      break;
    }
  }
  return;
}

void parse_url(char *url, char *host, char *port, char *uri) {
  char *ptr1, *ptr2, *ptr3;
  ptr1 = strchr(url, ':');
  ptr2 = strchr(ptr1+1, ':');
  if (ptr2) {
    ptr3 = strchr(ptr2+1, '/');
  } else {
    ptr3 = strchr(ptr1+3, '/');
  }
  if (ptr3) {
    strcpy(uri, "/");
    strcat(uri, ptr3+1);
    *ptr3 = '\0';
  } else {
    strcpy(uri, "/");
  }
  if (ptr2) {
    strcpy(port, ptr2+1);
    *ptr2 = '\0';
  }
  strcpy(host, ptr1+3);
  printf("parse from url: %s\r\n",url);
  printf("host: %s\r\n",host);
  printf("port: %s\r\n",port);
  printf("uri: %s\r\n\r\n",uri);
  return;
}

void sbuf_init(sbuf_t *sp, int n) {
    sp->buf = Calloc(n, sizeof(int));
    sp->n = n;
    sp->front = sp->rear = 0;
    Sem_init(&sp->mutex, 0, 1);
    Sem_init(&sp->slots, 0, n);
    Sem_init(&sp->items, 0, 0);
}

void sbuf_deinit(sbuf_t *sp) {
    Free(sp->buf);
}

void sbuf_insert(sbuf_t *sp, int item) {
    P(&sp->slots);
    P(&sp->mutex);
    sp->buf[(++sp->rear)%(sp->n)] = item;
    V(&sp->mutex);
    V(&sp->items);
}

int sbuf_remove(sbuf_t *sp) {
    int item;
    P(&sp->items);
    P(&sp->mutex);
    item = sp->buf[(++sp->front)%(sp->n)];
    V(&sp->mutex);
    V(&sp->slots);
    return item;
}