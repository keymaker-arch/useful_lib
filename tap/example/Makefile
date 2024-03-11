.PHONY: all clean

all: example

example: main.c ../tap.c ../tap.h
	$(CC) -o $@ $^

clean:
	rm -f example
