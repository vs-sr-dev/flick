# TODO

Contesto: [RECON.md](RECON.md) (ricognizione), [README.md](README.md) (stato e uso).

## Sessione del 2026-11-11: homebrew dell'SDK di ppcasm, chiavetta USB

Spunto: su Reddit l'autore del DOOM per HyperScan segnala schermo nero col suo homebrew
(ISO sua non ancora arrivata; riprodotto col DOOM precompilato dell'SDK).

- **Chiavetta USB** (`src/usb.c`, `--usb FILE`, `.img` trascinato sulla finestra): l'SDK
  non tocca il controller USB host (0x081C, non mappato), chiama il driver del BIOS in RAM
  a 4 indirizzi fissi (`DrvUSBH_Initial` 0xA001E5D4, `LUNInitial` 0xA001E9E4, `ReadSector`
  0xA001EEE8, `WriteSector` 0xA001EFCC). Con un'immagine attaccata `score7_step` risponde
  lì (settori da/verso il file, tempo emulato zero); senza, il BIOS gira com'è e non trova
  nulla, e DOOM resta in `while(DiskB_Initial());` prima di `TV_Init` = lo schermo nero.
  Solo col BIOS noto (CRC). `tools/mkusb.py CARTELLA OUT.img` fa l'immagine (MBR + FAT32,
  nomi 8.3: il FatFS dell'SDK è senza LFN).
- **I2C:** il flag di fine trasferimento si alza anche con l'IRQ disabilitato (MAME: solo
  se abilitato). `I2C_Read8` dell'SDK non abilita l'IRQ e aspetta il flag: DEMO, PONG e
  FlashRecovery restavano lì. BIOS e giochi l'IRQ lo abilitano: tracce invariate.
- **UART:** lo stato (0x08150010) legge 0x90 (RX vuoto, TX vuoto) invece di 0; con `--trace`
  resta 0 come MAME, perché il BIOS lo legge a ogni carattere e le tracce divergerebbero.
  Serve a `print_string` dell'SDK e a `uart_loader` (che poi aspetta un binario dalla
  seriale: lì si ferma, giustamente).
- **Provati** (ISO con solo `HYPER.EXE`, avviate dal BIOS): HelloWorld, DEMO, PONG,
  FlashRecovery (schermata dell'installer, non eseguito), USBLoaderCD (menu delle app in
  `/apps` della chiavetta, pad, avvio di `/apps/NOME/hyper.exe`), DOOM (dal suo disco e dal
  loader, WAD shareware, crea `apps/doom/savegame` sull'immagine). `USB/USBLoader` no: è
  linkato a 0xA0E01000, è il payload che il CFW carica da USB, non un `Hyper.Exe` da CD.
- **Aperto, da chiedere a ppcasm:** il DOOM dell'SDK passa `&DG_ScreenBuffer` (indirizzo
  del puntatore, 0xA0101EE4) a `TV_Buffer_Set` invece del buffer (0xA0107728): in Flick
  (che qui usa l'indirizzo così com'è, come il codice di MAME; MAME non provato) l'immagine
  esce 35 righe più in basso e 98 pixel a destra, col bordo che rientra da sinistra. Sulla
  console vera è uguale? Se no, il registro dell'indirizzo del frame buffer fa qualcosa che
  non sappiamo. Dal sorgente, DOOM non legge il pad (gira solo la demo).
