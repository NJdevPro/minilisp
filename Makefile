CFLAGS=-std=gnu99  -O2 -Wall

.PHONY: clean test

minilisp: minilisp.c

clean:
	rm -f minilisp *~

test: minilisp
	@./test.sh
