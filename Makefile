CFLAGS=-std=c99 -pedantic -Wall -O2 -g -ggdb -I/opt/local/include
LDFLAGS=-L/opt/local/lib -levent

all: cobb2

cobb2: dline.o main.o parse.o trie.o
	gcc dline.o main.o parse.o trie.o -o cobb2 $(LDFLAGS)

trie.o: trie.c

dline.o: dline.c

main.o: main.c

parse.o: parse.c

clean:
	rm -f cobb2 *.o
