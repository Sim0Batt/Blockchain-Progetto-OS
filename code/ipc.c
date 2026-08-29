#include "ipc.h"
#include "utils/errors.h"
#include "encoding/crypto.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

/* Scadenza assoluta "adesso + timeoutMs", come la vuole sem_timedwait. */
static void deadlineIn(struct timespec *ts, unsigned int timeoutMs) {
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec  += timeoutMs / 1000;
    ts->tv_nsec += (long)(timeoutMs % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec++; ts->tv_nsec -= 1000000000L; }
}

/* sem_wait che riprova se interrotto da un signal: pause/resume (SIGCONT) o un
 * SIGCHLD possono interrompere un'attesa in corso, e non e' un errore. */
static int semWaitSafe(sem_t *s) {
    int r;
    do {
        r = sem_wait(s);
    } while (r == -1 && errno == EINTR);
    return r;
}

/* Ciclo di vita del segmento condiviso. */

SharedState *ipcCreate(uint32_t difficulty, uint32_t num_nodes) {
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (fd == -1) { perror("ipcCreate: shm_open"); return NULL; }
    if (ftruncate(fd, sizeof(SharedState)) == -1) { perror("ipcCreate: ftruncate"); close(fd); shm_unlink(SHM_NAME); return NULL; }
    SharedState *st = mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (st == MAP_FAILED) { perror("ipcCreate: mmap"); shm_unlink(SHM_NAME); return NULL; }
    memset(st, 0, sizeof(SharedState));
    st->difficulty = difficulty;
    st->num_nodes  = num_nodes;
    st->running    = 1;
    sem_init(&st->tx_pool.empty, 1, TX_POOL_CAP);
    sem_init(&st->tx_pool.full, 1, 0);
    sem_init(&st->tx_pool.mutex, 1, 1);
    for (int i = 0; i < MAX_NODES; i++) {
        sem_init(&st->inboxes[i].empty, 1, NODE_INBOX_CAP);
        sem_init(&st->inboxes[i].full, 1, 0);
        sem_init(&st->inboxes[i].mutex, 1, 1);
        sem_init(&st->heads[i].mutex, 1, 1);
    }
    sem_init(&st->consensus.mutex, 1, 1);
    return st;
}

void ipcDestroy(SharedState *st) {
    if (st == NULL) return;
    sem_destroy(&st->tx_pool.empty);
    sem_destroy(&st->tx_pool.full);
    sem_destroy(&st->tx_pool.mutex);
    for (int i = 0; i < MAX_NODES; i++) {
        sem_destroy(&st->inboxes[i].empty);
        sem_destroy(&st->inboxes[i].full);
        sem_destroy(&st->inboxes[i].mutex);
        sem_destroy(&st->heads[i].mutex);
    }
    sem_destroy(&st->consensus.mutex);
    munmap(st, sizeof(SharedState));
    shm_unlink(SHM_NAME);
}

/* Bounded buffer delle transazioni: client -> miner. */

int txpoolPut(SharedState *st, const Transaction *tx) {
    if (st == NULL || tx == NULL) {
        return PARSE_ERROR;
    }
    if (semWaitSafe(&st->tx_pool.empty) == -1) { /* aspetta uno slot libero */
        return IPC_ERROR;
    }
    semWaitSafe(&st->tx_pool.mutex);
    st->tx_pool.slots[st->tx_pool.tail] = *tx;
    st->tx_pool.tail = (st->tx_pool.tail + 1) % TX_POOL_CAP;
    sem_post(&st->tx_pool.mutex);
    sem_post(&st->tx_pool.full);
    return SUCCESS;
}

int txpoolGet(SharedState *st, Transaction *out) {
    if (st == NULL || out == NULL) {
        return PARSE_ERROR;
    }
    if (semWaitSafe(&st->tx_pool.full) == -1) { /* aspetta un elemento */
        return IPC_ERROR;
    }
    semWaitSafe(&st->tx_pool.mutex);
    *out = st->tx_pool.slots[st->tx_pool.head];
    st->tx_pool.head = (st->tx_pool.head + 1) % TX_POOL_CAP;
    sem_post(&st->tx_pool.mutex);
    sem_post(&st->tx_pool.empty);
    return SUCCESS;
}

