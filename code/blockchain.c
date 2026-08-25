/* ============================================================
 *  Bootstrapper del sistema blockchain.
 *
 *  Uso (come da specifica):
 *    ./blockchain <num_nodes> <num_miners> <num_clients>
 *                 [transaction_frequency] [difficulty] [initial_state.csv]
 *
 *  Crea la shared memory, poi via fork() genera i processi figli.
 *  NON usa exec(): i figli ereditano la mappatura della shm e i semafori
 *  gia' inizializzati dal padre.
 * ============================================================ */
#include "shared_state.h"
#include "ipc.h"
#include "node.h"
#include "utils/errors.h"
#include "utils/csv_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

/* Valori di default per i parametri opzionali. */
#define DEFAULT_TX_FREQUENCY  1
#define DEFAULT_DIFFICULTY    12

static void usage(const char *prog) {
    fprintf(stderr,
            "Uso: %s <num_nodes> <num_miners> <num_clients> "
            "[transaction_frequency] [difficulty] [initial_state.csv]\n",
            prog);
}

/* Converte una stringa in unsigned long verificando che sia un numero
 * valido. Ritorna SUCCESS o PARSE_ERROR. */
static int parseUnsigned(const char *s, unsigned long *out) {
    if (s == NULL || *s == '\0') {
        return PARSE_ERROR;
    }
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return PARSE_ERROR;
    }
    *out = v;
    return SUCCESS;
}

int main(int argc, char *argv[]) {
    /* --- 1. Parsing e validazione degli argomenti --- */
    if (argc < 4 || argc > 7) {
        usage(argv[0]);
        return PARSE_ERROR;
    }

    unsigned long numNodes = 0, numMiners = 0, numClients = 0;
    unsigned long txFrequency = DEFAULT_TX_FREQUENCY;
    unsigned long difficulty  = DEFAULT_DIFFICULTY;
    const char *initialState  = NULL;

    if (parseUnsigned(argv[1], &numNodes) != SUCCESS ||
        parseUnsigned(argv[2], &numMiners) != SUCCESS ||
        parseUnsigned(argv[3], &numClients) != SUCCESS) {
        fprintf(stderr, "Errore: num_nodes, num_miners e num_clients devono essere interi.\n");
        usage(argv[0]);
        return PARSE_ERROR;
    }
    if (argc >= 5 && parseUnsigned(argv[4], &txFrequency) != SUCCESS) {
        fprintf(stderr, "Errore: transaction_frequency non valida.\n");
        return PARSE_ERROR;
    }
    if (argc >= 6 && parseUnsigned(argv[5], &difficulty) != SUCCESS) {
        fprintf(stderr, "Errore: difficulty non valida.\n");
        return PARSE_ERROR;
    }
    if (argc == 7) {
        initialState = argv[6];
    }

    /* Vincoli di dominio. */
    if (numNodes == 0) {
        fprintf(stderr, "Errore: serve almeno un node.\n");
        return PARSE_ERROR;
    }
    if (numNodes > MAX_NODES) {
        fprintf(stderr, "Errore: num_nodes massimo supportato e' %d.\n", MAX_NODES);
        return PARSE_ERROR;
    }
    if (difficulty == 0) {
        fprintf(stderr, "Errore: difficulty deve essere >= 1 (e' il denominatore "
                        "della probabilita' di mining).\n");
        return PARSE_ERROR;
    }

    printf("Avvio: %lu node, %lu miner, %lu client "
           "(frequency=%lu, difficulty=%lu, stato iniziale=%s)\n",
           numNodes, numMiners, numClients, txFrequency, difficulty,
           initialState ? initialState : "nessuno");

    /* --- 2. Creazione della shared memory (PRIMA di ogni fork) --- */
    SharedState *st = ipcCreate((uint32_t)difficulty, (uint32_t)numNodes);
    if (st == NULL) {
        fprintf(stderr, "Errore: impossibile creare la shared memory.\n");
        return IPC_ERROR;
    }

    /* --- 3. Fork dei node: ogni figlio esegue runNode e non ritorna --- */
    for (unsigned long i = 0; i < numNodes; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork (node)");
            st->running = 0;
            ipcDestroy(st);
            return IPC_ERROR;
        }
        if (pid == 0) {
            /* figlio: diventa un node. Niente exec -> eredita la shm. */
            int rc = runNode(st, (uint32_t)i);
            _exit(rc);
        }
    }

    /* TODO strato 2: fork di miner e client, CLI del parent, signal. */
    (void)numMiners;
    (void)numClients;
    (void)txFrequency;
    (void)initialState;

    printf("Node avviati. (CLI non ancora implementata: attendo i figli)\n");

    /* --- 4. Attesa della terminazione dei figli --- */
    int status = 0;
    while (wait(&status) > 0) {
        /* raccoglie tutti i figli; esce quando non ce ne sono piu' */
    }

    /* --- 5. Cleanup delle risorse IPC --- */
    ipcDestroy(st);
    printf("Sistema terminato, risorse IPC rilasciate.\n");
    return SUCCESS;
}
