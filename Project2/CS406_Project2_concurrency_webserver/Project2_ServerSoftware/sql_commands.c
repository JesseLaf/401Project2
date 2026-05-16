#include "sql.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define RESP_MAX 8192

typedef struct {
    int col_idx;
    int op; /* 0:= 1:!= 2:< 3:> */
    char value[256];
} WhereClause;

static void trim_in_place(char *s) {
    size_t start = 0;
    size_t len = strlen(s);

    while (start < len && isspace((unsigned char)s[start])) start++;
    while (len > start && isspace((unsigned char)s[len - 1])) len--;

    if (start > 0) memmove(s, s + start, len - start);
    s[len - start] = '\0';
}

static int starts_with_ci(const char *s, const char *prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return 0;
        s++;
        prefix++;
    }
    return 1;
}

static const char *find_ci(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) return haystack;

    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) return p;
    }
    return NULL;
}

static int split_csv_values(const char *src, char values[][256], int max_values) {
    int count = 0;
    int in_quote = 0;
    char cur[256];
    int cur_len = 0;

    for (const char *p = src; ; p++) {
        char ch = *p;
        int at_end = (ch == '\0');

        if (!at_end && ch == '\'') {
            in_quote = !in_quote;
            if (cur_len < (int)sizeof(cur) - 1) cur[cur_len++] = ch;
            continue;
        }

        if (at_end || (!in_quote && ch == ',')) {
            cur[cur_len] = '\0';
            if (count >= max_values) return -1;
            strncpy(values[count], cur, 255);
            values[count][255] = '\0';
            trim_in_place(values[count]);
            count++;
            cur_len = 0;

            if (at_end) break;
            continue;
        }

        if (cur_len < (int)sizeof(cur) - 1) cur[cur_len++] = ch;
    }

    return count;
}

static int schema_col_index(const Schema *s, const char *name) {
    for (int i = 0; i < s->ncols; i++) {
        if (strcmp(s->cols[i].name, name) == 0) return i;
    }
    return -1;
}

static void strip_quotes(char *s) {
    size_t len = strlen(s);
    if (len >= 2 && ((s[0] == '\'' && s[len - 1] == '\'') || (s[0] == '"' && s[len - 1] == '"'))) {
        memmove(s, s + 1, len - 2);
        s[len - 2] = '\0';
    }
}

