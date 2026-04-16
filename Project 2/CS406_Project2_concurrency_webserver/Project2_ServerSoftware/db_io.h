#ifndef __DB_IO_H__
#define __DB_IO_H__

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define BLOCK_SIZE    256
#define BLOCK_PAYLOAD 252   /* 256 - 4 bytes reserved for next-block pointer */
#define SCHEMA_FILE   "schema.db"
#define NO_NEXT       "XXXX"

/* Read exactly one 256-byte block from fd at block index idx into buf.
   buf must be at least BLOCK_SIZE bytes. */
void db_read_block(int fd, int idx, char *buf);

/* Write exactly one 256-byte block from buf into fd at block index idx. */
void db_write_block(int fd, int idx, char *buf);

/* Return the next-block index stored in the last 4 bytes of buf,
   or -1 if it is "XXXX" (no next block). */
int db_get_next(char *buf);

/* Write next_idx into the last 4 bytes of buf as a 4-digit string.
   Pass -1 to write "XXXX". */
void db_set_next(char *buf, int next_idx);

/* Fill buf with '.' characters and set next pointer to XXXX. */
void db_init_block(char *buf);

/* Append a new empty block to the end of fd and return its index. */
int db_alloc_block(int fd);

/* Open (or create) a .db file by name. Returns fd. */
int db_open_file(const char *filename);

/* Initialize schema.db if it does not exist. */
void db_init_schema();

#endif /* __DB_IO_H__ */