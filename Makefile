default: all

all: cmaze

cmaze: cmaze.c list.h
	gcc -g -Wall -o $@ $<
