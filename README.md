# Blockchain OS — Roadmap di progetto

Documento di coordinamento del gruppo. L'obiettivo è dividerci il lavoro in **pezzi con confini netti**, così ognuno lavora su file diversi e i merge conflict quasi spariscono.

---

## 0. Regole del gioco (leggere prima di tutto)

**Dove si vincono i punti.** La valutazione pesa così:
- **70% presentazione orale** — ognuno deve saper difendere il *proprio* pezzo: perché quella scelta e non le alternative viste a lezione.
- **20% codice** — correttezza, aderenza alle specifiche, qualità.
- **10% report** — chiarezza, completezza (max 5 pagine).

Conseguenza pratica: **scrivere il codice è metà del lavoro. L'altra metà è saperlo spiegare.** Ogni scelta di design va annotata mentre la facciamo (vedi il file `SCELTE.md`, §5), non ricostruita a memoria la sera prima.

**Disciplina git (obbligatoria su repo di gruppo):**
- `git pull` **sempre** prima di iniziare a lavorare.
- **Un branch per pezzo** (`git checkout -b nome-pezzo`), mai lavorare tutti direttamente su `main`.
- Merge su `main` solo quando il pezzo compila e passa i test, e possibilmente con una rapida occhiata di un altro del gruppo.
- Prima di ogni push: `git status` + `git diff --cached --stat` per non mandare su binari, log o file spuri.

**Coerenza del codice (così sembra scritto da una mano sola):**
- Convenzione di naming condivisa: `snake_case`, funzioni con prefisso di modulo (`chain_append`, `block_hash`, `ipc_send`).
- Pattern d'errore unico ovunque: ogni funzione fallibile ritorna un `int` error code da `errors.h`, il chiamante controlla e propaga.
- Un `.clang-format` condiviso nella repo → formattazione automatica identica per tutti.

---

## 1. Decisioni da prendere TUTTI INSIEME (bloccano il resto)

Queste tre vanno chiuse **prima** di dividerci, perché definiscono i contratti su cui tutti costruiscono. Meglio una call di mezz'ora ora che rifare interfacce dopo.

### 1.1 — Meccanismo IPC (la decisione madre)
Il PDF lascia libertà. Le opzioni realistiche:
- **POSIX message queue** (`mq_*`) — confini di messaggio già pronti, ottime per "coda transazioni per miner" e "coda blocchi per node". Richiede `-lrt`.
- **Shared memory + semafori** — massime prestazioni, mappa perfettamente la sincronizzazione vista a lezione (mutua esclusione sulla chain condivisa), ma tutta la sincronizzazione è a carico nostro.
- **Named pipe (FIFO)** — semplici, ma il fan-in/fan-out molti-a-molti diventa un intreccio di FIFO.

Va scelto e **giustificato nel report**. È il cuore concettuale del progetto.

### 1.2 — Il block hash usa 5 o 6 campi?
Il PDF si contraddice. La sezione hashing elenca 5 campi (`index, timestamp, prev_hash, merkle_root, nonce`) — transazioni **escluse**, impegnate via il `merkle_root`. Ma `--hash` parla di 6 campi. Lettura consigliata: **hash su 5 campi header, transazioni fuori** (come le blockchain vere). Da decidere e blindare, oppure chiedere al prof. Tocca `--hash`, `--verify` e la validazione dei node insieme.

### 1.3 — Protocollo dei messaggi + error code condivisi
Definire una volta sola:
- Le `struct` dei messaggi che viaggiano nell'IPC (submit transazione, broadcast blocco, ecc.).
- I codici d'errore (`errors.h`), che il PDF pretende **condivisi tra C e Bash con lo stesso valore numerico**.

Questo è il contratto: una volta fisso, ognuno può lavorare sul suo processo in parallelo.

---

## 2. I workstream (pezzi assegnabili)

