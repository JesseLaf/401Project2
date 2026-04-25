#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "db_io.h"
#include "sql.h"

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

/* ── Test 6: schema_encode and schema_parse round-trip ── */
static void test_schema_encode_parse() {
    Schema s;
    strcpy(s.name, "movies");
    s.ncols = 3;
    strcpy(s.cols[0].name, "id");    s.cols[0].type = TYPE_SMALLINT; s.cols[0].width = 4;
    strcpy(s.cols[1].name, "title"); s.cols[1].type = TYPE_CHAR;     s.cols[1].width = 30;
    strcpy(s.cols[2].name, "length");s.cols[2].type = TYPE_INTEGER;  s.cols[2].width = 8;
    s.row_width = 42;

    char buf[BLOCK_PAYLOAD];
    schema_encode(&s, buf);

    Schema out;
    int rc = schema_parse(buf, &out);

    int ok = (rc == 0)
          && (strcmp(out.name, "movies") == 0)
          && (out.ncols == 3)
          && (out.cols[1].width == 30)
          && (out.row_width == 42);

    printf("test_schema_enc_parse : %s\n", ok ? PASS : FAIL);
}

/* ── Test 7: schema_create and schema_find ── */
static void test_schema_create_find() {
    unlink(SCHEMA_FILE);  /* start fresh */

    Schema s;
    strcpy(s.name, "movies");
    s.ncols = 2;
    strcpy(s.cols[0].name, "id");    s.cols[0].type = TYPE_SMALLINT; s.cols[0].width = 4;
    strcpy(s.cols[1].name, "title"); s.cols[1].type = TYPE_CHAR;     s.cols[1].width = 20;
    s.row_width = 24;

    int rc_create = schema_create(&s);

    Schema found;
    int rc_find = schema_find("movies", &found);

    int ok = (rc_create == 0)
          && (rc_find   == 0)
          && (strcmp(found.name, "movies") == 0)
          && (found.ncols == 2);

    printf("test_schema_create_find: %s\n", ok ? PASS : FAIL);

    /* Duplicate create should fail */
    int rc_dup = schema_create(&s);
    printf("test_schema_no_dup    : %s\n", rc_dup == -1 ? PASS : FAIL);
}

int main() {
    printf("=== db_io unit tests ===\n");
    cleanup();

    test_init_block();
    test_next_roundtrip();
    test_write_read();
    test_alloc_blocks();
    test_init_schema();
    test_schema_encode_parse();
    test_schema_create_find();

    cleanup();
    printf("========================\n");
    return 0;
}