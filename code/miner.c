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

/* ================= Helper interni (non esposti in miner.h) ============ */

// Scrive una riga di log con timestamp, id del miner e pid.
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
    fflush(log); // il log deve restare leggibile anche se il processo viene ucciso
}

// Unisce i due motivi di abort: shutdown o cima avanzata sul node agganciato.
// Separata da minerShouldAbort (ipc.h), che resta un check puro sulla cima.
static int minerAbortRequested(SharedState *st, uint32_t nodeId, uint64_t builtOnIndex) {
    if (!st->running) {
        return 1;
    }
    return minerShouldAbort(st, nodeId, builtOnIndex);
}

/* Pausa dopo un errore transitorio: evita di riciclare il loop a CPU piena
 * senza incidere sulla latenza dello shutdown. */
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

/* ============================ API pubblica ============================= */

int minerBuildCandidate(SharedState *st, uint32_t nodeId, Block *candidate, uint64_t *builtOnIndex) {
    if (st == NULL || candidate == NULL || builtOnIndex == NULL) {
        return PARSE_ERROR;
    }

    memset(candidate, 0, sizeof(Block));

    // Cima su cui costruire, letta dal node 'nodeId' via NodeHead: altezza
    // (= indice del prossimo blocco) e hash in cima (= prev_hash del candidato).
    uint64_t height;
    char prevHash[HASH_BUF_SIZE];
    int rc = minerReadTip(st, nodeId, &height, prevHash);
    if (rc != SUCCESS) {
        return rc;
    }

    candidate->index = height;
    candidate->timestamp = (uint64_t)time(NULL);
    // prev_hash = prevHash. Con chain vuota (height 0) prevHash e' vuoto: e' il
    // genesis, e il node non valida il prev_hash dell'index 0 (appendChainBlock
    // in utils/chain.c), quindi la stringa vuota va bene.
    strncpy(candidate->prev_hash, prevHash, HASH_BUF_SIZE - 1);
    candidate->nonce = 0;
    *builtOnIndex = height;

    // Drena le tx presenti, fino a MAX_TX_PER_BLOCK, con la get non bloccante:
    // txpoolGet bloccherebbe impedendo i check di abort e shutdown. Pool vuoto
    // => tx_count 0, e decide runMiner.
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
            return MEMORY_ERROR; // non dovrebbe accadere con i limiti di shared_state.h
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
        return PARSE_ERROR; // evita la divisione per zero su random() % difficulty
    }

    int minato = 0;
    while (!minato) {
        // Sleep a passi da 1s (N random 1..5), non un sleep(N) monolitico: cosi'
        // si controlla lo stato condiviso fra un passo e l'altro.
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

    // Seed per-processo: i miner nascono da fork() quasi nello stesso istante,
    // senza lo XOR col pid genererebbero sequenze identiche.
    srandom((unsigned int)(time(NULL) ^ getpid()));

    char logName[64];
    snprintf(logName, sizeof(logName), "miner-%d.log", getpid());
    FILE *log = fopen(logName, "w");
    if (log == NULL) {
        return IO_ERROR;
    }

    // Validati una volta: difficulty e num_nodes li fissa il bootstrapper.
    // num_nodes e' anche il divisore di minerId % num_nodes, quindi lo 0 va
    // escluso prima di calcolare il nodeId.
    if (st->difficulty == 0) {
        minerLog(log, minerId, "difficulty non valida (0): il miner non parte");
        fclose(log);
        return PARSE_ERROR;
    }
    if (st->num_nodes == 0) {
        minerLog(log, minerId, "nessun node attivo (num_nodes=0): il miner non parte");
        fclose(log);
        return PARSE_ERROR;
    }

    // Il miner si aggancia a un solo node: partizione stabile per id. Ne
    // segue la testa pubblicata (NodeHead) per cima corrente e abort.
    uint32_t nodeId = (uint32_t)minerId % st->num_nodes;

    minerLog(log, minerId, "avviato, difficulty=%u, agganciato al node %u",
             st->difficulty, nodeId);

    while (st->running) {
        Block candidate;
        uint64_t builtOnIndex;

        int rc = minerBuildCandidate(st, nodeId, &candidate, &builtOnIndex);
        if (rc != SUCCESS) {
            minerLog(log, minerId, "errore costruzione candidato: %s", codesToString(rc));
            sleepInterruptible(ERROR_RETRY_SECONDS);
            continue;
        }
        // Niente blocchi vuoti: se il pool e' vuoto si aspetta a passi da 100ms
        // rileggendo st->running (stop reattivo) e si ricostruisce.
        if (candidate.tx_count == 0) {
            for (int passo = 0; passo < 10 && st->running; passo++) {
                sleepInterruptible(0.1);
            }
            continue;
        }

        minerLog(log, minerId, "candidato costruito: index=%llu tx=%u",
                 (unsigned long long)candidate.index, candidate.tx_count);

        minerLog(log, minerId, "inizio mining su index=%llu", (unsigned long long)builtOnIndex);
        int result = minerMineCandidate(st, nodeId, &candidate, builtOnIndex);

        if (result == MINER_ABORTED) {
            // Shutdown: le tx si perdono, il sistema si sta fermando comunque.
            if (!st->running) {
                minerLog(log, minerId,
                         "shutdown durante il mining: scarto %u tx", candidate.tx_count);
                continue;
            }

            minerLog(log, minerId,
                     "abort: cima avanzata, reinserisco %u tx nel pool",
                     candidate.tx_count);
            // Le tx di un blocco abortito sono ancora valide: tornano nel pool.
            // txpoolTimedput a passi da 200ms rileggendo st->running: a pool pieno
            // non restiamo appesi e allo shutdown molliamo.
            for (uint32_t i = 0; i < candidate.tx_count && st->running; i++) {
                while (st->running && txpoolTimedput(st, &candidate.tx[i], 200) != SUCCESS) {
                    /* pool pieno: ritenta finche' non si libera o parte lo stop */
                }
            }
            continue;
        }

        if (result != MINER_MINED) {
            minerLog(log, minerId, "errore durante il mining: %s", codesToString(result));
            sleepInterruptible(ERROR_RETRY_SECONDS);
            continue;
        }

        minerLog(log, minerId, "blocco minato: index=%llu nonce=%llu",
                 (unsigned long long)candidate.index, (unsigned long long)candidate.nonce);

        // Broadcast a tutte le inbox (-1 = nessuna esclusione): sono i node a
        // validarlo/appenderlo e a ripubblicare la testa. Best-effort: salta le
        // inbox piene.
        int putrc = inboxBroadcast(st, &candidate, -1);
        if (putrc != SUCCESS) {
            minerLog(log, minerId, "errore broadcast blocco ai node: %s", codesToString(putrc));
            sleepInterruptible(ERROR_RETRY_SECONDS);
        } else {
            minerLog(log, minerId, "blocco index=%llu inviato ai node",
                     (unsigned long long)candidate.index);
        }
    }

    minerLog(log, minerId, "shutdown pulito (running=0)");
    fclose(log);
    return SUCCESS;
}
