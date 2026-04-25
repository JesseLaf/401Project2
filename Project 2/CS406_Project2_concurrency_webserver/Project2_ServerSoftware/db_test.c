#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "db_io.h"

#define PASS "\033[32mPASS\033[0m"
#define FAIL "\033[31mFAIL\033[0m"

/* Helper: remove test files left over from a previous run */
static void cleanup() {
    unlink("test_blocks.db");
    unlink(SCHEMA_FILE);
}

/* ── Test 1: init_block fills with dots and sets XXXX ── */
static void test_init_block() {
    char buf[BLOCK_SIZE];
    db_init_block(buf);

    int all_dots = 1;
    for (int i = 0; i < BLOCK_PAYLOAD; i++) {
        if (buf[i] != '.') { all_dots = 0; break; }
    }
    int correct_next = (strncmp(buf + BLOCK_PAYLOAD, "XXXX", 4) == 0);

    printf("test_init_block      : %s\n", (all_dots && correct_next) ? PASS : FAIL);
}

/* ── Test 2: set_next / get_next round-trip ── */
static void test_next_roundtrip() {
    char buf[BLOCK_SIZE];
    db_init_block(buf);

    db_set_next(buf, 7);
    int result = db_get_next(buf);
    printf("test_next_set_get     : %s  (expected 7, got %d)\n",
           result == 7 ? PASS : FAIL, result);

    db_set_next(buf, -1);
    result = db_get_next(buf);
    printf("test_next_xxxx        : %s  (expected -1, got %d)\n",
           result == -1 ? PASS : FAIL, result);
}

/* ── Test 3: write a block then read it back ── */
static void test_write_read() {
    int fd = db_open_file("test_blocks.db");

    char write_buf[BLOCK_SIZE];
    db_init_block(write_buf);
    /* Put some recognisable data in the payload */
    strncpy(write_buf, "HELLO_BLOCK_ZERO", 16);
    db_set_next(write_buf, 1);

    db_write_block(fd, 0, write_buf);

    char read_buf[BLOCK_SIZE];
    db_read_block(fd, 0, read_buf);

    int payload_ok  = (strncmp(read_buf, "HELLO_BLOCK_ZERO", 16) == 0);
    int next_ok     = (db_get_next(read_buf) == 1);

    printf("test_write_read       : %s\n", (payload_ok && next_ok) ? PASS : FAIL);
    close(fd);
}

/* ── Test 4: alloc two blocks, check indices ── */
static void test_alloc_blocks() {
    int fd = db_open_file("test_blocks.db");

    /* File already has block 0 from test_write_read; alloc block 1 */
    int idx1 = db_alloc_block(fd);
    int idx2 = db_alloc_block(fd);

    printf("test_alloc_idx1       : %s  (expected 1, got %d)\n",
           idx1 == 1 ? PASS : FAIL, idx1);
    printf("test_alloc_idx2       : %s  (expected 2, got %d)\n",
           idx2 == 2 ? PASS : FAIL, idx2);

    close(fd);
}

/* ── Test 5: db_init_schema creates schema.db with one empty block ── */
static void test_init_schema() {
    db_init_schema();

    int fd = db_open_file(SCHEMA_FILE);
    char buf[BLOCK_SIZE];
    db_read_block(fd, 0, buf);

    int all_dots = 1;
    for (int i = 0; i < BLOCK_PAYLOAD; i++) {
        if (buf[i] != '.') { all_dots = 0; break; }
    }
    int correct_next = (db_get_next(buf) == -1);

    printf("test_init_schema      : %s\n", (all_dots && correct_next) ? PASS : FAIL);
    close(fd);
}

int main() {
    printf("=== db_io unit tests ===\n");
    cleanup();

    test_init_block();
    test_next_roundtrip();
    test_write_read();
    test_alloc_blocks();
    test_init_schema();

    cleanup();
    printf("========================\n");
    return 0;
}