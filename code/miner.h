#ifndef MINER_H
#define MINER_H

#include "shared_state.h"

/* Processo miner: prende le transazioni dal tx_pool, costruisce un blocco
 * candidato, lo "mina" (proof-of-work simulato) e lo consegna ai node via
 * inbox. Se intanto la cima su cui e' costruito avanza, cioe' un altro miner
 * ha vinto la corsa, il lavoro viene abortito e si riparte dalla nuova cima. */

/* Esito del mining di un singolo candidato. */
#define MINER_MINED   0
#define MINER_ABORTED 1  /* cima avanzata o shutdown */

/* Entry point del processo miner: il bootstrapper la chiama nel figlio dopo la
 * fork(), senza exec. Il miner si aggancia al node 'minerId % st->num_nodes' e
 * ne segue la testa pubblicata; gira finche' st->running non diventa 0 e logga
 * su miner-<PID>.log. Ritorna SUCCESS o un error code da errors.h. */
int runMiner(SharedState *st, int minerId);

/* I due passi interni di runMiner(), esposti per poterli provare in
 * isolamento senza far girare un processo miner. Il 'nodeId' e' quello del
 * node a cui il miner e' agganciato: cima e abort arrivano da li'. */

/* Costruisce il candidato: cima del node 'nodeId', tx drenate dal pool fino a
 * MAX_TX_PER_BLOCK e merkle root. Non mina. In 'builtOnIndex' torna l'indice
 * su cui e' costruito. Chain vuota significa genesis, con prev_hash vuoto; un
 * candidato con tx_count == 0 non e' un errore.
 * Ritorna SUCCESS o un error code da errors.h. */
int minerBuildCandidate(SharedState *st, uint32_t nodeId, Block *candidate, uint64_t *builtOnIndex);

/* Mining simulato su un candidato gia' pronto: attese a passi da 1s piu'
 * random() % difficulty, controllando shutdown e cima avanzata fra un passo e
 * l'altro. Ritorna MINER_MINED, MINER_ABORTED o un error code da errors.h
 * (PARSE_ERROR se difficulty e' 0). */
int minerMineCandidate(SharedState *st, uint32_t nodeId, Block *candidate, uint64_t builtOnIndex);

#endif /* MINER_H */