static int encode_cell(const Column *c, const char *raw_value, char *dst, char *response) {
    char tmp[256];
    strncpy(tmp, raw_value, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    trim_in_place(tmp);

    if (c->type == TYPE_SMALLINT) {
        int v = atoi(tmp);
        if (v < 0 || v > 9999) {
            strcpy(response, "ERROR: smallint out of supported range 0..9999");
            return -1;
        }
        snprintf(dst, c->width + 1, "%04d", v);
        return 0;
    }

    if (c->type == TYPE_INTEGER) {
        int v = atoi(tmp);
        if (v < 0 || v > 99999999) {
            strcpy(response, "ERROR: int out of supported range 0..99999999");
            return -1;
        }
        snprintf(dst, c->width + 1, "%08d", v);
        return 0;
    }

    strip_quotes(tmp);
    if ((int)strlen(tmp) > c->width) {
        snprintf(response, 256, "ERROR: char value too long for width %d", c->width);
        return -1;
    }
    memset(dst, ' ', c->width);
    memcpy(dst, tmp, strlen(tmp));
    return 0;
}

static void decode_cell(const Column *c, const char *src, char *out, size_t out_sz) {
    if (c->type == TYPE_CHAR) {
        int n = c->width;
        if (n >= (int)out_sz) n = (int)out_sz - 1;
        memcpy(out, src, n);
        out[n] = '\0';
        while (n > 0 && out[n - 1] == ' ') out[--n] = '\0';
        return;
    }

    int n = c->width;
    if (n >= (int)out_sz) n = (int)out_sz - 1;
    memcpy(out, src, n);
    out[n] = '\0';
}

static int row_offset_for_col(const Schema *s, int col_idx) {
    int offset = 0;
    for (int i = 0; i < col_idx; i++) offset += s->cols[i].width;
    return offset;
}

static int parse_where_clause(const Schema *s, const char *where_text, WhereClause *out, char *response) {
    char cond[256];
    strncpy(cond, where_text, sizeof(cond) - 1);
    cond[sizeof(cond) - 1] = '\0';
    trim_in_place(cond);

    char *semi = strchr(cond, ';');
    if (semi) *semi = '\0';

    char *op_pos = NULL;
    int op = -1;

    if ((op_pos = strstr(cond, "!=")) != NULL) {
        op = 1;
    } else if ((op_pos = strchr(cond, '=')) != NULL) {
        op = 0;
    } else if ((op_pos = strchr(cond, '<')) != NULL) {
        op = 2;
    } else if ((op_pos = strchr(cond, '>')) != NULL) {
        op = 3;
    } else {
        strcpy(response, "ERROR: unsupported WHERE operator");
        return -1;
    }

    char col[MAX_NAME_LEN];
    char val[256];

    if (op == 1) {
        *op_pos = '\0';
        op_pos += 2;
    } else {
        *op_pos = '\0';
        op_pos += 1;
    }

    strncpy(col, cond, sizeof(col) - 1);
    col[sizeof(col) - 1] = '\0';
    trim_in_place(col);

    strncpy(val, op_pos, sizeof(val) - 1);
    val[sizeof(val) - 1] = '\0';
    trim_in_place(val);
    strip_quotes(val);

    int idx = schema_col_index(s, col);
    if (idx < 0) {
        snprintf(response, 256, "ERROR: unknown column '%s' in WHERE", col);
        return -1;
    }

    out->col_idx = idx;
    out->op = op;
    strncpy(out->value, val, sizeof(out->value) - 1);
    out->value[sizeof(out->value) - 1] = '\0';
    return 0;
}

static int where_matches(const Schema *s, const char *row, const WhereClause *w) {
    int off = row_offset_for_col(s, w->col_idx);
    const Column *c = &s->cols[w->col_idx];
    char cell[256];
    decode_cell(c, row + off, cell, sizeof(cell));

    if (c->type == TYPE_CHAR) {
        int cmp = strcmp(cell, w->value);
        if (w->op == 0) return cmp == 0;
        if (w->op == 1) return cmp != 0;
        if (w->op == 2) return cmp < 0;
        if (w->op == 3) return cmp > 0;
        return 0;
    }

    int lhs = atoi(cell);
    int rhs = atoi(w->value);
    if (w->op == 0) return lhs == rhs;
    if (w->op == 1) return lhs != rhs;
    if (w->op == 2) return lhs < rhs;
    if (w->op == 3) return lhs > rhs;
    return 0;
}

static int is_empty_chunk(const char *row, int row_width) {
    for (int i = 0; i < row_width; i++) {
        if (row[i] != '.' && row[i] != 'x' && row[i] != 'X') return 0;
    }
    return 1;
}

static int read_table_rows(const Schema *s, char **rows_out, int *row_count_out, char *response) {
    char filename[MAX_NAME_LEN + 8];
    snprintf(filename, sizeof(filename), "%s.db", s->name);

    int fd = db_open_file(filename);

    off_t end = lseek(fd, 0, SEEK_END);
    if (end < 0) {
        close(fd);
        strcpy(response, "ERROR: failed to read table file");
        return -1;
    }

    if (end == 0) {
        db_alloc_block(fd);
        end = lseek(fd, 0, SEEK_END);
    }

    int blocks = (int)(end / BLOCK_SIZE);
    if (blocks <= 0) {
        close(fd);
        *rows_out = NULL;
        *row_count_out = 0;
        return 0;
    }

    int payload_bytes = blocks * BLOCK_PAYLOAD;
    char *payload = (char *)malloc((size_t)payload_bytes);
    if (!payload) {
        close(fd);
        strcpy(response, "ERROR: out of memory");
        return -1;
    }

    char block[BLOCK_SIZE];
    for (int i = 0; i < blocks; i++) {
        db_read_block(fd, i, block);
        memcpy(payload + i * BLOCK_PAYLOAD, block, BLOCK_PAYLOAD);
    }
    close(fd);

    int cap = payload_bytes / s->row_width + 1;
    char *rows = (char *)malloc((size_t)(cap * s->row_width));
    if (!rows) {
        free(payload);
        strcpy(response, "ERROR: out of memory");
        return -1;
    }

    int count = 0;
    for (int off = 0; off + s->row_width <= payload_bytes; off += s->row_width) {
        if (is_empty_chunk(payload + off, s->row_width)) continue;
        memcpy(rows + count * s->row_width, payload + off, s->row_width);
        count++;
    }

    free(payload);
    *rows_out = rows;
    *row_count_out = count;
    return 0;
}

static int write_table_rows(const Schema *s, const char *rows, int row_count, char *response) {
    char filename[MAX_NAME_LEN + 8];
    snprintf(filename, sizeof(filename), "%s.db", s->name);

    int fd = db_open_file(filename);
    if (ftruncate(fd, 0) < 0) {
        close(fd);
        strcpy(response, "ERROR: failed to rewrite table file");
        return -1;
    }

    int total_bytes = row_count * s->row_width;
    int blocks = (total_bytes + BLOCK_PAYLOAD - 1) / BLOCK_PAYLOAD;
    if (blocks <= 0) blocks = 1;

    for (int i = 0; i < blocks; i++) (void)db_alloc_block(fd);

    char block[BLOCK_SIZE];
    int copied = 0;
    for (int i = 0; i < blocks; i++) {
        db_init_block(block);
        db_set_next(block, (i + 1 < blocks) ? (i + 1) : -1);

        int remaining = total_bytes - copied;
        int chunk = remaining > BLOCK_PAYLOAD ? BLOCK_PAYLOAD : remaining;
        if (chunk > 0) {
            memcpy(block, rows + copied, (size_t)chunk);
            copied += chunk;
        }
        db_write_block(fd, i, block);
    }

    close(fd);
    return 0;
}

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
    char *end = strrchr(tmp, ')');
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
    char q[1024];
    strncpy(q, query, sizeof(q) - 1);
    q[sizeof(q) - 1] = '\0';
    trim_in_place(q);

    if (!starts_with_ci(q, "INSERT INTO")) {
        strcpy(response, "ERROR: malformed INSERT statement");
        return -1;
    }

    const char *p = q + strlen("INSERT INTO");
    while (*p && isspace((unsigned char)*p)) p++;

    char table[MAX_NAME_LEN];
    int tlen = 0;
    while (*p && !isspace((unsigned char)*p) && *p != '(' && tlen < MAX_NAME_LEN - 1) {
        table[tlen++] = *p++;
    }
    table[tlen] = '\0';

    const char *values_kw = find_ci(q, "VALUES");
    if (table[0] == '\0' || values_kw == NULL) {
        strcpy(response, "ERROR: malformed INSERT statement");
        return -1;
    }

    const char *open = strchr(values_kw, '(');
    const char *close = strrchr(values_kw, ')');
    if (!open || !close || close <= open) {
        strcpy(response, "ERROR: malformed VALUES list");
        return -1;
    }

    char values_src[1024];
    size_t vlen = (size_t)(close - open - 1);
    if (vlen >= sizeof(values_src)) vlen = sizeof(values_src) - 1;
    memcpy(values_src, open + 1, vlen);
    values_src[vlen] = '\0';

    Schema s;
    if (schema_find(table, &s) < 0) {
        snprintf(response, 256, "ERROR: table '%s' does not exist", table);
        return -1;
    }

    char vals[MAX_COLS][256];
    int nvals = split_csv_values(values_src, vals, MAX_COLS);
    if (nvals != s.ncols) {
        snprintf(response, 256, "ERROR: expected %d values, got %d", s.ncols, nvals);
        return -1;
    }

    char *row = (char *)malloc((size_t)s.row_width);
    if (!row) {
        strcpy(response, "ERROR: out of memory");
        return -1;
    }

    int off = 0;
    for (int i = 0; i < s.ncols; i++) {
        if (encode_cell(&s.cols[i], vals[i], row + off, response) < 0) {
            free(row);
            return -1;
        }
        off += s.cols[i].width;
    }

    char *rows = NULL;
    int count = 0;
    if (read_table_rows(&s, &rows, &count, response) < 0) {
        free(row);
        return -1;
    }

    char *new_rows = (char *)realloc(rows, (size_t)((count + 1) * s.row_width));
    if (!new_rows) {
        free(row);
        free(rows);
        strcpy(response, "ERROR: out of memory");
        return -1;
    }
    rows = new_rows;
    memcpy(rows + count * s.row_width, row, s.row_width);
    count++;
    free(row);

    if (write_table_rows(&s, rows, count, response) < 0) {
        free(rows);
        return -1;
    }

    free(rows);
    snprintf(response, 256, "OK: inserted 1 row into '%s'", table);
    return 0;
}

