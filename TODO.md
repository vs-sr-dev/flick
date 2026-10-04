# TODO: prossima sessione

Contesto: [RECON.md](RECON.md) (ricognizione del 2026-10-04, completa). Si parte con **M1: il BIOS
arriva al logo**.

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
- [ ] Criteri di uscita: trace-diff identico sulle prime 72 M istruzioni (`cmp` con
      `dumps/ref/bios-boot-72M.trace`); screenshot a 4,000 s identico a `bios-logo-4s.png`.

## Più avanti (da RECON.md)

- M2: CD servo + I2C/pad + PPU completa → X-Men e IWL muti; catalogo dei 29 servizi BIOS dalle tracce.
- M3: SPU, RFID, Marvel Heroes (carta big-endian `00 04 00 01`).
- M4: timing (CPI per classe, vblank per semiquadro, seek) tarato in A/B sulla **velocità di
  movimento** nei video reali; vincolo: la carta deve restare leggibile.
- M5: HLE del BIOS validato contro l'LLE; avvio rapido.
