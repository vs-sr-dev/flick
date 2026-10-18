# TODO

Contesto: [RECON.md](RECON.md) (ricognizione), [README.md](README.md) (stato e uso).

## Stato alla fine della sessione del 2026-10-05

- **M1 fatto:** CPU S+core 7, bus, timer, I2C + pad, PPU, timing schermo/TV; boot identico
  al byte alla traccia di MAME per 72 M istruzioni, logo identico al pixel.
- **M2 quasi fatto:** CD servo + immagini (cue a file singolo, ISO, MODE1/2352), SPU con
  audio SDL, lettore RFID e pannello carte (445 carte, salvataggi in `cards/`).
- **Tutti e cinque i giochi arrivano al gameplay.** Giocati dall'utente: X-Men (carte, mossa
  finale, esperienza salvata sulla carta), Marvel Heroes e Ben 10 anche **in due**.
- **Regressione:** `BIOS=... bash tools/regress.sh` = M1 + tracce sparse di 6 dischi (X-Men
  ISO e dump USE2, Spider-Man, Marvel Heroes, IWL, Ben 10): tutte identiche a MAME.
- **Timing:** preset `fast` predefinito (CPI 1, vblank per frame come MAME: X-Men a 30 fps
  come sulle console vere); `--timing mame` / F3 = MAME al ciclo.
- **Frontend:** toolbar (disco, carte, pausa, reset, timing, screenshot, scala, schermo
  intero, volume), reset e cambio disco a runtime, gamepad e secondo giocatore (la
  tastiera prende il primo giocatore libero), cartelle `cards/`, `screenshots/` e `debug/`
  accanto all'eseguibile.

