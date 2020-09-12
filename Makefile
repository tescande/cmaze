CC = gcc
CFLAGS = -g -Wall `pkg-config --cflags glib-2.0`
LINKFLAGS = `pkg-config --libs glib-2.0`
SRCS = main.c cmaze.c
OBJS = $(SRCS:%.c=%.o)

default: all

all: cmaze

.SUFFIXES: .c .o

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@

cmaze: $(OBJS)
	$(CC) $(OBJS) -o $@ $(LINKFLAGS)

clean:
	rm -f cmaze *.o

main.o: cmaze.h
cmaze.o: list.h cmaze.h
