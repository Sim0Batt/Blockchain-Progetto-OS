#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

#include "miner.h"
#include "ipc.h"
#include "shared_state.h"
#include "utils/errors.h"
#include "encoding/crypto.h"

// Riga di log con timestamp, id del miner e pid.
static void minerLog(FILE *log, int minerId, const char *fmt, ...) {
    if (log == NULL) {
        return;
    }

    time_t now = time(NULL);
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmNow);

    fprintf(log, "[%s] [miner %d pid %d] ", ts, minerId, getpid());

    va_list args;
    va_start(args, fmt);
    vfprintf(log, fmt, args);
    va_end(args);

    fprintf(log, "\n");
    fflush(log); // il log resta leggibile anche se il processo viene ucciso
}

// I due motivi di abort insieme: shutdown, oppure cima avanzata sul node
// agganciato (minerShouldAbort da solo guarda solo la cima).
static int minerAbortRequested(SharedState *st, uint32_t nodeId, uint64_t builtOnIndex) {
    if (!st->running) {
        return 1;
    }
    return minerShouldAbort(st, nodeId, builtOnIndex);
}

/* Pausa dopo un errore transitorio: evita di riciclare il loop a CPU piena
 * senza rallentare lo shutdown. */
#define ERROR_RETRY_SECONDS 0.1

// nanosleep che riprende dal residuo se interrotto da un signal (EINTR).
static void sleepInterruptible(double seconds) {
    if (seconds <= 0.0) {
        return;
    }
    struct timespec req;
    req.tv_sec = (time_t)seconds;
    req.tv_nsec = (long)((seconds - (double)req.tv_sec) * 1e9);

    struct timespec rem;
    while (nanosleep(&req, &rem) == -1 && errno == EINTR) {
        req = rem;
    }
}

int minerBuildCandidate(SharedState *st, uint32_t nodeId, Block *candidate, uint64_t *builtOnIndex) {
    if (st == NULL || candidate == NULL || builtOnIndex == NULL) {
        return PARSE_ERROR;
    }

    memset(candidate, 0, sizeof(Block));

    // Cima del node 'nodeId': l'altezza e' l'indice del prossimo blocco,
    // l'hash in cima diventa il prev_hash del candidato.
    uint64_t height;
    char prevHash[HASH_BUF_SIZE];
    int rc = minerReadTip(st, nodeId, &height, prevHash);
    if (rc != SUCCESS) {
        return rc;
    }

    candidate->index = height;
    candidate->timestamp = (uint64_t)time(NULL);
    // Con chain vuota prevHash e' vuoto: stiamo costruendo il genesis, e il node
    // non valida il prev_hash dell'index 0 (appendChainBlock in utils/chain.c).
    strncpy(candidate->prev_hash, prevHash, HASH_BUF_SIZE - 1);
    candidate->nonce = 0;
    *builtOnIndex = height;

    // Drena le tx gia' presenti con la get non bloccante: txpoolGet ci
    // lascerebbe appesi, senza piu' controllare abort e shutdown. Se il pool e'
    // vuoto esce con tx_count 0 e decide runMiner.
    Transaction tx;
    while (candidate->tx_count < MAX_TX_PER_BLOCK) {
        int getrc = txpoolTryget(st, &tx);
        if (getrc == IPC_EMPTY) {
            break;
        }
        if (getrc != SUCCESS) {
            return getrc;
        }
        candidate->tx[candidate->tx_count++] = tx;
    }

    // calculateMerkleRoot() vuole le tx in un'unica stringa separata da "::".
    char joined[MAX_TX_PER_BLOCK * TX_MAX_LEN + MAX_TX_PER_BLOCK * 2 + 1] = {0};
    size_t used = 0;
    for (uint32_t i = 0; i < candidate->tx_count; i++) {
        int n = snprintf(joined + used, sizeof(joined) - used, "%s%s",
                          candidate->tx[i].text,
                          (i + 1 < candidate->tx_count) ? "::" : "");
        if (n < 0 || (size_t)n >= sizeof(joined) - used) {
            return MEMORY_ERROR; // improbabile con i limiti di shared_state.h
        }
        used += (size_t)n;
    }

    calculateMerkleRoot(joined, candidate->merkle_root);

    return SUCCESS;
}

int minerMineCandidate(SharedState *st, uint32_t nodeId, Block *candidate, uint64_t builtOnIndex) {
    if (st == NULL || candidate == NULL) {
        return PARSE_ERROR;
    }
    if (st->difficulty == 0) {
        return PARSE_ERROR; // random() % difficulty dividerebbe per zero
    }

    int minato = 0;
    while (!minato) {
        // Attesa spezzata in passi da 1s invece di un unico sleep(N): cosi' si
        // rilegge lo stato condiviso fra un passo e l'altro.
        int passi = 1 + (int)(random() % 5);
        for (int passo = 0; passo < passi; passo++) {
            sleep(1);
            if (minerAbortRequested(st, nodeId, builtOnIndex)) {
                return MINER_ABORTED;
            }
        }

        if ((uint32_t)(random() % st->difficulty) == 0) {
            minato = 1;
        }
        candidate->nonce++; // cosmetico: il proof-of-work e' simulato

        if (minerAbortRequested(st, nodeId, builtOnIndex)) {
            return MINER_ABORTED;
        }
    }

    return MINER_MINED;
}

