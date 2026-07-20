#include <stdio.h>

#include "utils/errors.h"
#include "encoding/encoding.h"
#include "encoding/crypto.h"
#include "utils/csv_manager.h"
#include "shared_state.h"
#include "utils/tx.h"
#include "miner.h"
#include "client.h"

#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

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
    printf("U64: %llu\n", (unsigned long long)valueOut);

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
    // Vettore di verifica noto (fornito dal gruppo per la merkle root del genesis)
    const char *expectedGenesisMerkle = "b815a93dd7f59058a27e63558ba5aa6445d851f316070ec13db673d5ab38e0cc";
    if (strcmp(merkleOut, expectedGenesisMerkle) == 0) {
        printf("Merkle Root Genesis: %s\n", codesToString(SUCCESS));
    } else {
        // NON e' compito nostro (workstream E/F) fixare calculateMerkleRoot: e' il
        // modulo crypto di Simon (workstream A). Se questo test fallisce e il valore
        // ottenuto e' "4e178110...", e' il bug noto del padding a singola tx: segnalarlo
        // a Simon, non toccare encoding/crypto.c da qui.
        printf("Merkle Root Genesis: MISMATCH (atteso %s, ottenuto %s) -> segnalare a Simon (workstream A)\n",
               expectedGenesisMerkle, merkleOut);
    }
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

    // 1. Creiamo una chain fittizia ALLOCANDOLA SULL'HEAP
    Blockchain *originalChain = malloc(sizeof(Blockchain));
    if (originalChain == NULL) {
        printf("Errore: memoria Heap insufficiente per allocare originalChain!\n");
        return 1;
    }
    memset(originalChain, 0, sizeof(Blockchain));

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
    originalChain->blocks[0] = b0;
    originalChain->height = 1;

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
    originalChain->blocks[1] = b1;
    originalChain->height = 2;

    // 2. Salviamo lo stato sul file CSV
    const char *test_csv_file = "test_state.csv";
    int save_rc = saveBlockchainCsv(originalChain, test_csv_file);
    printf("Salvataggio CSV (%s): %s\n", test_csv_file, codesToString(save_rc));

    if (save_rc == SUCCESS) {
        // 3. Creiamo una nuova chain vuota ALLOCANDOLA SULL'HEAP
        Blockchain *loadedChain = malloc(sizeof(Blockchain));
        if (loadedChain == NULL) {
            printf("Errore: memoria Heap insufficiente per allocare loadedChain!\n");
            free(originalChain);
            return 1;
        }
        memset(loadedChain, 0, sizeof(Blockchain));

        // 4. Carichiamo il file CSV
        int load_rc = loadCsv(test_csv_file, loadedChain);
        printf("Caricamento CSV (%s): %s\n\n", test_csv_file, codesToString(load_rc));

        if (load_rc == SUCCESS) {
            printf("--- RISULTATO DEL CARICAMENTO ---\n");
            printf("Altezza originale: %llu | Altezza caricata: %llu\n",
                   (unsigned long long)originalChain->height, (unsigned long long)loadedChain->height);

            // 5. Verifichiamo i dati letti dal Blocco 1
            if (loadedChain->height >= 2) {
                Block loaded_b1 = loadedChain->blocks[1];
                printf("\nDati del Blocco 1 caricato:\n");
                printf(" - Index: %llu\n", (unsigned long long)loaded_b1.index);
                printf(" - Prev Hash: %s\n", loaded_b1.prev_hash);
                printf(" - Numero di transazioni: %u\n", loaded_b1.tx_count);
                for (uint32_t i = 0; i < loaded_b1.tx_count; i++) {
                    printf("   [Tx %u]: %s\n", i, loaded_b1.tx[i].text);
                }
            }
        }
        free(loadedChain); // Liberiamo la memoria Heap
    }

    free(originalChain); // Liberiamo la memoria Heap

    /* ================= TEST CLIENT (workstream F) ================= */

    printf("--- TEST CLIENT: GENERAZIONE TX VALIDE (100 tx) ---\n");
    srandom((unsigned int)time(NULL) ^ (unsigned int)getpid());
    int invalidCount = 0;
    for (int i = 0; i < 100; i++) {
        Transaction tx;
        int gen_rc = clientGenerateTransaction(&tx);
        if (gen_rc != SUCCESS || txIsValid(tx.text) != SUCCESS) {
            invalidCount++;
            printf("  tx #%d NON valida: '%s' (%s)\n", i, tx.text, codesToString(gen_rc));
        }
    }
    printf("Transazioni non valide su 100: %d -> %s\n",
           invalidCount, invalidCount == 0 ? codesToString(SUCCESS) : codesToString(INVALID_TRANSACTION));
    printf("\n");

    printf("--- TEST CLIENT: SEED PER-PROCESSO (sequenze diverse) ---\n");
    // Due seed diversi simulano due client con pid diversi: e' lo scenario
    // che srandom(time(NULL) ^ getpid()) in runClient() garantisce.
    Transaction seqA[5], seqB[5];
    int seqRc = SUCCESS;
    srandom(1111);
    for (int i = 0; i < 5; i++) {
        int genRc = clientGenerateTransaction(&seqA[i]);
        if (genRc != SUCCESS) seqRc = genRc;
    }
    srandom(2222);
    for (int i = 0; i < 5; i++) {
        int genRc = clientGenerateTransaction(&seqB[i]);
        if (genRc != SUCCESS) seqRc = genRc;
    }
    if (seqRc != SUCCESS) {
        printf("Generazione sequenze FALLITA: %s\n", codesToString(seqRc));
    }

    int sequencesDiffer = 0;
    for (int i = 0; i < 5; i++) {
        if (strcmp(seqA[i].text, seqB[i].text) != 0) {
            sequencesDiffer = 1;
            break;
        }
    }
    printf("Sequenza A[0]: %s\n", seqA[0].text);
    printf("Sequenza B[0]: %s\n", seqB[0].text);
    printf("Sequenze diverse con seed diversi: %s\n",
           sequencesDiffer ? codesToString(SUCCESS) : "FALLITO (sequenze identiche)");
    printf("\n");

    /* ================= TEST MINER (workstream E) ================= */

    printf("--- TEST MINER: MINING CON DIFFICULTY PICCOLA ---\n");
    SharedState *minerTestSt = malloc(sizeof(SharedState));
    if (minerTestSt == NULL) {
        printf("Errore: memoria insufficiente per il test del miner\n");
    } else {
        memset(minerTestSt, 0, sizeof(SharedState));
        // difficulty=1 rende il test deterministico (random() % 1 == 0
        // sempre): mina al primo tentativo, senza flakiness.
        minerTestSt->difficulty = 1;
        minerTestSt->running = 1;

        Block candidate;
        memset(&candidate, 0, sizeof(Block));
        candidate.index = 0;

        srandom((unsigned int)time(NULL) ^ (unsigned int)getpid());
        int result = minerMineCandidate(minerTestSt, &candidate, 0);
        printf("Esito mining (difficulty=1, cima non avanzata): %s\n",
               result == MINER_MINED ? "MINER_MINED" : codesToString(result));
        printf("Nonce dopo il mining: %llu\n", (unsigned long long)candidate.nonce);
        free(minerTestSt);
    }
    printf("\n");

    printf("--- TEST MINER: ABORT SU SHUTDOWN (fork + shared mmap) ---\n");
    // L'altro motivo di abort, la cima avanzata, non e' testabile finche'
    // manca il canale node -> miner: vedi la nota architetturale in miner.h.
    // Serve memoria realmente condivisa: su memoria normale il figlio
    // scriverebbe su una copia copy-on-write, invisibile al padre.
    SharedState *abortSt = mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE,
                                 MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (abortSt == MAP_FAILED) {
        printf("Errore: mmap fallita per il test di abort\n");
    } else {
        memset(abortSt, 0, sizeof(SharedState));
        abortSt->difficulty = 1000000; // alta apposta: non deve minare per caso
        abortSt->running = 1;

        pid_t pid = fork();
        if (pid == 0) {
            // Richiede lo shutdown a meta' mining, come farebbe la CLI
            sleep(2);
            abortSt->running = 0;
            _exit(0);
        } else if (pid > 0) {
            Block candidate2;
            memset(&candidate2, 0, sizeof(Block));
            candidate2.index = 0;

            srandom((unsigned int)time(NULL) ^ (unsigned int)getpid());
            int abortResult = minerMineCandidate(abortSt, &candidate2, 0);
            waitpid(pid, NULL, 0);

            printf("Esito mining con shutdown a meta' mining (atteso abort): %s\n",
                   abortResult == MINER_ABORTED ? codesToString(SUCCESS) : "FALLITO (non ha abortito)");
        } else {
            printf("Errore: fork fallita per il test di abort\n");
        }
        munmap(abortSt, sizeof(SharedState));
    }
    printf("\n");

    return 0;
}