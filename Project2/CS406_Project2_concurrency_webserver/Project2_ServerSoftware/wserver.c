#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "request.h"
#include "io_helper.h"
#include "thread_pool.h"

#define MAXBUF 8192

char default_root[] = ".";

/* Global thread pool — shared between master and all workers */
static thread_pool_t pool;

/* ── Worker thread function ──────────────────────────────────────────
   Each worker loops forever: dequeue the next request per the
   scheduling policy, handle it, then close the connection.          */
static void *worker_thread(void *arg) {
    (void)arg;  /* unused */

    while (1) {
        req_entry_t req = pool_dequeue(&pool);

        if (req.preread) {
            /* SFF: master already read the request line and headers.
               Re-stat the file in case it changed, then serve it.   */
            struct stat sbuf;
            if (stat(req.filename, &sbuf) < 0) {
                request_error(req.conn_fd, req.filename,
                              "404", "Not found",
                              "server could not find this file");
            } else {
                request_handle_preread(req.conn_fd, req.filename,
                                       req.cgiargs, &sbuf,
                                       req.is_static);
            }
        } else {
            /* FIFO: worker reads and handles the full request */
            request_handle(req.conn_fd);
        }

        close_or_die(req.conn_fd);
    }

    return NULL;
}

/* ── Master thread (main) ────────────────────────────────────────────
   Accepts connections and places them into the shared buffer.
   For SFF it also reads the request line, parses the URI, and
   stats the file so workers can schedule by file size.              */
int main(int argc, char *argv[]) {
    int   c;
    char *root_dir    = default_root;
    int   port        = 10000;
    int   num_threads = 1;
    int   num_buffers = 1;
    int   sched_alg   = SCHED_FIFO_ALG;

    while ((c = getopt(argc, argv, "d:p:t:b:s:")) != -1) {
        switch (c) {
        case 'd':
            root_dir = optarg;
            break;
        case 'p':
            port = atoi(optarg);
            break;
        case 't':
            num_threads = atoi(optarg);
            break;
        case 'b':
            num_buffers = atoi(optarg);
            break;
        case 's':
            if (strcasecmp(optarg, "SFF") == 0)
                sched_alg = SCHED_SFF_ALG;
            else
                sched_alg = SCHED_FIFO_ALG;
            break;
        default:
            fprintf(stderr,
                "usage: wserver [-d basedir] [-p port] "
                "[-t threads] [-b buffers] [-s schedalg]\n");
            exit(1);
        }
    }

    if (num_threads < 1) {
        fprintf(stderr, "wserver: threads must be a positive integer\n");
        exit(1);
    }
    if (num_buffers < 1) {
        fprintf(stderr, "wserver: buffers must be a positive integer\n");
        exit(1);
    }

    /* Change into the base directory */
    chdir_or_die(root_dir);

    /* Initialize the shared request buffer */
    pool_init(&pool, num_buffers, sched_alg);

    /* Spawn worker threads */
    pthread_t *workers = malloc((size_t)num_threads * sizeof(pthread_t));
    if (!workers) {
        fprintf(stderr, "wserver: out of memory\n");
        exit(1);
    }
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&workers[i], NULL, worker_thread, NULL) != 0) {
            fprintf(stderr, "wserver: pthread_create failed\n");
            exit(1);
        }
    }

    /* Master loop — accept connections and enqueue them */
    int listen_fd = open_listen_fd_or_die(port);

    while (1) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        int conn_fd = accept_or_die(listen_fd,
                                    (sockaddr_t *)&client_addr,
                                    (socklen_t *)&client_len);

        req_entry_t entry;
        memset(&entry, 0, sizeof(entry));
        entry.conn_fd = conn_fd;

        if (sched_alg == SCHED_SFF_ALG) {
            /* Read the request line and headers so we can stat the file */
            char buf[MAXBUF], method[MAXBUF], uri[MAXBUF], version[MAXBUF];

            readline_or_die(conn_fd, buf, MAXBUF);
            sscanf(buf, "%s %s %s", method, uri, version);

            /* Consume and discard all remaining headers */
            readline_or_die(conn_fd, buf, MAXBUF);
            while (strcmp(buf, "\r\n") != 0) {
                readline_or_die(conn_fd, buf, MAXBUF);
            }

            /* Parse URI into filename and cgiargs */
            entry.is_static = request_parse_uri(uri, entry.filename,
                                                 entry.cgiargs);

            /* Stat the file to get its size for SFF ordering */
            struct stat sbuf;
            if (stat(entry.filename, &sbuf) == 0) {
                entry.file_size = (int)sbuf.st_size;
            } else {
                entry.file_size = 0;
            }

            entry.preread = 1;
        }

        pool_enqueue(&pool, &entry);
    }

    free(workers);
    pool_destroy(&pool);
    return 0;
}