CC = gcc

mfs: mfs.o
	gcc -o mfs mfs.o -g -Wall -Werror --std=c99

clean:
	rm -f *.o *.a mfs

.PHONY: all clean