/* ─────────────────────────────────────────
   sql_select  (to be implemented)
   ───────────────────────────────────────── */
int sql_select(const char *query, char *response) {
    char q[1024];
    strncpy(q, query, sizeof(q) - 1);
    q[sizeof(q) - 1] = '\0';
    trim_in_place(q);

    if (!starts_with_ci(q, "SELECT")) {
        strcpy(response, "ERROR: malformed SELECT statement");
        return -1;
    }

    const char *from_kw = find_ci(q, " FROM ");
    if (!from_kw) {
        strcpy(response, "ERROR: missing FROM clause");
        return -1;
    }

    char select_part[512];
    size_t slen = (size_t)(from_kw - (q + strlen("SELECT")));
    const char *sp = q + strlen("SELECT");
    while (*sp && isspace((unsigned char)*sp)) sp++;
    if ((size_t)(from_kw - sp) >= sizeof(select_part)) {
        strcpy(response, "ERROR: SELECT column list too long");
        return -1;
    }
    slen = (size_t)(from_kw - sp);
    memcpy(select_part, sp, slen);
    select_part[slen] = '\0';
    trim_in_place(select_part);

    const char *after_from = from_kw + strlen(" FROM ");
    while (*after_from && isspace((unsigned char)*after_from)) after_from++;

    char table[MAX_NAME_LEN];
    int tlen = 0;
    while (after_from[tlen] && !isspace((unsigned char)after_from[tlen]) &&
           after_from[tlen] != ';' && tlen < MAX_NAME_LEN - 1) {
        table[tlen] = after_from[tlen];
        tlen++;
    }
    table[tlen] = '\0';

    if (table[0] == '\0') {
        strcpy(response, "ERROR: missing table name in SELECT");
        return -1;
    }

    const char *where_kw = find_ci(after_from, " WHERE ");
    int has_where = (where_kw != NULL);

    Schema s;
    if (schema_find(table, &s) < 0) {
        snprintf(response, 256, "ERROR: table '%s' does not exist", table);
        return -1;
    }

    int selected_idx[MAX_COLS];
    int selected_count = 0;

    if (strcmp(select_part, "*") == 0) {
        for (int i = 0; i < s.ncols; i++) selected_idx[selected_count++] = i;
    } else {
        char names[MAX_COLS][256];
        int n = split_csv_values(select_part, names, MAX_COLS);
        if (n <= 0) {
            strcpy(response, "ERROR: malformed SELECT column list");
            return -1;
        }
        for (int i = 0; i < n; i++) {
            int idx = schema_col_index(&s, names[i]);
            if (idx < 0) {
                snprintf(response, 256, "ERROR: unknown column '%s'", names[i]);
                return -1;
            }
            selected_idx[selected_count++] = idx;
        }
    }

    WhereClause wc;
    if (has_where) {
        if (parse_where_clause(&s, where_kw + strlen(" WHERE "), &wc, response) < 0) {
            return -1;
        }
    }

    char *rows = NULL;
    int count = 0;
    if (read_table_rows(&s, &rows, &count, response) < 0) return -1;

    response[0] = '\0';
    size_t used = 0;

    for (int i = 0; i < selected_count; i++) {
        const char *name = s.cols[selected_idx[i]].name;
        used += (size_t)snprintf(response + used, RESP_MAX - used, "%s%s",
                                 (i == 0) ? "" : ",", name);
    }
    used += (size_t)snprintf(response + used, RESP_MAX - used, "\n");

    int matched = 0;
    for (int r = 0; r < count; r++) {
        const char *row = rows + r * s.row_width;
        if (has_where && !where_matches(&s, row, &wc)) continue;

        for (int c = 0; c < selected_count; c++) {
            int idx = selected_idx[c];
            int off = row_offset_for_col(&s, idx);
            char cell[256];
            decode_cell(&s.cols[idx], row + off, cell, sizeof(cell));
            used += (size_t)snprintf(response + used, RESP_MAX - used, "%s%s",
                                     (c == 0) ? "" : ",", cell);
        }
        used += (size_t)snprintf(response + used, RESP_MAX - used, "\n");
        matched++;

        if (used >= RESP_MAX - 64) {
            used += (size_t)snprintf(response + used, RESP_MAX - used,
                                     "... output truncated ...\n");
            break;
        }
    }

    if (matched == 0) {
        used += (size_t)snprintf(response + used, RESP_MAX - used, "(0 rows)\n");
    }

    free(rows);
    return 0;
}

