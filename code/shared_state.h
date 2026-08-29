#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <stdint.h>
#include <semaphore.h>

#define HASH_HEX_LEN      64
#define HASH_BUF_SIZE     (HASH_HEX_LEN + 1)
#define MAX_TX_PER_BLOCK  16
#define TX_MAX_LEN        256
#define MAX_CHAIN         1024
#define TX_POOL_CAP       64
#define MAX_NODES         16     /* tetto sul numero di node */
#define NODE_INBOX_CAP    32     /* slot dell'inbox di un node */

typedef struct { char text[TX_MAX_LEN]; } Transaction;

typedef struct {
    uint64_t    index;
    uint64_t    timestamp;
    char        prev_hash[HASH_BUF_SIZE];
    char        merkle_root[HASH_BUF_SIZE];
    uint64_t    nonce;
    uint32_t    tx_count;
    Transaction tx[MAX_TX_PER_BLOCK];
} Block;

/* La chain: ogni node ne tiene una copia locale, non sta in shm. */
typedef struct {
    Block    blocks[MAX_CHAIN];
    uint64_t height;
} Blockchain;

/* Bounded buffer delle transazioni: client -> miner. */
typedef struct {
    Transaction slots[TX_POOL_CAP];
    int   head, tail;
    sem_t empty, full, mutex;
} TxPool;

/* Inbox di un node: blocchi in arrivo dal broadcast dei miner e dalla
 * propagazione dei peer. E' solo trasporto, non e' la chain. */
typedef struct {
    Block slots[NODE_INBOX_CAP];
    int   head, tail;
    sem_t empty, full, mutex;
} NodeInbox;

/* La testa pubblicata da un node, su cui si aggancia il miner collegato.
 * Coordinamento best-effort, non e' la chain: ogni node scrive solo la propria
 * casella, cosi' non esiste uno stato globale condiviso della chain. */
typedef struct {
    uint64_t height;                   /* altezza della copia locale del node */
    char     lastHash[HASH_BUF_SIZE];  /* hash del blocco in cima */
    sem_t    mutex;
} NodeHead;

/* Registro di arbitraggio: un solo blocco vincitore per indice, append-only.
 * Non e' la chain, e' solo l'arbitro fra miner che vanno alla stessa altezza. */
typedef struct {
    Block block;                  /* blocco vincitore per questo indice */
    char  hash[HASH_BUF_SIZE];    /* hash del vincitore, calcolato una volta sola */
    int   valid;                  /* 0 = indice non ancora deciso */
} DecidedSlot;

typedef struct {
    DecidedSlot slots[MAX_CHAIN]; /* slots[i] = esito deciso per l'indice i */
    uint64_t    decided;          /* quanti indici consecutivi sono decisi */
    sem_t       mutex;            /* serializza la decisione di un indice */
} ConsensusLog;

/* Stato condiviso: canali di comunicazione + registro di arbitraggio.
 * La chain non sta qui: ogni node ne tiene la propria copia locale. */
typedef struct {
    TxPool       tx_pool;               /* client -> miner */
    NodeInbox    inboxes[MAX_NODES];    /* miner/peer -> node */
    NodeHead     heads[MAX_NODES];      /* node -> miner (coordinamento) */
    ConsensusLog consensus;             /* arbitraggio globale per indice */
    uint32_t     num_nodes;             /* node attivi (<= MAX_NODES) */
    uint32_t     difficulty;
    volatile int running;
} SharedState;

#endif
