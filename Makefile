CC = gcc
CFLAGS = -g -Wall `pkg-config --cflags gtk+-3.0`
LINKFLAGS = `pkg-config --libs gtk+-3.0`
SRCS = main.c cmaze.c gtk_maze.c
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
cmaze.o: cmaze.h
gtk_maze.o: cmaze.h
