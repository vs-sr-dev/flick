# TODO

Contesto: [RECON.md](RECON.md) (ricognizione del 2026-10-04). **M1 completato**; M2 quasi fatto (vedi sotto).

## 0. Pulizia — fatto (2026-10-04, sera)

- [x] Fork: strumentazione committata sul branch locale `recon` (`b5313f2f`, `269f5a34`);
      copia in [tools/mame/recon.patch](tools/mame/recon.patch) (`git diff hs-lba-0287 recon`).
      Per HS-LBA: `git checkout hs-lba-0287` e ricompilare.
- [x] `hyprscan_prev.exe` cancellato.
- [x] `git init` (branch `main`), `.gitattributes` con LF come Undertow.

## 1. Oracolo: traccia dal fork MAME — fatto

- [x] `HS_TRACE=file[:N]` in `score.cpp` (formato nel commento sopra `struct hs_trace`):
      `S` stato iniziale; `<pc> <op> reg=val…` per istruzione (op a 4 cifre se 16 bit);
      ` R4/W1 addr val` per gli accessi MMIO dell'istruzione; `L n c=` linea IRQ alzata;
      `I n c=` / `X cause` eccezione presa, stampata prima della riga dell'istruzione in cui
      avviene; `E` stato finale. MAME esce dopo N istruzioni. Indirizzi non mascherati.
- [x] Riferimento con [tools/mame/record-ref.sh](tools/mame/record-ref.sh) in `dumps/ref/`
      (fuori da git): **72 M istruzioni = 4,000 s esatti a CPI 6** (non 50 M: il logo compare
      tra 2 e 3 s, a 3-5 s è fermo, a 6 s dissolve), 1,68 GB, deterministico (due run
      identiche al byte); `bios-logo-4s.png`. In 72 M: 33 mila accessi MMIO, IRQ 39/53/56.
- Note per il port: al reset MAME mette `cr29 = 0x20000000` (JTAG off, nel driver) e
  `cr3 = EXCPVEC = 0x9F000000`; tutti gli altri registri a zero.

## 2-3. Scheletro e M1 — fatto (2026-10-04, sera)

- [x] CMake + Ninja, C99 (+ `unsigned __int128` per il tempo esatto), SDL2 opzionale;
      README, LICENSE (MIT), THIRD_PARTY_NOTICES (BSD-3 per i port da MAME).
- [x] CLI: `--bios`, `--headless`, `--seconds`, `--frames`, `--screenshot`, `--trace`,
      `--cpi`, `--log-uart`, `-v`. Finestra SDL in tempo reale (ESC esce).
- [x] `score7.c` (port 1:1), `sched.c` (regole del `device_scheduler`: tempo CPU =
      cicli × aps troncato, un evento a T è visibile dalla prima istruzione con
      ciclo ≥ floor(T/aps)), `bus.c`, `timer.c` (un evento per tick, come MAME),
      `i2c.c` + pad, `screen.c` (timing di `screen_device`, TV, fade), `ppu.c`.
- [x] **Criteri di uscita superati**: traccia di 72 M istruzioni identica al byte
      (`tools/m1check.sh`), screenshot a 4,000 s identico (0 pixel diversi).
- Velocità: 4 s emulati in 0,8 s (CPI 6), circa 5× il tempo reale.

## M2 — in corso (2026-10-05, notte)

- [x] CD servo (port), immagini cue a file singolo e ISO identiche a `cdrom_file` di MAME.
- [x] SPU (port del nostro device): DAC FIFO a timer, 24 voci, audio SDL.
- [x] Lettore RFID (port), `--card`; X-Men arriva al combattimento con Wolverine.
- [x] Tracce sparse (`file:N:K[:FROM]`) da entrambi i lati; `tools/regress.sh` =
      M1 + X-Men/Spider-Man/Marvel Heroes 60 s + IWL 90 s, tutti identici a MAME.
- [x] Pannello carte (F1/F2), catalogo di 445 carte da MAME (CC0), salvataggi in `cards/`.
- [ ] Ben 10 (`J:/.../roms/Ben 10 (USA) (USE2).cue`, volume `BT_V1_0_USE2`) e il dump
      vero di X-Men USE2 (bin/cue): aggiungerli a `tools/games.sh` e registrare i riferimenti.
- [ ] Parità con input: i tasti iniettati in MAME via Lua cadono ai confini di frame,
      i nostri `--input` a tempi in secondi; serve un formato comune (frame o ciclo).
- [ ] Spider-Man: legge i file senza le voci open/read/close; capire come (tracce).
- [ ] Catalogo dei 29 servizi BIOS con nomi e firme dalle tracce (serve a M5).
- [ ] Prestazioni: timer analitici invece di un evento per tick (oggi ~3x tempo reale).

## Più avanti (da RECON.md)

- M2: CD servo + I2C/pad + PPU completa → X-Men e IWL muti; catalogo dei 29 servizi BIOS dalle tracce.
- M3: SPU, RFID, Marvel Heroes (carta big-endian `00 04 00 01`).
- M4: timing (CPI per classe, vblank per semiquadro, seek) tarato in A/B sulla **velocità di
  movimento** nei video reali; vincolo: la carta deve restare leggibile.
- M5: HLE del BIOS validato contro l'LLE; avvio rapido.
