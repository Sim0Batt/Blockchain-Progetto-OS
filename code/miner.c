#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "miner.h"
#include "ipc.h"
#include "shared_state.h"
#include "utils/errors.h"
#include "encoding/crypto.h"

/* ================= Helper interni (non esposti in miner.h) ============ */

// Scrive una riga di log con timestamp leggibile, tipo "[2026-07-19 10:03:11] [miner 1 pid 4821] messaggio"
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
    fflush(log); // flush subito: vogliamo il log aggiornato anche se il processo viene ucciso
}

// Combina i due motivi per cui un tentativo di mining va abortito: la cima
// e' avanzata (minerShouldAbort) oppure e' stato richiesto lo shutdown
// pulito (st->running == 0). Tenuta separata da minerShouldAbort perche'
// quest'ultima deve restare un check "puro" sulla cima, testabile da solo.
static int minerAbortRequested(SharedState *st, uint64_t builtOnIndex) {
    if (!st->running) {
        return 1;
    }
    return minerShouldAbort(st, builtOnIndex);
}

/* ============================ API pubblica ============================= */

int minerReadTip(SharedState *st, uint64_t *tipIndex, char tipHash[HASH_BUF_SIZE]) {
    if (st == NULL || tipIndex == NULL || tipHash == NULL) {
        return PARSE_ERROR;
    }

    // STUB/WRAPPER: vedi il commento esteso in miner.h. Oggi la chain e'
    // ancora condivisa in SharedState (chain[] + height), quindi ci
    // appoggiamo alle funzioni di ipc.c gia' esistenti.
    *tipIndex = chainHeight(st);

    if (*tipIndex == 0) {
        // Chain vuota: nessun blocco precedente, prev_hash convenzionale
        // = hash della stringa vuota (stessa convenzione usata per il
        // genesis in test.c).
        calculateSha256("", tipHash);
        return SUCCESS;
    }

    return chainTopHash(st, tipHash);
}

int minerShouldAbort(SharedState *st, uint64_t builtOnIndex) {
    if (st == NULL) {
        return 1; // stato invalido: per sicurezza consideriamo il lavoro stale
    }
    // La cima e' avanzata rispetto a quando abbiamo iniziato a costruire
    // il candidato => il nostro prev_hash/index non sono piu' validi.
    return chainHeight(st) > builtOnIndex;
}

int minerBuildCandidate(SharedState *st, Block *candidate, uint64_t *builtOnIndex) {
    if (st == NULL || candidate == NULL || builtOnIndex == NULL) {
        return PARSE_ERROR;
    }

    memset(candidate, 0, sizeof(Block));

    char tipHash[HASH_BUF_SIZE];
    int rc = minerReadTip(st, builtOnIndex, tipHash);
    if (rc != SUCCESS) {
        return rc;
    }

    candidate->index = *builtOnIndex;
    candidate->timestamp = (uint64_t)time(NULL);
    strncpy(candidate->prev_hash, tipHash, HASH_BUF_SIZE - 1);
    candidate->nonce = 0;

    // Politica di riempimento del blocco (vedi SCELTE.md): drena tutte le
    // tx disponibili ORA nel pool, fino a MAX_TX_PER_BLOCK, con la get
    // NON bloccante (txpoolTryget). Se il pool e' vuoto si mina un
    // blocco vuoto, invece di aspettare con una get bloccante: bloccarsi
    // qui impedirebbe di controllare abort/shutdown, violando il
    // requisito di responsivita' del miner.
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

    // La firma di calculateMerkleRoot() (Simon) vuole una stringa unica
    // con le tx separate da "::", non un array: la costruiamo qui.
    char joined[MAX_TX_PER_BLOCK * TX_MAX_LEN + MAX_TX_PER_BLOCK * 2 + 1] = {0};
    size_t used = 0;
    for (uint32_t i = 0; i < candidate->tx_count; i++) {
        int n = snprintf(joined + used, sizeof(joined) - used, "%s%s",
                          candidate->tx[i].text,
                          (i + 1 < candidate->tx_count) ? "::" : "");
        if (n < 0 || (size_t)n >= sizeof(joined) - used) {
            return MEMORY_ERROR; // non dovrebbe mai accadere con i limiti di shared_state.h
        }
        used += (size_t)n;
    }

    // Se candidate->tx_count == 0, joined e' la stringa vuota: coerente con
    // calculateMerkleRoot(), che in quel caso valorizza merkleRoot a
    // sha256("").
    calculateMerkleRoot(joined, candidate->merkle_root);

    return SUCCESS;
}