| # | Workstream | File principali | Dipende da | Difficoltà |
|---|-----------|-----------------|-----------|-----------|
| A | Crypto + script Bash | `utils/encoding.*`, `sha256.*`, `merkle.*`, `blockchain.sh` | — | Media |
| B | Block + Chain + CSV | `block.*`, `chain.*`, `csv.*` | A (per gli hash) | Media |
| C | IPC + protocollo | `ipc.*`, `messages.h` | decisione §1.1/1.3 | Alta |
| D | Node | `node.*` | B, C | Alta |
| E | Miner | `miner.*` | B, C | Alta |
| F | Client | `client.*` | C | Bassa |
| G | Bootstrapper + CLI | `blockchain.c`, `cli.*` | tutti | Alta |
| X | Build + edge case + report + orale | `Makefile`, `build.sh`, `report.pdf`, `SCELTE.md` | trasversale | — |

### A — Layer crittografico + `blockchain.sh`
Parte da `encoding` (già esistente, da rifinire) e ci costruisce sopra il resto crittografico. È **testabile in isolamento**, senza IPC né processi — per questo è il primo pezzo che può partire subito.
- Rifinire `encoding`: fix `-Wpointer-sign` (firma `const char*` o cast), validazione hex vera (`isxdigit`, non solo `sscanf`), `#include <string.h>` esplicito.
- SHA256 (usare una implementazione nota e vendorizzata, o OpenSSL `libcrypto`).
- Merkle root — **ATTENZIONE alla trappola**: con una sola transazione NON è `sha256(tx)`. La lista dispari viene paddata con `sha256("")` e poi accoppiata. Test-target verificato: `merkle("Genesis block") = b815a93dd7f59058...`.
- Serializzazione dell'header e block hash (vedi decisione §1.2).
- Script `blockchain.sh` con `--verify <csv>`, `--hash <block_hex>`, `--merkle <tx::tx>`. In Bash gli hash si fanno con `sha256sum` (coreutils): deve produrre **gli stessi hash** del C.
- **Milestone verificabile**: `./blockchain.sh --verify initial_state.csv` → OK, e `--merkle "Genesis block"` → `b815a9...`.

### B — Strutture dati Block + Chain + CSV I/O
- `struct Block` (index, timestamp, prev_hash, merkle_root, nonce, lista transazioni).
- Chain come linked list; funzioni `chain_append`, `chain_validate`.
- Load/save CSV nel formato del PDF. **Attenzione al parsing**: i primi 5 campi sono separati da virgola, il 6° (transazioni, separate da `::`) può essere quotato.
- Validazione di un blocco = `index == prev.index + 1` **e** `prev_hash == hash(prev)`. (Il proof-of-work è finto: nessun target di zeri — la validità è solo il *linkage*.)
- Deve produrre hash **identici** al Workstream A (coerenza C↔Bash).

### C — Layer IPC + protocollo messaggi
- Implementa il meccanismo scelto in §1.1: setup/teardown canali, `ipc_send`/`ipc_recv`, funzione di broadcast.
- Definisce le `struct` messaggio (`messages.h`) concordate in §1.3.
- È il pezzo da cui dipendono D, E, F → va consegnato presto, anche solo come interfaccia + stub, così gli altri ci lavorano contro.

### D — Processo Node
- Riceve blocchi, li valida (via B), li appende, li propaga agli altri node.
- **Consensus/anti-fork**: serializzare l'accettazione dei blocchi in modo che il fork non nasca (lock globale su chi appende / node coordinatore / ordinamento messaggi). Questa è la scelta di sincronizzazione centrale, da giustificare nel report.
- Edge case: blocco con indice troppo avanti (buco nella chain) → rifiuto; due blocchi quasi simultanei → accettarne uno solo, coerentemente.

