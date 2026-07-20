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

// nanosleep interrompibile da EINTR: riprende dal residuo. Stesso helper
// (e stesso nome) di client.c: i signal di pause/resume o un SIGCHLD non
// devono accorciare l'attesa.
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

/* ---------------- Cima locale (vedi nota architetturale in miner.h) ----
 * Dopo il refactor la chain NON e' piu' in shared memory: chainHeight() e
 * chainTopHash() non esistono piu' in ipc.h. Ogni processo tiene una copia
 * locale, quindi il miner tiene qui l'ultima cima NOTA A QUESTO PROCESSO:
 * indice del prossimo blocco da costruire + hash del blocco in cima.
 * Inizializzata pigramente alla chain vuota, avanza quando questo miner
 * vince una corsa (minerAdvanceLocalTip).                               */
static uint64_t minerTipIndex = 0;
static char     minerTipHash[HASH_BUF_SIZE];
static int      minerTipReady = 0;

// Fa avanzare la cima locale dopo che QUESTO miner ha piazzato un blocco:
// il prossimo candidato si costruira' sopra di esso.
// TODO: confermare con Nicola. Qui l'avanzamento e' ottimistico: diamo per
// accettato il blocco appena consegnato al block_buf, ma e' il node a
// validarlo e ad appenderlo davvero (appendChainBlock), e potrebbe
// rifiutarlo. Nel modello a chain locale l'aggiornamento autoritativo
// dovrebbe arrivare dal node, non da noi.
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
        // Prima chiamata: partiamo dalla chain vuota. Nessun blocco
        // precedente => prev_hash convenzionale = hash della stringa vuota
        // (stessa convenzione usata per il genesis in test.c).
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
    // il candidato => il nostro prev_hash/index non sono piu' validi.
    //
    // TODO: confermare con Nicola. QUESTO E' IL PUNTO DI INTEGRAZIONE che
    // il refactor ha lasciato scoperto. minerTipIndex e' la cima locale di
    // QUESTO processo: avanza solo quando siamo NOI a minare, quindi oggi
    // questo confronto non puo' mai rilevare che un ALTRO miner ha vinto
    // la corsa. L'abort resta funzionante solo per lo shutdown (gestito da
    // minerAbortRequested, che controlla st->running).
    // Il refactor ha rimosso chainHeight()/chainTopHash() da ipc.h senza
    // sostituirli, e SharedState non espone piu' nessuna vista sulla cima:
    // serve un canale node -> miner in shared memory, es. un campo
    //     struct { uint64_t height; char top_hash[HASH_BUF_SIZE]; sem_t mutex; } tip;
    // aggiornato dai node dopo appendChainBlock(). Appena esiste, si
    // riscrivono SOLO minerReadTip e questa funzione per leggerlo: il
    // resto del miner non cambia.
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

    // Politica di riempimento del blocco (vedi SCELTE.md §3): drena tutte
    // le tx disponibili ORA nel pool, fino a MAX_TX_PER_BLOCK, con la get
    // NON bloccante (txpoolTryget). Mai txpoolGet bloccante: bloccarsi qui
    // impedirebbe di controllare abort/shutdown, violando il requisito di
    // responsivita' del miner.
    // Se il pool e' vuoto il candidato esce con tx_count == 0: non e' un
    // errore, decide runMiner cosa farne (oggi: attesa breve e ricostruzione,
    // niente blocchi vuoti in chain).
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

    // Validata UNA VOLTA SOLA, prima del loop. La difficulty la fissa il
    // bootstrapper e non cambia a runtime: scoprirla invalida dentro il
    // loop (minerMineCandidate -> PARSE_ERROR -> continue) trasformerebbe
    // il while in un busy-loop che brucia CPU e riempie il log senza mai
    // progredire.
    if (st->difficulty == 0) {
        minerLog(log, minerId, "difficulty non valida (0): il miner non parte");
        fclose(log);
        return PARSE_ERROR;
    }

    while (st->running) {
        Block candidate;
        uint64_t builtOnIndex;

        int rc = minerBuildCandidate(st, &candidate, &builtOnIndex);
        if (rc != SUCCESS) {
            minerLog(log, minerId, "errore costruzione candidato: %s", codesToString(rc));
            continue;
        }
        // Politica sui blocchi vuoti (vedi SCELTE.md §3): se il drain non
        // ha trovato nessuna tx NON miniamo un blocco vuoto -- a sistema
        // fermo (client lenti o gia' usciti) intaserebbe la chain di
        // blocchi senza transazioni. Aspettiamo un attimo e ricostruiamo.
        // L'attesa e' a passi da 100ms rileggendo st->running: max 1s di
        // latenza sullo stop, quindi il miner resta responsivo.
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
            // Shutdown in corso: NON reinseriamo. txpoolPut e' bloccante
            // sul semaforo 'empty' e ipc.h non offre una variante
            // non-bloccante o con timeout per il producer. Se il pool e'
            // pieno mentre il sistema si ferma, nessuno lo drenera' piu':
            // il reinserimento resterebbe appeso per sempre e il processo
            // non terminerebbe. Le tx si perdono, ma stiamo comunque
            // spegnendo tutto.
            // TODO: confermare con Nicola -- con una txpoolTryput() o
            // txpoolTimedput() in ipc.h potremmo tentare il reinserimento
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
            // Il prossimo candidato va costruito SOPRA questo blocco:
            // senza questo avanzamento rigenereremmo sempre lo stesso
            // index con lo stesso prev_hash.
            minerAdvanceLocalTip(&candidate);
        }
    }

    minerLog(log, minerId, "shutdown pulito (running=0)");
    fclose(log);
    return SUCCESS;
}