int txpoolTryget(SharedState *st, Transaction *out) {
    if (st == NULL || out == NULL) {
        return PARSE_ERROR;
    }
    if (sem_trywait(&st->tx_pool.full) == -1) {
        /* vuoto o interrotto: nessun elemento disponibile ora */
        return IPC_EMPTY;
    }
    semWaitSafe(&st->tx_pool.mutex);
    *out = st->tx_pool.slots[st->tx_pool.head];
    st->tx_pool.head = (st->tx_pool.head + 1) % TX_POOL_CAP;
    sem_post(&st->tx_pool.mutex);
    sem_post(&st->tx_pool.empty);
    return SUCCESS;
}

/* Inbox per node: miner/peer -> node. */

int inboxPut(SharedState *st, uint32_t nodeId, const Block *blk) {
    if (st == NULL || blk == NULL || nodeId >= st->num_nodes) return PARSE_ERROR;
    NodeInbox *ib = &st->inboxes[nodeId];
    if (semWaitSafe(&ib->empty) == -1) return IPC_ERROR;
    semWaitSafe(&ib->mutex);
    ib->slots[ib->tail] = *blk;
    ib->tail = (ib->tail + 1) % NODE_INBOX_CAP;
    sem_post(&ib->mutex);
    sem_post(&ib->full);
    return SUCCESS;
}

int inboxGet(SharedState *st, uint32_t nodeId, Block *out) {
    if (st == NULL || out == NULL || nodeId >= st->num_nodes) return PARSE_ERROR;
    NodeInbox *ib = &st->inboxes[nodeId];
    if (semWaitSafe(&ib->full) == -1) return IPC_ERROR;
    semWaitSafe(&ib->mutex);
    *out = ib->slots[ib->head];
    ib->head = (ib->head + 1) % NODE_INBOX_CAP;
    sem_post(&ib->mutex);
    sem_post(&ib->empty);
    return SUCCESS;
}

int inboxTimedget(SharedState *st, uint32_t nodeId, Block *out, unsigned int timeoutMs) {
    if (st == NULL || out == NULL || nodeId >= st->num_nodes) return PARSE_ERROR;
    NodeInbox *ib = &st->inboxes[nodeId];
    struct timespec ts;
    deadlineIn(&ts, timeoutMs);
    int r;
    do { r = sem_timedwait(&ib->full, &ts); } while (r == -1 && errno == EINTR);
    if (r == -1) return IPC_EMPTY;   /* scaduta: nessun blocco in attesa */
    semWaitSafe(&ib->mutex);
    *out = ib->slots[ib->head];
    ib->head = (ib->head + 1) % NODE_INBOX_CAP;
    sem_post(&ib->mutex);
    sem_post(&ib->empty);
    return SUCCESS;
}

/* Variante non bloccante di inboxPut, usata dalla propagazione: un node fermo
 * su un peer saturo smetterebbe di consumare la propria inbox, e con qualche
 * node in attesa reciproca si arriverebbe allo stallo. */
int inboxTryput(SharedState *st, uint32_t nodeId, const Block *blk) {
    if (st == NULL || blk == NULL || nodeId >= st->num_nodes) return PARSE_ERROR;
    NodeInbox *ib = &st->inboxes[nodeId];
    if (sem_trywait(&ib->empty) == -1) return IPC_ERROR;  /* piena: salta */
    semWaitSafe(&ib->mutex);
    ib->slots[ib->tail] = *blk;
    ib->tail = (ib->tail + 1) % NODE_INBOX_CAP;
    sem_post(&ib->mutex);
    sem_post(&ib->full);
    return SUCCESS;
}

/* Broadcast best-effort: i peer con inbox piena vengono saltati, tanto il
 * blocco gli arrivera' da un altro percorso di propagazione. */
int inboxBroadcast(SharedState *st, const Block *blk, int exclude) {
    if (st == NULL || blk == NULL) return PARSE_ERROR;
    for (uint32_t i = 0; i < st->num_nodes; i++) {
        if ((int)i == exclude) continue;
        inboxTryput(st, i, blk);
    }
    return SUCCESS;
}

/* Coordinamento node -> miner: una testa pubblicata per node. */

int nodePublishHead(SharedState *st, uint32_t nodeId, uint64_t height, const char *lastHash) {
    if (st == NULL || lastHash == NULL || nodeId >= st->num_nodes) return PARSE_ERROR;
    NodeHead *h = &st->heads[nodeId];
    semWaitSafe(&h->mutex);
    h->height = height;
    strncpy(h->lastHash, lastHash, HASH_BUF_SIZE - 1);
    h->lastHash[HASH_BUF_SIZE - 1] = '\0';
    sem_post(&h->mutex);
    return SUCCESS;
}

