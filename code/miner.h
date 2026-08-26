#ifndef MINER_H
#define MINER_H

#include "shared_state.h"

/* ============================================================
 *  Processo Miner (workstream E).
 *  Preleva transazioni dal tx_pool, costruisce un blocco candidato,
 *  lo "mina" (proof-of-work simulato) e lo consegna ai node via
 *  inbox broadcast. Se nel frattempo la cima del node su cui e'
 *  costruito avanza (un altro miner ha vinto la corsa), il lavoro e'
 *  abortito e ricomincia sulla nuova cima.
 * ============================================================ */

/* Esito del loop di mining su un singolo blocco candidato. */
#define MINER_MINED   0  /* blocco minato con successo               */
#define MINER_ABORTED 1  /* lavoro scartato: cima avanzata o shutdown */

/* Entry point del processo miner, chiamata dal bootstrapper (G) nel
 * figlio dopo la fork() (niente exec, niente main() qui). La difficulty
 * si legge da st->difficulty. Il miner si aggancia a un solo node,
 * 'minerId % st->num_nodes', e ne segue la testa pubblicata (NodeHead)
 * per la cima corrente e per l'abort. Gira finche' st->running non
 * diventa 0. Logga su miner-<PID>.log. Ritorna SUCCESS o un error code
 * da errors.h. */
int runMiner(SharedState *st, int minerId);

/* ---- Helper esposti per testabilita' (vedi test.c) --------------------
 * Non fanno parte dell'interfaccia verso il bootstrapper: sono i pezzi che
 * runMiner() usa internamente, esposti per poterli testare in isolamento
 * senza far girare un intero processo miner.
 *
 * La cima su cui costruire e l'abort su cima avanzata arrivano dal canale
 * node -> miner (NodeHead): minerReadTip e minerShouldAbort le fornisce il
 * layer IPC (ipc.h); qui si passa solo il 'nodeId' del node a cui il miner
 * e' agganciato. */

/* Costruisce il blocco candidato: legge la cima del node 'nodeId'
 * (minerReadTip) + drena il tx_pool fino a MAX_TX_PER_BLOCK (txpoolTryget,
 * non bloccante) + merkle root. Non fa mining.
 * 'builtOnIndex' in output = indice su cui e' stato costruito (== altezza
 * letta dalla testa). Chain vuota (altezza 0, prevHash vuoto) => si
 * costruisce il genesis: l'index 0 non ha blocco precedente e il node non
 * ne valida il prev_hash (vedi appendChainBlock in utils/chain.c), quindi
 * il prev_hash vuoto va bene. Un candidato con tx_count == 0 non e' un
 * errore. Ritorna SUCCESS o un error code da errors.h. */
int minerBuildCandidate(SharedState *st, uint32_t nodeId, Block *candidate, uint64_t *builtOnIndex);

/* Esegue il loop di mining simulato su un candidato gia' costruito: sleep
 * a passi interrompibili da 1s (N random 1..5) + random()%difficulty. Fra
 * un passo e l'altro controlla shutdown e cima avanzata sul node 'nodeId'.
 * Ritorna MINER_MINED, MINER_ABORTED, oppure un error code da errors.h
 * (es. PARSE_ERROR se st->difficulty == 0). */
int minerMineCandidate(SharedState *st, uint32_t nodeId, Block *candidate, uint64_t builtOnIndex);

#endif /* MINER_H */
