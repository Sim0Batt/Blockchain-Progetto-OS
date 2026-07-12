#ifndef IPC_H
#define IPC_H

#include "shared_state.h"

/* ============================================================
 *  Layer IPC: gestione del segmento di shared memory, dei due
 *  bounded buffer e dell'accesso serializzato alla chain.
 *
 *  Meccanismo: POSIX shared memory (shm_open) + semafori unnamed
 *  (pshared=1) embeddati in SharedState. I processi figli ereditano
 *  la mappatura via fork() -- niente exec.
 * ============================================================ */

/* Nome del segmento in /dev/shm. Prefisso "blockchain_" -> lo pesca
 * il target clean del Makefile (rm -f /dev/shm/blockchain_*). */
#define SHM_NAME "/blockchain_state"

/* ---------------- Ciclo di vita (solo il bootstrapper) ---------------- */

/* Crea il segmento, lo mappa, azzera i buffer e inizializza i 7 semafori
 * (pshared=1). Va chiamata UNA volta dal padre PRIMA di fork().
 * Ritorna il puntatore mappato, oppure NULL su errore. */
SharedState *ipcCreate(uint32_t difficulty);

/* Smappa e rimuove il segmento: sem_destroy + munmap + shm_unlink.
 * Chiamata dal padre allo shutdown. */
void ipcDestroy(SharedState *st);

/* ---------------- Bounded buffer transazioni (client -> miner) -------- */

/* Producer (client): inserisce una transazione. Bloccante se il pool e'
 * pieno (backpressure). Ritorna SUCCESS o IPC_ERROR. */
int txpoolPut(SharedState *st, const Transaction *tx);

/* Consumer (miner): preleva una transazione. Bloccante se il pool e' vuoto. */
int txpoolGet(SharedState *st, Transaction *out);

/* Consumer non-bloccante: preleva se disponibile, altrimenti ritorna
 * subito IPC_EMPTY. Il miner lo usa per "drenare" le tx gia' presenti. */
int txpoolTryget(SharedState *st, Transaction *out);

/* ---------------- Bounded buffer blocchi (miner -> node) -------------- */

/* Producer (miner): consegna un blocco minato. Bloccante se pieno. */
int blockbufPut(SharedState *st, const Block *blk);

/* Consumer (node): preleva un blocco da validare. Bloccante se vuoto. */
int blockbufGet(SharedState *st, Block *out);

/* ---------------- Chain condivisa (sezione critica) ------------------ */

/* Lettura ottimistica dell'altezza corrente, SENZA lock. Una lettura
 * stale costa al massimo un ciclo di mining sprecato, mai un errore:
 * il controllo autoritativo e' dentro chainAppendValidated(). */
uint64_t chainHeight(SharedState *st);

/* Copia in 'out' l'hash del blocco in cima (serve al miner per prev_hash).
 * Ritorna SUCCESS, oppure BLOCK_NOT_FOUND se la chain e' vuota. */
int chainTopHash(SharedState *st, char out[HASH_BUF_SIZE]);

/* Copia il blocco di indice 'index' in 'out'. Ritorna SUCCESS o
 * BLOCK_NOT_FOUND. Usato dal node per sincronizzare la sua copia locale. */
int chainGetBlock(SharedState *st, uint64_t index, Block *out);

/* CUORE DEL CONSENSO. Il node chiama questa per appendere un blocco.
 * Acquisisce chain_mutex, RI-VALIDA sotto lock (index == height e
 * prev_hash == hash della cima) e appende solo se valido. La
 * ri-validazione dentro il lock e' cio' che impedisce i fork.
 * Ritorna SUCCESS, INVALID_BLOCK o CHAIN_MISMATCH. */
int chainAppendValidated(SharedState *st, const Block *blk);

#endif /* IPC_H */
