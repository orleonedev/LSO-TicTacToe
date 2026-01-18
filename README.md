# Tris Multiplayer (Tic-Tac-Toe)

<table>
  <tr>
    <td width="192">
      <img src="assets/logo-federico-II.svg" alt="Unina Logo" width="192"/>
    </td>
    <td>
      <strong>Corso:</strong> Laboratorio di Sistemi Operativi @ Università degli studi di Napoli Federico II (UNINA)<br/>
      <strong>Progetto:</strong> La Partita di Tris<br/>
      <strong>Autori:</strong> Oreste Leone N86/1980 , Giuseppe Falso N86/2941
    </td>
  </tr>
</table>

## Panoramica del Progetto

Questo progetto implementa una versione multiplayer del gioco del Tris (Tic-Tac-Toe) basata su un'architettura **Client-Server** in linguaggio C, conforme agli standard POSIX. Il sistema permette a più utenti di connettersi a un server centrale, accedere a una lobby comune, creare nuove partite o unirsi a quelle esistenti tramite un sistema di matchmaking gestito.

## Compilazione ed Esecuzione

### Requisiti di Sistema

* Sistema Operativo: Linux, macOS o qualsiasi sistema POSIX-compliant.
* Compilatore: GCC (GNU Compiler Collection).
* Build System: GNU Make.

### Istruzioni

1. **Compilazione del Progetto:**
    Per compilare sia il server che il client, eseguire il comando:

    ``` bash
        make
    ```

2. **Avvio del Server:**
    Il server deve essere avviato specificando la porta di ascolto:

    ``` bash
        ./server_app <PORT>
        # Esempio: ./server_app 8080
    ```

3. **Avvio del Client:**
    Il client richiede l'indirizzo IP e la porta del server:

    ```bash
    ./client_app <SERVER_IP> <SERVER_PORT>
    # Esempio (locale): ./client_app 127.0.0.1 8080
    ```