- **Aperto, il ritmo del loop del BIOS e delle letture dei pad** (prove dell'utente sulla
  0.1.3-dev, nessuna modifica al codice):
  - PONG e il menu del loader non aspettano il vblank: vanno al ritmo di
    `HS_Controller_Read` (10 letture I2C). Preset fast (I2C a 108 MHz, 0,85 ms a lettura):
    98 giri/s, palla da lato a lato in 0,8 s, un passo di menu ogni 40 ms = ingiocabile,
    grilletti "hit & miss". Con 3,2 ms a lettura: 29 giri/s, un tocco di 0,1 s = un passo.
  - Il jingle suona per ogni lettura sotto ~3,3 ms (clock I2C >= 28 MHz col divisore di
    MAME): i 108 MHz erano 4 volte più del necessario. Ma all'orecchio dell'utente 3,2 ms
    e 0,85 ms sono uguali, e sbagliati entrambi:
  - **Sulla console vera i suoni del logo sono tre, in sincrono coi tre impulsi visivi
    (utente). In Flick: tre impulsi, un suono e mezzo, poi "pzzt".** Il jingle è il KVAG a
    0xA0071410 (flash 0x72214): 2,5 s, un solo "zzzmmm" (0,6 s di silenzio, salita, coda).
    Il BIOS lo suona in loop (`0xA0009BCC(0)`, id 0 in `0xA000A3D4`) dall'inizio alla fine
    dell'animazione: DAC acceso a 5,56 s, spento a 9,23 s = 3,67 s, 110 passi, uno per
    frame; picchi visivi a +0,80, +2,24, +3,70 s (44 frame l'uno). Tre suoni interi vogliono
    ~7,5 s: **l'animazione in Flick va circa al doppio**, cioè il loop del BIOS sulla
    console gira a ~15 Hz, non a 30. Torna con le letture dei pad lente (33,86 ms per le
    10 letture col clock di MAME = più di un frame).
  - Ma non basta rallentare l'I2C: a 27 MHz in Flick il jingle tace e l'animazione resta
    a 113 frame; con CPI 2-6 va da 113 a 139 frame, il periodo degli impulsi resta 44
    frame. Nel modello c'è un errore su come il BIOS aspetta (vblank, I2C, riempimento del
    FIFO) non ancora trovato: serve leggere il loop principale del BIOS.
  - Come si è misurato (script usa e getta, non nel repo): decodifica dei 6 KVAG del BIOS
    (IMA ADPCM, nibble alto prima) e log per frame di luminosità ed eventi DAC in una
    copia strumentata. Altri campioni: 0xA007C9E0
    (stereo 11025 Hz, tre colpi) suonato da `0xA0010CD8` ogni 19 tick, altra schermata.
  - **Confermato dall'utente sul vero: circa 2,5 s tra un picco e l'altro del logo
    ("forse un filino meno").** Resta da misurare il tempo della palla di PONG da lato a
    lato (attesi 2,5-3,5 s; da chiedere a ppcasm).
  - **Letto il loop del BIOS (dopo la 0.1.4). Ipotesi forte: sulla console l'interrupt
    "vblank" della PPU (0x88010084 bit 0) nel logo arriva ogni ~58 ms (3,5 semiquadri),
    non a ogni frame.** I fatti nel codice del BIOS:
    - `WaitVBlank` (0xA001528C): aspetta che il contatore di frame (gp-15468, +1 a ogni
      IRQ PPU bit 0, handler 0xA000C4D0 -> 0xA001513C) cambi rispetto all'ultimo visto, e
      **solo mentre aspetta** chiama il servizio audio (0xA000A0A8 -> 0xA000AEC0, che
      decodifica il blocco successivo del jingle; il FIQ del DAC copia nel FIFO il blocco
      pronto e ne chiede un altro: serve una chiamata ogni 46 ms).
    - `pad_poll` (0xA0019114): attesa a vuoto `while (stato != 0);` (0xA0019254) che la
      catena di 10 letture I2C (a interrupt, 0xA0018C60) finisca, poi la rilancia
      (0xA0019000). Se la catena dura più di un periodo di IRQ, ogni giro contiene un IRQ,
      `WaitVBlank` non aspetta mai e l'audio non viene più servito (stato assorbente): è
      il jingle muto col clock I2C di MAME. Quindi sulla console la catena dura meno del
      periodo dell'IRQ, e il giro del BIOS = un periodo dell'IRQ.
    - Passo del logo (0xA000F870): contatore += 2 a giro (+2 se è premuto il tasto bit 6
      dei pad: raddoppia la velocità); luminosità = onda triangolare di periodo 128/3 =
      42,7 giri; jingle a contatore 66, fine a 280 (107 giri).
    - 42,7 giri in ~2,5 s (misura dell'utente sul vero) = 58 ms a giro. **Prova (copia
      strumentata, `HS_PPU_FIELDS=3,4`): con l'IRQ ogni 3 e 4 semiquadri alternati i
      picchi visivi cadono a +1,22, +3,75, +6,32 s e i tre "zzzmmm" a 1,35, 3,85, 6,35 s:
      tre suoni in sincrono, come sul vero.** Con 4,4 (un IRQ ogni 2 frame) i picchi
      vanno a 2,9 s l'uno e si sfasano; con 3,3 a 2,15 s.
    - **Corretto dall'utente dopo la prova a finestra: sul vero il terzo suono NON viene
      tagliato; parte un po' prima rispetto all'impulso, più dei precedenti, e finisce
      appena prima del LOADING.** È il quadro a 4 semiquadri (2 frame, 66,7 ms, 15
      immagini al secondo), non a 3,5: finestra del jingle 7,20 s, picchi visivi a +1,42,
      +4,29, +7,22, suoni udibili +0,84..2,34, +3,34..4,89, +5,84..7,24 (il terzo intero,
      finisce col logo; anticipo sul picco 0,58, 0,95, 1,38 s). A 3,5 il terzo è tagliato
      a +6,31. Gli impulsi sarebbero allora a 2,87 s l'uno (l'utente aveva stimato ~2,5 a
      occhio): da ricontrollare su un video contando i fotogrammi.
    - **Confermato dall'utente a finestra (4 semiquadri + rilascio): "equivalente al
      vero", il terzo suono finisce giusto.** Il "pzzt" residuo era il gradino allo
      spegnimento del DAC (il BIOS ferma il jingle a 107 giri = 7,14 s, a ~1,97 s dentro
      il terzo campione, livello ~26%): ora l'uscita del FIFO scende a zero in ~15 ms
      (`FIFO_RELEASE` in spu.c, **nel codice**; nei giochi cambia solo quei 36 ms).
      Latenza audio di Flick dall'accensione del DAC: 233 ms = 5 mezzi buffer, di cui
      uno (46 ms) è il cuscinetto dell'anello; il resto è la logica del BIOS.
    - Primo confronto dei registri PPU: nel logo sono accesi **due layer a tutto schermo
      (tx0 0x100b, tx1 0x18b con blend che varia) e 511 sprite**; nelle schermate di
      X-Men un layer o nessuno e al massimo 100 sprite. Pista per il modello: costo =
      pixel dei layer accesi (+ blend, + sprite), periodo = frame interi necessari.
      Mancano i registri durante i combattimenti (serve uno script di input fino lì).
    - Lettura: la PPU dell'SPG290 compone l'immagine in un buffer e ci mette più di un
      frame quando la scena è pesante (640x480 con sfumatura a tutto schermo nel logo);
      MAME e Flick danno un IRQ per frame fisso. Non è la risoluzione da sola: X-Men a
      640x480 combatte a 30 fps sul vero. Serve un modello del tempo di composizione,
      da tarare su scene misurate sul vero (logo 58 ms, combattimenti di X-Men 33 ms).
    - Ne discende che anche l'I2C va rivisto con dati veri (PONG): i 108 MHz non sono
      giustificati dal jingle; resta solo il vincolo catena < periodo dell'IRQ nel BIOS.
