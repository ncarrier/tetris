CC ?= gcc
CFLAGS = -Wall -Wconversion -O0 -g

all:	tetris

tetris:	main.c
	$(CC) $(CFLAGS) main.c -o tetris

clean:
	rm -f tetris
