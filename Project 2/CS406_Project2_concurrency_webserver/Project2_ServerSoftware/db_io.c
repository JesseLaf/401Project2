#include "db_io.h"

#include <errno.h>
#include <sys/types.h>

static void db_fatal(const char *msg) {
    perror(msg);
    exit(1);
}

static void read_full_or_die(int fd, char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t rc = read(fd, buf + off, len - off);
        if (rc < 0) {
            db_fatal("read");
        }
        if (rc == 0) {
            fprintf(stderr, "db_read_block: unexpected EOF\n");
            exit(1);
        }
        off += (size_t)rc;
    }
}

static void write_full_or_die(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t rc = write(fd, buf + off, len - off);
        if (rc < 0) {
            db_fatal("write");
        }
        off += (size_t)rc;
    }
}

void db_read_block(int fd, int idx, char *buf) {
    off_t off = (off_t)idx * BLOCK_SIZE;
    if (idx < 0) {
        fprintf(stderr, "db_read_block: invalid block index %d\n", idx);
        exit(1);
    }
    if (lseek(fd, off, SEEK_SET) < 0) {
        db_fatal("lseek");
    }
    read_full_or_die(fd, buf, BLOCK_SIZE);
}

void db_write_block(int fd, int idx, char *buf) {
    off_t off = (off_t)idx * BLOCK_SIZE;
    if (idx < 0) {
        fprintf(stderr, "db_write_block: invalid block index %d\n", idx);
        exit(1);
    }
    if (lseek(fd, off, SEEK_SET) < 0) {
        db_fatal("lseek");
    }
    write_full_or_die(fd, buf, BLOCK_SIZE);
}

int db_get_next(char *buf) {
    char next[5];

    memcpy(next, buf + BLOCK_PAYLOAD, 4);
    next[4] = '\0';

    if (strncmp(next, NO_NEXT, 4) == 0) {
        return -1;
    }
    return atoi(next);
}

void db_set_next(char *buf, int next_idx) {
    if (next_idx < -1 || next_idx > 9999) {
        fprintf(stderr, "db_set_next: invalid next index %d\n", next_idx);
        exit(1);
    }

    if (next_idx == -1) {
        memcpy(buf + BLOCK_PAYLOAD, NO_NEXT, 4);
        return;
    }

    snprintf(buf + BLOCK_PAYLOAD, 5, "%04d", next_idx);
}

void db_init_block(char *buf) {
    memset(buf, '.', BLOCK_SIZE);
    db_set_next(buf, -1);
}

int db_alloc_block(int fd) {
    off_t end = lseek(fd, 0, SEEK_END);
    char buf[BLOCK_SIZE];
    int idx;

    if (end < 0) {
        db_fatal("lseek");
    }
    if ((end % BLOCK_SIZE) != 0) {
        fprintf(stderr, "db_alloc_block: file size is not block-aligned\n");
        exit(1);
    }

    idx = (int)(end / BLOCK_SIZE);
    db_init_block(buf);
    write_full_or_die(fd, buf, BLOCK_SIZE);

    return idx;
}

int db_open_file(const char *filename) {
    int fd = open(filename, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        db_fatal("open");
    }
    return fd;
}

void db_init_schema() {
    int fd;

    if (access(SCHEMA_FILE, F_OK) == 0) {
        return;
    }

    fd = db_open_file(SCHEMA_FILE);
    (void)db_alloc_block(fd);

    if (close(fd) < 0) {
        db_fatal("close");
    }
}