/* ─────────────────────────────────────────
   sql_update  (to be implemented)
   ───────────────────────────────────────── */
int sql_update(const char *query, char *response) {
    char q[1024];
    strncpy(q, query, sizeof(q) - 1);
    q[sizeof(q) - 1] = '\0';
    trim_in_place(q);

    if (!starts_with_ci(q, "UPDATE")) {
        strcpy(response, "ERROR: malformed UPDATE statement");
        return -1;
    }

    const char *set_kw = find_ci(q, " SET ");
    const char *where_kw = find_ci(q, " WHERE ");
    if (!set_kw || !where_kw || where_kw <= set_kw) {
        strcpy(response, "ERROR: UPDATE requires SET and WHERE clauses");
        return -1;
    }

    char table[MAX_NAME_LEN];
    const char *tp = q + strlen("UPDATE");
    while (*tp && isspace((unsigned char)*tp)) tp++;
    int tlen = 0;
    while (tp < set_kw && !isspace((unsigned char)*tp) && tlen < MAX_NAME_LEN - 1) {
        table[tlen++] = *tp++;
    }
    table[tlen] = '\0';

    if (table[0] == '\0') {
        strcpy(response, "ERROR: missing table name in UPDATE");
        return -1;
    }

    Schema s;
    if (schema_find(table, &s) < 0) {
        snprintf(response, 256, "ERROR: table '%s' does not exist", table);
        return -1;
    }

    char set_expr[256];
    size_t set_len = (size_t)(where_kw - (set_kw + strlen(" SET ")));
    if (set_len >= sizeof(set_expr)) set_len = sizeof(set_expr) - 1;
    memcpy(set_expr, set_kw + strlen(" SET "), set_len);
    set_expr[set_len] = '\0';
    trim_in_place(set_expr);

    char *eq = strchr(set_expr, '=');
    if (!eq) {
        strcpy(response, "ERROR: malformed SET clause");
        return -1;
    }
    *eq = '\0';

    char set_col[MAX_NAME_LEN];
    char set_val[256];
    strncpy(set_col, set_expr, sizeof(set_col) - 1);
    set_col[sizeof(set_col) - 1] = '\0';
    trim_in_place(set_col);

    strncpy(set_val, eq + 1, sizeof(set_val) - 1);
    set_val[sizeof(set_val) - 1] = '\0';
    trim_in_place(set_val);

    int set_idx = schema_col_index(&s, set_col);
    if (set_idx < 0) {
        snprintf(response, 256, "ERROR: unknown column '%s' in SET", set_col);
        return -1;
    }

    WhereClause wc;
    if (parse_where_clause(&s, where_kw + strlen(" WHERE "), &wc, response) < 0) {
        return -1;
    }

    char *rows = NULL;
    int count = 0;
    if (read_table_rows(&s, &rows, &count, response) < 0) return -1;

    int set_off = row_offset_for_col(&s, set_idx);
    char encoded[256];
    if (encode_cell(&s.cols[set_idx], set_val, encoded, response) < 0) {
        free(rows);
        return -1;
    }

    int updated = 0;
    for (int r = 0; r < count; r++) {
        char *row = rows + r * s.row_width;
        if (where_matches(&s, row, &wc)) {
            memcpy(row + set_off, encoded, s.cols[set_idx].width);
            updated++;
        }
    }

    if (write_table_rows(&s, rows, count, response) < 0) {
        free(rows);
        return -1;
    }

    free(rows);
    snprintf(response, 256, "OK: updated %d row(s) in '%s'", updated, table);
    return 0;
}

