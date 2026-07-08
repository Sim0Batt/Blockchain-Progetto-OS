CC=gcc
CFLAGS= -Wall -Wextra -g1 #-Wall -Wextra can arise all the errors during runtime

build: blockchain

blockchain: blockchain.c
	$(CC) $(CFLAGS) -o blockchain blockchain.c #the actual command that runs and compiles the code into blockchain.c


# Possible commands
clean:
	rm -f blockchain

run: build
	./blockchain $(ARGS)