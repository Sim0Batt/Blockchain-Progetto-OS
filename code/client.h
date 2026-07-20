#ifndef CLIENT_H
#define CLIENT_H

#include "shared_state.h"

/* ============================================================
 *  Processo Client (workstream F).
 *  Genera transazioni random valide e le sottomette al tx_pool
 *  condiviso, a frequenza costante.
 * ============================================================ */

/* Entry point del processo client, chiamata dal bootstrapper (G) nel
 * figlio dopo la fork() (niente exec, niente main() qui).
 * txFrequency = transazioni al secondo, deve essere > 0.
 * Gira finche' st->running non diventa 0. Logga su client-<PID>.log.
 * Ritorna SUCCESS o un error code da errors.h.
 * // TODO: confermare firma con Nicola (bootstrapper G) */
int runClient(SharedState *st, double txFrequency, int clientId);

/* ---- Helper esposto SOLO per testabilita' (vedi test.c) --------------
 * Non e' pensato per essere chiamato dal bootstrapper: e' la stessa
 * funzione che runClient() usa internamente per generare una tx, esposta
 * qui per poterla testare in isolamento (formato regex, seed per-processo)
 * senza dover mandare su un intero SharedState/IPC.
 * Genera una transazione random valida (due nomi + importo >= 1) dentro
 * 'out'. Usa random(): il chiamante e' responsabile del seed (srandom).
 * Ritorna SUCCESS o un error code su fallimento di serializzazione. */
int clientGenerateTransaction(Transaction *out);

#endif /* CLIENT_H */
