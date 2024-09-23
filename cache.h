typedef struct cache_entry {
    char *uri;
    char *data;
    int size;
    char *filetype;
    time_t access_time;    // 마지막 접근 시간
    struct cache_entry *next;  // 다음 캐시 엔트리를 가리키는 포인터
} cache_entry;

typedef struct {
    cache_entry *entries;  // 첫 번째 캐시 엔트리
    pthread_rwlock_t lock; // 파티션의 readers-writers lock
    int partition_size;    // 파티션 내 총 엔트리 크기
} cache_partition;

void cache_init();
cache_entry *cache_lookup(char *uri, int fd);
void cache_insert(char *uri, char *data, char *filetype, int size);
void add_entry_to_partition(cache_partition *partition, cache_entry *new_entry);
cache_entry *create_new_entry(char *uri, char *data, int size, char *type);
void evict_oldest_entry(cache_partition *partition);
cache_entry *find_entry_in_partition(cache_partition *partition, char *uri);
unsigned long hash(char *str);