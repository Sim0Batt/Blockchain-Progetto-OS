#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

#include "client.h"
#include "ipc.h"
#include "shared_state.h"
#include "utils/errors.h"
#include "utils/tx.h"

/* ================= Helper interni (non esposti in client.h) ============ */

// Scrive una riga di log con timestamp leggibile, tipo "[2026-07-19 10:03:11] [client 2 pid 4821] messaggio"
static void clientLog(FILE *log, int clientId, const char *fmt, ...) {
    if (log == NULL) {
        return;
    }

    time_t now = time(NULL);
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmNow);

    fprintf(log, "[%s] [client %d pid %d] ", ts, clientId, getpid());

    va_list args;
    va_start(args, fmt);
    vfprintf(log, fmt, args);
    va_end(args);

    fprintf(log, "\n");
    fflush(log); // flush subito: vogliamo il log aggiornato anche se il processo viene ucciso
}

// nanosleep interrompibile da EINTR (signal di pause/resume, SIGCHLD, ...): riprende dal residuo
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

int clientGenerateTransaction(Transaction *out) {
    if (out == NULL) {
        return PARSE_ERROR;
    }

    // Pool di nomi alfanumerici da cui pescare mittente/destinatario
    static const char *names[] = {
        "Alice", "Bob", "Charlie", "Dave", "Eve", "Frank",
        "Grace", "Heidi", "Ivan", "Judy", "Mallory", "Oscar"
    };
    const int nameCount = (int)(sizeof(names) / sizeof(names[0]));

    int senderIdx = (int)(random() % nameCount);
    int receiverIdx;
    do {
        receiverIdx = (int)(random() % nameCount);
    } while (receiverIdx == senderIdx); // evitiamo "Alice pays Alice ..."

    int amount = 1 + (int)(random() % 1000); // importo in [1, 1000], mai 0 (rispetta [1-9][0-9]*)

    int n = snprintf(out->text, TX_MAX_LEN, "%s pays %s %d coins",
                      names[senderIdx], names[receiverIdx], amount);
    if (n < 0 || n >= TX_MAX_LEN) {
        return MEMORY_ERROR; // non dovrebbe mai accadere con nomi fissi e TX_MAX_LEN=256
    }

    return SUCCESS;
}

int runClient(SharedState *st, double txFrequency, int clientId) {
    if (st == NULL) {
        return PARSE_ERROR;
    }
    if (txFrequency <= 0.0) {
        // errors.h non ha un codice dedicato per "argomento non valido":
        // riusiamo PARSE_ERROR, coerente con l'uso che ne fa gia' ipc.c
        // per gli argomenti NULL.
        return PARSE_ERROR;
    }

    // Seed per-processo: OBBLIGATORIO. Tutti i client vengono fork()ati
    // quasi nello stesso istante: senza XOR col pid, time(NULL) sarebbe
    // identico per tutti e genererebbero la stessa identica sequenza di
    // transazioni (vedi SCELTE.md).
    srandom((unsigned int)(time(NULL) ^ getpid()));

    char logName[64];
    snprintf(logName, sizeof(logName), "client-%d.log", getpid());
    FILE *log = fopen(logName, "w");
    if (log == NULL) {
        return IO_ERROR;
    }

    double intervalSeconds = 1.0 / txFrequency;
    clientLog(log, clientId, "avviato, frequenza=%.3f tx/s (intervallo=%.3fs)",
              txFrequency, intervalSeconds);

    while (st->running) {
        Transaction tx;
        int rc = clientGenerateTransaction(&tx);
        if (rc != SUCCESS) {
            clientLog(log, clientId, "errore generazione tx: %s", codesToString(rc));
            continue;
        }

        // Autocontrollo difensivo: generiamo gia' tx valide by construction,
        // ma verifichiamo comunque contro la regex ufficiale (coordinata
        // con Simon, workstream A) prima di sottometterla al pool.
        if (txIsValid(tx.text) != SUCCESS) {
            clientLog(log, clientId, "tx generata malformata, scartata: %s", tx.text);
            continue;
        }

        // txpoolPut e' bloccante se il pool e' pieno: e' backpressure
        // corretta, il client aspetta e non perde transazioni.
        int putrc = txpoolPut(st, &tx);
        if (putrc != SUCCESS) {
            clientLog(log, clientId, "errore sottomissione tx al pool: %s", codesToString(putrc));
            continue;
        }
        clientLog(log, clientId, "sottomessa: %s", tx.text);

        sleepInterruptible(intervalSeconds);
    }

    clientLog(log, clientId, "shutdown pulito (running=0)");
    fclose(log);
    return SUCCESS;
}
