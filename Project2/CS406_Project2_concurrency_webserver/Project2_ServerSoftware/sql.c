#include "sql.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/* ─────────────────────────────────────────
   Internal helpers
   ───────────────────────────────────────── */

/* Return the on-disk width for a given type string, e.g. "smallint", "int",
   "char(30)". Fills col->type and col->width. Returns 0 on success, -1 on error. */
static int parse_type(const char *typestr, Column *col) {
    if (strcmp(typestr, "smallint") == 0) {
        col->type  = TYPE_SMALLINT;
        col->width = 4;
        return 0;
    }
    if (strcmp(typestr, "int") == 0 || strcmp(typestr, "integer") == 0) {
        col->type  = TYPE_INTEGER;
        col->width = 8;
        return 0;
    }
    /* char(n) */
    int n = 0;
    if (sscanf(typestr, "char(%d)", &n) == 1 && n > 0) {
        col->type  = TYPE_CHAR;
        col->width = n;
        return 0;
    }
    return -1;
}

/* ─────────────────────────────────────────
   schema_parse
   raw format: "tablename|col1:type1,col2:type2,...;"
   ───────────────────────────────────────── */
int schema_parse(const char *raw, Schema *out) {
    char tmp[BLOCK_PAYLOAD + 1];
    strncpy(tmp, raw, BLOCK_PAYLOAD);
    tmp[BLOCK_PAYLOAD] = '\0';

    /* Strip trailing dots and semicolons used as padding */
    int len = strlen(tmp);
    while (len > 0 && (tmp[len-1] == '.' || tmp[len-1] == ';'))
        tmp[--len] = '\0';

    /* Split on '|' to get table name and column list */
    char *pipe = strchr(tmp, '|');
    if (!pipe) return -1;
    *pipe = '\0';

    strncpy(out->name, tmp, MAX_NAME_LEN - 1);
    out->name[MAX_NAME_LEN - 1] = '\0';

    /* Walk the column list "col1:type1,col2:type2,..." */
    char *col_str = pipe + 1;
    out->ncols    = 0;
    out->row_width = 0;

    char *tok = strtok(col_str, ",");
    while (tok != NULL && out->ncols < MAX_COLS) {
        /* Each token is "colname:typename" */
        char *colon = strchr(tok, ':');
        if (!colon) return -1;
        *colon = '\0';

        Column *c = &out->cols[out->ncols];
        strncpy(c->name, tok, MAX_NAME_LEN - 1);
        c->name[MAX_NAME_LEN - 1] = '\0';

        if (parse_type(colon + 1, c) < 0) return -1;

        out->row_width += c->width;
        out->ncols++;

        tok = strtok(NULL, ",");
    }

    return (out->ncols > 0) ? 0 : -1;
}

/* ─────────────────────────────────────────
   schema_encode
   Writes "tablename|col1:type1,col2:type2,...;"
   then pads with '.' up to BLOCK_PAYLOAD bytes.
   ───────────────────────────────────────── */
void schema_encode(const Schema *s, char *out_buf) {
    char tmp[BLOCK_PAYLOAD + 1];
    int  pos = 0;

    pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%s|", s->name);

    for (int i = 0; i < s->ncols; i++) {
        const Column *c = &s->cols[i];
        if (i > 0) tmp[pos++] = ',';

        if (c->type == TYPE_SMALLINT)
            pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%s:smallint", c->name);
        else if (c->type == TYPE_INTEGER)
            pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%s:int", c->name);
        else
            pos += snprintf(tmp + pos, sizeof(tmp) - pos, "%s:char(%d)", c->name, c->width);
    }
    tmp[pos++] = ';';

    /* Pad remainder with dots */
    memset(out_buf, '.', BLOCK_PAYLOAD);
    memcpy(out_buf, tmp, pos < BLOCK_PAYLOAD ? pos : BLOCK_PAYLOAD);
}

/* ─────────────────────────────────────────
   schema_find
   Scans every block in schema.db looking for
   a line starting with "tablename|".
   ───────────────────────────────────────── */
int schema_find(const char *table_name, Schema *out) {
    db_init_schema();
    int fd = db_open_file(SCHEMA_FILE);

    char buf[BLOCK_SIZE];
    int  idx = 0;

    while (idx >= 0) {
        db_read_block(fd, idx, buf);

        /* Each block can hold multiple schema entries separated by '\n',
           but we store one per block for simplicity — scan payload */
        char payload[BLOCK_PAYLOAD + 1];
        memcpy(payload, buf, BLOCK_PAYLOAD);
        payload[BLOCK_PAYLOAD] = '\0';

        /* Check if this block holds the requested table */
        char prefix[MAX_NAME_LEN + 2];
        snprintf(prefix, sizeof(prefix), "%s|", table_name);

        if (strncmp(payload, prefix, strlen(prefix)) == 0) {
            close(fd);
            return schema_parse(payload, out);
        }

        idx = db_get_next(buf);
    }

    close(fd);
    return -1;  /* not found */
}

/* ─────────────────────────────────────────
   schema_create
   Appends a new table schema to schema.db.
   Returns -1 if the table already exists.
   ───────────────────────────────────────── */
int schema_create(const Schema *s) {
    db_init_schema();

    /* Reject duplicates */
    Schema existing;
    if (schema_find(s->name, &existing) == 0)
        return -1;

    int fd  = db_open_file(SCHEMA_FILE);
    char buf[BLOCK_SIZE];

    /* Walk to the last block in the chain */
    int idx  = 0;
    int prev = -1;
    db_read_block(fd, idx, buf);
    while (db_get_next(buf) >= 0) {
        prev = idx;
        idx  = db_get_next(buf);
        db_read_block(fd, idx, buf);
    }

    /* If the current last block's payload starts with '.' it is empty — use it.
       Otherwise allocate a new block and chain it on. */
    int use_idx;
    if (buf[0] == '.') {
        use_idx = idx;
    } else {
        int new_idx = db_alloc_block(fd);
        db_set_next(buf, new_idx);
        db_write_block(fd, idx, buf);   /* update old last block's next pointer */
        db_read_block(fd, new_idx, buf);
        use_idx = new_idx;
    }

    /* Write the schema entry into the block */
    schema_encode(s, buf);              /* fills payload bytes 0..251 */
    db_set_next(buf, -1);               /* this is the new tail */
    db_write_block(fd, use_idx, buf);

    close(fd);
    return 0;
}