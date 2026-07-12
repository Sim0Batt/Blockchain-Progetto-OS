#ifndef ERRORS_H
#define ERRORS_H

#define SUCCESS 0
#define INVALID_BLOCK 1
#define CHAIN_MISMATCH 2
#define INVALID_TRANSACTION 3
#define BLOCK_NOT_FOUND 4
#define IO_ERROR 5
#define IPC_ERROR 6
#define PARSE_ERROR 7
#define MEMORY_ERROR 8
#define IPC_EMPTY 9

#include <string.h>


char* codesToString(int code);



#endif
