#include "node.h"
#include "ipc.h"
#include "utils/errors.h"
#include "encoding/crypto.h"
#include <string.h>
#include <unistd.h>

int nodeHandleBlock(SharedState *st, uint32_t nodeId, Blockchain *chain, const Block *blk, FILE *log) {
    uint64_t before = chain->height;
    int rc = appendChainBlock(chain, blk);
    if (rc == SUCCESS) {
        /* pubblica la propria testa: il miner attaccato a questo node la legge */
        char topHash[HASH_BUF_SIZE];
        calculateBlockHash(&chain->blocks[chain->height - 1], topHash);
        nodePublishHead(st, nodeId, chain->height, topHash);
        if (log) fprintf(log, "node %u: appended index=%llu (height %llu->%llu), propagating\n",
                         nodeId, (unsigned long long)blk->index,
                         (unsigned long long)before, (unsigned long long)chain->height);
        /* blocco NUOVO -> propaga ai peer, escludendo se stessi */
        inboxBroadcast(st, blk, (int)nodeId);
    } else {
        if (log) fprintf(log, "node %u: rejected index=%llu (rc=%d), not propagating\n",
                         nodeId, (unsigned long long)blk->index, rc);
    }
    return rc;
}

int runNode(SharedState *st, uint32_t nodeId) {
    Blockchain chain;
    memset(&chain, 0, sizeof(chain));
    char logname[64];
    snprintf(logname, sizeof(logname), "node-%d.log", (int)getpid());
    FILE *log = fopen(logname, "w");
    if (!log) return IO_ERROR;
    while (st->running) {
        Block blk;
        if (inboxGet(st, nodeId, &blk) != SUCCESS) continue;
        nodeHandleBlock(st, nodeId, &chain, &blk, log);
        fflush(log);
    }
    fclose(log);
    return SUCCESS;
}
