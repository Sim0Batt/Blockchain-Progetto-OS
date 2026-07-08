#include <stdio.h>
#include "errors.h"

char* codesToString(int code) {
    switch (code) {
        case SUCCESS: return "SUCCESS";
        case INVALID_BLOCK: return "INVALID_BLOCK";
        case CHAIN_MISMATCH: return "CHAIN_MISMATCH";
        case INVALID_TRANSACTION: return "INVALID_TRANSACTION";
        case BLOCK_NOT_FOUND: return "BLOCK_NOT_FOUND";
        case IO_ERROR: return "IO_ERROR";
        case IPC_ERROR: return "IPC_ERROR";
        case PARSE_ERROR: return "PARSE_ERROR";
        case MEMORY_ERROR: return "MEMORY_ERROR";
        default: return "UNKNOWN_ERROR";
    }
}

