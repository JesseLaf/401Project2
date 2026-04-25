#include "sql.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

/* ─────────────────────────────────────────
   sql_create
   Parses: CREATE TABLE name (col type, ...);
   ───────────────────────────────────────── */
int sql_create(const char *query, char *response) {
    Schema s;
    char   tmp[512];
    char   table_name[MAX_NAME_LEN];

    /* ── 1. Extract table name ── */
    if (sscanf(query, " CREATE TABLE %31s", table_name) != 1) {
        strcpy(response, "ERROR: malformed CREATE TABLE statement");
        return -1;
    }

    /* Strip trailing '(' if it got attached to the name */
    char *paren = strchr(table_name, '(');
    if (paren) *paren = '\0';

    strncpy(s.name, table_name, MAX_NAME_LEN - 1);
    s.name[MAX_NAME_LEN - 1] = '\0';
    s.ncols     = 0;
    s.row_width = 0;

    /* ── 2. Find the opening '(' ── */
    const char *col_start = strchr(query, '(');
    if (!col_start) {
        strcpy(response, "ERROR: missing '(' in CREATE TABLE");
        return -1;
    }
    col_start++;  /* skip past '(' */

    /* Copy column list into a mutable buffer, stop at ')' */
    strncpy(tmp, col_start, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *end = strchr(tmp, ')');
    if (end) *end = '\0';

    /* ── 3. Parse each "colname type" pair ── */
    char *tok = strtok(tmp, ",");
    while (tok != NULL && s.ncols < MAX_COLS) {
        /* Skip leading whitespace */
        while (*tok == ' ' || *tok == '\n' || *tok == '\t') tok++;

        char col_name[MAX_NAME_LEN];
        char col_type[64];

        if (sscanf(tok, "%31s %63s", col_name, col_type) != 2) {
            tok = strtok(NULL, ",");
            continue;
        }

        /* Remove trailing semicolons or extra punctuation */
        char *sc = strchr(col_type, ';');
        if (sc) *sc = '\0';

        Column *c = &s.cols[s.ncols];
        strncpy(c->name, col_name, MAX_NAME_LEN - 1);
        c->name[MAX_NAME_LEN - 1] = '\0';

        if (strcmp(col_type, "smallint") == 0) {
            c->type  = TYPE_SMALLINT;
            c->width = 4;
        } else if (strcmp(col_type, "int") == 0 ||
                   strcmp(col_type, "integer") == 0) {
            c->type  = TYPE_INTEGER;
            c->width = 8;
        } else {
            int n = 0;
            if (sscanf(col_type, "char(%d)", &n) != 1 || n <= 0) {
                snprintf(response, 256,
                         "ERROR: unknown type '%s' for column '%s'",
                         col_type, col_name);
                return -1;
            }
            c->type  = TYPE_CHAR;
            c->width = n;
        }

        s.row_width += c->width;
        s.ncols++;
        tok = strtok(NULL, ",");
    }

    if (s.ncols == 0) {
        strcpy(response, "ERROR: no columns defined");
        return -1;
    }

    /* ── 4. Write schema entry — fails if table already exists ── */
    if (schema_create(&s) < 0) {
        snprintf(response, 256, "ERROR: table '%s' already exists", s.name);
        return -1;
    }

    /* ── 5. Create the empty data file for this table ── */
    char filename[MAX_NAME_LEN + 8];
    snprintf(filename, sizeof(filename), "%s.db", s.name);

    int fd = db_open_file(filename);
    db_alloc_block(fd);
    close(fd);

    snprintf(response, 256, "OK: table '%s' created with %d column(s)", s.name, s.ncols);
    return 0;
}

/* ─────────────────────────────────────────
   sql_insert  (to be implemented)
   ───────────────────────────────────────── */
int sql_insert(const char *query, char *response) {
    (void)query;
    strcpy(response, "ERROR: INSERT not yet implemented");
    return -1;
}

/* ─────────────────────────────────────────
   sql_select  (to be implemented)
   ───────────────────────────────────────── */
int sql_select(const char *query, char *response) {
    (void)query;
    strcpy(response, "ERROR: SELECT not yet implemented");
    return -1;
}

/* ─────────────────────────────────────────
   sql_update  (to be implemented)
   ───────────────────────────────────────── */
int sql_update(const char *query, char *response) {
    (void)query;
    strcpy(response, "ERROR: UPDATE not yet implemented");
    return -1;
}

/* ─────────────────────────────────────────
   sql_delete  (to be implemented)
   ───────────────────────────────────────── */
int sql_delete(const char *query, char *response) {
    (void)query;
    strcpy(response, "ERROR: DELETE not yet implemented");
    return -1;
}