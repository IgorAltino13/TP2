all: bin/client bin/server

bin/client: client.c common.o | bin
	gcc -Wall client.c common.o -o bin/client

bin/server: server.c common.o | bin
	gcc -Wall server.c common.o -o bin/server -lm

common.o: common.c common.h
	gcc -Wall -c common.c

bin:
	mkdir -p bin

clean:
	rm -rf bin common.o
