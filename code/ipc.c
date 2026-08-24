#include "ipc.h"
#include "utils/errors.h"

#include <fcntl.h>    /* O_CREAT, O_RDWR            */
#include <sys/mman.h> /* shm_open, mmap, PROT_*, MAP_* */
#include <unistd.h>   /* ftruncate, close          */
#include <string.h>   /* memset                    */
#include <stdio.h>    /* perror                    */
#include <errno.h>    /* errno, EINTR              */

/* ---- Helper interno ---- */

/* sem_wait che riprova se interrotto da un signal (EINTR). I signal di
 * pause/resume (SIGCONT) o un SIGCHLD possono interrompere una sem_wait
 * in corso: non e' un errore, si riprova. */
static int semWaitSafe(sem_t *s) {
    int r;
    do {
        r = sem_wait(s);
    } while (r == -1 && errno == EINTR);
    return r;
}

/* ============================ Ciclo di vita ============================ */

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
    }
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
    }
    munmap(st, sizeof(SharedState));
    shm_unlink(SHM_NAME);
}

/* ================= Bounded buffer transazioni (client -> miner) ======== */

int txpoolPut(SharedState *st, const Transaction *tx) {
    if (st == NULL || tx == NULL) {
        return PARSE_ERROR;
    }
    if (semWaitSafe(&st->tx_pool.empty) == -1) { /* aspetta uno slot libero */
        return IPC_ERROR;
    }
    semWaitSafe(&st->tx_pool.mutex); /* --- sezione critica --- */
    st->tx_pool.slots[st->tx_pool.tail] = *tx;
    st->tx_pool.tail = (st->tx_pool.tail + 1) % TX_POOL_CAP;
    sem_post(&st->tx_pool.mutex);
    sem_post(&st->tx_pool.full); /* un elemento in piu' */
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
    sem_post(&st->tx_pool.empty); /* uno slot libero in piu' */
    return SUCCESS;
}

int txpoolTryget(SharedState *st, Transaction *out) {
    if (st == NULL || out == NULL) {
        return PARSE_ERROR;
    }
    if (sem_trywait(&st->tx_pool.full) == -1) {
        /* vuoto (EAGAIN) o interrotto: nessun elemento disponibile ora */
        return IPC_EMPTY;
    }
    semWaitSafe(&st->tx_pool.mutex);
    *out = st->tx_pool.slots[st->tx_pool.head];
    st->tx_pool.head = (st->tx_pool.head + 1) % TX_POOL_CAP;
    sem_post(&st->tx_pool.mutex);
    sem_post(&st->tx_pool.empty);
    return SUCCESS;
}

/* ================= Inbox per-node (miner/peer -> node) ================= */

/* Consegna un blocco all'inbox del node 'nodeId'. Bloccante se piena. */
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

/* Il node 'nodeId' preleva un blocco dalla SUA inbox. Bloccante se vuota. */
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

/* Invia il blocco a tutte le inbox attive tranne 'exclude'.
 * exclude = -1     -> tutte (broadcast del miner).
 * exclude = nodeId -> tutte tranne se stesso (propagazione ai peer). */
int inboxBroadcast(SharedState *st, const Block *blk, int exclude) {
    if (st == NULL || blk == NULL) return PARSE_ERROR;
    for (uint32_t i = 0; i < st->num_nodes; i++) {
        if ((int)i == exclude) continue;
        int rc = inboxPut(st, i, blk);
        if (rc != SUCCESS) return rc;
    }
    return SUCCESS;
}
