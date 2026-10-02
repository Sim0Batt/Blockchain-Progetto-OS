# Blockchain Progetto OS

## Indice
- [Panoramica del Progetto](#panoramica-del-progetto)
- [Architettura e Componenti](#architettura-e-componenti)
- [Requisiti di Sistema](#requisiti-di-sistema)
- [Installazione e Configurazione](#installazione-e-configurazione)
- [Guida all'Uso](#guida-alluso)
- [Struttura del Codice](#struttura-del-codice)
- [Autore](#autore)

---

## Panoramica del Progetto

**Blockchain Progetto OS** è un'applicazione sviluppata per esplorare e implementare i meccanismi fondamentali alla base delle tecnologie blockchain, integrando concetti tipici dei sistemi operativi. Il progetto simula la gestione di una catena di blocchi sicura, la validazione delle transazioni, il consenso distribuito e la gestione delle risorse di calcolo.

L'obiettivo principale è fornire un ambiente di test solido e modulare per comprendere il funzionamento crittografico dei blocchi, la gestione della memoria e la concorrenza applicata alla sincronizzazione dei nodi.

---

## Architettura e Componenti

Il sistema è suddiviso nei seguenti moduli principali:

1. **Gestione dei Blocchi (Block):** Struttura dati fondamentale che contiene l'indice, il timestamp, i dati delle transazioni, l'hash del blocco precedente e il codice di validazione corrente.
2. **Catena (Blockchain):** Logica per la gestione della sequenza dei blocchi, la verifica dell'integrità della catena e la risoluzione dei conflitti basata sulla regola della catena più lunga.
3. **Meccanismo di Consenso:** Algoritmo implementato per validare l'aggiunta di nuovi blocchi e garantire la sicurezza e l'immutabilità dei dati distribuiti.
4. **Interfaccia di Comunicazione:** Modulo per la simulazione della trasmissione dei dati tra diversi nodi operanti nell'ambiente di rete.

---

## Requisiti di Sistema

Per compilare ed eseguire il progetto sono necessari i seguenti strumenti:
- Compilatore compatibile con gli standard moderni del linguaggio di programmazione utilizzato (es. GCC).
- `make` o un sistema di build equivalente per la gestione delle dipendenze e la compilazione.
- `git` per la clonazione del repository.

---

## Installazione e Configurazione

Clonare il repository sul proprio computer locale utilizzando il terminale:

```bash
git clone https://github.com/Sim0Batt/Blockchain-Progetto-OS.git
cd Blockchain-Progetto-OS
```

Procedere alla compilazione del codice sorgente utilizzando il comando di build appropriato:

```bash
make
```

---

## Guida all'Uso

Dopo aver completato la fase di compilazione, è possibile avviare l'applicazione eseguendo il binario generato (sostituisci il nome dell'eseguibile se diverso):

```bash
./blockchain_os
```

Il programma avvierà la simulazione, mostrando a schermo i log relativi alla creazione del blocco genesi, alla generazione dei blocchi successivi e alle verifiche di integrità della catena.

---

## Struttura del Codice

La struttura delle cartelle e dei file principali del progetto è organizzata (indicativamente) come segue:

- `src/`: Contiene i codici sorgente dell'applicazione e la logica dei moduli.
- `include/`: Contiene i file di intestazione e le definizioni delle strutture dati.
- `Makefile`: Script di automazione per la compilazione e la pulizia del progetto.

---

## Autore

Progetto sviluppato da [Simone Battisti](https://github.com/Sim0Batt), [Davide Basso](https://github.com/DavideBasso04) e [Nicola Avellino](https://github.com/NicolaAve)
