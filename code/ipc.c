#include "ipc.h"
#include "utils/errors.h"

#include <fcntl.h>    /* O_CREAT, O_RDWR            */
#include <sys/mman.h> /* shm_open, mmap, PROT_*, MAP_* */
#include <unistd.h>   /* ftruncate, close          */
#include <string.h>   /* memset                    */
#include <stdio.h>    /* perror                    */

/* Crea il segmento di shared memory, lo mappa e inizializza tutto.
 * Va chiamata UNA volta dal padre PRIMA di fork(). */
SharedState *ipc_create(uint32_t difficulty) {
    /* 1. Crea/apre l'oggetto di shared memory con nome. */
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (fd == -1) {
        perror("ipc_create: shm_open");
        return NULL;
    }

    /* 2. Dimensiona l'oggetto: deve contenere una SharedState intera. */
    if (ftruncate(fd, sizeof(SharedState)) == -1) {
        perror("ipc_create: ftruncate");
        close(fd);
        shm_unlink(SHM_NAME);
        return NULL;
    }

    /* 3. Mappa l'oggetto (MAP_SHARED: scritture visibili a tutti i processi). */
    SharedState *st = mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE,
                           MAP_SHARED, fd, 0);
    close(fd); /* dopo mmap il fd non serve piu' */
    if (st == MAP_FAILED) {
        perror("ipc_create: mmap");
        shm_unlink(SHM_NAME);
        return NULL;
    }

    /* 4. Azzera tutto e inizializza i campi non-semaforo. */
    memset(st, 0, sizeof(SharedState));
    st->height = 0;
    st->difficulty = difficulty;
    st->running = 1;
    /* head/tail dei buffer sono gia' 0 grazie al memset. */

    /* 5. Inizializza i 7 semafori (pshared=1: vivono nella shm, condivisi
     *    tra processi). Valori iniziali = pattern bounded buffer + mutex. */
    if (sem_init(&st->tx_pool.empty, 1, TX_POOL_CAP) == -1 ||
        sem_init(&st->tx_pool.full, 1, 0) == -1 ||
        sem_init(&st->tx_pool.mutex, 1, 1) == -1 ||
        sem_init(&st->block_buf.empty, 1, BLOCK_BUF_CAP) == -1 ||
        sem_init(&st->block_buf.full, 1, 0) == -1 ||
        sem_init(&st->block_buf.mutex, 1, 1) == -1 ||
        sem_init(&st->chain_mutex, 1, 1) == -1) {
        perror("ipc_create: sem_init");
        munmap(st, sizeof(SharedState));
        shm_unlink(SHM_NAME);
        return NULL;
    }

    return st;
}

/* Distrugge i semafori, smappa e rimuove il segmento. Chiamata dal padre
 * allo shutdown, quando i figli sono terminati. */
void ipc_destroy(SharedState *st) {
    if (st == NULL) {
        return;
    }
    sem_destroy(&st->tx_pool.empty);
    sem_destroy(&st->tx_pool.full);
    sem_destroy(&st->tx_pool.mutex);
    sem_destroy(&st->block_buf.empty);
    sem_destroy(&st->block_buf.full);
    sem_destroy(&st->block_buf.mutex);
    sem_destroy(&st->chain_mutex);
    munmap(st, sizeof(SharedState));
    shm_unlink(SHM_NAME);
}