### E — Processo Miner
- Riceve transazioni (coda), costruisce il blocco candidato.
- Loop di mining **finto**: `sleep(1–5s)` + `random()` con probabilità `1/difficulty`.
- **Il `sleep` deve restare interrompibile**: non un `sleep(5)` monolitico. O sleep a piccoli passi con check IPC non-bloccante tra i passi, o un signal che interrompe il `sleep` (ritorna con `EINTR`). Da giustificare.
- **Abort su blocco nuovo**: se arriva un blocco dai node che rende stale il lavoro, abortisce e ricomincia. Meccanismo (signal vs check non-bloccante) da scegliere e difendere.
- Broadcast del blocco minato ai node.

### F — Processo Client
- Genera transazioni random valide (regex `^[A-Za-z0-9]+ pays [A-Za-z0-9]+ [1-9][0-9]* coins$`).
- Le sottomette ai miner a frequenza `transaction_frequency` (da CLI).
- Il pezzo più piccolo — buon primo incarico per chi è meno a suo agio col C.

### G — Bootstrapper + CLI
- `./blockchain <num_nodes> <num_miners> <num_clients> [freq] [difficulty] [state.csv]`.
- Arg parsing, `fork()`/`exec` di N node + M miner + K client, ognuno logga su `nome-PID.log`.
- Il parent resta vivo e offre la CLI: `submit`, `request blockchain/block`, `save`, `pause`, `resume`, `stop`.
- `pause`/`resume` = `SIGSTOP`/`SIGCONT` a tutti i figli. `stop` = terminazione pulita. Cleanup delle risorse IPC.
- È l'integratore: si chiude bene solo quando D/E/F esistono.

### X — Trasversale (di tutti, o a rotazione)
- **Build system**: `Makefile` + `build.sh` allineati (stessi target `build`/`clean`/`run`/`test`), compilazione di `utils/`, `.PHONY` sui target, flag IPC (`-lrt`, `-lpthread`). **Da sistemare subito** perché blocca la compilazione di tutti.
- **Error handling + edge case**: file vuoto/solo header, CSV corrotto, crash di un processo senza hang, `--verify` con errori *specifici* per blocco.
- **Report** (`report.pdf`, ≤5 pagine) e **`SCELTE.md`** (vedi §5).

---

## 3. Divisione suggerita (adattatela)

Per un gruppo di 3 persone, split bilanciato che minimizza i conflitti:

- **Persona 1 — "Dati & Crypto"**: Workstream **A + B**. Tutto testabile in isolamento, parte subito, nessuna dipendenza. Possiede anche il fix del build system (X-build), che sblocca gli altri.
- **Persona 2 — "IPC & Consensus"**: Workstream **C + D**. La parte concettualmente più difficile (sincronizzazione, anti-fork). Definisce il protocollo messaggi con cui gli altri parlano.
- **Persona 3 — "Processi & Orchestrazione"**: Workstream **E + F + G**. Il runtime: miner, client, bootstrapper e CLI.

Report, `SCELTE.md` e test degli edge case: **ognuno scrive la parte del proprio pezzo**, poi si assembla.

Se siete in 2: Persona 1 prende A+B+X, Persona 2 prende C+D+E+F+G, e G lo si fa insieme come integrazione finale.

---

## 4. Ordine temporale (fasi e milestone)

Le fasi hanno dipendenze: alcune cose sbloccano le altre. Mappate le fasi sulla vostra **data d'esame scelta** (attempt su Moodle) — la deadline hard è ~5 giorni prima dell'esame.

**Fase 0 — Fondamenta (tutti insieme, subito)**
- [ ] Chiudere le 3 decisioni di §1 (IPC, hash 5/6 campi, protocollo+error code).
- [ ] Sistemare il build system (Makefile+build.sh coerenti, `.PHONY`, compila `utils/`).
- [ ] `.clang-format` condiviso nella repo.
- ▸ *Milestone*: `make build` e `./build.sh build` compilano tutto pulito da zero.

**Fase 1 — Layer testabili in isolamento (parallelo)**
- [ ] A: crypto + `blockchain.sh` → `--verify initial_state.csv` OK, `--merkle` col target giusto.
- [ ] B: block/chain/CSV → load+save del genesis round-trip, hash coerenti col C.
- ▸ *Milestone*: hash e merkle identici tra C e Bash, verificati su `initial_state.csv`.

