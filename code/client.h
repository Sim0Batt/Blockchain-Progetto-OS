#ifndef CLIENT_H
#define CLIENT_H

#include "shared_state.h"

/* Processo client: genera transazioni random valide e le sottomette al
 * tx_pool condiviso a frequenza costante. */

/* Entry point del processo client: il bootstrapper la chiama nel figlio dopo
 * la fork(), senza exec. 'txFrequency' e' in transazioni al secondo e deve
 * essere > 0. Gira finche' st->running non diventa 0 e logga su
 * client-<PID>.log. Ritorna SUCCESS o un error code da errors.h. */
int runClient(SharedState *st, double txFrequency, int clientId);

/* Genera in 'out' una transazione random valida: due nomi diversi e un
 * importo >= 1. Usa random(), quindi il seed e' compito del chiamante.
 * Esposta per poterla provare in isolamento, senza tirare su SharedState e
 * IPC. Ritorna SUCCESS o un error code se la serializzazione fallisce. */
int clientGenerateTransaction(Transaction *out);

#endif /* CLIENT_H */
