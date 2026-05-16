#ifndef __THREAD_POOL_H__
#define __THREAD_POOL_H__

#include <pthread.h>
#include <sys/stat.h>

#define SCHED_FIFO_ALG 0
#define SCHED_SFF_ALG  1

#define MAX_FILENAME 512
#define MAX_CGIARGS  512

/* One slot in the shared request buffer */
typedef struct {
    int  conn_fd;                  /* accepted socket descriptor          */
    char filename[MAX_FILENAME];   /* resolved filename (used for SFF)    */
    char cgiargs[MAX_CGIARGS];     /* CGI query string (used for SFF)     */
    int  file_size;                /* file size in bytes; 0 if unknown    */
    int  is_static;                /* 1 = static file, 0 = dynamic CGI   */
    int  preread;                  /* 1 = master already read the request */
} req_entry_t;

/* Shared buffer with synchronization state */
typedef struct {
    req_entry_t    *buf;         /* circular buffer of request entries  */
    int             capacity;    /* maximum number of entries           */
    int             count;       /* current number of entries           */
    int             head;        /* index of oldest entry               */
    int             tail;        /* index where next entry will be written */
    int             sched_alg;   /* SCHED_FIFO_ALG or SCHED_SFF_ALG    */
    pthread_mutex_t mutex;
    pthread_cond_t  not_full;    /* signaled when a slot becomes free   */
    pthread_cond_t  not_empty;   /* signaled when an entry is added     */
} thread_pool_t;

/* Initialize pool with given capacity and scheduling algorithm */
void pool_init(thread_pool_t *pool, int capacity, int sched_alg);

/* Master calls this to add a request — blocks if buffer is full */
void pool_enqueue(thread_pool_t *pool, req_entry_t *entry);

/* Worker calls this to get the next request per scheduling policy —
   blocks if buffer is empty */
req_entry_t pool_dequeue(thread_pool_t *pool);

/* Free all pool resources */
void pool_destroy(thread_pool_t *pool);

#endif /* __THREAD_POOL_H__ */