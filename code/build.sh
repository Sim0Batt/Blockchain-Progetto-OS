#!/bin/bash

bold=$(tput bold)
normal=$(tput sgr0)
underline=$(tput smul)


# FIXME: percorsi OpenSSL non portabili su Ubuntu (Homebrew/macOS-only).
# Su Ubuntu 24.04 openssl/sha.h e libcrypto sono gia' nei path di sistema:
# questi -I/-L non servono e possono far fallire lo script se la cartella
# non esiste. Non e' compito del pezzo E/F (miner/client) sistemarlo:
# segnalato a chi possiede il build (workstream X).
CC="gcc"
CFLAGS=(-Wall -Wextra -Wno-unused-parameter -g1 -Iutils -I/opt/homebrew/opt/openssl@3/include)
# miner.c/client.c/ipc.c sono in root come blockchain.c/test.c: vanno
# elencati esplicitamente (il glob utils/*.c non li vede). Tenere allineato
# a SRCS/TEST_OBJS nel Makefile.
SRCS=(blockchain.c miner.c client.c ipc.c utils/*.c encoding/*.c)
TEST=(test.c miner.c client.c ipc.c utils/*.c encoding/*.c)

LDFLAGS="-L/opt/homebrew/opt/openssl@3/lib"

help(){
  echo "---------------------------------------------------"
  echo "|                ${bold}BLOCKCHAIN${normal}                       |"
  echo "---------------------------------------------------"
  echo "| Builds and runs file for the blockchain project |"
  echo "---------------------------------------------------"
  echo "${underline}COMMANDS${normal}"
  echo "  -${bold}run:${normal} builds and runs the blockchain.c main file"
  echo "  -${bold}build:${normal} builds the blockchain.c main file"
  echo "  -${bold}clean:${normal} cleans the workspace from previous builds"
  echo "  -${bold}test:${normal} runs the test.c file with the test of the structure"
  exit 1
}

build() {
  $CC "${CFLAGS[@]}" "${SRCS[@]}" -o blockchain $LDFLAGS -lcrypto
}

buildTest(){
  $CC "${CFLAGS[@]}" "${TEST[@]}" -o test $LDFLAGS -lcrypto
}

clean() {
  rm -f blockchain
}

run() {
  build
  ./blockchain
}

test(){
  buildTest
  ./test
}

if [[ $# -lt 1 ]]; then
    help
fi



case "$1" in
  build)
    build
    ;;

  clean)
    clean
    ;;

  run)
    run
    ;;

  test)
    test
    ;;

  *)
    help
    ;;
esac