int runMiner(SharedState *st, int minerId) {
    if (st == NULL) {
        return PARSE_ERROR;
    }

    // Seed per processo: i miner nascono da fork() quasi nello stesso istante,
    // senza lo XOR col pid produrrebbero sequenze identiche.
    srandom((unsigned int)(time(NULL) ^ getpid()));

    char logName[64];
    snprintf(logName, sizeof(logName), "miner-%d.log", getpid());
    FILE *log = fopen(logName, "w");
    if (log == NULL) {
        return IO_ERROR;
    }

    // Controlli una volta sola: num_nodes e' anche il divisore che sceglie il
    // nodeId, quindi lo zero va intercettato qui.
    if (st->difficulty == 0) {
        minerLog(log, minerId, "invalid difficulty (0): miner not starting");
        fclose(log);
        return PARSE_ERROR;
    }
    if (st->num_nodes == 0) {
        minerLog(log, minerId, "no active node (num_nodes=0): miner not starting");
        fclose(log);
        return PARSE_ERROR;
    }

    // Ogni miner si aggancia a un solo node, in modo stabile, e ne segue la
    // testa pubblicata per sapere la cima e quando abortire.
    uint32_t nodeId = (uint32_t)minerId % st->num_nodes;

    minerLog(log, minerId, "started, difficulty=%u, attached to node %u",
             st->difficulty, nodeId);

    while (st->running) {
        Block candidate;
        uint64_t builtOnIndex;

        int rc = minerBuildCandidate(st, nodeId, &candidate, &builtOnIndex);
        if (rc != SUCCESS) {
            minerLog(log, minerId, "error building candidate: %s", codesToString(rc));
            sleepInterruptible(ERROR_RETRY_SECONDS);
            continue;
        }
        // Niente blocchi vuoti: a pool vuoto si aspetta a passi da 100ms,
        // rileggendo st->running per non ritardare lo stop.
        if (candidate.tx_count == 0) {
            for (int passo = 0; passo < 10 && st->running; passo++) {
                sleepInterruptible(0.1);
            }
            continue;
        }

        minerLog(log, minerId, "candidate built: index=%llu tx=%u",
                 (unsigned long long)candidate.index, candidate.tx_count);

        minerLog(log, minerId, "starting mining on index=%llu", (unsigned long long)builtOnIndex);
        int result = minerMineCandidate(st, nodeId, &candidate, builtOnIndex);

        if (result == MINER_ABORTED) {
            // Shutdown: le tx si perdono, tanto il sistema si sta fermando.
            if (!st->running) {
                minerLog(log, minerId,
                         "shutdown during mining: discarding %u tx", candidate.tx_count);
                continue;
            }

            minerLog(log, minerId,
                     "abort: tip advanced, re-queuing %u tx into the pool",
                     candidate.tx_count);
            // Le tx del blocco abortito sono ancora valide, quindi tornano nel
            // pool. Il put a scadenza evita di restare appesi a pool pieno.
            for (uint32_t i = 0; i < candidate.tx_count && st->running; i++) {
                while (st->running && txpoolTimedput(st, &candidate.tx[i], 200) != SUCCESS) {
                    /* pool pieno: ritenta finche' non si libera o parte lo stop */
                }
            }
            continue;
        }

        if (result != MINER_MINED) {
            minerLog(log, minerId, "error during mining: %s", codesToString(result));
            sleepInterruptible(ERROR_RETRY_SECONDS);
            continue;
        }

        minerLog(log, minerId, "block mined: index=%llu nonce=%llu",
                 (unsigned long long)candidate.index, (unsigned long long)candidate.nonce);

        // Broadcast a tutte le inbox (-1 = nessuna esclusione): sono i node a
        // validare, appendere e ripubblicare la testa. Best-effort: le inbox
        // piene vengono saltate.
        int putrc = inboxBroadcast(st, &candidate, -1);
        if (putrc != SUCCESS) {
            minerLog(log, minerId, "error broadcasting block to nodes: %s", codesToString(putrc));
            sleepInterruptible(ERROR_RETRY_SECONDS);
        } else {
            minerLog(log, minerId, "block index=%llu sent to nodes",
                     (unsigned long long)candidate.index);
        }
    }

    minerLog(log, minerId, "clean shutdown (running=0)");
    fclose(log);
    return SUCCESS;
}
