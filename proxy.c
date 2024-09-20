#include <stdio.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

#include "csapp.h"

void doit(int fd);
// void read_requesthdrs(rio_t *rp);
void read_requesthdrs(rio_t *rp, char *forward_buf);
// int parse_uri(char *uri, char *filename, char *cgiargs);
void parse_url(char *url, char *host, char *port, char *uri);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\r\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\r\n", hostname, port);
    doit(connfd);   // line:netp:tiny:doit
    Close(connfd);  // line:netp:tiny:close
  }
}

void doit(int fd){
  // int is_static;
  // struct stat sbuf;
  // char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
  // char filename[MAXLINE], cgiargs[MAXLINE];
  char host[MAXLINE], port[MAXLINE], uri[MAXLINE], proxy_request[MAXLINE], proxy_buf[MAXLINE], foward_buf[MAXLINE];
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
  read_requesthdrs(&rio, foward_buf);
  
  parse_url(url, host, port, uri);
  
  proxy_fd = Open_clientfd(host, port);
  Rio_readinitb(&proxy_rio, proxy_fd);

  sprintf(proxy_request, "GET %s HTTP/1.0\r\n", uri);
  sprintf(proxy_request, "%sHost: %s\r\n", proxy_request, host);
  sprintf(proxy_request, "%s%s", proxy_request, user_agent_hdr);
  sprintf(proxy_request, "%sConnection: close\r\n", proxy_request);
  sprintf(proxy_request, "%sProxy-Connection: close\r\n", proxy_request);
  sprintf(proxy_request, "%s%s", proxy_request, foward_buf);
  Rio_writen(proxy_fd, proxy_request, (int)strlen(proxy_request));
  printf("Proxy Request headers:\r\n");
  printf("%s", proxy_request);

  printf("Response headers:\r\n");
  Rio_readlineb(&proxy_rio, proxy_buf, MAXLINE);
  printf("%s", proxy_buf);
  Rio_writen(fd, proxy_buf, (int)strlen(proxy_buf));
  while (strcmp(proxy_buf, "\r\n")) {
    Rio_readlineb(&proxy_rio, proxy_buf, MAXLINE);
    printf("%s", proxy_buf);
    Rio_writen(fd, proxy_buf, (int)strlen(proxy_buf));
    // if (strncmp(proxy_buf, "Content-length: ", (int)strlen("Content-length: "))==0){
    //   p = strchr(proxy_buf, ':');
    //   strcpy(filesize,p+2);
    //   printf("%s",filesize);
    // }
  }

  while ((n = Rio_readlineb(&proxy_rio, proxy_buf, MAXLINE)) != 0) {
    // Rio_readlineb(&proxy_rio, proxy_buf, MAXLINE);
    // printf("forward to client: %s", proxy_buf);
    Rio_writen(fd, proxy_buf, n);
  }
  printf("Body forwarded successfully\r\n\r\n");
  // if (stat(filename, &sbuf) < 0) {
  //   clienterror(fd, filename, "404", "Not found", "그런 파일 없다");
  //   return;
  // }
  // if (is_static) {
  //   if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
  //     clienterror(fd, filename, "403", "Forbidden", "읽을 권한 있어?");
  //     return;
  //   }
  //   serve_static(fd, filename, sbuf.st_size);
  // } else {
  //   if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
  //     clienterror(fd, filename, "403", "Forbidden", "실행 권한 있어?");
  //     return;
  //   }
  //   serve_dynamic(fd, filename, cgiargs);
  // }
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

  Rio_readlineb(rp, buf, MAXLINE);
  printf("%s", buf);
  // Host는 proxy가 직접 쏴줄테니까
  // sprintf(forward_buf, "%s", buf);
  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    
    // 특정 헤더가 있는지 검사
    int should_skip = 0;  // 헤더를 스킵할지 여부
    for (int i = 0; preempted_header[i] != NULL; i++) {
        if (strncmp(buf, preempted_header[i], strlen(preempted_header[i])) == 0) {
            should_skip = 1;  // 스킵해야 할 헤더임
            break;
        }
    }
    // 스킵하지 않을 경우에만 forward_buf에 추가
    if (!should_skip) {
        sprintf(forward_buf + strlen(forward_buf), "%s", buf);
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
  
  // if (!strstr(uri, "cgi-bin")) {
  //   strcpy(cgiargs, "");
  //   strcpy(filename, ".");
  //   strcat(filename, uri);
  //   if (uri[strlen(uri)-1] == '/') {
  //     strcat(filename, "home.html");
  //   }
  //   return 1;
  // } else {
  //   ptr = index(uri, '?');
  //   if (ptr) {
  //     strcpy(cgiargs, ptr+1);
  //     *ptr = '\0';
  //   } else {
  //     strcpy(cgiargs, "");
  //   }
  //   strcpy(filename, ".");
  //   strcat(filename, uri);
  //   return 0;
  // }
}

// void serve_static(int fd, char *filename, int filesize) {
//   int srcfd;
//   char *srcp, filetype[MAXLINE], buf[MAXLINE];

//   get_filetype(filename, filetype);
//   sprintf(buf, "HTTP/1.0 200 OK\r\n");
//   sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
//   sprintf(buf, "%sConnection: close\r\n", buf);
//   sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
//   sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
//   Rio_writen(fd, buf, strlen(buf));
//   printf("Response headers:\r\n");
//   printf("%s\r\n", buf);

//   srcfd = Open(filename, O_RDONLY, 0);
//   srcp = Mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
//   Close(srcfd);
//   Rio_writen(fd, srcp, filesize);
//   Munmap(srcp, filesize);
// }

// void get_filetype(char *filename, char *filetype) {
//   if (strstr(filename, ".html")) {
//     strcpy(filetype, "text/html");
//   } else if (strstr(filename, ".gif")) {
//     strcpy(filetype, "image/gif");
//   } else if (strstr(filename, ".png")) {
//     strcpy(filetype, "image/png");
//   } else if (strstr(filename, ".jpg")) {
//     strcpy(filetype, "image/jpeg");
//   } else {
//     strcpy(filetype, "text/plain");
//   }
// }

// void serve_dynamic(int fd, char *filename, char *cgiargs) {
//   char buf[MAXLINE], *emptylist[] = { NULL };
//   pid_t pid;
//   sprintf(buf, "HTTP/1.0 200 OK\r\n");
//   Rio_writen(fd, buf, strlen(buf));
//   sprintf(buf, "Server: Tiny Web Server\r\n");
//   Rio_writen(fd, buf, strlen(buf));

//   if ((pid = Fork()) == 0) {
//     setenv("QUERY_STRING", cgiargs, 1);
//     Dup2(fd, STDOUT_FILENO);
//     Execve(filename, emptylist, environ);
//   }
//   printf("%d", pid);
//   wait(NULL);
// }