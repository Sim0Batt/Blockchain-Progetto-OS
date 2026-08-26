#include "node.h"
#include "ipc.h"
#include "utils/errors.h"
#include "encoding/crypto.h"
#include <string.h>
#include "utils/csv_manager.h"
#include <unistd.h>

void handleCliCommand(Blockchain *chain, const Block *cmd) {
    if (cmd->nonce == 1) {
        if (saveBlockchainCsv(chain, cmd->tx[0].text) == SUCCESS) {
            printf("\n=> [Node 0] Blockchain saved '%s'\n> ", cmd->tx[0].text);
        } else {
            printf("\n=> [Node 0] Error while saving blockchain.\n> ");
        }
    }
    else if (cmd->nonce == 2) {
        printf("\n--- BLOCKCHAIN STATE (Height: %llu) ---\n", (unsigned long long)chain->height);
        for (uint64_t i = 0; i < chain->height; i++) {
            printf("[Block %llu] Prev Hash: %.16s... | Transactions: %u\n",
                   (unsigned long long)chain->blocks[i].index, chain->blocks[i].prev_hash, chain->blocks[i].tx_count);
        }
        printf("> ");
    }
    else if (cmd->nonce == 3) {
        uint64_t start_idx = cmd->timestamp;
        if (start_idx >= chain->height) {
            printf("\n=> [Node 0] Index %llu not found.\n> ", (unsigned long long)start_idx);
        } else {
            printf("\n--- BLOCKCHAIN (index %llu) ---\n", (unsigned long long)start_idx);
            for (uint64_t i = start_idx; i < chain->height; i++) {
                printf("[Block %llu] Transactions: %u\n", (unsigned long long)chain->blocks[i].index, chain->blocks[i].tx_count);
            }
            printf("> ");
        }
    }
    else if (cmd->nonce == 4) {
        int found = -1;
        for (uint64_t i = 0; i < chain->height; i++) {
            char current_hash[HASH_BUF_SIZE];
            calculateBlockHash(&chain->blocks[i], current_hash);
            if (strcmp(current_hash, cmd->tx[0].text) == 0) {
                found = i;
                break;
            }
        }
        if (found == -1) printf("\n=> [Node 0] Hash not found.\n> ");
        else {
            printf("\n--- BLOCKCHAIN (from hash to index %d) ---\n", found);
            for (uint64_t i = found; i < chain->height; i++) {
                printf("[Block %llu] Transactions: %u\n", (unsigned long long)chain->blocks[i].index, chain->blocks[i].tx_count);
            }
            printf("> ");
        }
    }
    else if (cmd->nonce == 5 || cmd->nonce == 6) {
        int target_idx = -1;
        if (cmd->nonce == 5) {
            if (cmd->timestamp < chain->height) target_idx = cmd->timestamp;
        } else {
            for (uint64_t i = 0; i < chain->height; i++) {
                char current_hash[HASH_BUF_SIZE];
                calculateBlockHash(&chain->blocks[i], current_hash);
                if (strcmp(current_hash, cmd->tx[0].text) == 0) { target_idx = i; break; }
            }
        }

        if (target_idx == -1) printf("\n=> [Node 0] Block not found.\n> ");
        else {
            Block *b = &chain->blocks[target_idx];
            printf("\n--- BLOCK %llu ---\nPrev Hash: %s\nMerkle: %s\nTransactions (%u):\n",
                   (unsigned long long)b->index, b->prev_hash, b->merkle_root, b->tx_count);
            for(uint32_t j = 0; j < b->tx_count; j++) printf("  - %s\n", b->tx[j].text);
            printf("> ");
        }
    }
    fflush(stdout);
}

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

int runNode(SharedState *st, uint32_t nodeId, const char *initialState) {
    Blockchain chain;
    memset(&chain, 0, sizeof(chain));
    char logname[64];
    snprintf(logname, sizeof(logname), "node-%d.log", (int)getpid());
    FILE *log = fopen(logname, "w");
    if (!log) return IO_ERROR;

    // Caricamento dello stato iniziale
    if (initialState != NULL) {
        if (loadCsv(initialState, &chain) != SUCCESS) {
            fprintf(stderr, "Error: no initial state found\n");
            fclose(log);
            return PARSE_ERROR;
        }

        if (chain.height > 0) {
            char topHash[HASH_BUF_SIZE];
            calculateBlockHash(&chain.blocks[chain.height - 1], topHash);
            nodePublishHead(st, nodeId, chain.height, topHash);
        }
        fprintf(log, "note %u: loaded initial state, height: %llu\n", nodeId, (unsigned long long)chain.height);
    }


    while (st->running) {
        Block blk;
        if (inboxGet(st, nodeId, &blk) != SUCCESS) continue;

        if (blk.index == UINT64_MAX) {
            if (nodeId == 0) {
                handleCliCommand(&chain, &blk);
            }
            continue;
        }

        nodeHandleBlock(st, nodeId, &chain, &blk, log);
        fflush(log);
    }
    fclose(log);
    return SUCCESS;
}
