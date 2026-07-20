#include <string.h>
#include "../shared_state.h"
#include "../utils/errors.h"
#include "../encoding/crypto.h"
#include "chain.h"


// Funzione di controllo validità del blocck
int validateBlock(const Block *previousBlock, const Block *newBlock) {
    // Controlliamo che i due blocchi siano connessi nello stesso array
    if (newBlock->index != previousBlock->index + 1) {
        return INVALID_BLOCK;
    }

    char expectedPreviousHash[HASH_BUF_SIZE];
    calculateBlockHash(previousBlock, expectedPreviousHash);

    // Controlliamo che l'hash sia andato a buon fine
    if (strcmp(expectedPreviousHash, newBlock->prev_hash) != 0) {
        return CHAIN_MISMATCH;
    }

    return SUCCESS;
}


// Funzione per aggiungere un blocco in coda alla copia locale della chain.
// Nessun lock: la Blockchain e' privata del processo che la possiede.
int appendChainBlock(Blockchain *chain, const Block *newBlock) {
    uint64_t chainHeight = chain->height;

    // Controllo se il blocco è valido a meno che l'array non sia vuoto (height=0)
    if (chainHeight > 0) {
        const Block prev = chain->blocks[chainHeight - 1];
        int validation = validateBlock(&prev, newBlock);
        if (validation != SUCCESS) {
            return validation;
        }
    }

    // Controlliamo il possibile memory overflow
    if (chainHeight >= MAX_CHAIN) {
        return MEMORY_ERROR;
    }

    // Aggiungiamo il blocco alla lista e aumentiamo l'altezza
    chain->blocks[chainHeight] = *newBlock;
    chain->height++;

    return SUCCESS;
}
