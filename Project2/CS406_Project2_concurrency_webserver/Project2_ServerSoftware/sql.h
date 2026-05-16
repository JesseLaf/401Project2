#ifndef __SQL_H__
#define __SQL_H__

#include "db_io.h"

/* ── Column types ── */
#define TYPE_SMALLINT 0   /* 4-char string, e.g. "0002" */
#define TYPE_INTEGER  1   /* 8-char string, e.g. "00000100" */
#define TYPE_CHAR     2   /* n-char string, blank-padded   */

#define MAX_COLS      16
#define MAX_NAME_LEN  32
#define MAX_TABLES    64

/* ── A single column definition ── */
typedef struct {
    char name[MAX_NAME_LEN];
    int  type;       /* TYPE_SMALLINT, TYPE_INTEGER, TYPE_CHAR */
    int  width;      /* number of chars this field occupies on disk */
} Column;

/* ── A parsed table schema ── */
typedef struct {
    char   name[MAX_NAME_LEN];
    Column cols[MAX_COLS];
    int    ncols;
    int    row_width;  /* sum of all column widths */
} Schema;

/* ─────────────────────────────────────────
   Schema file functions
   ───────────────────────────────────────── */

/* Parse a raw schema string (as stored in schema.db) into a Schema struct.
   e.g. "movies|id:smallint,title:char(30),length:int;"
   Returns 0 on success, -1 on error. */
int schema_parse(const char *raw, Schema *out);

/* Encode a Schema struct back into a raw string for storage.
   out_buf must be at least BLOCK_PAYLOAD bytes. */
void schema_encode(const Schema *s, char *out_buf);

/* Look up a table by name in schema.db.
   Fills 'out' on success. Returns 0 if found, -1 if not found. */
int schema_find(const char *table_name, Schema *out);

/* Write a new table schema into schema.db.
   Returns 0 on success, -1 if table already exists. */
int schema_create(const Schema *s);

/* ─────────────────────────────────────────
   SQL command functions (implemented later)
   ───────────────────────────────────────── */
int  sql_create(const char *query, char *response);
int  sql_insert(const char *query, char *response);
int  sql_select(const char *query, char *response);
int  sql_update(const char *query, char *response);
int  sql_delete(const char *query, char *response);

#endif /* __SQL_H__ */