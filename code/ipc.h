#ifndef IPC_H
#define IPC_H

#include "shared_state.h"

/* Layer IPC: shared memory POSIX piu' semafori unnamed (pshared=1) dentro
 * SharedState; i figli ereditano la mappatura dalla fork(), senza exec.
 * La chain non passa di qui: ogni node ne tiene una copia locale. */

/* Nome del segmento in /dev/shm. Il prefisso "blockchain_" serve al target
 * clean del Makefile (rm -f /dev/shm/blockchain_*). */
#define SHM_NAME "/blockchain_state"

/* Ciclo di vita: solo il bootstrapper. */

/* Crea il segmento, lo mappa, azzera i buffer e inizializza i semafori. Va
 * chiamata una volta sola dal padre, prima della fork().
 * Ritorna il puntatore mappato, oppure NULL su errore. */
SharedState *ipcCreate(uint32_t difficulty, uint32_t num_nodes);

/* Smappa e rimuove il segmento. La chiama il padre allo shutdown. */
void ipcDestroy(SharedState *st);

/* Bounded buffer delle transazioni: client -> miner. */

/* Producer (client): inserisce una transazione. Bloccante se il pool e' pieno,
 * per fare backpressure. Ritorna SUCCESS o IPC_ERROR. */
int txpoolPut(SharedState *st, const Transaction *tx);

/* Consumer (miner): preleva una transazione. Bloccante se il pool e' vuoto. */
int txpoolGet(SharedState *st, Transaction *out);

/* Consumer non bloccante: preleva se c'e' qualcosa, altrimenti ritorna subito
 * IPC_EMPTY. Il miner lo usa per drenare le tx gia' nel pool. */
int txpoolTryget(SharedState *st, Transaction *out);

/* Inbox per node: miner/peer -> node. */

/* Consegna un blocco all'inbox del node 'nodeId'. Bloccante se piena. */
int inboxPut(SharedState *st, uint32_t nodeId, const Block *blk);

/* Il node 'nodeId' preleva un blocco dalla propria inbox. Bloccante se
 * vuota. */
int inboxGet(SharedState *st, uint32_t nodeId, Block *out);

/* Invia il blocco a tutte le inbox attive tranne 'exclude'
 * (-1 = nessuna esclusione). */
int inboxBroadcast(SharedState *st, const Block *blk, int exclude);

int txpoolTimedput(SharedState *st, const Transaction *tx, unsigned int timeoutMs);
int inboxTryput(SharedState *st, uint32_t nodeId, const Block *blk);
int nodePublishHead(SharedState *st, uint32_t nodeId, uint64_t height, const char *lastHash);
int minerReadTip(SharedState *st, uint32_t nodeId, uint64_t *height, char prevHash[HASH_BUF_SIZE]);
int minerShouldAbort(SharedState *st, uint32_t nodeId, uint64_t builtOnIndex);

#endif /* IPC_H */
