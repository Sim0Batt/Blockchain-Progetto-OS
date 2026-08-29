#ifndef NODE_H
#define NODE_H
#include "shared_state.h"
#include "utils/chain.h"
#include <stdio.h>

/* Manda un blocco ricevuto all'arbitraggio, aggiorna la copia locale col
 * vincitore e, se era nuovo, lo propaga ai peer. */
int nodeHandleBlock(SharedState *st, uint32_t nodeId, Blockchain *chain, const Block *blk, FILE *log);

/* Applica i blocchi decisi che mancano alla copia locale, dall'altezza
 * corrente in poi. Ritorna quanti ne ha applicati. */
int nodeApplyDecided(SharedState *st, uint32_t nodeId, Blockchain *chain, FILE *log);

/* Loop del processo node: prepara la copia locale della chain, apre il log e
 * consuma la propria inbox finche' st->running non diventa 0. */
int runNode(SharedState *st, uint32_t nodeId, const char *initialState);
#endif
