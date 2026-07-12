#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include "../shared_state.h"
#include "../utils/errors.h"
#include "../encoding/crypto.h"
#include "chain.h"
#include "csv_manager.h"
#include "../encoding/encoding.h"

// Funzione per salvare la blockchain sul file CSV
int saveBlockchainCsv(const SharedState *ss, const char *filename) {
    FILE *file = fopen(filename, "w");

    if (!file) return IO_ERROR;

    fprintf(file, "index, timestamp, prev_hash, merkle_root, nonce, transactions\n");

    uint64_t height = ss->height;

    for (uint64_t i = 0; i < height; i++) {

        /*
         * In questociclo prendiamo le varie linee del fiel CSV e le dividiamo per i 5 parametri
         * richiesti index, timestamp, previous_hash, merkle_root, nonce. Dopo i 5 parametri
         * mettiamo le transizione hashate separate da :: come richiesto.
         */
        const Block current = ss->chain[i];

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
int loadCsv(const char *filename, SharedState *ss) {
    FILE *file = fopen(filename, "r");

    if (!file) return IO_ERROR;

    char line[4096];

    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return IO_ERROR;
    }

    ss->height = 0;

    while (fgets(line, sizeof(line), file)) {
        Block tmp;
        memset(&tmp, 0, sizeof(Block));

        // Rimuoviamo la linea finale
        line[strcspn(line, "\r\n")] = 0;

        char indexStr[32], timestampStr[32], nonceStr[32];
        char transactionsStr[2048] = {0};

        // Controllo per vedere se ci sono tutti i parametri senza spazi tramite Regex
        if (sscanf(line, "%[^,],%[^,],%[^,],%[^,],%[^,],%[^\n]", indexStr, timestampStr,
                   tmp.prev_hash, tmp.merkle_root, nonceStr, transactionsStr) < 5) {
            fclose(file);
            return PARSE_ERROR;
        }

        hexToU64(indexStr, &tmp.index);
        hexToU64(timestampStr, &tmp.timestamp);
        hexToU64(nonceStr, &tmp.nonce);

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

        // Accodiamo il singolo blocco parsato alla shared memory
        if (ss->height > 0) {
            if (validateBlock(&ss->chain[ss->height - 1], &tmp) != SUCCESS) {
                fclose(file);
                return CHAIN_MISMATCH;
            }
        }

        ss->chain[ss->height++] = tmp;
    }
    fclose(file);
    return SUCCESS;
}