- **Da fare:** pulsante USB nella toolbar e scelta del file dalla finestra (oggi solo
  trascinamento e `--usb`); cartella dell'host come chiavetta, senza passare da `mkusb.py`;
  la ISO vera dell'utente Reddit quando arriva.

## Sessione del 2026-10-18: Flick è pubblico

- **Nome Flick**, icona, `flick.ini`, avvio senza argomenti, exe GUI, pannello dei controlli
  (F4), percorsi UTF-8; repo pubblico https://github.com/vs-sr-dev/flick, annunciato su
  r/Hyperscan; riga nel README del profilo.
- **Release** (pre-release, zip Windows + tar.gz Linux): 0.1.0, 0.1.1 (Linux), 0.1.2
  (`--quickload`, gira hs-lba), 0.1.3 (audio di IWL). `tools/package_release.sh` e
  `tools/package_linux.sh` (da WSL).
- **IWL:** fase video al 42% -> skip delle attese sul contatore di riga (esatto); audio
  mancante -> registro di stato dei canali SPU. Confermati dall'utente.
- **Prossimo:** **core libretro** (sezione 1c, scelta dell'utente il 2026-10-24, strada per
  Android); testimonianze di partite fino in fondo (thread, issue); poi filtro TV,
  modo "passa la carta", save state, screenshot nel README con disclaimer, Mac (da una
  sessione sul MacBook), release automatiche.

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
- [x] Pacchetto Windows: `tools/package_release.sh` -> `dist/flick-0.1.0-win64.zip` (exe,
      `SDL2.dll`, README, LICENSE, THIRD_PARTY_NOTICES con la licenza zlib di SDL2,
      LICENSE-SDL2.txt); versione da `project(VERSION)` in CMake (`-dev` nelle build normali),
      `--version` e proprietà del file. Primo pacchetto preparato il 2026-10-18 (WIP).
