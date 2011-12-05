CFLAGS=-std=c99 -pedantic -Wall -O2 -g -ggdb

all: cobb2

cobb2: dline.o main.o trie.o
	gcc dline.o main.o trie.o -o cobb2

trie.o: trie.c dline.c cobb2.h dline.h trie.h

dline.o: dline.c cobb2.h dline.h

main.o: main.c cobb2.h dline.h

clean:
	rm -f cobb2 *.o
