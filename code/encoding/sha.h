#ifndef BLOCKCHAIN_PROGETTO_OS_SHA_H
#define BLOCKCHAIN_PROGETTO_OS_SHA_H
#include <stdint.h>
#include <stdint.h>
#include <stddef.h>

#define SHA256_BLOCK_SIZE 32

typedef struct SHA256CTX {
    uint8_t data[64];
    uint32_t datalen;
    unsigned long long bitlen;
    uint32_t state[8];
} SHA256CTX;

void sha256Init(SHA256CTX *ctx);
void sha256Update(SHA256CTX *ctx, const uint8_t data[], size_t len);
void sha256Final(SHA256CTX *ctx, uint8_t hash[]);



#endif