### Fatto in questa sessione (dettagli nei commit)
- **Jingle del boot** (lo "zzzzzmmm" del logo che pulsa, confermato dall'utente): DAC FIFO
  avviato nell'ordine del BIOS, SoftCh = bit 12 di 0x1034, **I2C cloccato a 108 MHz**
  (col clock di MAME le 10 letture dei pad occupano un frame intero e il BIOS non nutre
  mai il jingle). `--wav FILE` registra l'audio.
- **Audio dei canali hardware:** nibble ADPCM **dal più basso** (verificato su campioni di
  IWL e Ben 10: dall'alto derivano e saturano); **ADPCM36** (ADPCM_SEL bit 15, i colpi di
  Ben 10, decodifica come MAME, scelta all'orecchio dall'utente); VolSel su tutto il mix.
- **HVGA (640x240) parte dalla riga 8 dei layer:** split-screen di Ben 10 pulito (riga 0
  dei tilemap = spazzatura, metà da 120 righe, IRQ di riga a 120). Confermato dall'utente.
- Strumenti: **Ctrl+F12 / `--debug-dump`** (RAM + stato video) e **`--render-dump`** per
  rifare un frame offline; `sched_reset()`; `--field-vblank` di nuovo funzionante.

## Prossima sessione

### 0. Nome
- [x] **Flick** (2026-10-18): eseguibile `flick.exe`, finestra, README, commenti dei sorgenti.
      Restano col vecchio nome la cartella del progetto, RECON.md/PROMPT.md (storici) e la
      patch del fork MAME; il repo GitHub nascerà già come `flick`.

### 1. Verso la prima release
- [x] `flick.ini` accanto all'eseguibile (BIOS, ultima cartella dei dischi, volume, scala,
      timing scelto con F3), **avvio senza argomenti** con dialogo per il BIOS (al primo
      avvio o se il file è sparito; controlla che sia di 1 MB) e per il disco; `flick
      disco.cue` apre il disco ("Apri con", trascinamento sull'exe). Da provare a mano.
- [x] Applicazione GUI su Windows (scelta dell'utente): niente finestra del prompt col doppio
      clic; da terminale `AttachConsole`, errori di avvio e fatali anche in un message box.
      cmd/PowerShell non aspettano la fine (bash sì): vedi README.
- [ ] Pacchetto Windows come Undertow: zip con exe, `SDL2.dll`, README, LICENSE,
      THIRD_PARTY_NOTICES; script `tools/package_release.sh`; versione nel binario.
- [ ] Repository GitHub (privato o pubblico: decide l'utente) e workflow di release.
- [ ] `tools/games.sh` punta a percorsi di questa macchina (`D:`, `J:`): renderli relativi o
      configurabili prima di pubblicare.
- [ ] Percorsi non ASCII: i file trascinati arrivano in UTF-8, `fopen` vuole ANSI.

### 2. Frontend
- [ ] Filtro TV: l'HyperScan esce solo in composito, che fonde il dithering dei giochi
      (RGB565 a trame alternate). Spento / composito (YIQ, luminanza ~3 px, crominanza molto
      più larga) / CRT (composito + scanline). Screenshot e dump restano senza filtro.
- [ ] Modo "passa la carta" (es. Maiusc+Invio): la carta resta ~2 s e si toglie da sola;
      oggi in due giocatori serve F2 fra una carta e l'altra (vedi README).
- [ ] Gamepad: aprire il pannello carte dal pad (es. View+LB), rimappatura dei tasti.
- [ ] Save state (CPU, RAM, device, scheduler; formato versionato). Il dump di debug ha
      già lo stato video.

### 3. Fedeltà, da verificare
- [ ] **IWL:** le voci escono a picco -18 dBFS (il gioco imposta VolSel 1): ascoltarle.
- [ ] **X-Men, menu:** col nuovo I2C i pad sono letti 30 volte al secondo invece di 15; se
      i menu seguono il polling potrebbero animarsi al doppio. Confronto coi video.
- [ ] Gameplay di Spider-Man e IWL col preset fast: carte, frame rate a confronto con i video.
- [ ] IRQ di riga del PPU (irq_control bit 2, irq_timing_v/h) non emulato, nemmeno da MAME;
      Ben 10 lo abilita a riga 120 in due giocatori (gestore via BIOS), senza effetti visibili.
- [ ] Canali hardware: loop (tone mode 2, LoopAddr) non implementati; senso esatto del
      coefficiente ADPCM36.
- [ ] Parità con input fra noi e MAME (formato comune a frame o cicli) per estendere la
      regressione al gameplay.

### 4. M4: timing reale
- [ ] Seek del CD (MAME istantaneo; 100-200 ms sull'hardware) e durata dei caricamenti a
      confronto con video cronometrati (X-Men; Ben 10 ha ~2 minuti di caricamenti al boot).
- [ ] Calibrazione dell'offset del servo ("Off Cal"): 61 iterazioni con ritardi software
      perché il nostro servo non converge; su hardware vero probabilmente poche.
- [ ] Il log UART del BIOS (`--log-uart`) descrive i seek (`SEEK mode`, `NowQ=`).
- [ ] Boot reale: >1 minuto dal logo HyperScan ai loghi Marvel su console vera.

### 5. Verso M5 (boot senza BIOS)
- [ ] Spider-Man legge i file senza le voci open/read/close della tabella BIOS: capire come.
- [ ] Catalogo dei 29 servizi BIOS (nomi, argomenti, risultati) dalle tracce LLE.

### Note
- `dumps/` occupa circa 15 GB: servono i riferimenti di `dumps/ref/` e
  `dumps/sparse/ref-*.trace`; il resto (`ours-*`, tracce complete vecchie) si può cancellare.
- Il Wolverine dell'utente (10 punti esperienza) è in `build/cards/xmen001.bin`: cancellare
  `build/` lo cancella.
- Dal tool Bash: `\n` e `\\` dentro heredoc e script Python arrivano trasformati; le
  stringhe C con escape vanno scritte con l'editor (o con `fputc(10, f)`). Con
  `/c/msys64/mingw64/bin` nel PATH parte il python di mingw senza Pillow: usare
  `/c/Python314/python.exe`.
- Il codice del BIOS in RAM si disassembla con `D:/Homebrew4/Pippin/mame/unidasm.exe
  -arch score7` (gp del BIOS = 0xA0045140); BIOS in `J:/Users/utente/Downloads/roms/hyprscan/`.
- Fork MAME: branch locale `recon` (HS_TRACE completo e sparso, profiling); per HS-LBA
  tornare a `hs-lba-0287`. La sua patch SPU (`HS-LBA/mame/0002-spg290-spu.patch`) ha ancora
  i nibble ADPCM dall'alto e non ha ADPCM36.
