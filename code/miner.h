#ifndef MINER_H
#define MINER_H

#include "shared_state.h"

/* ============================================================
 *  Processo Miner (workstream E).
 *  Preleva transazioni dal tx_pool, costruisce un blocco candidato,
 *  lo "mina" (proof-of-work simulato) e lo consegna ai node via
 *  block_buf. Se nel frattempo la cima della chain avanza (un altro
 *  miner ha vinto), il lavoro e' abortito e ricomincia sulla nuova cima.
 * ============================================================ */

/* Esito del loop di mining su un singolo blocco candidato. */
#define MINER_MINED   0  /* blocco minato con successo               */
#define MINER_ABORTED 1  /* lavoro scartato: cima avanzata o shutdown */

/* Entry point del processo miner, chiamata dal bootstrapper (G) nel
 * figlio dopo la fork() (niente exec, niente main() qui). La difficulty
 * si legge da st->difficulty. Gira finche' st->running non diventa 0.
 * Logga su miner-<PID>.log. Ritorna SUCCESS o un error code da errors.h.
 * // TODO: confermare firma con Nicola (bootstrapper G) */
int runMiner(SharedState *st, int minerId);

/* ---- Helper esposti SOLO per testabilita' (vedi test.c) ---------------
 * Non fanno parte dell'interfaccia verso il bootstrapper: sono gli stessi
 * pezzi che runMiner() usa internamente, esposti qui per poterli testare
 * in isolamento (mining con difficulty piccola, abort su cambio cima)
 * senza dover far girare un intero processo miner. */

/* Legge la cima corrente della chain su cui costruire il prossimo blocco:
 * 'tipIndex' = indice del prossimo blocco (== altezza corrente della
 * chain), 'tipHash' = hash del blocco in cima (prev_hash del candidato).
 *
 * STUB/WRAPPER (vedi §1 del task E/F): il prof ha vietato la chain
 * condivisa globale (mandato del 13/07): ogni node dovra' tenere una
 * copia locale e i blocchi si propagano via IPC. Il refactor di Nicola
 * che toglie chain[] da SharedState NON e' ancora arrivato: oggi questo
 * wrapper si appoggia a chainHeight()/chainTopHash() (ipc.h), che leggono
 * ancora st->chain/st->height. Quando il refactor arriva, si riscrive
 * SOLO questa funzione (es. leggendo da un messaggio/canale IPC invece
 * che dalla chain condivisa): il resto del miner non si tocca.
 * Ritorna SUCCESS, oppure un error code da errors.h. */
int minerReadTip(SharedState *st, uint64_t *tipIndex, char tipHash[HASH_BUF_SIZE]);

/* Ritorna 1 se il lavoro costruito su 'builtOnIndex' e' diventato stale
 * (la cima e' avanzata rispetto a quando abbiamo iniziato), 0 altrimenti.
 * STUB/WRAPPER: stessa nota di minerReadTip qui sopra. */
int minerShouldAbort(SharedState *st, uint64_t builtOnIndex);

/* Costruisce il blocco candidato: tip corrente (minerReadTip) + drain del
 * tx_pool fino a MAX_TX_PER_BLOCK (txpoolTryget, non bloccante: vedi
 * SCELTE.md sulla politica di riempimento) + merkle root. Non fa mining.
 * 'builtOnIndex' in output = indice su cui e' stato costruito (== tip).
 * Ritorna SUCCESS o un error code da errors.h. */
int minerBuildCandidate(SharedState *st, Block *candidate, uint64_t *builtOnIndex);

/* Esegue il loop di mining simulato su un candidato gia' costruito: sleep
 * a passi interrompibili da 1s (N random 1..5) + random()%difficulty.
 * Ritorna MINER_MINED, MINER_ABORTED, oppure un error code da errors.h
 * (es. PARSE_ERROR se st->difficulty == 0). */
int minerMineCandidate(SharedState *st, Block *candidate, uint64_t builtOnIndex);

#endif /* MINER_H */
