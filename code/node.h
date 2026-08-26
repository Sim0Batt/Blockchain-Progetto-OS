#ifndef NODE_H
#define NODE_H
#include "shared_state.h"
#include "utils/chain.h"
#include <stdio.h>

/* Gestisce UN blocco ricevuto: valida+appende sulla copia locale, e se e'
 * nuovo (append SUCCESS) lo propaga ai peer. Ritorna l'esito dell'append. */
int nodeHandleBlock(SharedState *st, uint32_t nodeId, Blockchain *chain, const Block *blk, FILE *log);

/* Loop del processo Node: init copia locale, apri log, consuma la propria
 * inbox all'infinito. */
int runNode(SharedState *st, uint32_t nodeId, const char *initialState);
#endif
