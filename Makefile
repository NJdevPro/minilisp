CC=gcc
CFLAGS=-std=gnu99 -O2 -Wall -Wshadow -Wextra -Wno-unused-parameter
LDFLAGS=

.PHONY: clean test

all: bestline.o minilisp

bestline.o: 
	cd bestline && $(MAKE)

minilisp: bestline.o
	cd src && $(CC) $(CFLAGS) -c gc.c minilisp.c repl.c
	cd src && $(CC) $(LDFLAGS) -o minilisp ../bestline/bestline.o gc.o minilisp.o repl.o
	mv src/minilisp .

clean:
	cd bestline && $(MAKE) clean
	cd src && rm -f minilisp *~

test: minilisp
	@./test.sh
