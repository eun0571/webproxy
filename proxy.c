#include "proxy.h"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

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
    printf("Thread %lu is created for client service\r\n", tid);
  }

  cache_init();
  printf("Proxy cache is initialized\r\n");
  printf("\r\n");

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
  char host[MAXLINE], port[MAXLINE], uri[MAXLINE], forward_buf[MAXLINE];
  int proxy_fd;
  rio_t rio, proxy_rio;
  // char *p, filesize[MAXLINE];
  char cache_size[MAXLINE], cache_type[MAXLINE], cache_object[MAX_OBJECT_SIZE];
  // cache_entry *hit_entry;

  // 클라이언트로부터 요청라인request line을 읽습니다.
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\r\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, url, version);

  // GET method가 아니면 거절합니다.
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented", "Proxy doesn't implement this method");
    return;
  }

  // url로부터 host, port, uri를 찾습니다.
  parse_url(url, host, port, uri);

/*
 * cache로부터 uri를 찾습니다.
 * hit한 경우 cache_lookup이 response를 serve합니다.
 * response은 못 찾을 경우 NULL을 return 하고 계속 진행합니다.
 */
  if (cache_lookup(uri, fd)) {
    printf("%s was cached in proxy server\r\n",uri);
    printf("proxy served %s to client\r\n",uri);
    // 찾은 경우 return
    return;
  }
  
  // Client로부터 나머지 헤더를 읽고 Tiny에 전송할 header들을 찾아 forward_buf에 담습니다.
  read_requesthdrs(&rio, forward_buf);

  // Tiny와 연결을 준비합니다.
  proxy_fd = Open_clientfd(host, port);
  Rio_readinitb(&proxy_rio, proxy_fd);

  // Tiny로 request 요청
  forward_request(proxy_fd, host, uri, forward_buf);

  // Tiny로부터 response headers를 받아 Client로 전달합니다.
  // caching을 위해 filesize와 filetype를 parsing합니다.
  read_responsehdrs(&proxy_rio, fd, cache_size, cache_type);

  int cache_size_int = atoi(cache_size);
  // Tiny로부터 response body를 받아 버퍼에 담습니다.
  read_responsebodys(&proxy_rio, cache_object, cache_size_int);

  // Client로 전달
  Rio_writen(fd, cache_object, cache_size_int);
  printf("response body served to client successfully\r\n\r\n");

  // Cache에 추가
  if (cache_size_int > MAX_OBJECT_SIZE) {
    printf("filesize is too large to caching in proxy\r\n\r\n");
  } else {
    cache_insert(uri, cache_object, cache_type, cache_size_int);
  }

  return;
}

void read_responsebodys(rio_t *rp, char *cache_object, int cache_size_int) {
  printf("Start reading Response body\r\n");
  Rio_readnb(rp, cache_object, cache_size_int);
}

void read_responsehdrs(rio_t *rp, int fd, char *cache_size, char *cache_type) {
  char buf[MAXLINE];
  char size[] = "Content-length: ";
  char type[] = "Content-type: ";
  int n;

  printf("Forwarding Response headers:\r\n");
  while ((n = Rio_readlineb(rp, buf, MAXLINE)) != 0) {
    Rio_writen(fd, buf, n);
    printf("%s", buf);
    if (!strncmp(buf, size, strlen(size))) {
      strcpy(cache_size, buf+strlen(size));
      printf("filesize parsed for caching\r\n");
    }
    if (!strncmp(buf, type, strlen(type))) {
      char *p = strchr(buf, '\r');
      *p = '\0';
      strcpy(cache_type, buf+strlen(type));
      printf("filetype parsed for caching\r\n");
    }
    if (!strcmp(buf, "\r\n")) {
      break;
    }
  }
}

void forward_request(int fd, char *host, char *uri, char *forward_buf) {
  char buf[MAXLINE];

  sprintf(buf, "GET %s HTTP/1.0\r\n", uri);
  sprintf(buf, "%sHost: %s\r\n", buf, host);
  sprintf(buf, "%s%s", buf, user_agent_hdr);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sProxy-Connection: close\r\n", buf);
  sprintf(buf, "%s%s", buf, forward_buf);
  // sprintf(buf, "%s%s", buf, "\r\n");
  Rio_writen(fd, buf, (int)strlen(buf));
  printf("Proxy Request headers:\r\n");
  printf("%s", buf);
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

  while ((n = Rio_readlineb(rp, buf, MAXLINE)) != 0) {
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

void cache_init() {
  pthread_rwlockattr_init(&attr);
  pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);

    for (int i = 0; i < NTHREADS; i++) {
        pthread_rwlock_init(&cache_partitions[i].lock, &attr);
        cache_partitions[i].entries = NULL;  // 엔트리 리스트 초기화
        cache_partitions[i].partition_size = 0;
    }
}

cache_entry *cache_lookup(char *uri, int fd) {
    int partition_index = hash(uri) % NPARTITIONS;  // 적절한 파티션 결정
    cache_partition *partition = &cache_partitions[partition_index];
    char buf[MAXLINE];

    pthread_rwlock_rdlock(&partition->lock);  // 읽기 락
    printf("looking for %s in cache partition %d\r\n",uri, partition_index);
    
    cache_entry *entry = find_entry_in_partition(partition, uri);

    if (entry) {
      sprintf(buf, "HTTP/1.0 200 OK\r\n");
      sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
      sprintf(buf, "%sConnection: close\r\n", buf);
      sprintf(buf, "%sContent-length: %d\r\n", buf, entry->size);
      sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, entry->filetype);
      Rio_writen(fd, buf, strlen(buf));
      printf("Proxy cached Response headers:\r\n");
      printf("%s\r\n", buf);
      Rio_writen(fd, entry->data, entry->size);
      printf("cached data served successfully\r\n\r\n");
    } else {
      printf("%s isn't cached in proxy server\r\n",uri);
      printf("trying to request %s to tiny server\r\n\r\n",uri);
    }

    pthread_rwlock_unlock(&partition->lock);  // 락 해제

    // 느슨한 방식으로 access_time 업데이트
    if (entry) {
        time_t now = time(NULL);
        entry->access_time = now;  // 필요시만 업데이트
    }

    return entry;
}

