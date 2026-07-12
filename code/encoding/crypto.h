#ifndef CRYPT_H
#define CRYPT_H

#include "../shared_state.h"

void calculateSha256(const char *input, char *output);

void calculateMerkleRoot(const char *transaction, char *merkleRoot);

void calculateBlockHash(const Block *block, char *hash);

#endif
