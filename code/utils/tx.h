#ifndef TX_H
#define TX_H

#include "../utils/errors.h"

/* ============================================================
 *  Validazione del formato testuale di una transazione.
 *  Vive qui perche' serve al miner (difesa in profondita' sulle tx prelevate
 *  dal pool) e al client (autocontrollo prima del submit).
 * ============================================================ */

/* Ritorna SUCCESS se 's' rispetta il formato
 * "^[A-Za-z0-9]+ pays [A-Za-z0-9]+ [1-9][0-9]* coins$", altrimenti
 * INVALID_TRANSACTION. */
int txIsValid(const char *s);

#endif /* TX_H */
