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

// Combina i due motivi di abort: cima avanzata o shutdown richiesto.
// Separata da minerShouldAbort per lasciare a quest'ultima un check puro
// sulla cima, testabile da solo.
static int minerAbortRequested(SharedState *st, uint64_t builtOnIndex) {
    if (!st->running) {
        return 1;
    }
    return minerShouldAbort(st, builtOnIndex);
}

/* Attesa dopo un errore transitorio, prima di riprovare. Serve a non
 * riciclare il loop a CPU piena; resta abbastanza corta da non incidere
 * sulla latenza dello shutdown. */
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

/* La chain non e' condivisa: ogni processo ne tiene una copia locale.
 * Qui il miner conserva la cima nota a se stesso: indice del prossimo
 * blocco da costruire e hash del blocco in cima. Vedi la nota
 * architetturale in miner.h. */
static uint64_t minerTipIndex = 0;
static char     minerTipHash[HASH_BUF_SIZE];
static int      minerTipReady = 0;

// Avanza la cima locale dopo che questo miner ha piazzato un blocco.
// TODO: confermare con Nicola. L'avanzamento e' ottimistico: diamo per
// accettato il blocco appena consegnato al block_buf, ma a validarlo e
// appenderlo e' il node, che potrebbe rifiutarlo. L'aggiornamento
// autoritativo dovrebbe arrivare da lui.
static void minerAdvanceLocalTip(const Block *blk) {
    calculateBlockHash(blk, minerTipHash);
    minerTipIndex = blk->index + 1;
    minerTipReady = 1;
}

/* ============================ API pubblica ============================= */

int minerReadTip(SharedState *st, uint64_t *tipIndex, char tipHash[HASH_BUF_SIZE]) {
    if (st == NULL || tipIndex == NULL || tipHash == NULL) {
        return PARSE_ERROR;
    }

    if (!minerTipReady) {
        // Chain vuota: nessun blocco precedente, prev_hash convenzionale
        // = sha256(""), stessa convenzione del genesis.
        calculateSha256("", minerTipHash);
        minerTipIndex = 0;
        minerTipReady = 1;
    }

    *tipIndex = minerTipIndex;
    memcpy(tipHash, minerTipHash, HASH_BUF_SIZE);
    return SUCCESS;
}

