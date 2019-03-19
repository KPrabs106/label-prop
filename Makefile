CC = g++
CCFLAGS = -g -Wall -std=c++11
LDFLAGS = -lpthread

all: labelprop

labelprop: labelprop.o
	$(CC) $(CCFLAGS) labelprop.o -o labelprop $(LDFLAGS) 

labelprop.o: labelprop.c
	$(CC) $(CCFLAGS) -c labelprop.c -o labelprop.o
