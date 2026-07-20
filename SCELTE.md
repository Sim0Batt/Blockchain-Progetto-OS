# SCELTE.md — Decisioni di design

Una voce per ogni decisione non banale, schema fisso: **Decisione → Scelta fatta → Alternative → Perché → Trade-off → Domanda probabile + risposta**.

Ognuno compila le voci del proprio pezzo. Questa sezione copre i workstream **E (Miner)** e **F (Client)**.

---

## Miner (E)

### 1. Responsività durante lo sleep di mining: check non-bloccante vs signal

**Decisione**: come rendere interrompibile il `sleep` di mining simulato (mai un `sleep(N)` monolitico).

**Scelta fatta**: sleep a passi da 1 secondo (`sleep(1)`, N passi random 1..5), con un check non-bloccante dello stato condiviso (`minerShouldAbort` + flag `st->running`) dopo ogni passo e dopo il tentativo di mining.

**Alternative viste a lezione**: usare un signal (es. `SIGUSR1` inviato dal node quando arriva un blocco nuovo) che interrompe il `sleep()` in corso facendolo ritornare con `EINTR`.

**Perché questa**: i signal handler devono essere *async-signal-safe* (solo un piccolo sottoinsieme di funzioni libc è chiamabile in sicurezza da un handler). Con più miner e più fonti di segnale (pause/resume via `SIGSTOP`/`SIGCONT`, eventuali `SIGCHLD`) il rischio di race condition e di interazioni inattese fra signal handler cresce rapidamente. Un check non-bloccante su una variabile in shared memory è deterministico, facile da ragionare, e non richiede alcuna sezione critica speciale (la lettura di `st->height` è già lock-free per design, vedi `chainHeight()` in `ipc.c`).

**Trade-off accettato**: latenza di reazione all'abort fino a ~1 secondo (il tempo di un passo), invece di una reazione istantanea col signal. Accettabile: il mining è comunque simulato e su scala di secondi.

**Domanda probabile + risposta**: *"Perché non un signal, sarebbe più reattivo?"* → Sì, ma introdurrebbe async-signal-safety da gestire su ogni funzione chiamata durante l'handling (incluso l'I/O di logging), a fronte di un guadagno di reattività di al più 1 secondo che qui non serve.

---

### 2. Abort su cambio cima e re-inserimento delle tx nel pool

**Decisione**: cosa fare quando, mentre si sta minando un blocco costruito su `builtOnIndex`, la cima della chain avanza (un altro miner ha vinto la corsa).

**Scelta fatta**: abortire immediatamente il tentativo (`minerShouldAbort` confronta `chainHeight()` con `builtOnIndex`) e reinserire tutte le transazioni del blocco abortito nel `tx_pool` con `txpoolPut`, prima di ricostruire un nuovo candidato sulla nuova cima.

**Alternative viste a lezione**: scartare silenziosamente le transazioni del blocco abortito (si riaccumuleranno naturalmente dai client); oppure continuare comunque a minare il blocco stale e scartarlo solo alla `blockbufPut` (spreco di tempo di mining).

**Perché questa**: scartare le tx le farebbe sparire fino al prossimo submit dei client, ritardando la loro inclusione nella chain senza motivo — sono già valide, il problema è solo che il blocco che le conteneva è diventato stale, non le transazioni stesse. Abortire *appena* rilevato il cambio di cima (nei check a grana di 1s) evita anche di sprecare tempo di mining su un lavoro già inutile.

**Trade-off accettato**: leggero overhead di N chiamate a `txpoolPut` per ogni abort (bloccanti se il pool è pieno); nel caso raro in cui il pool sia pieno proprio in quel momento, il miner resta bloccato lì finché non si libera uno slot, ritardando l'inizio del prossimo candidato.

**Domanda probabile + risposta**: *"Non rischi un loop dove ogni miner si abortisce a vicenda in continuazione?"* → No: l'abort avviene solo quando la cima è *già* avanzata (un blocco è stato accettato), quindi ogni abort corrisponde a un progresso reale della chain, non a un ciclo infinito.

---

### 3. Politica di riempimento del blocco (quante tx, se attendere)

**Decisione**: quante transazioni includere in un blocco candidato, e cosa fare se il `tx_pool` è vuoto al momento della costruzione.

**Scelta fatta**: drenare tutte le tx disponibili *in quel momento* fino a `MAX_TX_PER_BLOCK`, usando `txpoolTryget` (non bloccante). Se il pool è vuoto, si mina comunque un blocco con `tx_count == 0` (blocco "vuoto"), invece di aspettare che arrivi almeno una transazione.

**Alternative viste a lezione**: usare `txpoolGet` bloccante per garantire almeno 1 tx per blocco prima di iniziare il mining.

**Perché questa**: `txpoolGet` bloccante impedirebbe al miner di controllare lo stato di abort/shutdown mentre aspetta una tx che potrebbe non arrivare mai (es. client fermi, `transaction_frequency` bassa) — il miner resterebbe sordo a un comando di `stop` dalla CLI, violando il requisito di sezioni critiche/attese corte e di terminazione pulita. Minare blocchi eventualmente vuoti mantiene il miner sempre responsivo, allo stesso costo del "mining" (comunque finto, basato su probabilità e non su calcolo reale).

