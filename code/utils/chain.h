#include "../shared_state.h"
#include "../utils/errors.h"
#include "../encoding/crypto.h"

#ifndef CHAIN_H
#define CHAIN_H


int validateBlock(const Block *previousBlock, const Block *newBlock);
int appendChainBlock(Blockchain *chain, const Block *newBlock);

#endif
