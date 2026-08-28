#ifndef NODE_H
#define NODE_H
#include "shared_state.h"
#include "utils/chain.h"
#include <stdio.h>

/* Gestisce un blocco ricevuto: lo valida e lo appende alla copia locale, e se
 * era nuovo lo propaga ai peer. Ritorna l'esito dell'append. */
int nodeHandleBlock(SharedState *st, uint32_t nodeId, Blockchain *chain, const Block *blk, FILE *log);

/* Loop del processo node: prepara la copia locale della chain, apre il log e
 * consuma la propria inbox finche' st->running non diventa 0. */
int runNode(SharedState *st, uint32_t nodeId, const char *initialState);
#endif
