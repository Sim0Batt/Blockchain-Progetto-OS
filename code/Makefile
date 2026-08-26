# ============================================================
#  Blockchain OS - Makefile
#  Un solo eseguibile (bootstrapper): via fork() diventa
#  node / miner / client. Niente exec => i figli ereditano
#  la shared memory e i semafori gia' inizializzati.
# ============================================================

CC      := gcc
CFLAGS  := -std=gnu11 -Wall -Wextra -g -I. -Iutils -Iencoding
LDFLAGS := -pthread          # semafori POSIX (sem_*)
LDLIBS  := -lrt -lcrypto              # shared memory POSIX (shm_open)

# --- Sorgenti dell'eseguibile principale ---
# I sorgenti in root sono elencati esplicitamente per tenere test.c (che ha
# un suo main()) fuori dal binario; utils/ ed encoding/ entrano via wildcard.
SRCS      := blockchain.c ipc.c node.c miner.c client.c $(wildcard utils/*.c) $(wildcard encoding/*.c)
OBJS      := $(SRCS:.c=.o)

UTIL_OBJS := $(patsubst %.c,%.o,$(wildcard utils/*.c))
ENC_OBJS  := $(patsubst %.c,%.o,$(wildcard encoding/*.c))
HDRS      := $(wildcard *.h) $(wildcard utils/*.h) $(wildcard encoding/*.h)

# --- Eseguibili prodotti ---
BIN       := blockchain
TEST_BIN  := test_encoding

# Argomenti passabili a run:
#   make run ARGS="3 5 10 1 12 initial_state.csv"
ARGS ?=

.PHONY: build run clean test

build: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

# Compilazione separata: ogni .o dipende dal suo .c e da TUTTI gli header
%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

run: build
	./$(BIN) $(ARGS)

test: $(TEST_BIN)
	./$(TEST_BIN)

# test.c esercita anche miner/client/ipc: servono i rispettivi .o (ipc.o per
# minerReadTip/minerShouldAbort/nodePublishHead). tx.o e crypto.o arrivano da
# UTIL_OBJS/ENC_OBJS.
$(TEST_BIN): test.o miner.o client.o ipc.o $(UTIL_OBJS) $(ENC_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f $(BIN) $(TEST_BIN) $(OBJS) test.o
	rm -f *-*.log
	rm -f /dev/shm/blockchain_*