**Fase 2 — IPC (sblocca i processi)**
- [ ] C: canali + protocollo messaggi + stub interfaccia.
- ▸ *Milestone*: due processi di test si scambiano un messaggio.

**Fase 3 — Processi (parallelo, contro l'interfaccia IPC)**
- [ ] D: node valida/appende/propaga + consenso.
- [ ] E: miner mina/broadcast/abort.
- [ ] F: client genera/submit.
- ▸ *Milestone*: un client → un miner → un node, un blocco viene minato e appeso.

**Fase 4 — Integrazione**
- [ ] G: bootstrapper spawna N/M/K + CLI completa + pause/resume/stop + cleanup.
- ▸ *Milestone*: `./blockchain 3 5 10 1 12 initial_state.csv` gira, la CLI risponde, la chain cresce.

**Fase 5 — Robustezza (edge case)**
- [ ] Tutti gli edge case del PDF: file vuoto, indice troppo avanti, CSV corrotto, crash processo, blocchi simultanei, pause+resume.
- [ ] `--verify` con errori specifici per blocco.
- ▸ *Milestone*: il sistema regge ogni edge case senza hang né crash.

**Fase 6 — Consegna (spesso sottovalutata, vale il 70%+10%)**
- [ ] `report.pdf` (≤5 pagine).
- [ ] `SCELTE.md` completo → prove orali.
- [ ] Test finale su **Ubuntu 24.04 pulito** (o il fisso Linux nativo).
- [ ] Packaging `tar.gz` nel formato esatto.
- ▸ *Milestone*: archivio pronto e testato su macchina pulita, orale provato.

---

## 5. La parte finale (non lasciarla all'ultimo)

### `SCELTE.md` — il file che vince l'orale
Per **ogni decisione di design**, una voce con questo schema:

> **Decisione** → **Scelta fatta** → **Alternative viste a lezione** → **Perché questa** → **Trade-off accettato** → **Domanda probabile all'orale + risposta**

Ognuno compila le voci del proprio pezzo *mentre* lo scrive. Esempi di decisioni che ci finiranno di sicuro: meccanismo IPC, anti-fork dei node, abort del miner (signal vs check), hash 5/6 campi, `.PHONY` nel Makefile, lock ordering nelle sezioni critiche.

### `report.pdf` (≤5 pagine)
Struttura consigliata: descrizione del sistema → scelte di design (IPC, consenso, concorrenza) con giustificazione → gestione errori/edge case → note. Paginazione stretta: 5 pagine sono poche, niente fronzoli.

### Packaging finale
Archivio `Cognome1_Cognome2_Cognome3.tar.gz` che contiene:
```
report.pdf
code/
  ├── Makefile  (o build.sh)
  └── ... tutto il sorgente ...
```
Testarlo scompattandolo su una macchina Ubuntu pulita e lanciando `make build` + `make run`: se non compila lì, non compila per il prof.

---

## 6. Checklist di consegna (ultimo controllo)

- [ ] `make build`/`clean`/`run` funzionano; `clean` pulisce anche le risorse IPC.
- [ ] `build.sh build`/`clean`/`run` fanno lo stesso.
- [ ] `blockchain.sh --verify/--hash/--merkle` corretti sugli edge case (file vuoto, solo header).
- [ ] Ogni processo logga su `nome-PID.log`.
- [ ] Error code condivisi C↔Bash con stessi valori.
- [ ] Nessun binario/log/file spurio nell'archivio.
- [ ] Compila e gira su Ubuntu 24.04 pulita.
- [ ] `report.pdf` ≤ 5 pagine, dentro l'archivio.
- [ ] Archivio nominato coi cognomi, formato `.tar.gz`.
- [ ] Ognuno sa difendere il proprio pezzo all'orale.