- [x] Repository pubblico https://github.com/vs-sr-dev/flick e pre-release **v0.1.0**
      (2026-10-18, `0c8959e`, zip da `tools/package_release.sh`, SHA-256 nelle note).
- [ ] Screenshot nel README, con un disclaimer sui contenuti di proprietà dei rispettivi
      titolari (decisione dell'utente: più avanti).
- [ ] Release automatica da GitHub Actions (MSYS2) al push di un tag: per ora a mano.
- [x] Percorsi di questa macchina fuori dagli script: `tools/local.sh` (ignorato da git,
      modello in `tools/local.sh.example`) con BIOS, ROMPATH, MAME, PY e dischi; `games.sh`
      cerca `discs/<gioco>.cue` per default.
- [x] Percorsi non ASCII: tutto UTF-8 dentro, API wide su Windows (argv, `os_fopen`,
      `os_mkdir`, dialoghi, cartella dell'exe); provato con `Prova àè ñ 日本`.

### 1b. Linux e macOS
- [x] **Linux** (2026-10-18, provato in WSL Ubuntu 24.04 + WSLg): compila senza warning,
      traccia M1 identica a MAME (a meno dei CRLF del riferimento), stessa velocità.
      Cartelle: accanto all'exe se scrivibile, sennò XDG; dialoghi con zenity/kdialog (provati
      con un finto zenity); `tools/package_linux.sh` -> `flick-<ver>-linux-x86_64.tar.gz`
      (SDL2 di sistema, glibc >= 2.34). Pubblicato nella pre-release **v0.1.1** (`64138eb`).
- [ ] Linux: provarlo su un desktop vero (zenity reale, gamepad, audio PulseAudio/PipeWire),
      icona e voce `.desktop`; magari AppImage.
- [ ] macOS (da una sessione Claude Code sul MacBook dell'utente, clonando il repo): `os_exe_dir` c'è (non compilato); servono `~/Library/Application Support/Flick`,
      dialogo (NSOpenPanel o osascript), `.app` con `.icns`, firma/notarizzazione. Serve un tester.

### 1c. Core libretro (la strada per Android) - prossima sessione
Scelta dell'utente (2026-10-24) dopo due richieste di una versione Android (un commento in
portoghese, un commento a un commit su GitHub). Un core libretro porta Flick su RetroArch:
Android, macOS, Steam Deck, console; menu, file, rimappatura e shader li dà il frontend.
Su Android i core si installano solo dall'Online Updater, quindi alla fine serve il
buildbot di libretro. Un'app Android standalone (SDL2 + SAF + touch) resta possibile dopo,
sullo stesso core. `flick.exe` resta com'è.

**Passo 1, core base (una sessione):**
- [ ] `libretro.h` da libretro-common (MIT) in `src/libretro/`; nuovo `src/libretro.c`; target
      CMake `flick_libretro` (`SHARED`, opzione `HS_LIBRETRO`) con il core senza `main.c`,
      `toolbar.c`, `controls.c` e le parti SDL di `os.c`/`input.c`. Prefisso `lib` tolto su Windows.
- [ ] `retro_get_system_info`: nome "Flick", versione da CMake, estensioni `cue|iso|bin`,
      `need_fullpath = true` (cdimg legge dal file).
- [ ] `retro_get_system_av_info`: 640x480 massimo, misura reale da `screen_width/height()`
      a ogni frame (HVGA 640x240 per Ben 10 in due giocatori: aspect 4:3 comunque);
      `fps` 60 da verificare col periodo di `screen.c`; `sample_rate` = `SPU_OUT_RATE` (44100).
- [ ] `RETRO_ENVIRONMENT_SET_PIXEL_FORMAT` XRGB8888 (è già il formato di `g_frame`).
- [ ] BIOS: `hyperscan.bin` dalla cartella system (`GET_SYSTEM_DIRECTORY`), con il controllo
      sul dump noto di `37b05f7`; se manca, messaggio (`SET_MESSAGE`) e `retro_load_game`
      fallisce. Dichiararlo con `SET_FIRMWARE`/file `.info` per il menu "System Files".
- [ ] `retro_run`: un frame = `machine_run_until(cycles + CPU_CLOCK / 60)` come il ramo senza
      audio del loop SDL ([main.c](src/main.c), ~riga 770), poi `spu_sync` + `spu_take` ->
      `audio_sample_batch` (~735 campioni stereo per frame), poi `video_refresh(g_frame, w, h,
      sizeof(g_frame[0]))`. Il preset di timing resta `fast`.
- [ ] Input: RetroPad -> pulsanti HyperScan con `input_set`/`input_joy` ([input.c](src/input.c)),
      due porte; descrittori (`SET_INPUT_DESCRIPTORS`) con i nomi di `input_button_name`.
- [ ] Log via `GET_LOG_INTERFACE` al posto di `fprintf(stderr)`; nessun `exit()` nel core
      (gli errori fatali di oggi vanno trasformati in ritorni d'errore).
- [ ] Stato globale: ~76 variabili statiche/globali. `retro_unload_game` + `retro_load_game`
      di un altro disco nello stesso processo deve ripartire pulito: controllare che
      `machine_reset` (+ `sched_reset`, card, cards, ui) azzeri tutto; ricaricare due
      giochi di fila e confrontare con un avvio a freddo.
- [ ] Verifica con il RetroArch dell'utente, **`F:\RetroArch 2\`** (portable, exe del luglio
      2023 ~1.15; cartelle relative `:\cores`, `:\system`, `:\saves`; log verboso già attivo).
      Caricare il core dalla cartella di build senza copiarlo in `cores\` (`retroarch.exe -L
      <build>\flick_libretro.dll <disco>.cue --verbose`); il BIOS va copiato in `system\` come
      `hyperscan.bin`. Non toccare `retroarch.cfg` né gli altri core: è l'installazione che
      l'utente usa. Provare i cinque giochi; un frame con `--screenshot` dello standalone e lo
      stesso istante dal core devono coincidere (stesso preset, nessun input).
- [ ] **Velocità su Android, prima di andare oltre:** compilare il core per arm64-v8a con
      l'NDK (solo 64 bit: serve `unsigned __int128`), caricarlo in RetroArch e misurare X-Men
      in combattimento e IWL (fase t=56-60 s). Telefoni: **Poco X3 Pro** dell'utente (M2102J20SG,
      Snapdragon 860, core A76 a ~2,96 GHz: fascia media di oggi) e **Samsung S24 Ultra** del padre (8 Gen 3,
      fascia alta: chiedere prima, è un'installazione da origini sconosciute). Serve il
      RetroArch dell'APK di retroarch.com: quello del Play Store non carica core copiati a mano.
      Se non regge, il passo successivo è l'interprete (`score7.c`), non il frontend.
- [ ] (più avanti) iPhone 15 Pro Max dell'utente: RetroArch per iOS compilato con il core dalla
      sessione sul MacBook, installato con Apple ID gratuito (firma di 7 giorni). Niente JIT
      necessario (interprete). Sull'App Store solo se libretro include il core.

**Passo 2, carte e salvataggi:**
- [ ] Pannello carte col pad: un tasto del RetroPad (es. L3 o Select+L) apre il pannello di
      [ui.c](src/ui.c), d-pad/A/B guidano `ui_key`; l'overlay ARGB di `ui_render` composto
      sopra `g_frame` prima di `video_refresh`. Ricerca per testo senza tastiera: lista
      filtrata per gioco (già da `cards_detect_game`), scorrimento a pagine.
- [ ] Carte salvate (`cards/*.bin`) nella cartella saves (`GET_SAVE_DIRECTORY`/flick/).
- [ ] Opzioni del core (`SET_CORE_OPTIONS_V2`): timing fast/mame, volume se serve.
- [ ] Cambio disco: interfaccia disk control (`SET_DISK_CONTROL_EXT_INTERFACE`), per ora un
      disco per gioco; reset (`retro_reset` -> `machine_reset`).

**Passo 3, save state** (vale anche per lo standalone, vedi 2. Frontend):
- [ ] `retro_serialize_size`/`retro_serialize`/`retro_unserialize`: CPU, SDRAM/SRAM, device,
      scheduler (eventi in tempo atto), posizione nel CD, SPU; formato versionato. Con
      questo RetroArch dà anche rewind e run-ahead.

**Passo 4, distribuzione:**
- [ ] `Makefile.libretro` (preferito dal buildbot; CMake accettato), `flick_libretro.info`
      (nome, estensioni, firmware `hyperscan.bin` con MD5), sezione nel README.
- [ ] Ricetta per il buildbot / PR ai repository di libretro (licenze: MIT + BSD-3 di
      `card.c`, ok). Solo quando passi 1-2 sono provati e la velocità su Android è nota.

### 2. Frontend
- [ ] Filtro TV: l'HyperScan esce solo in composito, che fonde il dithering dei giochi
      (RGB565 a trame alternate). Spento / composito (YIQ, luminanza ~3 px, crominanza molto
      più larga) / CRT (composito + scanline). Screenshot e dump restano senza filtro.
- [ ] Modo "passa la carta" (es. Maiusc+Invio): la carta resta ~2 s e si toglie da sola;
      oggi in due giocatori serve F2 fra una carta e l'altra (vedi README).
- [x] Rimappatura: pannello dei controlli (F4, pulsante sulla barra), tastiera e controller,
      salvata in `flick.ini` (2026-10-18).
- [ ] Gamepad: aprire i pannelli dal pad (es. View+LB per le carte).
- [ ] Save state (CPU, RAM, device, scheduler; formato versionato). Il dump di debug ha
      già lo stato video.

### 3. Fedeltà, da verificare
- [x] **IWL, audio mancante** (2026-10-18): non erano i loop. IWL suona sul canale 0 (ADPCM
      one-shot, modo 0x9037) e dopo l'avvio legge lo stato dei canali (0x103c); il fork
      restituiva l'ultimo valore scritto, il gioco vedeva il canale fermo, lo spegneva e lo
      rifaceva: ~60 campioni e poi niente. Ora stato e flag di stop (0x102c) come SPG2xx
      (preset fast). La traccia vocale del video iniziale c'è, "qualità perfetta" (utente).
      Gli altri quattro giochi: WAV identico al byte; regressione identica.
- [x] IWL: sentire il resto (menu, incontri): partita intera, suono perfetto ovunque, e tutte
      le carte provate riconosciute (utente, 2026-10-18). Voci a picco -18 dBFS (VolSel 1).
- [ ] **X-Men, menu:** col nuovo I2C i pad sono letti 30 volte al secondo invece di 15; se
      i menu seguono il polling potrebbero animarsi al doppio. Confronto coi video.
- [ ] Gameplay di Spider-Man e IWL col preset fast: carte, frame rate a confronto con i video.
- [x] **IWL a scatti (2026-10-18):** il lettore video aspetta la riga 240 leggendo il contatore
      di riga del PPU (0x94); con l'I/O nel ciclo lo skip degli idle loop non scattava e la
      fase video andava al 42%. Ora le letture "ferme fino a un istante" (sched_io_steady)
      permettono lo skip: 2500-3300%, uscite identiche al byte (PNG, WAV, ciclo finale).
- [ ] IWL, t=56-60 s: ritardo software a contatori in RAM (a00196f0), 76-108% di velocità;
      lavoro vero (a0024c88, decompressione?) a ~140%. Un PC lento potrebbe scattare lì.
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