void cache_insert(char *uri, char *data, char *filetype, int size) {
    int partition_index = hash(uri) % NPARTITIONS;
    cache_partition *partition = &cache_partitions[partition_index];
    struct tm *time_info;

    pthread_rwlock_wrlock(&partition->lock);  // 쓰기 락

    // LRU 정책에 따라 엔트리 삽입 및 삭제
    while (partition->partition_size + size > MAX_PARTITION_SIZE) {
      printf("%d",MAX_PARTITION_SIZE);
      evict_oldest_entry(partition);  // 오래된 엔트리 삭제
    }

    cache_entry *new_entry = create_new_entry(uri, data, size, filetype);
    add_entry_to_partition(partition, new_entry);
    partition->partition_size += size;

    printf("data cached successfully\r\n");
    printf("cached file info\r\n");
    printf("uri: %s\r\n", partition->entries->uri);
    printf("filetype: %s\r\n", partition->entries->filetype);
    printf("filesize: %d\r\n", partition->entries->size);
    time(&partition->entries->access_time);
    time_info = localtime(&partition->entries->access_time);
    printf("access_time: %s\r\n", asctime(time_info));

    pthread_rwlock_unlock(&partition->lock);  // 락 해제
}

void add_entry_to_partition(cache_partition *partition, cache_entry *new_entry) {
    new_entry->next = partition->entries;  // 새로운 엔트리를 리스트의 맨 앞에 추가
    partition->entries = new_entry;        // 리스트의 첫 엔트리를 새로운 엔트리로 업데이트
    
}

cache_entry *create_new_entry(char *uri, char *data, int size, char *type) {
  cache_entry *new_cache_entry = (cache_entry *)Malloc(sizeof(cache_entry));
  if (new_cache_entry == NULL) {
    // 메모리 할당 실패 시 처리
    fprintf(stderr, "Error: malloc failed to allocate memory for cache entry.\n");
    return NULL;
  }
  
  time_t now = time(NULL);
  new_cache_entry->access_time = now;
  new_cache_entry->data = (char *)Malloc(size);
  new_cache_entry->data = data;
  new_cache_entry->filetype = (char *)Malloc(sizeof(MAXLINE));
  new_cache_entry->filetype = type;
  new_cache_entry->next = NULL;
  new_cache_entry->size = size;
  new_cache_entry->uri = (char *)Malloc(sizeof(MAXLINE));
  new_cache_entry->uri = uri;
  return new_cache_entry;
}

void evict_oldest_entry(cache_partition *partition) {
    cache_entry *prev = NULL;
    cache_entry *current = partition->entries;
    cache_entry *oldest = current;
    cache_entry *oldest_prev = NULL;

    // 리스트를 순회하며 가장 오래된 엔트리 찾기
    while (current != NULL) {
        if (current->access_time < oldest->access_time) {
            oldest = current;
            oldest_prev = prev;
        }
        prev = current;
        current = current->next;
    }

    // 오래된 엔트리 삭제
    if (oldest_prev == NULL) {
        partition->entries = oldest->next;  // 첫 번째 엔트리 삭제
    } else {
        oldest_prev->next = oldest->next;   // 리스트 중간에서 삭제
    }
    partition->partition_size -= oldest->size;
    free(oldest->uri);
    free(oldest->data);
    free(oldest->filetype);
    free(oldest);
}

cache_entry *find_entry_in_partition(cache_partition *partition, char *uri) {
  cache_entry *current_entry = partition->entries;

  while (current_entry) {
    printf("current_entry's uri: %s, uri: %s\r\n",current_entry->uri, uri);
    printf("strcmp(): %d\r\n", strcmp(current_entry->uri, uri));
    if (!strcmp(current_entry->uri, uri)) {
      printf("cache hit!\r\n");
      break;
    }
    current_entry = current_entry->next;
  }
  return current_entry;
}

unsigned long hash(char *str) {
    unsigned long hash = 17;  // 초기값 변경
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;  /* hash * 33 + c */
    }

    return hash;
}