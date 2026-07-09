#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <stdint.h>
#include <semaphore.h>

/* ============================================================
 *  Limiti a compile-time.
 *  La shared memory ha dimensione FISSA, quindi ogni struttura
 *  qui dentro deve avere taglia nota a compile-time:
 *  niente puntatori, niente malloc.
 * ============================================================ */
#define HASH_HEX_LEN      64                  /* SHA256 = 256 bit = 64 char hex */
#define HASH_BUF_SIZE     (HASH_HEX_LEN + 1)  /* + terminatore '\0'            */

#define MAX_TX_PER_BLOCK  16    /* transazioni massime per blocco       */
#define TX_MAX_LEN        256   /* lunghezza massima di una transazione */
#define MAX_CHAIN        1024   /* blocchi massimi nella chain          */
#define TX_POOL_CAP        64   /* slot bounded buffer transazioni      */
#define BLOCK_BUF_CAP      32   /* slot bounded buffer blocchi          */

/* Una transazione: stringa a lunghezza fissa (bounded dalla regex). */
typedef struct {
    char text[TX_MAX_LEN];
} Transaction;

/* Un blocco. Hash come stringhe hex (coerenti col CSV e con
 * blockchain.sh). Il block hash copre solo i 5 campi header:
 * le transazioni sono impegnate via merkle_root. */
typedef struct {
    uint64_t    index;                     /* posizione nella chain (0-based) */
    uint64_t    timestamp;                 /* unix epoch, come nel CSV        */
    char        prev_hash[HASH_BUF_SIZE];  /* hash header del blocco prec.    */
    char        merkle_root[HASH_BUF_SIZE];/* radice merkle delle tx          */
    uint64_t    nonce;                     /* proof-of-work simulato          */
    uint32_t    tx_count;                  /* quante tx valide in tx[]        */
    Transaction tx[MAX_TX_PER_BLOCK];      /* transazioni del blocco          */
} Block;

/* Bounded buffer transazioni: client (producer) -> miner (consumer).
 * Schema classico a 3 semafori. */
typedef struct {
    Transaction slots[TX_POOL_CAP];
    int   head;          /* prossimo da consumare */
    int   tail;          /* prossimo da riempire  */
    sem_t empty;         /* slot liberi (init = TX_POOL_CAP) */
    sem_t full;          /* slot pieni  (init = 0)           */
    sem_t mutex;         /* mutua escl. (init = 1)           */
} TxPool;

/* Bounded buffer blocchi: miner (producer) -> node (consumer). */
typedef struct {
    Block slots[BLOCK_BUF_CAP];
    int   head;
    int   tail;
    sem_t empty;         /* init = BLOCK_BUF_CAP */
    sem_t full;          /* init = 0             */
    sem_t mutex;         /* init = 1             */
} BlockBuffer;

/* Lo stato condiviso: UN solo segmento shm con dentro tutto.
 * Tutti i processi lo mappano via mmap. */
typedef struct {
    /* --- La chain condivisa (sezione critica) --- */
    Block    chain[MAX_CHAIN]; /* chain[i] = blocco con index i        */
    uint64_t height;           /* quanti blocchi presenti              */
    sem_t    chain_mutex;      /* serializza gli append (init = 1)     */

    /* --- I due bounded buffer --- */
    TxPool      tx_pool;       /* client -> miner */
    BlockBuffer block_buf;     /* miner  -> node  */

    /* --- Configurazione runtime --- */
    uint32_t     difficulty;   /* denominatore prob. di mining */
    volatile int running;      /* 0 => shutdown pulito         */
} SharedState;

#endif /* SHARED_STATE_H */
