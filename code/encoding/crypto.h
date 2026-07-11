#ifndef CRYPT_H
#define CRYPT_H

#include "../shared_state.h"

void calculateSha256(const char *input, char *output);

void calculateMerkelRoot(const char *transaction, const char *merkelRoot);

void calculateBlockHash(const Block *block, char *hash);

#endif
