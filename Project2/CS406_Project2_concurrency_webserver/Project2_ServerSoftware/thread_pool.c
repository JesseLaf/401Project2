#include "thread_pool.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void pool_init(thread_pool_t *pool, int capacity, int sched_alg) {
    pool->buf = (req_entry_t *)malloc((size_t)capacity * sizeof(req_entry_t));
    if (!pool->buf) {
        fprintf(stderr, "pool_init: out of memory\n");
        exit(1);
    }
    pool->capacity  = capacity;
    pool->count     = 0;
    pool->head      = 0;
    pool->tail      = 0;
    pool->sched_alg = sched_alg;

    pthread_mutex_init(&pool->mutex,     NULL);
    pthread_cond_init(&pool->not_full,   NULL);
    pthread_cond_init(&pool->not_empty,  NULL);
}

void pool_enqueue(thread_pool_t *pool, req_entry_t *entry) {
    pthread_mutex_lock(&pool->mutex);

    /* Block master until a slot is free */
    while (pool->count == pool->capacity) {
        pthread_cond_wait(&pool->not_full, &pool->mutex);
    }

    pool->buf[pool->tail] = *entry;
    pool->tail = (pool->tail + 1) % pool->capacity;
    pool->count++;

    pthread_cond_signal(&pool->not_empty);
    pthread_mutex_unlock(&pool->mutex);
}

req_entry_t pool_dequeue(thread_pool_t *pool) {
    pthread_mutex_lock(&pool->mutex);

    /* Block worker until an entry is available */
    while (pool->count == 0) {
        pthread_cond_wait(&pool->not_empty, &pool->mutex);
    }

    req_entry_t result;

    if (pool->sched_alg == SCHED_FIFO_ALG) {
        /* Take the oldest entry at the head of the circular buffer */
        result     = pool->buf[pool->head];
        pool->head = (pool->head + 1) % pool->capacity;
        pool->count--;

    } else {
        /* SFF: scan all entries and pick the one with the smallest file_size */
        int min_idx  = pool->head;
        int min_size = pool->buf[pool->head].file_size;

        for (int i = 1; i < pool->count; i++) {
            int idx = (pool->head + i) % pool->capacity;
            if (pool->buf[idx].file_size < min_size) {
                min_size = pool->buf[idx].file_size;
                min_idx  = idx;
            }
        }

        result = pool->buf[min_idx];

        /* Remove the chosen entry by shifting everything from head
           up to min_idx forward by one slot */
        int cur = min_idx;
        while (cur != pool->head) {
            int prev    = (cur - 1 + pool->capacity) % pool->capacity;
            pool->buf[cur] = pool->buf[prev];
            cur = prev;
        }
        pool->head = (pool->head + 1) % pool->capacity;
        pool->count--;
    }

    pthread_cond_signal(&pool->not_full);
    pthread_mutex_unlock(&pool->mutex);

    return result;
}

void pool_destroy(thread_pool_t *pool) {
    free(pool->buf);
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->not_full);
    pthread_cond_destroy(&pool->not_empty);
}