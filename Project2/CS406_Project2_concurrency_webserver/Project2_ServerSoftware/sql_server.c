#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sql.h"

#define QUERY_MAX 4096
#define RESP_MAX  8192

/* ── URL-decode: replace '+' with ' ' and '%XX' with the actual character ── */
static void url_decode(const char *src, char *dst, size_t dst_sz) {
    size_t i = 0;
    while (*src && i < dst_sz - 1) {
        if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else if (*src == '%' && isxdigit((unsigned char)src[1])
                                && isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], '\0' };
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

/* ── Trim leading/trailing whitespace in place ── */
static void trim(char *s) {
    size_t start = 0, len = strlen(s);
    while (start < len && isspace((unsigned char)s[start])) start++;
    while (len > start && isspace((unsigned char)s[len-1])) len--;
    if (start > 0) memmove(s, s + start, len - start);
    s[len - start] = '\0';
}

/* ── Dispatch the SQL query to the correct handler ── */
static int dispatch(const char *query, char *response) {
    /* Case-insensitive prefix check for each command */
    if (strncasecmp(query, "CREATE", 6) == 0)
        return sql_create(query, response);
    if (strncasecmp(query, "INSERT", 6) == 0)
        return sql_insert(query, response);
    if (strncasecmp(query, "SELECT", 6) == 0)
        return sql_select(query, response);
    if (strncasecmp(query, "UPDATE", 6) == 0)
        return sql_update(query, response);
    if (strncasecmp(query, "DELETE", 6) == 0)
        return sql_delete(query, response);

    snprintf(response, RESP_MAX,
             "ERROR: unknown command (must be CREATE/INSERT/SELECT/UPDATE/DELETE)");
    return -1;
}

int main(void) {
    char raw[QUERY_MAX]  = {0};
    char query[QUERY_MAX] = {0};
    char response[RESP_MAX] = {0};

    /* ── 1. Get the query string from the environment ── */
    const char *qs = getenv("QUERY_STRING");
    if (!qs || qs[0] == '\0') {
        printf("Content-Type: text/plain\r\n\r\n");
        printf("ERROR: no query string provided\n");
        printf("Usage: /sql_server.cgi?SELECT * FROM tablename\n");
        fflush(stdout);
        return 0;
    }

    /* ── 2. URL-decode then trim ── */
    url_decode(qs, raw, sizeof(raw));
    strncpy(query, raw, sizeof(query) - 1);
    trim(query);

    /* ── 3. Dispatch to SQL handler ── */
    dispatch(query, response);

    /* ── 4. Send HTTP response (headers + body) ── */
    printf("Content-Type: text/plain\r\n\r\n");
    printf("%s\n", response);
    fflush(stdout);

    return 0;
}