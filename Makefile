CFLAGS=-std=c99 -pedantic -Wall -O3 -g -ggdb -I/opt/local/include
LDFLAGS=-L/opt/local/lib -levent

all: cobb2

cobb2: cmalloc.o dline.o http.o main.o parse.o server.o trie.o
	gcc cmalloc.o dline.o http.o main.o parse.o server.o trie.o -o cobb2 $(LDFLAGS)

trie.o: trie.c

dline.o: dline.c

main.o: main.c

parse.o: parse.c

http.o: http.c

server.o: server.c

cmalloc.o: cmalloc.c

clean:
	rm -f cobb2 *.o
