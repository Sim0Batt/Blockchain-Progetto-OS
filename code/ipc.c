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

SharedState *ipcCreate(uint32_t difficulty) {
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (fd == -1) {
        perror("ipcCreate: shm_open");
        return NULL;
    }
    if (ftruncate(fd, sizeof(SharedState)) == -1) {
        perror("ipcCreate: ftruncate");
        close(fd);
        shm_unlink(SHM_NAME);
        return NULL;
    }
    SharedState *st = mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE,
                           MAP_SHARED, fd, 0);
    close(fd);
    if (st == MAP_FAILED) {
        perror("ipcCreate: mmap");
        shm_unlink(SHM_NAME);
        return NULL;
    }
    memset(st, 0, sizeof(SharedState));
    st->difficulty = difficulty;
    st->running = 1;
    if (sem_init(&st->tx_pool.empty, 1, TX_POOL_CAP) == -1 ||
        sem_init(&st->tx_pool.full, 1, 0) == -1 ||
        sem_init(&st->tx_pool.mutex, 1, 1) == -1 ||
        sem_init(&st->block_buf.empty, 1, BLOCK_BUF_CAP) == -1 ||
        sem_init(&st->block_buf.full, 1, 0) == -1 ||
        sem_init(&st->block_buf.mutex, 1, 1) == -1) {
        perror("ipcCreate: sem_init");
        munmap(st, sizeof(SharedState));
        shm_unlink(SHM_NAME);
        return NULL;
    }
    return st;
}

void ipcDestroy(SharedState *st) {
    if (st == NULL) {
        return;
    }
    sem_destroy(&st->tx_pool.empty);
    sem_destroy(&st->tx_pool.full);
    sem_destroy(&st->tx_pool.mutex);
    sem_destroy(&st->block_buf.empty);
    sem_destroy(&st->block_buf.full);
    sem_destroy(&st->block_buf.mutex);
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

/* ================= Bounded buffer blocchi (miner -> node) ============== */

int blockbufPut(SharedState *st, const Block *blk) {
    if (st == NULL || blk == NULL) {
        return PARSE_ERROR;
    }
    if (semWaitSafe(&st->block_buf.empty) == -1) {
        return IPC_ERROR;
    }
    semWaitSafe(&st->block_buf.mutex);
    st->block_buf.slots[st->block_buf.tail] = *blk;
    st->block_buf.tail = (st->block_buf.tail + 1) % BLOCK_BUF_CAP;
    sem_post(&st->block_buf.mutex);
    sem_post(&st->block_buf.full);
    return SUCCESS;
}

int blockbufGet(SharedState *st, Block *out) {
    if (st == NULL || out == NULL) {
        return PARSE_ERROR;
    }
    if (semWaitSafe(&st->block_buf.full) == -1) {
        return IPC_ERROR;
    }
    semWaitSafe(&st->block_buf.mutex);
    *out = st->block_buf.slots[st->block_buf.head];
    st->block_buf.head = (st->block_buf.head + 1) % BLOCK_BUF_CAP;
    sem_post(&st->block_buf.mutex);
    sem_post(&st->block_buf.empty);
    return SUCCESS;
}
