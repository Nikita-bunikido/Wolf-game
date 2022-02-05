CC=gcc
CFLAGS=-Wall -O1 -std=c11

default: main.c
	$(CC) $(CFLAGS) main.c -o main.exe