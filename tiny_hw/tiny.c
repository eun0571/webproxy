/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

////////////////////////////////////////////////////////////
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
////////////////////////////////////////////////////////////


#define MIN(a,b) ((a)>(b))? (b) : (a)

void doit(int fd);
// void read_requesthdrs(rio_t *rp);
void read_requesthdrs(rio_t *rp, int alive);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

void sigpipe_handler(int signum) {
    printf("SIGPIPE received. Continuing execution...\n");
}

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

/////////////////////////////////////////////////////////////
  struct sigaction sa;
  sa.sa_handler = sigpipe_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGPIPE, &sa, NULL);
/////////////////////////////////////////////////////////////

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // line:netp:tiny:doit
    Close(connfd);  // line:netp:tiny:close
  }
}

void doit(int fd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  int alive = 0;
  rio_t rio;

  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\r\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
    clienterror(fd, method, "501", "Not implemented", "Tiny doesn't implement this method");
    return;
  }
  
  read_requesthdrs(&rio, alive);
  is_static = parse_uri(uri, filename, cgiargs);
  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }
  if (is_static) {
    if ((!S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method);
  } else {
    if ((!S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't execute the file");
      return;
    }
    serve_dynamic(fd, filename, cgiargs, method);
  }
  
}

void read_requesthdrs(rio_t *rp, int alive) {
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf,"\r\n") != 0) {
    printf("%s", buf);
    if (strstr(buf, "Connection: keep-alive")) {
      alive = 1;
    }
    Rio_readlineb(rp, buf, MAXLINE);
  }
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;
  if (!strstr(uri, "cgi-bin")) {
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri)-1] == '/') {
      strcat(filename, "home.html");
    }
    return 1;
  } else {
    ptr = index(uri, '?');
    if (ptr) {
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    } else {
      strcpy(cgiargs, "");
    }
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

void serve_static(int fd, char *filename, int filesize, char *method) {
  int srcfd, n, offset=0;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  get_filetype(filename, filetype);
  // sprintf(buf, "HTTP/1.1 200 OK\r\n");
  sprintf(buf, "HTTP/1.1 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  // sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sConnection: keep-alive\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-Range: bytes 0-%d/%d\r\n", buf,filesize-1,filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("fd: %d\n",fd);
  printf("Response header:\n");
  printf("%s", buf);

  if ((strcasecmp(method, "HEAD")) == 0) {
    printf("HEAD method detected, no file body will be sent.\n");
    return;
  }
  // if (strcasecmp(method, "HEAD")) {
  //   srcfd = Open(filename, O_RDONLY, 0);
  //   srcp = Mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  //   Close(srcfd);
  //   while (offset < filesize) {
  //     n = MIN(filesize-offset, MAXLINE);
  //     sprintf(buf, "%x\r\n", n);
  //     Rio_writen(fd, buf, strlen(buf));
  //     Rio_writen(fd, srcp+offset, n);
  //     Rio_writen(fd, "\r\n", 2);
  //     offset += n;
  //   }
  //   Rio_writen(fd, "0\r\n\r\n", 5);
  //   Munmap(srcp, filesize);
  // }

  srcfd = Open(filename, O_RDONLY, 0);
  srcp = Mmap(NULL, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close(srcfd);
  // Rio_writen(fd, srcp, filesize);
  if (rio_writen(fd, srcp, filesize) < 0) {
    // 리턴 값 기반 에러 처리
    fprintf(stderr, "Error writing file body.\n");
    Munmap(srcp, filesize);
    return;
  }
  Munmap(srcp, filesize);
  
  // 11.9
  // srcfd = Open(filename, O_RDONLY, 0);
  // srcp = (char *)Malloc(filesize);
  // Rio_readn(srcfd, srcp, filesize);
  // Close(srcfd);
  // Rio_writen(fd, srcp, filesize);
  // Free(srcp);

}

void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html")) {
    strcpy(filetype, "text/html");
  } else if (strstr(filename, ".png")) {
    strcpy(filetype, "image/png");
  } else if (strstr(filename, ".gif")) {
    strcpy(filetype, "image/gif");
  } else if (strstr(filename, ".jpg")) {
    strcpy(filetype, "image/jpeg");
  // 11.7
  } else if (strstr(filename, ".mp4")) {
    strcpy(filetype, "video/mp4");
  } else if (strstr(filename, ".mpeg")) {
    strcpy(filetype, "video/mpeg");
  } else {
    strcpy(filetype, "text/plain");
  }
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method) {
  char buf[MAXLINE], *emptylist[] = { NULL };

  sprintf(buf, "HTTP/1.1 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response header:\n");
  printf("%s", buf);

  if (Fork() == 0) {
    setenv("QUERY_STRING", cgiargs, 1);
    setenv("REQUEST_METHOD", method, 1);
    Dup2(fd, STDOUT_FILENO);
    Execve(filename, emptylist, environ);
  }
  Wait(NULL);
};

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char body[MAXLINE], buf[MAXLINE];
  
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server\r\n", body);

  sprintf(buf, "HTTP/1.1 %s %s\r\n", errnum, shortmsg);
  sprintf(buf, "%sContent-length: %d\r\n", buf, (int)strlen(body));
  sprintf(buf, "Contetn-type: text/html\r\n\r\n");
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
};