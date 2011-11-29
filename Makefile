all:main.c
	gcc main.c -o tetris -Wall -Wconversion -O0 -g

clean:
	rm -f tetris
