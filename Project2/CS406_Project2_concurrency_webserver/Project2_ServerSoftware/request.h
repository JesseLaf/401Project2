#ifndef __REQUEST_H__

#include <sys/stat.h>

void request_handle(int fd);

void request_handle_preread(int fd, char *filename, char *cgiargs,
                             struct stat *sbuf, int is_static);

int request_parse_uri(char *uri, char *filename, char *cgiargs);

void request_error(int fd, char *cause, char *errnum,
                   char *shortmsg, char *longmsg);

#endif /* __REQUEST_H__ */