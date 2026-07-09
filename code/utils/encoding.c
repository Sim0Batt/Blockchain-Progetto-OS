#include <stdio.h>
#include "encoding.h"
#include <stddef.h>
#include <ctype.h>
#include <stdlib.h>

#include "errors.h"


/*
 * Queste funzioni servono al fine del salvataggio dei dati sul CSV, permettono la creazione del HEX e Bytes che andranno passati alla funzione di SHA256 per
 * la scrittura poi sul file CSV, che sarà: "index, timestamp, prev_hash, merkle_root, nonce, transactions"
 */

int u64ToHex(const uint64_t value, char *out, size_t size) {
    if (out == NULL || size < HEX_U64_BUF_SIZE) return PARSE_ERROR;

    // Questa funzione ritorna un buffer encoded a 16 cifre di HEX (%016llx)
    int hexedString = snprintf(out, size, "%016llx", (unsigned long long) value);

    // Controlli vari nel ritorno delle variabili
    if (hexedString < 0) return IO_ERROR;
    if (hexedString != HEX_U64_SIZE) return PARSE_ERROR;

    return SUCCESS;
}

int hexToU64(const char *hex, uint64_t *out) {
    if (out == NULL || hex == NULL) return PARSE_ERROR;

    // Controllo che la size sia giusta per l'Hex
    size_t hexSize = strlen(hex);
    if (hexSize != HEX_U64_SIZE) return PARSE_ERROR;

    unsigned long long value = 0;

    // Faccio il percorso inverso e riconverto da hex a U64
    if (sscanf(hex, "%16llx", &value) != 1) return PARSE_ERROR;

    // Setto il valore su out passato alla funzione
    *out = value;
    return SUCCESS;
}

int bytesToHex(const unsigned char *bytes, size_t sizeIn, char *out, size_t sizeOut) {
    if (bytes == NULL || out == NULL) return PARSE_ERROR;

    /*
     * Controlliamo che il biffer in output sia abbastanza grande per contenere l'HEX
     * Per farlo controlliamo che sia sizeIn*2 +1, il doppio della grandezza del buffer (vedi dopo perché) più un
     * carattere per l'end of line \0
     */

    if (sizeOut < (sizeIn * 2 + 1)) return PARSE_ERROR;

    /*
     * Per ogni byte in input scrivo 2 cifre in HEX minuscole (%02x) e converto l'intera stringa.
     * Il risultato totale avrà quindi grandezza sizeIn*2 (per questo viene fatto il controllo alla riga superiore)
     */

    for (size_t i = 0; i < sizeIn; i++) {
        snprintf(out + (i * 2), sizeOut - (i * 2), "%02x", bytes[i]);
    }

    // Alla fine della stringa mettiamo l'end of line \0
    out[sizeIn * 2] = '\0';

    return SUCCESS;
}

int hexToBytes(const unsigned char *hex, unsigned char *out, size_t size) {
    if (hex == NULL || out == NULL) return PARSE_ERROR;

    /*
     * Il controllo al contrario di prima dobbiamo capire se il buffer in entrata ha la grandezza giusta.
     * Dato che ora convertiamo da Hex a Byte, per ogni 2 Hex ci sarà un solo byte quindi la grandezza del buffer dovrà
     * essere la metà della lunghezza del Hex.
     */
    if (strlen(hex) != size * 2) return PARSE_ERROR;

    for (size_t i = 0; i < size; i++) {
        unsigned int byte = 0;

        // Qui avviene la conversione da Hex a Byte, per ogni 2 Hex troviamo un byte secondo il metodo di prima (%02x)
        if (sscanf(hex + (i * 2), "%02x", &byte) != 1) return PARSE_ERROR;

        /*
         * Qui converto in char perché byte è un unsigned int per convenzione di sscanf, ma alla fine della funzione a
         * noi interessa il carattere.
         */
        out[i] = (unsigned char) byte;
    }

    return SUCCESS;
}
