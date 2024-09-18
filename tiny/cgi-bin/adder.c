/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */
#include "csapp.h"

int main(void) {
  char *buf, *p, *a, *b;
  char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
  int n1 = 0, n2 = 0;

  // if ((buf = getenv("QUERY_STRING")) != NULL) {
  //   p = strchr(buf, '&');
  //   *p = '\0';
  //   strcpy(arg1, buf);
  //   strcpy(arg2, p+1);
  //   n1 = atoi(arg1);
  //   n2 = atoi(arg2);
  // }

  // 11.10
  if ((buf = getenv("QUERY_STRING")) != NULL) {
    p = strchr(buf, '&');
    *p = '\0';
    if (!(a = strchr(buf, '='))) {
      strcpy(arg1, buf);
    } else {
      strcpy(arg1, a+1);
    }
    if (!(b = strchr(p+1, '='))) {
      strcpy(arg1, a+1);
    } else {
      strcpy(arg2, b+1);
    }
    n1 = atoi(arg1);
    n2 = atoi(arg2);
  }
  
  sprintf(content, "Welcome to add.com: ");
  sprintf(content, "%sTHE Internet addition portal.\r\n", content);
  sprintf(content, "%s<p>The answer is: %d + %d = %d\r\n", content, n1, n2, n1 + n2);
  sprintf(content, "%s<p>Thanks for visiting\r\n", content);

  printf("Connection: close\r\n");
  printf("Content-length: %d\r\n", (int)strlen(content));
  printf("content-type: text/html\r\n\r\n");
  if (strcmp(getenv("REQUEST_METHOD"), "GET") == 0) {
    printf("%s", content);
  }
  
  fflush(stdout);

  exit(0);
}
/* $end adder */
