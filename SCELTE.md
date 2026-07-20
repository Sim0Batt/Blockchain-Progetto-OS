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

**Scelta fatta**: abortire immediatamente il tentativo (`minerShouldAbort` confronta la cima nota con `builtOnIndex`) e reinserire tutte le transazioni del blocco abortito nel `tx_pool` con `txpoolPut`, prima di ricostruire un nuovo candidato sulla nuova cima. **Eccezione**: se l'abort è dovuto allo shutdown (`st->running == 0`) il reinserimento viene saltato — vedi trade-off.

**Alternative viste a lezione**: scartare silenziosamente le transazioni del blocco abortito (si riaccumuleranno naturalmente dai client); oppure continuare comunque a minare il blocco stale e scartarlo solo alla `blockbufPut` (spreco di tempo di mining).

**Perché questa**: scartare le tx le farebbe sparire fino al prossimo submit dei client, ritardando la loro inclusione nella chain senza motivo — sono già valide, il problema è solo che il blocco che le conteneva è diventato stale, non le transazioni stesse. Abortire *appena* rilevato il cambio di cima (nei check a grana di 1s) evita anche di sprecare tempo di mining su un lavoro già inutile.

**Trade-off accettato**: leggero overhead di N chiamate a `txpoolPut` per ogni abort (bloccanti se il pool è pieno); nel caso raro in cui il pool sia pieno proprio in quel momento, il miner resta bloccato lì finché non si libera uno slot, ritardando l'inizio del prossimo candidato. **Allo shutdown questo diventa un deadlock**: se il pool è pieno e i miner stanno uscendo, nessuno lo drenerà più e la `txpoolPut` non tornerebbe mai, impedendo la terminazione. Per questo con `st->running == 0` le tx del blocco abortito vengono scartate: si perdono transazioni, ma il sistema si sta fermando comunque e la terminazione pulita ha priorità. Con una `txpoolTryput`/`txpoolTimedput` in `ipc.h` (non esiste oggi, vedi §7) si potrebbe tentare il reinserimento anche in quel caso senza rischiare di restare appesi.

**Domanda probabile + risposta**: *"Non rischi un loop dove ogni miner si abortisce a vicenda in continuazione?"* → No: l'abort avviene solo quando la cima è *già* avanzata (un blocco è stato accettato), quindi ogni abort corrisponde a un progresso reale della chain, non a un ciclo infinito.

---

### 3. Politica di riempimento del blocco (quante tx, se attendere)

**Decisione**: quante transazioni includere in un blocco candidato, e cosa fare se il `tx_pool` è vuoto al momento della costruzione.

**Scelta fatta**: drenare tutte le tx disponibili *in quel momento* fino a `MAX_TX_PER_BLOCK`, usando `txpoolTryget` (non bloccante). Se dopo il drain il candidato ha `tx_count == 0`, **non** si mina un blocco vuoto: si attende un tempo breve (max 1s, a passi da 100ms rileggendo `st->running`) e si ricostruisce il candidato da capo.

**Alternative viste a lezione**: (a) usare `txpoolGet` bloccante per garantire almeno 1 tx per blocco prima di iniziare il mining; (b) minare comunque il blocco vuoto.

**Perché questa**: è il compromesso fra le due. `txpoolGet` bloccante impedirebbe al miner di controllare lo stato di abort/shutdown mentre aspetta una tx che potrebbe non arrivare mai (es. client fermi, `transaction_frequency` bassa) — il miner resterebbe sordo a un comando di `stop` dalla CLI, violando il requisito di attese corte e terminazione pulita. Minare blocchi vuoti, però, a sistema fermo intasa la chain di blocchi privi di transazioni che consumano slot di `MAX_CHAIN` senza portare informazione. L'attesa breve *interrompibile* ottiene entrambe le cose: nessun blocco vuoto in chain, e latenza sullo stop limitata a 1s nel caso peggiore perché `st->running` viene riletto a ogni passo da 100ms.

**Trade-off accettato**: quando il pool è vuoto il miner fa un ciclo di polling a vuoto ogni secondo invece di stare fermo. È un costo trascurabile (10 `nanosleep` al secondo, nessun lavoro fra uno e l'altro) e comunque inferiore al busy-loop che si avrebbe ricostruendo il candidato senza attesa.

**Domanda probabile + risposta**: *"Perché non aspettare direttamente con una `get` bloccante, visto che tanto non hai niente da fare?"* → Perché "non ho niente da fare" vale per le transazioni, non per lo shutdown: dentro una `sem_wait` il processo non può più leggere `st->running` e non terminerebbe allo stop. L'attesa a passi mantiene il processo capace di reagire, che è un requisito di correttezza, non di efficienza.

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

- **⚠️ Manca il canale node → miner dopo il refactor (Nicola) — BLOCCANTE per il requisito "abort su blocco nuovo"**. Il refactor ha tolto `chain[]`/`height` da `SharedState` e con essi ha rimosso da `ipc.h` le funzioni `chainHeight()`/`chainTopHash()`/`chainGetBlock()`, **senza introdurre nulla al loro posto**: oggi `SharedState` contiene solo i due bounded buffer, `difficulty` e `running`, e `block_buf` viaggia in un solo verso (miner → node). Non esiste inoltre ancora nessun `node.*` in nessun branch (workstream D non iniziato).
  Conseguenza: il miner non ha **alcun** modo di sapere che un altro miner ha vinto la corsa. I wrapper `minerReadTip`/`minerShouldAbort` si appoggiano a una **cima locale al processo**, che avanza solo quando è questo miner a produrre un blocco: sufficiente a concatenare correttamente i propri blocchi, insufficiente a rilevare l'avanzamento globale della chain. L'abort resta quindi funzionante **solo per lo shutdown** (`st->running`).
  Serve un campo condiviso aggiornato dai node dopo `appendChainBlock()`, es. `struct { uint64_t height; char top_hash[HASH_BUF_SIZE]; sem_t mutex; } tip;` in `SharedState`. Appena esiste, si riscrivono **solo** quei due wrapper. Non l'ho aggiunto io perché `shared_state.h` è un contratto condiviso in mano al workstream C. Marcato con `// TODO: confermare con Nicola` in `miner.c` (dentro `minerShouldAbort` e `minerAdvanceLocalTip`).
- **Manca una put non-bloccante lato producer in `ipc.h` (Nicola)**: esiste `txpoolTryget` per il consumer, ma non la simmetrica per il producer. Serve una `txpoolTimedput(st, tx, timeoutMs)` basata su `sem_timedwait`: senza, la `txpoolPut` del client può restare appesa indefinitamente allo shutdown se il pool è pieno e i miner sono già usciti (il client è fermo *dentro* la put, non sul check di `st->running`). Non implementata qui per non duplicare la logica dei semafori fuori da `ipc.c`; punto marcato con un commento in `client.c`.
- **Bug noto nella merkle root a singola transazione (Simon)**: `calculateMerkleRoot("Genesis block")` produce `4e178110...` invece del valore atteso `b815a93d...`. Verificato e segnalato dal test `TEST MERKLE ROOT` in `test.c`, non corretto qui perché `encoding/crypto.c` è del workstream A.
- **Path OpenSSL non portabili in `build.sh` (workstream X)**: `build.sh` referenzia `/opt/homebrew/opt/openssl@3/...` (macOS/Homebrew), non esistenti su Ubuntu. Segnalato con `// FIXME` in `build.sh`, non risolto qui.
