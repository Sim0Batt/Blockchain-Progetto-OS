#include <stdio.h>

#include "utils/errors.h"
#include "encoding/encoding.h"
#include "encoding/crypto.h"

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
    rc = hexToBytes(exitBuffer, bytes, sizeof(bytes));
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
    printf("Final Block Hash: %s\n", blockHashOut);



    return 0;
}