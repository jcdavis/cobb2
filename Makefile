CFLAGS=-std=c99 -pedantic -Wall -O2 -g

all: cobb2

cobb2: dline.o main.o
	gcc dline.o main.o -o cobb2

dline.o: dline.c cobb2.h dline.h

main.o: main.c cobb2.h dline.h

clean:
	rm -f cobb2 *.o
