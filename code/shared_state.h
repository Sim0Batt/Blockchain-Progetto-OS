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
#define MAX_NODES         16     /* tetto sul numero di node        */
#define NODE_INBOX_CAP    32     /* slot inbox di un node           */

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

/* La chain: tipo riusabile, copia LOCALE di ogni node. Non in shm. */
typedef struct {
    Block    blocks[MAX_CHAIN];
    uint64_t height;
} Blockchain;

/* Bounded buffer transazioni: client -> miner. */
typedef struct {
    Transaction slots[TX_POOL_CAP];
    int   head, tail;
    sem_t empty, full, mutex;
} TxPool;

/* Inbox di un node: bounded buffer di blocchi in arrivo (broadcast del
 * miner + propagazione dai peer). Trasporto IPC, NON chain. */
typedef struct {
    Block slots[NODE_INBOX_CAP];
    int   head, tail;
    sem_t empty, full, mutex;
} NodeInbox;

/* Stato CONDIVISO: solo canali di comunicazione. NIENTE chain. */
typedef struct {
    TxPool       tx_pool;               /* client -> miner            */
    NodeInbox    inboxes[MAX_NODES];    /* miner/peer -> node         */
    uint32_t     num_nodes;             /* node attivi (<= MAX_NODES) */
    uint32_t     difficulty;
    volatile int running;
} SharedState;

#endif
