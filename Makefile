CC = gcc
CFLAGS = -g -Wall
LINKFLAGS =
SRCS = cmaze.c
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
