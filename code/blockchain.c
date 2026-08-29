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
#include "client.h"
#include "shared_state.h"
#include "ipc.h"
#include "miner.h"
#include "node.h"
#include "utils/errors.h"
#include "utils/csv_manager.h"
#include "utils/tx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>

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

void handleCommand(char cmd_buffer[512], unsigned long totalChildrens, pid_t *childrenPIDs, SharedState *st) {
    if (strncmp(cmd_buffer, "stop", 4) == 0) {
        printf("Closing the system...\n");
        st->running = 0;
        for (unsigned long i = 0; i < totalChildrens; i++) {
            kill(childrenPIDs[i], SIGCONT);
            kill(childrenPIDs[i], SIGINT);
        }
    }
    else if (strncmp(cmd_buffer, "pause", 5) == 0) {
        printf("Pausing the system...\n");
        for (unsigned long i = 0; i < totalChildrens; i++) {
            kill(childrenPIDs[i], SIGSTOP);
        }
    }
    else if (strncmp(cmd_buffer, "resume", 6) == 0) {
        printf("Resuming the system...\n");
        for (unsigned long i = 0; i < totalChildrens; i++) {
            kill(childrenPIDs[i], SIGCONT);
        }
    }
    else if (strncmp(cmd_buffer, "submit", 6) == 0) {
        printf("Submitting a transaction...\n");
        char txtext[TX_MAX_LEN] = {0};
        
        // FIX FONDAMENTALE: sscanf invece di scanf
        if (sscanf(cmd_buffer + 7, "\"%255[^\"]\"", txtext) == 1 || sscanf(cmd_buffer + 7, "%255[^\n]", txtext) == 1) {
            if (txIsValid(txtext) == SUCCESS) {
                Transaction tx;
                strncpy(tx.text, txtext, TX_MAX_LEN);
                if (txpoolPut(st, &tx) == SUCCESS) {
                    printf("Transaction queued\n");
                } else {
                    printf("IPC Error\n");
                }
            } else {
                printf("Transaction format not valid\n");
            }
        }
    }
    else if (strncmp(cmd_buffer, "save blockchain ", 16) == 0) { 
        char filename[256];
        if (sscanf(cmd_buffer + 16, "%255s", filename) == 1) {
            printf("Saving blockchain to %s\n", filename);
            Block cmdBlock;
            memset(&cmdBlock, 0, sizeof(Block));
            cmdBlock.index = UINT64_MAX;
            cmdBlock.nonce = 1;
            strncpy(cmdBlock.tx[0].text, filename, TX_MAX_LEN);
            inboxPut(st, 0 , &cmdBlock);
        }
    }
    else if (strncmp(cmd_buffer, "request blockchain", 18) == 0) {
        Block cmdBlock;
        memset(&cmdBlock, 0, sizeof(Block));
        cmdBlock.index = UINT64_MAX;
        
        if (strstr(cmd_buffer, "--index")) {
            uint64_t index;
            if (sscanf(strstr(cmd_buffer, "--index") + 8, "%llu", (unsigned long long *)&index) == 1) {
                cmdBlock.nonce = 3;
                cmdBlock.timestamp = index;
            }
        } else if (strstr(cmd_buffer, "--hash")) {
            char hash[HASH_BUF_SIZE];
            if (sscanf(strstr(cmd_buffer, "--hash") + 7, "%64s", hash) == 1) {
                cmdBlock.nonce = 4;
                strncpy(cmdBlock.tx[0].text, hash, TX_MAX_LEN);
            }
        } else {
            cmdBlock.nonce = 2;
        }
        if (cmdBlock.nonce != 0) inboxPut(st, 0 , &cmdBlock);
    }
    else if (strncmp(cmd_buffer, "request block ", 14) == 0 && strstr(cmd_buffer, "--index ")) {
        uint64_t index;
        if (sscanf(strstr(cmd_buffer, "--index ") + 8, "%llu", (unsigned long long *)&index) == 1) {
            Block cmdBlock;
            memset(&cmdBlock, 0, sizeof(Block));
            cmdBlock.index = UINT64_MAX;
            cmdBlock.nonce = 5;
            cmdBlock.timestamp = index;
            inboxPut(st, 0 , &cmdBlock);
        }
    }
    else if (strncmp(cmd_buffer, "request block-hash ", 19) == 0) {
        char hash[HASH_BUF_SIZE];
        if (sscanf(cmd_buffer + 19, "%64s", hash) == 1) {
            Block cmdBlock;
            memset(&cmdBlock, 0, sizeof(Block));
            cmdBlock.index = UINT64_MAX;
            cmdBlock.nonce = 6;
            strncpy(cmdBlock.tx[0].text, hash, TX_MAX_LEN);
            inboxPut(st, 0 , &cmdBlock);
        }
    }
    else if (strlen(cmd_buffer) > 0) {
        printf("Unknown command\n");
    }
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
        fprintf(stderr, "Error: num_nodes, num_miners and num_clients must be integer.\n");
        usage(argv[0]);
        return PARSE_ERROR;
    }
    if (argc >= 5 && parseUnsigned(argv[4], &txFrequency) != SUCCESS) {
        fprintf(stderr, "Error: transaction_frequency not valid.\n");
        return PARSE_ERROR;
    }
    if (argc >= 6 && parseUnsigned(argv[5], &difficulty) != SUCCESS) {
        fprintf(stderr, "Error: difficulty not valid.\n");
        return PARSE_ERROR;
    }
    if (argc == 7) {
        initialState = argv[6];
    }

    /* Vincoli di dominio. */
    if (numNodes == 0) {
        fprintf(stderr, "Error: at least one node is required.\n");
        return PARSE_ERROR;
    }
    if (numNodes > MAX_NODES) {
        fprintf(stderr, "Error: maximum supported num_nodes is %d.\n", MAX_NODES);
        return PARSE_ERROR;
    }
    if (difficulty == 0) {
        fprintf(stderr, "Error: difficulty must be >= 1 (it is the denominator for mining probability).\n");
        return PARSE_ERROR;
    }

    printf("Start: %lu node, %lu miner, %lu client "
           "(frequency=%lu, difficulty=%lu, initial state=%s)\n",
           numNodes, numMiners, numClients, txFrequency, difficulty,
           initialState ? initialState : "none");
    
    if (initialState != NULL) {
        Blockchain *tmp = malloc(sizeof(Blockchain));
        if (tmp == NULL) {
            fprintf(stderr, "Error: failed to allocate memory for initial state check.\n");
            return MEMORY_ERROR;
        }
        memset(tmp, 0, sizeof(Blockchain));
        int load = loadCsv(initialState, tmp);
        free(tmp);
        if (load != SUCCESS) {
            fprintf(stderr, "Error: failed to initialize initial state\n");
            return load;
        }
    }

    /* --- 2. Creazione della shared memory (PRIMA di ogni fork) --- */
    SharedState *st = ipcCreate((uint32_t)difficulty, (uint32_t)numNodes);
    if (st == NULL) {
        fprintf(stderr, "Error: failed to create shared memory.\n");
        return IPC_ERROR;
    }

    unsigned long totalChildrens = numNodes + numMiners + numClients;
    pid_t *childrenPIDs = malloc(totalChildrens * sizeof(pid_t));
    unsigned long currentChild = 0;

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
            int rc = runNode(st, (uint32_t)i, initialState);
            _exit(rc);
        }

        childrenPIDs[currentChild++] = pid;
    }



    (void)numMiners;
    (void)numClients;
    (void)txFrequency;
    (void)initialState;


    // Fork dei Miners
    for (unsigned long i = 0; i < numMiners; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork (miner)");
            st->running = 0;
            ipcDestroy(st);
            return IPC_ERROR;
        }

        if (pid == 0) {
            int rc = runMiner(st, (int)i);
            _exit(rc);
        }
        childrenPIDs[currentChild++] = pid;
    }


    for (unsigned long i = 0; i < numClients; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork (client)");
            st->running = 0;
            ipcDestroy(st);
            return IPC_ERROR;
        }

        if (pid == 0) {
            int rc = runClient(st, (double)txFrequency, (int)i);
            _exit(rc);
        }
        childrenPIDs[currentChild++] = pid;
    }

    // Loop CLI
    char cmd_buffer[512];
    printf("\n--- BLOCKCHAIN CLI ---\n");
    printf("Commands: submit, pause, resume, stop, save blockchain, request blockchain, request block, request block-hash\n");
    while (st->running) {
        printf("> ");
        fflush(stdout);

        if (fgets(cmd_buffer, sizeof(cmd_buffer), stdin) == NULL) {
            break;
        }
        cmd_buffer[strcspn(cmd_buffer, "\r\n")] = 0;

        handleCommand(cmd_buffer, totalChildrens, childrenPIDs, st);
    }

    printf("Node started\n");

    int status = 0;
    while (wait(&status) > 0);

    free(childrenPIDs);
    ipcDestroy(st);
    printf("System terminated, released all the resources\n");
    return SUCCESS;
}
