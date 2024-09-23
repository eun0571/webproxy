#include <stdio.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#include "csapp.h"
#include "sbuf.h"
#include "cache.h"

/*
 * cache로부터 uri를 찾습니다.
 * hit한 경우 cache_lookup이 response를 serve합니다.
 * response은 못 찾을 경우 NULL을 return 하고 계속 진행합니다.
 */

#define NTHREADS 4
#define SBUFSIZE 16
#define NPARTITIONS 10
#define MAX_PARTITION_SIZE (MAX_CACHE_SIZE / NPARTITIONS)

sbuf_t sbuf;
cache_partition cache_partitions[NPARTITIONS];  // 쓰레드 수만큼 파티션
pthread_rwlockattr_t attr;

void doit(int fd);
void read_requesthdrs(rio_t *rp, char *forward_buf);
void parse_url(char *url, char *host, char *port, char *uri);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void *thread(void *vargp);
void forward_request(int fd, char *host, char *uri, char *forward_buf);
void read_responsehdrs(rio_t *rp, int fd, char *cache_size, char *cache_type);
void read_responsebodys(rio_t *rp, char *cache_object, int cache_size_int);
