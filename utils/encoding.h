#ifndef ENCODING_H
#define ENCODING_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define HEX_U64_SIZE 16
#define HEX_U64_BUF_SIZE (HEX_U64_SIZE + 1)

int u64ToHex(uint64_t value, char *out, size_t size);

int hexToU64(const char *hex, uint64_t *out);

int bytesToHex(const unsigned char *bytes, size_t sizeIn, char *out, size_t sizeOut);

int hexToBytes(const unsigned char *hex, unsigned char *out, size_t size);

#endif
