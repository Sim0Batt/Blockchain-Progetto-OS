#ifndef IPC_H
#define IPC_H

#include "shared_state.h"

/* ============================================================
 *  Layer IPC: gestione del segmento di shared memory e dei due
 *  bounded buffer. La chain NON e' qui: ogni node ne possiede
 *  una copia locale (tipo Blockchain).
 *
 *  Meccanismo: POSIX shared memory (shm_open) + semafori unnamed
 *  (pshared=1) embeddati in SharedState. I processi figli ereditano
 *  la mappatura via fork() -- niente exec.
 * ============================================================ */

/* Nome del segmento in /dev/shm. Prefisso "blockchain_" -> lo pesca
 * il target clean del Makefile (rm -f /dev/shm/blockchain_*). */
#define SHM_NAME "/blockchain_state"

/* ---------------- Ciclo di vita (solo il bootstrapper) ---------------- */

/* Crea il segmento, lo mappa, azzera i buffer e inizializza i 6 semafori
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

#endif /* IPC_H */
