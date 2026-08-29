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

// Riga di log con timestamp, id del client e pid.
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
    fflush(log); // il log resta leggibile anche se il processo viene ucciso
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

int clientGenerateTransaction(Transaction *out) {
    if (out == NULL) {
        return PARSE_ERROR;
    }

    static const char *names[] = {
        "Alice", "Bob", "Charlie", "Dave", "Eve", "Frank",
        "Grace", "Heidi", "Ivan", "Judy", "Mallory", "Oscar"
    };
    const int nameCount = (int)(sizeof(names) / sizeof(names[0]));

    int senderIdx = (int)(random() % nameCount);
    int receiverIdx;
    do {
        receiverIdx = (int)(random() % nameCount);
    } while (receiverIdx == senderIdx); // niente "Alice pays Alice ..."

    int amount = 1 + (int)(random() % 1000); // mai 0: la regex vuole [1-9][0-9]*

    int n = snprintf(out->text, TX_MAX_LEN, "%s pays %s %d coins",
                      names[senderIdx], names[receiverIdx], amount);
    if (n < 0 || n >= TX_MAX_LEN) {
        return MEMORY_ERROR; // improbabile con nomi fissi e TX_MAX_LEN=256
    }

    return SUCCESS;
}

int runClient(SharedState *st, double txFrequency, int clientId) {
    if (st == NULL) {
        return PARSE_ERROR;
    }
    if (txFrequency <= 0.0) {
        // errors.h non ha un codice per "argomento non valido": riusiamo
        // PARSE_ERROR, come gia' fa ipc.c per gli argomenti NULL.
        return PARSE_ERROR;
    }

    // Seed per processo: i client nascono da fork() quasi nello stesso istante,
    // senza lo XOR col pid produrrebbero sequenze identiche.
    srandom((unsigned int)(time(NULL) ^ getpid()));

    char logName[64];
    snprintf(logName, sizeof(logName), "client-%d.log", getpid());
    FILE *log = fopen(logName, "w");
    if (log == NULL) {
        return IO_ERROR;
    }

    double intervalSeconds = 1.0 / txFrequency;
    clientLog(log, clientId, "started, frequency=%.3f tx/s (interval=%.3fs)",
              txFrequency, intervalSeconds);

    while (st->running) {
        Transaction tx;
        int rc = clientGenerateTransaction(&tx);
        if (rc != SUCCESS) {
            clientLog(log, clientId, "error generating tx: %s", codesToString(rc));
            sleepInterruptible(ERROR_RETRY_SECONDS);
            continue;
        }

        // Le tx sono gia' valide per costruzione, ma le ripassiamo alla regex
        // prima del submit: difesa in profondita'.
        if (txIsValid(tx.text) != SUCCESS) {
            clientLog(log, clientId, "generated tx malformed, discarded: %s", tx.text);
            sleepInterruptible(ERROR_RETRY_SECONDS);
            continue;
        }

        // Backpressure a pool pieno, ma con scadenza: rileggendo st->running non
        // restiamo bloccati qui quando nessun miner sta piu' drenando il pool.
        while (st->running && txpoolTimedput(st, &tx, 200) != SUCCESS) {
            /* pool pieno: ritenta finche' non si libera o parte lo stop */
        }
        if (!st->running) {
            break;
        }
        clientLog(log, clientId, "submitted: %s", tx.text);

        sleepInterruptible(intervalSeconds);
    }

    clientLog(log, clientId, "clean shutdown (running=0)");
    fclose(log);
    return SUCCESS;
}
