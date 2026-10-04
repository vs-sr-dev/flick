# TODO: prossima sessione

Contesto: [RECON.md](RECON.md) (ricognizione del 2026-10-04, completa). Si parte con **M1: il BIOS
arriva al logo**.

## 0. Pulizia (5 minuti)

- [ ] Fork `D:\Homebrew6\mame-hslba`: è sul branch locale `recon` con modifiche non
      committate. Decidere se committarle su `recon` (consigliato: servono come oracolo in
      M1), poi `git checkout hs-lba-0287` quando si torna a lavorare su HS-LBA.
- [ ] Cancellare `D:\Homebrew6\mame-hslba\hyprscan_prev.exe` (binario vecchio, 110 MB).
- [ ] `git init` del progetto, primo commit con `.gitignore`, `RECON.md`, `TODO.md`, `tools/recon/`.

## 1. Oracolo: traccia dal fork MAME

- [ ] `HS_TRACE=file[:N]` in `score.cpp`: una riga per istruzione (PC, opcode, registri
      cambiati) e una per ogni accesso MMIO (indirizzo, valore, r/w), per le prime N istruzioni.
      Formato testuale semplice, identico a quello che scriverà il nostro `--trace`.
- [ ] Registrare la traccia di riferimento del boot BIOS (50 M istruzioni, CPI 6, senza CD).

## 2. Scheletro del progetto

- [ ] CMake + Ninja, C99, SDL2 (opzionale in headless), MSYS2 mingw64; build da PowerShell.
- [ ] `README.md` (stato, legale: niente BIOS e dischi nel repo), `LICENSE` (MIT),
      `THIRD_PARTY_NOTICES.md` (BSD-3 per il codice da MAME: Sandro Ronco e altri autori).
- [ ] CLI: `--bios`, `--headless --frames N`, `--screenshot`, `--trace`, `--cpi`.

## 3. M1 vero e proprio

- [ ] `score7.c`: port dell'interprete da MAME `score.cpp` (con le nostre rotate),
      `m_icount` sostituito da un contatore di cicli con `--cpi` (default 6 per il diff).
- [ ] Gestione eccezioni/IRQ come MAME (`EXCPVEC`, PSR, vettori), reset a `0x9F000000`.
- [ ] `bus.c`: SDRAM 16 MB con mirror, SRAM interna 16 KB, flash 1 MB su `0x10000000-0x1FFFFFFF`,
      MMIO non mappato = ultimo valore scritto + log.
- [ ] `timer.c` (6 timer, `P_TIMER_CLK_SEL`, IRQ 56), `uart.c` (TX su log), stub CKG/MIU/SFTCFG
      (`0x0807006C` = SDRAM in self-refresh, come MAME).
- [ ] PPU e TV minimi per il logo (layer bitmap, palette, fade) + vblank IRQ 53.
- [ ] Criteri di uscita: trace-diff identico sui primi 50 M istruzioni; screenshot del logo
      identico a quello di MAME allo stesso istante.

## Più avanti (da RECON.md)

- M2: CD servo + I2C/pad + PPU completa → X-Men e IWL muti; catalogo dei 29 servizi BIOS dalle tracce.
- M3: SPU, RFID, Marvel Heroes (carta big-endian `00 04 00 01`).
- M4: timing (CPI per classe, vblank per semiquadro, seek) tarato in A/B sulla **velocità di
  movimento** nei video reali; vincolo: la carta deve restare leggibile.
- M5: HLE del BIOS validato contro l'LLE; avvio rapido.
