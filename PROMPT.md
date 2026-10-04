# HyperScanEmu: prompt di analisi di partenza

> Nome provvisorio. Questo file è il punto di partenza della prima sessione: contesto,
> ipotesi da verificare e la ricognizione da fare **prima** di scrivere l'emulatore.

## Obiettivo

Un emulatore Mattel HyperScan nostro, indipendente da MAME, che faccia girare i giochi
retail in modo plausibile: grafica corretta, **audio**, gameplay e **velocità** giusti.
Fedeltà al ciclo = passo successivo, non obiettivo iniziale (però il timing deve essere
abbastanza vero da non rallentare i giochi). Licenza MIT. Utile anche come strumento di
sviluppo per HS-LBA.

**Perché:** MAME upstream marca il driver `MACHINE_NOT_WORKING | MACHINE_NO_SOUND`; l'audio
lo abbiamo scritto noi (SPU, aprile-maggio 2026, video YouTube "First Ever HyperScan
Emulated Audio") e va ri-patchato a ogni versione; i giochi retail rallentano.

## Cosa esiste già (tutto locale)

| Cosa | Dove |
|-|-|
| Fork MAME con le nostre 3 patch (SPU, rotate, RFID) | `D:\Homebrew6\mame-hslba`, branch `hs-lba-0287`, binario `hyprscan.exe` |
| Pipeline matura (LBA1 su HyperScan) e memorie | `D:\Homebrew6\HS-LBA` (README, DEVLOG, `docs/`, `tools/m*.lua` harness) |
| Pipeline SPU/RFID originale | `D:\Homebrew2\hyperscan-emulator-master` (`spg290_spu.cpp` = nostro, BSD-3) |
| Datasheet e manuali | `D:\Homebrew2\hyperscan-emulator-master\docs\`: `spg290.pdf`, `s_core7.pdf`, `SPCE3200*.pdf`, `instruction_table16/32.txt`, schemi 1-3 |
| Manuali S+core | `D:\Homebrew6\HS-LBA\docs\`: `S+core7_pro.pdf`, `S+core IDE.chm` |
| SDK (registri, MP3Drv.c, ISR) | `D:\Homebrew2\hyperscan-emulator-master\PPCSDK\HyperScan-SPG29x-SDK-main` |
| Dischi retail | `...\hyperscan-emulator-master\ISO\`: IWL (bin/cue), X-Men (estratto, con `HYPER.EXE`) |
| BIOS | `J:\Users\utente\Downloads\roms\hyprscan\` (`hyperscan.bin` flash 1 MB, `spg290.bin` ROM interna 32 KB) |

Memorie da leggere prima: `C:\Users\utente\.claude\projects\d--Homebrew6-HS-LBA\memory\`
(soprattutto `hyperscan-interrupts`, `hyperscan-memory-map`, `mame-score-missing-rotates`,
`perf-baseline-m4`, `hyperscan-rfid-card-wiring`, `hslba-build-invocation`) e
`...\d--Homebrew2-hyperscan-emulator-master\memory\` (`feedback_mame_score_timing`,
`project_spu_roadmap`, `project_silicon_relics_sequel` = mappa hardware).

## Il quadro (dall'analisi del 2026-10-04)

MAME qui regala poco, a differenza del VIS (dove riusa un PC AT intero). Driver e device,
tutti BSD-3 (portabili in MIT con attribuzione):

| Componente | Righe | Stato |
|-|-|-|
| CPU S+core 7 `src/devices/cpu/score/score.cpp` | 1487 | `m_icount -= 6` flat (FIXME); rotate aggiunte da noi; vari `unemulated_op` |
| PPU `spg290_ppu.cpp` | 560 | TODO: compressione H/V, flip caratteri, glitch vari |
| CD servo `spg290_cdservo.cpp` | 415 | `// TODO: seek time` (seek gratis) |
| I2C, timer | 144 + 156 | ok |
| Driver `src/mame/tvgames/spg29x.cpp` | 654 | JPG (0x0802xxxx) e blocchi DMA (APBDMA, BLNDMA, LDMDMA...) commentati |
| SPU | nostra | DAC clock 54 MHz, FIFO ping-pong, 24 voci ADPCM |

Sappiamo già più di MAME su: clock DAC, semantica interrupt (linee = fronti in MAME,
contesto ISR dell'SDK rotto), cablaggio RFID, costo reale dei seek (100-200 ms), rotate.

## Ipotesi da verificare (in quest'ordine)

### H1: il rallentamento è timing emulato, non CPU dell'host
Flat 6 tick/istruzione a 108 MHz = ~18 MIPS emulati; un S+core con cache reale si avvicina
a 1 IPC. Se è così, i giochi rallentano nel tempo emulato anche con MAME al 100%.
- Esperimento: lanciare un retail nel fork, guardare la % di velocità di MAME durante il
  rallentamento. 100% → timing emulato; <100% → host.
- Se timing: patch locale `m_icount -= 6` → 2 e → 1, confrontare. Riferimento per la
  velocità vera: video di hardware reale (stesso metodo che ha trovato i 54 MHz del DAC).

### H2: i retail partono senza BIOS (boot HLE)
Noi carichiamo `HYPER.EXE` con `-quickload` (load 0xA00901FC, entry 0xA0091000). Se anche
gli `HYPER.EXE` retail girano così (linkati staticamente con l'SDK, nessuna chiamata nel
flash), l'emulatore può avviare senza BIOS, alla Undertow: più semplice e distribuibile.
- Esperimento: quickload dell'`HYPER.EXE` di X-Men (dati accanto) e di IWL.
- Controllo statico: chiamate o salti verso gli indirizzi del flash/ROM interna nel
  disassemblato del retail.

### H3: quanta PPU mancante usano davvero i retail
- Esperimento: loggare dal fork (o da Lua) le scritture ai registri PPU, JPG e DMA durante
  boot e gameplay di IWL e X-Men. Quali funzioni non emulate vengono toccate
  (compressione, flip, JPG, DMA di blending)?

## Oracoli (non c'è un test-set istruzione per istruzione come per l'80286)
1. **MAME come confronto differenziale** (trace in lockstep), sapendo che MAME ha già
   sbagliato (rotate): una divergenza dice dove guardare, il manuale ISA e la toolchain
   ufficiale Sunplus (PPCSDK) dicono chi ha ragione.
2. **Test che si auto-verificano** scritti con la toolchain (modello: `rotate_test.c` in
   HS-LBA): girano sull'emulatore oggi, sull'hardware domani.
3. **Catture di hardware reale** (timing, audio, video).
4. **Velocità dei retail**: quando un gioco va alla velocità della cattura reale, la
   tabella dei cicli è vicina al vero.

## Deliverable della prima sessione
Un report (`RECON.md`) con: esito di H1/H2/H3 con le misure; superficie da implementare
(device e registri effettivamente usati); scelta boot HLE vs BIOS; lingua/struttura del
progetto proposta (C99 + SDL2 come Undertow?); primo milestone. **Niente codice
dell'emulatore prima del report.**

## Regole pratiche
- Build MSYS2 (fork MAME, toolchain score-elf) **da PowerShell**, non dal tool Bash: vedi
  `hslba-build-invocation`; per MAME servono `MSYSTEM=MINGW64 OS=Windows_NT` e
  `source /etc/profile`.
- Mai committare BIOS, dischi o dati estratti (nemmeno in test); `.gitignore` dal primo
  commit.
- Codice preso da MAME: mantenere header BSD-3 e attribuzione (Sandro Ronco e altri) in
  `THIRD_PARTY_NOTICES.md`.
- Riferimento di struttura e stile: `D:\Homebrew6\pc-gizwiz` (Undertow): README,
  THIRD_PARTY_NOTICES, opzioni `--trace`, `--keys`, `--screenshot`, release zip.
