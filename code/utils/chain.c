#include <string.h>
#include <errno.h>
#include <semaphore.h>
#include "../shared_state.h"
#include "../utils/errors.h"
#include "../encoding/crypto.h"
#include "chain.h"


static int semWaitSafe(sem_t *s) {
    int r;
    do {
        r = sem_wait(s);
    } while (r == -1 && errno == EINTR);
    return r;
}


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


// Funzione per aggiungere un blocco in coda all'array
int appendChainBlock(SharedState *ss, const Block *newBlock) {
    // Prendiamo il mutex lock del semaforo
    semWaitSafe(&ss->chain_mutex);

    int chainHeight = ss->height;

    // Controllo se il blocco è valido a meno che l'array non sia vuoto (height=0)
    if (chainHeight > 0) {
        const Block prev = ss->chain[chainHeight - 1];
        int validation = validateBlock(&prev, newBlock);
        if (validation != SUCCESS) {
            // ATTENZIONE: rilascio del mutex lock ogni volta per evitare deadlock
            sem_post(&ss->chain_mutex);
            return validation;
        }
    }

    // Controlliamo il possibile memory overflow
    if (chainHeight >= MAX_CHAIN) {
        sem_post(&ss->chain_mutex);
        return MEMORY_ERROR;
    }

    // Aggiungiamo il blocco alla lista e aumentiamo l'altezza
    ss->chain[chainHeight] = *newBlock;
    ss->height++;

    sem_post(&ss->chain_mutex);

    return SUCCESS;

}
