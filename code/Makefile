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
# node.c si aggiungera' da solo via wildcard quando arriva (workstream D).
# miner.c/client.c/ipc.c sono in root come blockchain.c: il wildcard non li
# vede, vanno elencati esplicitamente (workstream E/F).
SRCS      := blockchain.c miner.c client.c ipc.c $(wildcard utils/*.c) $(wildcard encoding/*.c)
OBJS      := $(SRCS:.c=.o)

# Oggetti da linkare nel binario di test: tutto tranne blockchain.o, che ha
# il main() dell'eseguibile principale e andrebbe in conflitto col main()
# di test.c.
# MERGE: sostituisce UTIL_OBJS/ENC_OBJS introdotti nel refactor. Stesso
# intento (linkare anche gli oggetti di encoding/, che mancavano e facevano
# fallire il link di test_encoding con undefined reference), ma derivato da
# OBJS: cosi' include anche miner.o/client.o/ipc.o, necessari ai test dei
# workstream E/F, e resta allineato da solo se SRCS cambia.
TEST_OBJS := $(filter-out blockchain.o,$(OBJS))
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

$(TEST_BIN): test.o $(TEST_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f $(BIN) $(TEST_BIN) $(OBJS) test.o
	rm -f *-*.log
	rm -f /dev/shm/blockchain_*
