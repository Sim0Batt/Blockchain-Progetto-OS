#include <stdio.h>

#include "utils/errors.h"
#include "utils/encoding.h"

int main(int argc, char *argv[]) {

    char out[17];

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

    return 0;
}