int minerReadTip(SharedState *st, uint32_t nodeId, uint64_t *height, char prevHash[HASH_BUF_SIZE]) {
    if (st == NULL || height == NULL || prevHash == NULL || nodeId >= st->num_nodes) return PARSE_ERROR;
    NodeHead *h = &st->heads[nodeId];
    semWaitSafe(&h->mutex);
    *height = h->height;
    memcpy(prevHash, h->lastHash, HASH_BUF_SIZE);
    sem_post(&h->mutex);
    return SUCCESS;
}

int minerShouldAbort(SharedState *st, uint32_t nodeId, uint64_t builtOnIndex) {
    if (st == NULL || nodeId >= st->num_nodes) return 0;
    NodeHead *h = &st->heads[nodeId];
    semWaitSafe(&h->mutex);
    uint64_t cur = h->height;
    sem_post(&h->mutex);
    return cur > builtOnIndex;
}

/* Arbitraggio per indice: unico punto in cui si decide chi vince a una data
 * altezza, cosi' tutti i node applicano lo stesso blocco. */

int consensusDecide(SharedState *st, const Block *blk, Block *out) {
    if (st == NULL || blk == NULL || out == NULL) return PARSE_ERROR;
    if (blk->index >= MAX_CHAIN) return MEMORY_ERROR;

    ConsensusLog *cl = &st->consensus;
    semWaitSafe(&cl->mutex);

    DecidedSlot *slot = &cl->slots[blk->index];

    if (!slot->valid) {
        /* Un blocco stantio non deve vincere lo slot: si aggancerebbe al nulla
         * e nessun node riuscirebbe piu' ad applicarlo. */
        if (blk->index > 0) {
            DecidedSlot *prev = &cl->slots[blk->index - 1];
            if (!prev->valid) {
                sem_post(&cl->mutex);
                return INVALID_BLOCK;      /* buco: l'indice precedente non e' deciso */
            }
            if (strcmp(blk->prev_hash, prev->hash) != 0) {
                sem_post(&cl->mutex);
                return CHAIN_MISMATCH;     /* non si aggancia al vincitore precedente */
            }
        }
        /* append-only: da qui lo slot non cambia piu' */
        slot->block = *blk;
        calculateBlockHash(&slot->block, slot->hash);
        slot->valid = 1;
        if (blk->index == cl->decided) {
            cl->decided = blk->index + 1;
        }
    }

    *out = slot->block;                    /* il vincitore, non per forza 'blk' */
    sem_post(&cl->mutex);
    return SUCCESS;
}

int consensusGet(SharedState *st, uint64_t index, Block *out) {
    if (st == NULL || out == NULL) return PARSE_ERROR;
    if (index >= MAX_CHAIN) return MEMORY_ERROR;

    ConsensusLog *cl = &st->consensus;
    semWaitSafe(&cl->mutex);
    DecidedSlot *slot = &cl->slots[index];
    int found = slot->valid;
    if (found) {
        *out = slot->block;
    }
    sem_post(&cl->mutex);
    return found ? SUCCESS : BLOCK_NOT_FOUND;
}

int consensusSeed(SharedState *st, const Blockchain *chain) {
    if (st == NULL || chain == NULL) return PARSE_ERROR;
    if (chain->height > MAX_CHAIN) return MEMORY_ERROR;

    ConsensusLog *cl = &st->consensus;
    semWaitSafe(&cl->mutex);
    for (uint64_t i = 0; i < chain->height; i++) {
        DecidedSlot *slot = &cl->slots[i];
        slot->block = chain->blocks[i];
        calculateBlockHash(&slot->block, slot->hash);
        slot->valid = 1;
    }
    if (chain->height > cl->decided) {
        cl->decided = chain->height;
    }
    sem_post(&cl->mutex);
    return SUCCESS;
}

int txpoolTimedput(SharedState *st, const Transaction *tx, unsigned int timeoutMs) {
    if (st == NULL || tx == NULL) return PARSE_ERROR;
    struct timespec ts;
    deadlineIn(&ts, timeoutMs);
    int r;
    do { r = sem_timedwait(&st->tx_pool.empty, &ts); } while (r == -1 && errno == EINTR);
    if (r == -1) return IPC_ERROR;   /* timeout scaduto */
    semWaitSafe(&st->tx_pool.mutex);
    st->tx_pool.slots[st->tx_pool.tail] = *tx;
    st->tx_pool.tail = (st->tx_pool.tail + 1) % TX_POOL_CAP;
    sem_post(&st->tx_pool.mutex);
    sem_post(&st->tx_pool.full);
    return SUCCESS;
}
