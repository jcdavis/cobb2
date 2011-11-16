CFLAGS=-std=c99 -pedantic -Wall -g

all: cobb2

cobb2: dline.o main.o
	gcc dline.o main.o -o cobb2

dline.o: dline.c dline.h

main.o: main.c dline.h

clean:
	rm -f cobb2 *.o
