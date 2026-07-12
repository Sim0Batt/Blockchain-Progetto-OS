#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <openssl/sha.h>

#include "crypto.h"
#include "encoding.h"



void calculateSha256(const char *input, char *output) {
    unsigned char hash[SHA256_DIGEST_LENGTH];

    SHA256((const unsigned char *)input, strlen(input), hash);

    bytesToHex(hash, SHA256_DIGEST_LENGTH, output, HASH_BUF_SIZE);

}


// Funzione per calcolare la radice dell'albero Merkle
/*
 * Il Merkle tree è un albero che identifica un blocco contenente più transazioni hashate.
 * Ogni foglia rappresenta l'hash SHA256 della singola delle singole transazioni.
 * L'albero ha un totale di nodi dispari, perché alla fine in cima c'è la Merkle Root, il nodo radice
 * che identifica il blocco univocamente, il quale verrà salvato nella entry del CSV.
 * Se il numero di transizione è dispari in un blocco viene aggiunto un nodo hash vuoto di padding per
 * permettere la creazione dell'albero.
 */

void calculateMerkleRoot(const char *transaction, char *merkleRoot) {
    char emptyBuffer[HASH_BUF_SIZE];
    calculateSha256("", emptyBuffer);


    // Controlliamo se la transazione è vuota, questo significa che la siamo già alla radice dell'albero
    if (transaction == NULL || strlen(transaction) == 0) {
        // Se vuota valorizziamo la merkel root vuota perché vuol dire che la transaction stessa è la root
        strcpy(merkleRoot, emptyBuffer);
        return;
    }

    // Matrice temporanea per salvare i vari hash mentre scorriamo l'albero
    char hashes[MAX_TX_PER_BLOCK * 2][HASH_BUF_SIZE];
    int counter = 0;

    const char *start = transaction; // Puntatore all'inizio dell'albero (transizione corrente)
    const char *end; // Puntatore che andrà a salvare la fine della transizione corrente
    const char *delim = "::"; // Delimitatore scelto per separare da consegna


    /*
     * Separiamo la stringa in ari pezzi ogni volta che troviamo un :: e ne calcoliamo lo SHA256
     * per poi salvarlo nella posizione appropriata nella matrice temporanea
     */
    while ((end = strstr(start, delim)) != NULL) {
        char tx[TX_MAX_LEN] = {0};
        strncpy(tx, start, end - start);
        calculateSha256(tx, hashes[counter++]);
        start = end + 2;
    }

    if (*start != '\0') {
        calculateSha256(start, hashes[counter++]);
    }


    /*
     * Qui comincia la vera e propria "scalata" dell'albero Merkle, cominciamo a salire l'albero a due a due
     * fino ad arrivare alla root.
     */
    while (counter > 1) {

        // Creazione del blocco di padding
        if (counter % 2 != 0) {
            strncpy(hashes[counter++], emptyBuffer, HASH_BUF_SIZE);
        }

        int nextCount = 0;
        for (int i = 0; i < counter; i += 2) {
            char combined[HASH_BUF_SIZE * 2];

            // Concateniamo i vari hash per arrivare alla fine
            snprintf(combined, HASH_BUF_SIZE * 2, "%s%s", hashes[i], hashes[i + 1]);

            calculateSha256(combined, hashes[nextCount++]);
        }
        counter = nextCount;
    }

    // L'ultimo elemento rimasto dalla creazione della matrice sarà l'indice 0, quindi la radice dell'albero markle
    // Dato che andiamo dal basso verso l'alto sarà per forza l'ultimo
    strncpy(merkleRoot, hashes[0], HASH_BUF_SIZE);

}


// Funzione per calcolare l'hash dell'intero blocco di transazioni
void calculateBlockHash(const Block *block, char *hash) {
    char buffer[2048]; // Creiamo un buffer largo per contenere tutta la intestazione concatenata, di cui non sappiamo la grandezza a priori
    // Inizializziamo tutte le varie variabili che andranno salvate hashate nel CSV
    char indexHex[HEX_U64_BUF_SIZE];
    char timestampHex[HEX_U64_BUF_SIZE];
    char nonceHex[HEX_U64_BUF_SIZE];

    u64ToHex(block->index, indexHex, sizeof(indexHex));
    u64ToHex(block->timestamp, timestampHex, sizeof(timestampHex));
    u64ToHex(block->nonce, nonceHex, sizeof(nonceHex));

    snprintf(buffer, sizeof(buffer), "%s%s%s%s%s", indexHex, timestampHex, block->prev_hash, block->merkle_root, nonceHex);

    calculateSha256(buffer, hash);
}

