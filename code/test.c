#include <stdio.h>

#include "utils/errors.h"
#include "encoding/encoding.h"
#include "encoding/crypto.h"
#include "utils/csv_manager.h"
#include "shared_state.h"

#include <stdlib.h>

int main(int argc, char *argv[]) {

    char out[17];

    printf("--- TESTS ENCODING ---\n");
    // U64 to Hex
    int rc = u64ToHex(4919, out, sizeof(out));
    printf("U64 to Hex Exit Value: %s\n", codesToString(rc));
    printf("Hex: %s\n", out);

    // Hex to U64
    uint64_t valueOut;
    rc = hexToU64("0000000000001337", &valueOut);
    printf("Hex to U64 Exit Value: %s\n", codesToString(rc));
    printf("U64: %llu\n", valueOut);

    // Bytes to Hex
    unsigned char testBytes[4] = {'t', 'e', 's', 't'};
    char exitBuffer[9]; // 4*2 +1
    rc = bytesToHex(testBytes, sizeof(testBytes), exitBuffer, sizeof(exitBuffer));
    printf("Bytes to Hex Exit Value: %s\n", codesToString(rc));
    printf("Hex: %s\n", exitBuffer);

    // Hex to Bytes
    unsigned char bytes[4];
    rc = hexToBytes((const unsigned char *)exitBuffer, bytes, sizeof(bytes));
    printf("Hex to Bytes Exit Value: %s\n", codesToString(rc));
    printf("Reconstructed Bytes Text:");
    for (int i = 0; i < 4; i++)
        printf("%c", bytes[i]);



    char shaOut[HASH_BUF_SIZE];
    const char *testString = "test";
    calculateSha256(testString, shaOut);

    printf("\n\n--- TEST SHA256 ---\n");
    printf("Input: '%s'\n", testString);
    printf("SHA256 Output: %s\n", shaOut);
    if (strcmp(shaOut, "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08") == 0) printf("SHA256 Output is: %s\n", codesToString(SUCCESS));
    printf("\n");

    char merkleOut[HASH_BUF_SIZE];
    const char *genesisTx = "Genesis block";
    calculateMerkleRoot(genesisTx, merkleOut);
printf("--- TEST MERKLE ROOT ---\n");
    printf("Input transazioni: '%s'\n", genesisTx);
    printf("Merkle Root calcolata: %s\n", merkleOut);
    printf("\n");

    Block testBlock;
    memset(&testBlock, 0, sizeof(Block)); // Inizializza la memoria della struct a zero

    // Popoliamo i 5 campi dell'header utilizzati per l'hashing
    testBlock.index = 4919;
    testBlock.timestamp = 1620000000;
    testBlock.nonce = 1337;

    // Usiamo l'hash SHA256 vuoto come prev_hash per questo test
    calculateSha256("", testBlock.prev_hash);

    // Inseriamo la Merkle root calcolata al punto precedente
    strncpy(testBlock.merkle_root, merkleOut, HASH_BUF_SIZE);

    char blockHashOut[HASH_BUF_SIZE];
    calculateBlockHash(&testBlock, blockHashOut);

    printf("--- TEST BLOCK HASH ---\n");
    printf("Block Index: %llu\n", (unsigned long long)testBlock.index);
    printf("Block Timestamp: %llu\n", (unsigned long long)testBlock.timestamp);
    printf("Block Nonce: %llu\n", (unsigned long long)testBlock.nonce);
    printf("Block Prev Hash: %s\n", testBlock.prev_hash);
    printf("Block Merkle Root: %s\n", testBlock.merkle_root);
    printf("Final Block Hash: %s\n\n", blockHashOut);

    printf("--- TEST CSV I/O E CHAIN VALIDATION ---\n");

    // 1. Creiamo uno stato fittizio ALLOCANDOLO SULL'HEAP
    SharedState *original_ss = malloc(sizeof(SharedState));
    if (original_ss == NULL) {
        printf("Errore: memoria Heap insufficiente per allocare original_ss!\n");
        return 1;
    }
    memset(original_ss, 0, sizeof(SharedState));

    // -- Creazione del Blocco Genesis (Indice 0) --
    Block b0;
    memset(&b0, 0, sizeof(Block));
    b0.index = 0;
    b0.timestamp = 1700000000;
    b0.nonce = 1000;
    calculateSha256("", b0.prev_hash); // Padding per il genesis
    calculateMerkleRoot("Genesis block", b0.merkle_root);
    strcpy(b0.tx[0].text, "Genesis block");
    b0.tx_count = 1;
    original_ss->chain[0] = b0;
    original_ss->height = 1;

    // -- Creazione del Blocco 1 --
    Block b1;
    memset(&b1, 0, sizeof(Block));
    b1.index = 1;
    b1.timestamp = 1700000500;
    b1.nonce = 2000;
    calculateBlockHash(&b0, b1.prev_hash); // Il prev_hash DEVE essere l'hash del blocco 0
    calculateMerkleRoot("Alice pays Bob 10 coins::Charlie pays Dave 5 coins", b1.merkle_root);
    strcpy(b1.tx[0].text, "Alice pays Bob 10 coins");
    strcpy(b1.tx[1].text, "Charlie pays Dave 5 coins");
    b1.tx_count = 2;
    original_ss->chain[1] = b1;
    original_ss->height = 2;

    // 2. Salviamo lo stato sul file CSV
    const char *test_csv_file = "test_state.csv";
    int save_rc = saveBlockchainCsv(original_ss, test_csv_file);
    printf("Salvataggio CSV (%s): %s\n", test_csv_file, codesToString(save_rc));

    if (save_rc == SUCCESS) {
        // 3. Creiamo un nuovo stato vuoto ALLOCANDOLO SULL'HEAP
        SharedState *loaded_ss = malloc(sizeof(SharedState));
        if (loaded_ss == NULL) {
            printf("Errore: memoria Heap insufficiente per allocare loaded_ss!\n");
            free(original_ss);
            return 1;
        }
        memset(loaded_ss, 0, sizeof(SharedState));

        // 4. Carichiamo il file CSV
        int load_rc = loadCsv(test_csv_file, loaded_ss);
        printf("Caricamento CSV (%s): %s\n\n", test_csv_file, codesToString(load_rc));

        if (load_rc == SUCCESS) {
            printf("--- RISULTATO DEL CARICAMENTO ---\n");
            printf("Altezza originale: %llu | Altezza caricata: %llu\n",
                   (unsigned long long)original_ss->height, (unsigned long long)loaded_ss->height);

            // 5. Verifichiamo i dati letti dal Blocco 1
            if (loaded_ss->height >= 2) {
                Block loaded_b1 = loaded_ss->chain[1];
                printf("\nDati del Blocco 1 caricato:\n");
                printf(" - Index: %llu\n", (unsigned long long)loaded_b1.index);
                printf(" - Prev Hash: %s\n", loaded_b1.prev_hash);
                printf(" - Numero di transazioni: %u\n", loaded_b1.tx_count);
                for (uint32_t i = 0; i < loaded_b1.tx_count; i++) {
                    printf("   [Tx %u]: %s\n", i, loaded_b1.tx[i].text);
                }
            }
        }
        free(loaded_ss); // Liberiamo la memoria Heap
    }

    free(original_ss); // Liberiamo la memoria Heap

    return 0;
}