#!/bin/bash

bold=$(tput bold)
normal=$(tput sgr0)
underline=$(tput smul)


CC="gcc"
CFLAGS=(-Wall -Wextra -Wno-unused-parameter -g1 -Iutils -I/opt/homebrew/opt/openssl@3/include)
SRCS=(blockchain.c ipc.c utils/*.c encoding/*.c)
TEST=(test.c utils/*.c encoding/*.c)

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