# An admittedly primitive Makefile
# To compile, type "make" or "make all"
# To remove files, type "make clean"

CC     = gcc
CFLAGS = -Wall -g

SERVER_OBJS = wserver.o request.o io_helper.o
CLIENT_OBJS = wclient.o io_helper.o
DB_OBJS     = db_io.o

.SUFFIXES: .c .o

all: wserver wclient spin.cgi sql_server.cgi db_test

wserver: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o wserver $(SERVER_OBJS)

wclient: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o wclient $(CLIENT_OBJS)

spin.cgi: spin.c
	$(CC) $(CFLAGS) -o spin.cgi spin.c

sql_server.cgi: sql_server.o sql.o $(DB_OBJS) io_helper.o
	$(CC) $(CFLAGS) -o sql_server.cgi sql_server.o sql.o $(DB_OBJS) io_helper.o

db_test: db_test.o sql.o $(DB_OBJS)
	$(CC) $(CFLAGS) -o db_test db_test.o sql.o $(DB_OBJS)

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	-rm -f *.o wserver wclient spin.cgi sql_server.cgi db_test
	-rm -f schema.db *.db