#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <ctype.h>

#include "../shared_state.h"
#include "../utils/errors.h"
#include "../encoding/crypto.h"
#include "chain.h"
#include "csv_manager.h"
#include "../encoding/encoding.h"

// Vero se 's' e' fatta di esattamente 'expected' cifre esadecimali.
static int isHexString(const char *s, size_t expected) {
    if (s == NULL || strlen(s) != expected) {
        return 0;
    }
    for (size_t i = 0; i < expected; i++) {
        if (!isxdigit((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

// Funzione per salvare la blockchain sul file CSV
int saveBlockchainCsv(const Blockchain *chain, const char *filename) {
    FILE *file = fopen(filename, "w");

    if (!file) return IO_ERROR;

    // Intestazione come da specifica, senza spazi dopo le virgole
    fprintf(file, "index,timestamp,prev_hash,merkle_root,nonce,transactions\n");

    uint64_t height = chain->height;

    for (uint64_t i = 0; i < height; i++) {

        /*
         * In questociclo prendiamo le varie linee del fiel CSV e le dividiamo per i 5 parametri
         * richiesti index, timestamp, previous_hash, merkle_root, nonce. Dopo i 5 parametri
         * mettiamo le transizione hashate separate da :: come richiesto.
         */
        const Block current = chain->blocks[i];

        char indexHex[HEX_U64_BUF_SIZE];
        char nonceHex[HEX_U64_BUF_SIZE];
        char timestampHex[HEX_U64_BUF_SIZE];

        u64ToHex(current.index, indexHex, sizeof(indexHex));
        u64ToHex(current.nonce, nonceHex, sizeof(nonceHex));
        u64ToHex(current.timestamp, timestampHex, sizeof(timestampHex));

        fprintf(file, "%s,%s,%s,%s,%s,", indexHex, timestampHex, current.prev_hash,
                current.merkle_root, nonceHex);

        if (current.tx_count == 0) {
            fprintf(file, "\"\"\n");
        } else {
            fprintf(file, "\"");
            for (uint64_t j = 0; j < current.tx_count; j++) {
                fprintf(file, "%s", current.tx[j].text);
                if (j != current.tx_count - 1) fprintf(file, "::");
            }
            fprintf(file, "\"\n");
        }
    }

    fclose(file);
    return SUCCESS;
}

// Funzione per caricare in shared memory il file CSV con i vari blocchi e le relative transazioni
int loadCsv(const char *filename, Blockchain *chain) {
    FILE *file = fopen(filename, "r");

    if (!file) return IO_ERROR;

    char line[4096];

    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return IO_ERROR;
    }

    chain->height = 0;

    while (fgets(line, sizeof(line), file)) {
        Block tmp;
        memset(&tmp, 0, sizeof(Block));

        // Rimuoviamo la linea finale
        line[strcspn(line, "\r\n")] = 0;

        char indexStr[32], timestampStr[32], nonceStr[32];
        char transactionsStr[2048] = {0};

        // I limiti di larghezza sono obbligatori: %[^,] senza limite copia fino
        // alla virgola successiva e su un campo malformato sfonda il buffer.
        if (sscanf(line, "%31[^,],%31[^,],%64[^,],%64[^,],%31[^,],%2047[^\n]",
                   indexStr, timestampStr, tmp.prev_hash, tmp.merkle_root,
                   nonceStr, transactionsStr) < 5) {
            fclose(file);
            return PARSE_ERROR;
        }

        // I campi numerici sono hex a 16 cifre: li validiamo prima di convertirli
        if (!isHexString(indexStr, HEX_U64_SIZE) ||
            !isHexString(timestampStr, HEX_U64_SIZE) ||
            !isHexString(nonceStr, HEX_U64_SIZE)) {
            fclose(file);
            return PARSE_ERROR;
        }

        // Senza controllare il ritorno, un campo invalido restava a 0 dal memset
        // e il CSV corrotto passava per buono
        if (hexToU64(indexStr, &tmp.index) != SUCCESS ||
            hexToU64(timestampStr, &tmp.timestamp) != SUCCESS ||
            hexToU64(nonceStr, &tmp.nonce) != SUCCESS) {
            fclose(file);
            return PARSE_ERROR;
        }

        // Su questi due si regge la verifica della catena, devono essere esatti
        if (!isHexString(tmp.prev_hash, HASH_HEX_LEN) ||
            !isHexString(tmp.merkle_root, HASH_HEX_LEN)) {
            fclose(file);
            return PARSE_ERROR;
        }

        char *transactionPointer = transactionsStr;
        size_t transactionsLength = strlen(transactionPointer);

        // Qui parte il parsing delle transazioni
        if (transactionsLength > 0) {
            // Controlli e rimozione delle virgolette per garantire il parsing corretto dal testo
            if (transactionPointer[0] == '"') {
                transactionPointer++;
                transactionsLength--;
            }
            if (transactionPointer[transactionsLength - 1] == '"') {
                transactionPointer[transactionsLength - 1] = '\0';
            }

            char *token = strstr(transactionPointer, "::");
            tmp.tx_count = 0; // Isoliamo al transazione
            while (token != NULL && tmp.tx_count < MAX_TX_PER_BLOCK) {
                *token = '\0';
                strncpy(tmp.tx[tmp.tx_count].text, transactionPointer, TX_MAX_LEN);
                tmp.tx_count++;
                transactionPointer = token + 2;
                // strstr cerca la prima occurrency dal puntatore al primo "::"
                token = strstr(transactionPointer, "::");
            }

            // Aggiungiamo l'ultima transazione se c'è
            if (strlen(transactionPointer) > 0 && tmp.tx_count < MAX_TX_PER_BLOCK) {
                strncpy(tmp.tx[tmp.tx_count].text, transactionPointer, TX_MAX_LEN);
                tmp.tx_count++;
            }
        }

        // Accodiamo il singolo blocco parsato alla chain
        if (chain->height > 0) {
            if (validateBlock(&chain->blocks[chain->height - 1], &tmp) != SUCCESS) {
                fclose(file);
                return CHAIN_MISMATCH;
            }
        }

        chain->blocks[chain->height++] = tmp;
    }
    fclose(file);
    return SUCCESS;
}