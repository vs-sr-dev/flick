# TODO

Contesto: [RECON.md](RECON.md) (ricognizione), [README.md](README.md) (stato e uso).

## Stato alla fine della sessione del 2026-10-04/05

- **M1 fatto:** CPU S+core 7, bus, timer, I2C + pad, PPU, timing schermo/TV. Le prime 72 M
  istruzioni del boot sono identiche al byte alla traccia di MAME; logo identico al pixel.
- **M2 quasi fatto:** CD servo + immagini (cue a file singolo, ISO), SPU con audio SDL,
  lettore RFID, pannello carte F1/F2 (445 carte dal catalogo CC0 di MAME, salvataggi in
  `cards/<id>.bin`), input da tastiera e da script.
- **Regressione:** `BIOS=... bash tools/regress.sh` = M1 + tracce sparse di X-Men,
  Spider-Man, Marvel Heroes (60 s) e IWL (90 s): tutte identiche a MAME (circa 6 minuti).
- **Timing:** preset `fast` predefinito = CPI 1, vblank per frame come MAME, decoder della
  carta per rapporti, skip esatto dei loop d'attesa, timer analitici. X-Men combatte a
  **30 fps, confermato dall'utente sui video reali** ("praticamente identico"); `--timing
  mame` / F3 = MAME al ciclo. Host: circa 280% del tempo reale in combattimento.
- **Gameplay verificato dall'utente (X-Men):** audio perfetto; carta personaggio, carta
  della mossa finale eseguita; dopo il fight il gioco riscrive la carta (+10 esperienza,
  byte 0x32 = 0x0a), salvata in `cards/xmen001.bin` e riconosciuta da Card Evaluation.

## Sessione del 2026-10-05 (mattina): jingle del boot

- [x] **Il boot ora suona** (preset `fast`). Non era un canale diverso: è il DAC FIFO, con
      un loop KVAG (ADPCM, 22050 Hz, 2,5 s) che il BIOS decodifica in RAM. Tre cause:
      1. il BIOS abilita la FIFO (0x1088 = 0x4007) *prima* di scrivere indirizzo e sample
         clock: l'avvio veniva scartato (clock 0). Ora parte alla scrittura del clock;
      2. 0x1034 = 0x10cb non ha il bit 9 che il fork prende per SoftCh; nel layout SPG2xx
         (VolSel in 7:6, già usato) SoftCh è il bit 12, che hanno sia il BIOS sia i giochi
         (0x168b). Verificato: tutti e quattro i giochi scrivono solo 0x168b/0x10cb;
      3. il BIOS decodifica il jingle solo mentre aspetta il vblank (0xA001528C chiama il
         servizio audio 0xA000A0A8), ma ogni frame legge 10 registri dei pad via I2C con
         divisore 0x258: col clock di MAME (27 MHz / 4 / 601) sono 33,86 ms, un frame
         intero, e l'attesa del vblank non attende mai. Con l'I2C a 108 MHz (clock di
         sistema: MAME dà 27 MHz a tutte le periferiche) le letture prendono 1/4 di frame.
- [x] `--wav FILE` registra l'audio (anche headless).
- Verifiche: regressione identica a MAME (il preset `mame` non cambia); X-Men in
  combattimento resta a 30,0 fps col nuovo I2C.
- [ ] **Da ascoltare (utente):** il jingle nella finestra (o nel WAV inviato) a confronto
      con un video del boot reale: durata, loop, pausa iniziale di ~0,7 s.
- [ ] **Da controllare coi video:** col nuovo I2C X-Men legge i pad 30 volte al secondo
      nei menu invece di 15; se i menu sono legati al polling, potrebbero animarsi al
      doppio. In combattimento nessun cambiamento.
- [ ] La calibrazione dell'offset del servo ("Off Cal") fa 61 iterazioni con ritardi
      software (~130 ms a CPI 1) perché il nostro servo non converge mai: su hardware
      vero probabilmente ne bastano poche (M4, insieme ai seek).

## Prossima sessione

### 1. Comodità del frontend
- [ ] **Toolbar in cima alla finestra** per le opzioni: aprire un disco, carte (= F1),
      togliere la carta, timing fast/mame, schermo intero, scala della finestra, volume,
      screenshot, pausa, reset; più avanti save state. Decidere fra una barra disegnata da
      noi (stesso font e stile del pannello carte, portabile, a scomparsa quando il mouse
      esce) e un menu Win32 nativo via `SDL_SysWMinfo` (solo Windows). Proposta: disegnata
      da noi.
- [ ] Cartella `cards` accanto all'eseguibile, non nella cartella corrente (oggi finisce
      dove si lancia il programma). Spostare l'attuale `cards/xmen001.bin` (10 punti
      esperienza dell'utente) nella nuova posizione.
- [ ] Schermo intero (Alt+Invio), ridimensionamento con proporzioni, scala intera.
- [ ] Gamepad (SDL GameController) e secondo giocatore (porta I2C 1).
- [ ] Save state (CPU, RAM, device, scheduler; formato versionato).
- [ ] Avvio senza argomenti: dialogo per scegliere BIOS e disco, ricordati in un file di
      configurazione.

### 2. Copertura dei giochi
- [ ] **Ben 10** (`J:\Users\utente\Downloads\roms\Ben 10 (USA) (USE2).cue`, volume
      `BT_V1_0_USE2`, quinto gioco, mai provato) e il dump vero di **X-Men USE2**
      (`X-Men (USA) (USE2).cue`): aggiungerli a `tools/games.sh`, registrare i riferimenti
      con `tools/mame/record-games.sh`, provarli col preset fast.
- [ ] Gameplay di Spider-Man, Marvel Heroes, IWL e Ben 10 col preset fast: carte,
      frame rate a confronto con i video (un gioco VGA interlacciato a 60 fps smentirebbe
      il modello "CPI 1 + vblank per frame").
- [ ] Parità con input fra noi e MAME: MAME inietta i tasti via Lua ai confini di frame,
      `--input` a tempi in secondi; serve un formato comune (frame o ciclo) per estendere
      la regressione al gameplay.

### 3. Distribuzione
- [ ] Pacchetto Windows come Undertow: zip con exe, `SDL2.dll`, README, LICENSE,
      THIRD_PARTY_NOTICES, licenze; script `tools/package_release.sh`; versione nel binario.
- [ ] Repository GitHub (privato o pubblico: decide l'utente) e workflow di release.
- [ ] Le opzioni di default di `tools/games.sh` puntano a percorsi di questa macchina:
      renderle relative a `discs/` prima di pubblicare.

### 4. M4: timing reale
- [ ] Tempo di seek del CD (MAME: istantaneo; misurato 100-200 ms sull'hardware) e durata
      dei caricamenti di X-Men a confronto con un video cronometrato.
- [ ] Il log UART del BIOS (`--log-uart`) descrive i seek (`SEEK mode`, `NowQ=`): utile
      per il modello.
- [ ] Boot reale: >1 minuto dal logo HyperScan ai loghi Marvel su console vera.

### 5. Verso M5 (boot senza BIOS)
- [ ] Spider-Man legge i file senza le voci open/read/close della tabella BIOS: capire
      come dalle tracce.
- [ ] Catalogo dei 29 servizi BIOS (nomi, argomenti, risultati) dalle tracce LLE.

### Note
- `dumps/` occupa circa 14 GB (riferimenti completi a 72 M e 180 M istruzioni, tracce
  sparse): i riferimenti servono alla regressione, il resto si può cancellare.
- Dal tool Bash: `\n` dentro heredoc e one-liner Python arriva come a capo vero; le
  stringhe C con `\n` vanno modificate con l'editor.
- Fork MAME: branch locale `recon` (HS_TRACE completo e sparso, profiling); per HS-LBA
  tornare a `hs-lba-0287`.