int minerShouldAbort(SharedState *st, uint64_t builtOnIndex) {
    if (st == NULL) {
        return 1; // stato invalido: per sicurezza consideriamo il lavoro stale
    }
    // La cima e' avanzata rispetto a quando abbiamo iniziato a costruire
    // il candidato => prev_hash e index non sono piu' validi.
    //
    // TODO: confermare con Nicola. minerTipIndex e' la cima locale di
    // questo processo e avanza solo quando siamo noi a minare: il
    // confronto non puo' quindi rilevare che un altro miner ha vinto la
    // corsa. Serve un canale node -> miner in shared memory, es. un campo
    //     struct { uint64_t height; char top_hash[HASH_BUF_SIZE]; sem_t mutex; } tip;
    // aggiornato dai node dopo appendChainBlock(). Appena esiste, si
    // riscrivono solo minerReadTip e questa funzione per leggerlo.
    return minerTipIndex > builtOnIndex;
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

    // Drena le tx disponibili ORA, fino a MAX_TX_PER_BLOCK, con la get non
    // bloccante. Mai txpoolGet: bloccarsi qui impedirebbe di controllare
    // abort e shutdown (vedi SCELTE.md §3). Se il pool e' vuoto il
    // candidato esce con tx_count == 0 e decide runMiner cosa farne.
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

    // calculateMerkleRoot() vuole le tx in una stringa unica separate da
    // "::", non un array.
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

int minerMineCandidate(SharedState *st, Block *candidate, uint64_t builtOnIndex) {
    if (st == NULL || candidate == NULL) {
        return PARSE_ERROR;
    }
    if (st->difficulty == 0) {
        return PARSE_ERROR; // evita la divisione per zero su random() % difficulty
    }

    int minato = 0;
    while (!minato) {
        // Sleep interrompibile: N passi da 1s (N random 1..5), mai un
        // sleep(N) monolitico. Scelta di gruppo: check non bloccante dello
        // stato condiviso fra un passo e l'altro, non signal (vedi
        // SCELTE.md §1).
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
        candidate->nonce++; // cosmetico: il proof-of-work e' simulato

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

    // Seed per-processo: i miner nascono da fork() quasi nello stesso
    // istante, senza lo XOR col pid genererebbero sequenze identiche
    // (vedi SCELTE.md §4).
    srandom((unsigned int)(time(NULL) ^ getpid()));

    char logName[64];
    snprintf(logName, sizeof(logName), "miner-%d.log", getpid());
    FILE *log = fopen(logName, "w");
    if (log == NULL) {
        return IO_ERROR;
    }

    minerLog(log, minerId, "avviato, difficulty=%u", st->difficulty);

    // Validata una volta sola: la difficulty la fissa il bootstrapper e non
    // cambia a runtime. Scoprirla invalida dentro il loop ridurrebbe il
    // while a un busy-loop che non progredisce mai.
    if (st->difficulty == 0) {
        minerLog(log, minerId, "difficulty non valida (0): il miner non parte");
        fclose(log);
        return PARSE_ERROR;
    }

    int exitCode = SUCCESS;

    while (st->running) {
        Block candidate;
        uint64_t builtOnIndex;

        int rc = minerBuildCandidate(st, &candidate, &builtOnIndex);
        if (rc == IPC_ERROR) {
            // Canale IPC inutilizzabile (semafori gia' distrutti): riprovare
            // significherebbe solo girare a vuoto.
            minerLog(log, minerId, "canale IPC non disponibile, esco: %s", codesToString(rc));
            exitCode = IPC_ERROR;
            break;
        }
        if (rc != SUCCESS) {
            minerLog(log, minerId, "errore costruzione candidato: %s", codesToString(rc));
            sleepInterruptible(ERROR_RETRY_SECONDS);
            continue;
        }
        // Niente blocchi vuoti in chain (vedi SCELTE.md §3): se il pool era
        // vuoto aspettiamo a passi da 100ms rileggendo st->running, cosi'
        // la latenza sullo stop resta sotto il secondo, e ricostruiamo.
        if (candidate.tx_count == 0) {
            for (int passo = 0; passo < 10 && st->running; passo++) {
                sleepInterruptible(0.1);
            }
            continue;
        }

        minerLog(log, minerId, "candidato costruito: index=%llu tx=%u",
                 (unsigned long long)candidate.index, candidate.tx_count);

        minerLog(log, minerId, "inizio mining su index=%llu", (unsigned long long)builtOnIndex);
        int result = minerMineCandidate(st, &candidate, builtOnIndex);

        if (result == MINER_ABORTED) {
            // Allo shutdown non reinseriamo: txpoolPut e' bloccante e a pool
            // pieno, con i miner in uscita, nessuno lo drenerebbe piu': il
            // processo non terminerebbe. Le tx si perdono, ma il sistema si
            // sta fermando comunque.
            // TODO: confermare con Nicola -- con una txpoolTryput() o
            // txpoolTimedput() in ipc.h il reinserimento sarebbe possibile
            // anche qui senza rischiare di restare appesi.
            if (!st->running) {
                minerLog(log, minerId,
                         "shutdown: salto il reinserimento di %u tx (txpoolPut e' bloccante)",
                         candidate.tx_count);
                continue;
            }

            minerLog(log, minerId,
                     "abort: cima avanzata, reinserisco %u tx nel pool",
                     candidate.tx_count);
            // Le tx di un blocco abortito sono ancora valide: tornano nel
            // pool invece di essere scartate (vedi SCELTE.md §2).
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
            minerLog(log, minerId, "errore durante il mining: %s", codesToString(result));
            sleepInterruptible(ERROR_RETRY_SECONDS);
            continue;
        }

        minerLog(log, minerId, "blocco minato: index=%llu nonce=%llu",
                 (unsigned long long)candidate.index, (unsigned long long)candidate.nonce);

        int putrc = blockbufPut(st, &candidate);
        if (putrc == IPC_ERROR) {
            minerLog(log, minerId, "canale IPC non disponibile, esco: %s", codesToString(putrc));
            exitCode = IPC_ERROR;
            break;
        }
        if (putrc != SUCCESS) {
            minerLog(log, minerId, "errore broadcast blocco ai node: %s", codesToString(putrc));
            sleepInterruptible(ERROR_RETRY_SECONDS);
        } else {
            minerLog(log, minerId, "blocco index=%llu inviato ai node",
                     (unsigned long long)candidate.index);
            // Senza questo avanzamento il prossimo candidato rinascerebbe
            // con lo stesso index e lo stesso prev_hash.
            minerAdvanceLocalTip(&candidate);
        }
    }

    if (exitCode == SUCCESS) {
        minerLog(log, minerId, "shutdown pulito (running=0)");
    }
    fclose(log);
    return exitCode;
}
