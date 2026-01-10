# Tris Multiplayer (Tic-Tac-Toe)

**Autori:** Oreste Leone, Giuseppe Falso

## Panoramica del Progetto
Questo progetto implementa un gioco del Tris (Tic-Tac-Toe) multiplayer basato su un'architettura Client-Server in linguaggio C (standard POSIX). Il sistema permette a più utenti di connettersi a un server centrale, accedere a una lobby, creare nuove partite o unirsi a quelle esistenti tramite un sistema di code di richiesta gestite dall'host.

## Architettura del Sistema

### 1. Modello di Comunicazione
*   **Networking:** Utilizzo di socket TCP (AF_INET, SOCK_STREAM) per garantire una connessione affidabile tra client e server.
*   **Protocollo:** Implementazione di un protocollo binario personalizzato per lo scambio di messaggi (definito in `common/include/protocol.h`). Ogni pacchetto è composto da un header (tipo messaggio + dimensione payload) seguito dai dati specifici.

### 2. Design del Server
Il server è progettato per gestire lo stato globale senza l'uso di variabili globali, incapsulando tutto in una struttura `ServerContext`.
*   **Concorrenza:** Modello "One Thread Per Client". Ogni client connesso è gestito da un thread dedicato.
*   **Gestione I/O:** I thread worker utilizzano `select()` per monitorare simultaneamente:
    *   Il socket del client (per i dati di rete).
    *   Una pipe interna (canale di notifica) per ricevere messaggi da altri thread (es. aggiornamenti lobby, inviti, fine partita).
*   **Sincronizzazione:** Utilizzo di Mutex (`pthread_mutex`) per proteggere l'accesso concorrente alle liste condivise di giocatori e partite.
*   **Strutture Dati:** Utilizzo di liste concatenate per la gestione dinamica di giocatori e partite attive.

### 3. Design del Client
*   **Interfaccia:** CLI (Command Line Interface) interattiva.
*   **User Experience:** Pulizia del terminale per aggiornamenti di vista (Lobby, Griglia di gioco) e feedback immediato.

## Funzionalità Principali

### Gestione Lobby
*   Visualizzazione in tempo reale delle partite disponibili.
*   Aggiornamento automatico della vista all'arrivo di nuovi eventi (creazione/eliminazione partite).

### Sistema di Matchmaking e Code
*   **Creazione Partita:** Un utente può ospitare una partita scegliendo il proprio simbolo.
*   **Richiesta di Partecipazione:** Gli utenti possono richiedere di unirsi a una partita. Le richieste vengono accodate in una lista FIFO specifica per quella partita.
*   **Decisione Host:** Il server inoltrata le richieste all'host una alla volta. L'host può accettare o rifiutare lo sfidante.

### Logica di Gioco
*   Sincronizzazione dello stato della griglia tra i due client.
*   Validazione delle mosse e controllo vittoria server-side.
*   Gestione della disconnessione improvvisa dell'avversario (vittoria a tavolino).

### Fine Partita (Replay)
*   Il vincitore ha la priorità: può decidere se ospitare una nuova partita (riciclando la stanza) o tornare alla lobby.
*   In caso di pareggio, entrambi i giocatori possono scegliere se ospitare una nuova partita separatamente.

## Compilazione e Esecuzione

### Requisiti
*   Compilatore GCC
*   Sistema operativo POSIX (Linux/macOS)
*   Make

### Istruzioni
1.  **Compilare il progetto:**
    ```bash
    make
    ```

2.  **Avviare il Server:**
    ```bash
    ./server_app <PORT>
    # Esempio: ./server_app 8080
    ```

3.  **Avviare il Client:**
    ```bash
    ./client_app <SERVER_IP> <SERVER_PORT>
    # Esempio: ./client_app 127.0.0.1 8080
    ```

- differenziare meglio i comandi della lobby
- implementare l'annullamento della partita quando in attesa
- quando si riceve il rifiuto dall'host perchè ha iniziato una partita, non viene printata di nuovo la lobby
- invalid move anche se era valida e viene kickato dalla partita, mentre l-altro resta running blkoccato, la partita resta bloccata in running senza essere accessibile -- tutti i messaggi di errore ti kickano dalla partita
- dopo aver ottunuto un kick, non riceve più i messaggi di update della lobby
- tutti i messaggi di errore ti kickano dalla partita
- quando vinci la partita per disconnected, non diventi propetario della partita 
- se fai nuova partita dopo che ne hai vinta una e scegli O , entrambi i giocatori segnano come waiting for opponent ( client non svuota il proprio simbolo dopo ogni partita)