int minerMineCandidate(SharedState *st, Block *candidate, uint64_t builtOnIndex) {
    if (st == NULL || candidate == NULL) {
        return PARSE_ERROR;
    }
    if (st->difficulty == 0) {
        return PARSE_ERROR; // evita la divisione per zero su random() % difficulty
    }

    int minato = 0;
    while (!minato) {
        // Sleep INTERROMPIBILE: N passi da 1s (N random 1..5), MAI un
        // sleep(N) monolitico. Scelta di gruppo: check non-bloccante
        // dello stato condiviso tra un passo e l'altro, non signal
        // (async-signal-safety, vedi SCELTE.md).
        int passi = 1 + (int)(random() % 5);
        for (int passo = 0; passo < passi; passo++) {
            sleep(1);
            if (minerAbortRequested(st, builtOnIndex)) {
                return MINER_ABORTED;
            }
        }

        if ((uint32_t)(random() % st->difficulty) == 0) {
            minato = 1;
        }
        candidate->nonce++; // cosmetico: il proof-of-work e' finto

        if (minerAbortRequested(st, builtOnIndex)) {
            return MINER_ABORTED;
        }
    }

    return MINER_MINED;
}

int runMiner(SharedState *st, int minerId) {
    if (st == NULL) {
        return PARSE_ERROR;
    }

    // Seed per-processo: OBBLIGATORIO. I miner vengono fork()ati quasi
    // nello stesso istante: senza XOR col pid, time(NULL) sarebbe
    // identico per tutti e genererebbero la stessa sequenza di tentativi
    // (vedi SCELTE.md).
    srandom((unsigned int)(time(NULL) ^ getpid()));

    char logName[64];
    snprintf(logName, sizeof(logName), "miner-%d.log", getpid());
    FILE *log = fopen(logName, "w");
    if (log == NULL) {
        return IO_ERROR;
    }

    minerLog(log, minerId, "avviato, difficulty=%u", st->difficulty);

    while (st->running) {
        Block candidate;
        uint64_t builtOnIndex;

        int rc = minerBuildCandidate(st, &candidate, &builtOnIndex);
        if (rc != SUCCESS) {
            minerLog(log, minerId, "errore costruzione candidato: %s", codesToString(rc));
            continue;
        }
        minerLog(log, minerId, "candidato costruito: index=%llu tx=%u",
                 (unsigned long long)candidate.index, candidate.tx_count);

        minerLog(log, minerId, "inizio mining su index=%llu", (unsigned long long)builtOnIndex);
        int result = minerMineCandidate(st, &candidate, builtOnIndex);

        if (result == MINER_ABORTED) {
            minerLog(log, minerId,
                     "abort: cima avanzata (o shutdown), reinserisco %u tx nel pool",
                     candidate.tx_count);
            // Le tx di un blocco abortito NON si perdono: tornano nel
            // pool cosi' un altro miner (o questo, al prossimo giro) le
            // puo' includere (vedi SCELTE.md).
            for (uint32_t i = 0; i < candidate.tx_count; i++) {
                int putrc = txpoolPut(st, &candidate.tx[i]);
                if (putrc != SUCCESS) {
                    minerLog(log, minerId, "errore reinserimento tx nel pool: %s",
                             codesToString(putrc));
                }
            }
            continue;
        }

        if (result != MINER_MINED) {
            // es. PARSE_ERROR per difficulty == 0: non e' un blocco valido,
            // non lo mandiamo ai node.
            minerLog(log, minerId, "errore durante il mining: %s", codesToString(result));
            continue;
        }

        minerLog(log, minerId, "blocco minato: index=%llu nonce=%llu",
                 (unsigned long long)candidate.index, (unsigned long long)candidate.nonce);

        int putrc = blockbufPut(st, &candidate);
        if (putrc != SUCCESS) {
            minerLog(log, minerId, "errore broadcast blocco ai node: %s", codesToString(putrc));
        } else {
            minerLog(log, minerId, "blocco index=%llu inviato ai node",
                     (unsigned long long)candidate.index);
        }
    }

    minerLog(log, minerId, "shutdown pulito (running=0)");
    fclose(log);
    return SUCCESS;
}