**Trade-off accettato**: possibili blocchi vuoti nella chain quando il traffico di transazioni è basso rispetto al numero di miner attivi. Accettabile: non altera la correttezza del linkage della chain, e riflette un comportamento realistico (blocchi vuoti esistono anche in blockchain vere).

**Domanda probabile + risposta**: *"Non è uno spreco minare blocchi vuoti?"* → Il costo di un tentativo di mining è lo stesso a prescindere dal numero di tx (è simulato, non è lavoro computazionale reale); il costo di restare bloccati su una `get` senza poter reagire a shutdown/abort sarebbe invece un problema di correttezza, non solo di efficienza.

---

### 4. Seed per-processo del generatore casuale (miner)

**Decisione**: come inizializzare `random()` in ogni processo miner.

**Scelta fatta**: `srandom(time(NULL) ^ getpid())` una volta sola all'avvio di `runMiner`.

**Alternative viste a lezione**: nessun seeding esplicito (default seed = 1, identico per ogni processo); seeding solo su `time(NULL)`.

**Perché questa**: tutti i miner vengono creati via `fork()` quasi nello stesso istante dal bootstrapper. Con `time(NULL)` da solo, processi nati nello stesso secondo genererebbero *la stessa identica sequenza* di sleep-length e di esiti "minato/non minato" — rompendo l'indipendenza statistica assunta dal modello (più miner con probabilità di successo indipendenti). Lo XOR col PID (unico per processo) rompe questa correlazione a costo nullo.

**Trade-off accettato**: nessuno significativo; è un one-liner che va messo all'inizio di ogni processo figlio.

**Domanda probabile + risposta**: *"Perché non basta `time(NULL)`?"* → Perché più processi nati nello stesso secondo (molto probabile con `fork()` ravvicinati) avrebbero seed identico e quindi sequenze `random()` identiche, vanificando la simulazione di miner indipendenti.

---

## Client (F)

### 5. Interpretazione di `transaction_frequency`

**Decisione**: il parametro CLI `transaction_frequency` è transazioni/secondo o secondi/transazione?

**Scelta fatta**: transazioni al secondo (tx/s). L'intervallo fra due submit è calcolato come `1.0 / txFrequency` secondi, atteso con `nanosleep` interrompibile (retry su `EINTR`, pattern analogo a `semWaitSafe` in `ipc.c`).

**Alternative viste a lezione**: interpretarlo come periodo in secondi fra due transazioni (nessuna divisione necessaria).

**Perché questa**: "frequenza" nel linguaggio comune (ed è il nome del parametro: *frequency*, non *interval* o *period*) indica eventi per unità di tempo, coerente con l'uso in fisica/ingegneria (Hz = eventi/secondo). `txFrequency <= 0` non ha senso fisico (non esiste una frequenza nulla o negativa di eventi discreti) ed è trattato come errore (`PARSE_ERROR`), non come "client silenzioso".

**Trade-off accettato**: nessuno particolare; è solo una scelta di interpretazione del parametro, documentata qui per evitare ambiguità in fase di CLI (workstream G).

**Domanda probabile + risposta**: *"Cosa succede con frequenze molto alte (es. 1000 tx/s)?"* → L'intervallo calcolato (`1ms`) è comunque rispettato da `nanosleep`; il collo di bottiglia reale diventa la capacità del `tx_pool` (`TX_POOL_CAP` slot) e la velocità di consumo dei miner, che applicano backpressure naturale via `txpoolPut` bloccante.

---

### 6. Seed per-processo del generatore casuale (client)

**Decisione**: stessa problematica del miner (vedi punto 4), applicata al client.

**Scelta fatta**: `srandom(time(NULL) ^ getpid())` all'avvio di `runClient`, prima di generare qualunque transazione.

**Perché questa / Trade-off / Domanda**: identico al punto 4 — senza questo, più client fork()ati nello stesso secondo genererebbero la stessa identica sequenza di transazioni (stessi nomi, stessi importi, nello stesso ordine), cosa verificata esplicitamente nel test `TEST CLIENT: SEED PER-PROCESSO` in `test.c` (seed diversi → sequenze diverse).

---

## Nota su punti in sospeso (non miei, segnalati non risolti)

- **Refactor chain non condivisa (Nicola)**: il miner oggi legge la cima della chain tramite `chainHeight()`/`chainTopHash()` (`ipc.c`), isolato dietro i wrapper `minerReadTip`/`minerShouldAbort` in `miner.c`/`miner.h`. Quando arriva il refactor che toglie `chain[]` da `SharedState` (mandato del prof, email 13/07), andranno riscritti solo questi due wrapper.
- **Bug noto nella merkle root a singola transazione (Simon)**: `calculateMerkleRoot("Genesis block")` produce `4e178110...` invece del valore atteso `b815a93d...`. Verificato e segnalato dal test `TEST MERKLE ROOT` in `test.c`, non corretto qui perché `encoding/crypto.c` è del workstream A.
- **Path OpenSSL non portabili in `build.sh` (workstream X)**: `build.sh` referenzia `/opt/homebrew/opt/openssl@3/...` (macOS/Homebrew), non esistenti su Ubuntu. Segnalato con `// FIXME` in `build.sh`, non risolto qui.
