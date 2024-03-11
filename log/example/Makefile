.PHONY: all clean

all: example

example: main.c ../log.c ../log.h
	$(CC) -o $@ $^

clean:
	rm -f example