/* ─────────────────────────────────────────
   sql_delete  (to be implemented)
   ───────────────────────────────────────── */
int sql_delete(const char *query, char *response) {
    char q[1024];
    strncpy(q, query, sizeof(q) - 1);
    q[sizeof(q) - 1] = '\0';
    trim_in_place(q);

    if (!starts_with_ci(q, "DELETE FROM")) {
        strcpy(response, "ERROR: malformed DELETE statement");
        return -1;
    }

    const char *where_kw = find_ci(q, " WHERE ");
    if (!where_kw) {
        strcpy(response, "ERROR: DELETE requires WHERE clause");
        return -1;
    }

    const char *tp = q + strlen("DELETE FROM");
    while (*tp && isspace((unsigned char)*tp)) tp++;

    char table[MAX_NAME_LEN];
    int tlen = 0;
    while (tp < where_kw && !isspace((unsigned char)*tp) && tlen < MAX_NAME_LEN - 1) {
        table[tlen++] = *tp++;
    }
    table[tlen] = '\0';

    if (table[0] == '\0') {
        strcpy(response, "ERROR: missing table name in DELETE");
        return -1;
    }

    Schema s;
    if (schema_find(table, &s) < 0) {
        snprintf(response, 256, "ERROR: table '%s' does not exist", table);
        return -1;
    }

    WhereClause wc;
    if (parse_where_clause(&s, where_kw + strlen(" WHERE "), &wc, response) < 0) {
        return -1;
    }

    char *rows = NULL;
    int count = 0;
    if (read_table_rows(&s, &rows, &count, response) < 0) return -1;

    char *kept = (char *)malloc((size_t)(count * s.row_width));
    if (!kept) {
        free(rows);
        strcpy(response, "ERROR: out of memory");
        return -1;
    }

    int kept_count = 0;
    int deleted = 0;
    for (int r = 0; r < count; r++) {
        char *row = rows + r * s.row_width;
        if (where_matches(&s, row, &wc)) {
            deleted++;
            continue;
        }
        memcpy(kept + kept_count * s.row_width, row, s.row_width);
        kept_count++;
    }

    if (write_table_rows(&s, kept, kept_count, response) < 0) {
        free(rows);
        free(kept);
        return -1;
    }

    free(rows);
    free(kept);
    snprintf(response, 256, "OK: deleted %d row(s) from '%s'", deleted, table);
    return 0;
}