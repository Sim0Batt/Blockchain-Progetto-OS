#include "node.h"
#include "ipc.h"
#include "utils/errors.h"
#include "encoding/crypto.h"
#include <string.h>
#include <stdlib.h>
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

/* Catch-up: applica in ordine i blocchi gia' decisi partendo dalla propria
 * altezza. Cosi' un node che si e' perso un blocco recupera da solo, senza
 * dipendere dalla ri-propagazione dei peer che e' best-effort. */
int nodeApplyDecided(SharedState *st, uint32_t nodeId, Blockchain *chain, FILE *log) {
    int applied = 0;

    while (chain->height < MAX_CHAIN) {
        Block decided;
        if (consensusGet(st, chain->height, &decided) != SUCCESS) {
            break;                       /* prossimo indice non ancora deciso */
        }

        uint64_t before = chain->height;
        int rc = appendChainBlock(chain, &decided);
        if (rc != SUCCESS) {
            /* non dovrebbe capitare: meglio fermarsi che divergere */
            if (log) fprintf(log, "node %u: catch-up stopped at index=%llu (rc=%d)\n",
                             nodeId, (unsigned long long)before, rc);
            break;
        }

        char topHash[HASH_BUF_SIZE];
        calculateBlockHash(&chain->blocks[chain->height - 1], topHash);
        /* la testa pubblicata qui e' quella che legge il miner agganciato */
        nodePublishHead(st, nodeId, chain->height, topHash);
        if (log) fprintf(log, "node %u: appended index=%llu hash=%s (height %llu->%llu)\n",
                         nodeId, (unsigned long long)decided.index, topHash,
                         (unsigned long long)before, (unsigned long long)chain->height);
        applied++;
    }

    return applied;
}

int nodeHandleBlock(SharedState *st, uint32_t nodeId, Blockchain *chain, const Block *blk, FILE *log) {
    /* Prima l'arbitraggio: se un altro miner aveva gia' vinto quell'indice,
     * 'winner' e' il suo blocco. Applicando sempre il vincitore i node non
     * divergono per via dell'ordine di arrivo nelle inbox. */
    Block winner;
    int rc = consensusDecide(st, blk, &winner);
    if (rc != SUCCESS) {
        if (log) fprintf(log, "node %u: rejected index=%llu (rc=%d), not propagating\n",
                         nodeId, (unsigned long long)blk->index, rc);
        return rc;
    }

    /* la copia locale si aggiorna solo dal registro: un unico percorso di
     * append sia per i blocchi appena arrivati sia per quelli recuperati */
    int applied = nodeApplyDecided(st, nodeId, chain, log);

    if (applied > 0) {
        /* blocco nuovo per noi: propaga ai peer, escludendo se stessi */
        inboxBroadcast(st, &winner, (int)nodeId);
    } else if (log) {
        fprintf(log, "node %u: index=%llu already known, not propagating\n",
                nodeId, (unsigned long long)blk->index);
    }

    return SUCCESS;
}

int runNode(SharedState *st, uint32_t nodeId, const char *initialState) {
    Blockchain *chain = malloc(sizeof(Blockchain));
    if (chain == NULL) return MEMORY_ERROR;
    memset(chain, 0, sizeof(Blockchain));
    char logname[64];
    snprintf(logname, sizeof(logname), "node-%d.log", (int)getpid());
    FILE *log = fopen(logname, "w");
    if (!log) { free(chain); return IO_ERROR; }

    if (initialState != NULL) {
        if (loadCsv(initialState, chain) != SUCCESS) {
            fprintf(stderr, "Error: no initial state found\n");
            fclose(log);
            free(chain);
            return PARSE_ERROR;
        }

        if (chain->height > 0) {
            char topHash[HASH_BUF_SIZE];
            calculateBlockHash(&chain->blocks[chain->height - 1], topHash);
            nodePublishHead(st, nodeId, chain->height, topHash);
        }
        fprintf(log, "node %u: loaded initial state, height: %llu\n", nodeId, (unsigned long long)chain->height);
    }


    while (st->running) {
        /* catch-up a ogni giro, anche se nessun peer ci ha ripropagato niente */
        if (nodeApplyDecided(st, nodeId, chain, log) > 0) {
            fflush(log);
        }

        /* attesa con scadenza, non bloccante: cosi' il giro si ripete e
         * rileggiamo st->running anche a inbox ferma */
        Block blk;
        int rc = inboxTimedget(st, nodeId, &blk, 200);
        if (rc != SUCCESS) continue;   /* IPC_EMPTY: nessun blocco, si riprova */

        if (blk.index == UINT64_MAX) {
            if (nodeId == 0) {
                handleCliCommand(chain, &blk);
            }
            continue;
        }

        nodeHandleBlock(st, nodeId, chain, &blk, log);
        fflush(log);
    }
    fclose(log);
    free(chain);
    return SUCCESS;
}
