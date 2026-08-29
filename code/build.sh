#!/bin/bash

bold=$(tput bold)
normal=$(tput sgr0)
underline=$(tput smul)


CC="gcc"
CFLAGS=(-Wall -Wextra -Wno-unused-parameter -g1 -Iutils -Iencoding)
SRCS=(blockchain.c ipc.c node.c miner.c client.c utils/*.c encoding/*.c)
TEST=(test.c miner.c client.c ipc.c utils/*.c encoding/*.c)
LDFLAGS="-pthread"
LDLIBS="-lrt"

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
  $CC "${CFLAGS[@]}" "${SRCS[@]}" -o blockchain $LDFLAGS $LDLIBS
}

buildTest(){
  $CC "${CFLAGS[@]}" "${TEST[@]}" -o test $LDFLAGS $LDLIBS
}

# La shm va tolta a mano: se il padre muore male non arriva mai a ipcDestroy
clean() {
  rm -f blockchain test
  rm -f *.o utils/*.o encoding/*.o
  rm -f node-*.log miner-*.log client-*.log
  rm -f /dev/shm/blockchain_*
}

# Scenario di default, sovrascrivibile con ARGS="5 3 2 1 4" ./build.sh run
run() {
  build || return $?
  ./blockchain ${ARGS:-3 2 5 1 12 initial_state.csv}
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