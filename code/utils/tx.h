#ifndef TX_H
#define TX_H

#include "../utils/errors.h"

/* Validazione del formato testuale di una transazione. Sta in utils perche'
 * serve sia al client, che si autocontrolla prima del submit, sia al miner,
 * che ricontrolla le tx prese dal pool. */

/* Ritorna SUCCESS se 's' rispetta il formato
 * "^[A-Za-z0-9]+ pays [A-Za-z0-9]+ [1-9][0-9]* coins$", altrimenti
 * INVALID_TRANSACTION. */
int txIsValid(const char *s);

#endif /* TX_H */
