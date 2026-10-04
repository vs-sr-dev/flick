# HyperScanEmu: ricognizione (RECON)

Sessione del 2026-10-04. Base: fork MAME `vs-sr-dev/mame` @ `hs-lba-0287` più una patch
di strumentazione locale (branch `recon`, salvata in `tools/mame/recon.patch`).
Titoli usati: **IWL** (USE2, bin/cue), **X-Men** (estratto, rimpacchettato in ISO),
**Marvel Heroes** (USE2, Internet Archive). BIOS: `hyperscan.bin` (flash 1 MB).

## In breve

| Ipotesi | Esito |
|-|-|
| **H1**: il rallentamento è timing emulato, non CPU dell'host | **Confermata.** MAME gira al 170-240% sull'host mentre X-Men combatte a 15 fps. Con CPI 3 o 2 sale a 30 fps, con CPI 1 resta a 30 per colpa di un secondo difetto (vblank a 30 Hz in modo interlacciato); correggendo anche quello arriva a 60 fps. |
| **H2**: i retail partono senza BIOS | **Falsa così com'è.** Tutti e tre i giochi chiamano il BIOS attraverso una tabella di salti in RAM a `0xA0000800` (file da CD, carta, pad, timer, init). Ne usano 29 voci distinte su 67. Un boot HLE resta possibile, ma significa reimplementare quei 29 servizi. |
| **H3**: quanta PPU mancante usano i retail | **Quasi niente.** Mai toccati JPG, APBDMA, LDMDMA, BLNDMA, SD o USB. Compressione H/V, scroll per riga e flip dei caratteri mai attivati. Il sottoinsieme PPU usato è quello che MAME implementa già. |

Proposta: **C99 + SDL2**, CMake/Ninja come Undertow, licenza MIT. **Boot LLE dal BIOS per
la prima milestone**, con HLE dei 29 servizi come obiettivo successivo, validato
contro l'LLE.

---

## Metodo

- **Strumentazione nel core CPU del fork** (`score.cpp`, attivata da variabili d'ambiente):
  - `HS_CPI=n` fissa il costo per istruzione (in MAME 6, fisso);
  - `HS_CPI_AT=t:n` lo cambia a run avviata, così si arriva al combattimento a CPI 6
    (unico valore a cui la carta viene letta, vedi sotto) e poi si misura lo stesso
    combattimento a CPI diversi;
  - `HS_PROF=file` scrive per ogni secondo emulato: istruzioni, IRQ consegnati per
    vettore, chiamate alla tabella BIOS, pagine di codice calde; a fine run, un
    istogramma dei PC;
  - `HS_IDLE` / `HS_MARK` contano le istruzioni dentro i loop d'attesa (= tempo libero)
    e i passaggi per un PC (= frame di gioco al secondo).
- **Esperimento nel driver**: `HS_FIELDVBL=1` aggiunge un impulso di vblank PPU alla
  riga 262 quando il TVE è interlacciato (un vblank per semiquadro).
- **Harness Lua** (`tools/recon/recon.lua`): tap su tutte le letture e scritture
  `0x08000000-0x0824FFFF`, letture per registro al secondo (rivela i polling), cattura
  UART, istogramma degli impulsi RFID, input temporizzati, snapshot, dump della RAM bassa.
- **Analisi statica** con `score-elf-objdump` della toolchain Sunplus ufficiale
  (`-b binary -m score7 -EL --adjust-vma=…`).
- Run headless: `-nothrottle -video none -sound none`, fino a 16 istanze in parallelo.
  La carta è forgiata (120 byte; X-Men `03 00`, IWL `01 00`), con una copia per istanza
  perché più MAME non possono aprire lo stesso `.bin`.

---

## H1: timing emulato

### Misure

Clock: il BIOS scrive `P_CLK_PLLV_SEL = 7` → PLLV **108 MHz** (tabella SPCE3200 e
`SPG290_Constants.h`), `P_CLK_CPU_SEL = 0` (CPU = PLLV), AHB non diviso. Il clock di
MAME è giusto; il budget a CPI 6 è esattamente **18,0 M istruzioni/s**, misurato.

**X-Men, stesso combattimento**, frame di gioco al secondo (uscite dal loop d'attesa
`a00b7c8c`) e quota di istruzioni spese ad aspettare:

| CPI | vblank come MAME (30 Hz in VGA interlacciato) | vblank per semiquadro (60 Hz) |
|-|-|-|
| 6 (MAME) | **15 fps**, 7% libero | 15 fps, 13% libero |
| 3 | 29,6 fps, 16% | 29,9 fps, 16% |
| 2 | 30 fps, 44% | 30 fps, 44% |
| 1 | 30 fps (tetto), 72% | **59,9 fps**, 47% |

CPI 6 misurato su t=192-205; gli altri sono medie su t=210-290 dopo il cambio a t=205.
Lo Start premuto ogni 11 s mette periodicamente il gioco in pausa, ma il tempo libero
minimo resta entro 2-4 punti dalla media: pausa e combattimento costano uguale.

Velocità host di MAME in quei secondi a CPI 6: 168-240%. **La CPU dell'host non è il collo
di bottiglia**: lo è il tempo emulato.

Un frame di X-Men costa circa **0,95 M istruzioni** (a CPI 1, 60 fps col 47% libero →
8,8 ms di lavoro). Quindi:
- 60 fps richiedono CPI effettivo ≤ ~1,9 **e** vblank per semiquadro;
- 30 fps richiedono CPI ≤ ~3,7 (lavoro ≤ 2 semiquadri);
- a CPI 6 il frame dura 53 ms → 4 semiquadri → 15 fps.

**IWL** (QVGA progressivo, `TV_MODE = 0x1110`): a CPI 6 è libero l'81-85% del tempo anche
in combattimento, quindi non rallenta. Il problema è specifico dei giochi pesanti in
640×480 (X-Men e Marvel Heroes usano `0x1105`, VGA interlacciato).

### I due difetti di MAME

1. **CPI fisso a 6** (`m_icount -= 6; // FIXME`). Il manuale S+core7 (§6.5) descrive una
   pipeline con stalli hardware per dipendenza ("recommended software bubbles"), cioè
   ~1 istruzione per ciclo più stalli e miss di cache, non 6.
2. **Vblank a 30 Hz in modo interlacciato.** `tve_control_w` configura 525 righe con
   periodo raddoppiato, quindi un vblank per frame invece che per semiquadro. X-Men
   aspetta il vblank (`a00b7aec` è il corpo dell'handler: contatore, flag, commit dei
   registri sprite), perciò con questo difetto non supera mai i 30 fps.

### Un vincolo inatteso: la carta RFID

A CPI < 6 la carta **non viene più letta**: X-Men resta su "Scan your character card",
e IWL a CPI 2 e 1 non arriva mai al combattimento.
Il BIOS genera gli impulsi su `P_CSI_GPIO_SETUP` bit 1 con attese contate in istruzioni;
il device di MAME li classifica in cicli di portante a 13,56 MHz (<500 = 0,
500-999 = 1, 1000-4999 = strobe). Durate misurate:

| CPI | bit "0" | bit "1" | strobe |
|-|-|-|-|
| 6 | 150-199 | **500-549** | 1800-1949 |
| 4 | 100-149 | 350-399 | 1200-1299 |
| 3 | 100-149 | 300-349 | 1050-1199 |
| 2 | 50-99 | 250-299 | 950-1049 |

Il bit "1" a CPI 6 supera la soglia del 5%, e da CPI 4 in giù il BIOS ritenta all'infinito
(>10 mila strobe). Le durate hanno una parte fissa: le attese più lunghe sono
in parte scandite dal tick del timer. **O le soglie di MAME sono state tarate sul suo
CPI 6 (ragionamento circolare), oppure il codice RFID del BIOS (read-modify-write su
APB attraverso funzioni helper) costa davvero ~6 cicli per istruzione.** In entrambi i
casi il modello di timing del nostro emulatore deve tenere insieme carta e frame rate:
è un test di regressione naturale. Il dato vero si ottiene con un analizzatore logico sul
pin GATE di una console reale.

### Altri dati di timing

- **Boot del BIOS** fino al codice del gioco: X-Men ~25 s, Marvel Heroes ~22 s, IWL
  ~75 s emulati. Praticamente indipendente dal CPI (24-45 s per X-Men a CPI 6/2/1): è
  scandito da timer e animazioni, non dalla CPU.
- **Caricamenti di X-Men**: ~40 s di barra "LOADING" a CPI 6 con seek gratuiti. Il loop
  caldo è una copia byte-a-byte da decompressione (`a00a7204`), quindi CPU-bound: col
  CPI giusto si accorceranno molto, coi seek veri si allungheranno. Da tarare
  su una cattura reale.
- **Interrupt osservati**: 39 I2C (150-300/s), 53 vblank PPU (30 o 60/s), 56 timer
  (~2000/s: tick del BIOS a 2 kHz), 60 CD servo (~150/s durante le letture), 63 SPU FIQ
  (~22/s in X-Men = DAC FIFO a 44,1 kHz / 2048).

---

## H2: boot senza BIOS

### Cosa fanno gli eseguibili

- Il `HYPER.EXE` di **IWL** (70 KB) è solo un **loader**: apre `DAT\IWL.EXE`, lo legge in
  `0xA00A4000` (3,3 MB) e ci salta. X-Men (367 KB) e Marvel Heroes (650 KB) contengono il
  gioco completo (con zlib e FatFS linkati, quest'ultimo con codice SD inerte).
- Nessuno dei tre tocca **mai** direttamente il CD servo (`0x8806xxxx`): tutto il file
  I/O passa dal BIOS.
- Ogni servizio BIOS è uno stub `lw rX, [gp, -N]; brl rX`, seguito da una funzione che
  ripristina il `gp` del gioco. Gli slot puntano a una **tabella di trampolini in RAM,
  `0xA0000800-0xA0000B20`**: 67 voci da 12 byte (`ldis gp, 0xa004; ori gp, 0x5140;
  j <servizio>`), identica nei tre giochi. Il BIOS resta residente in
  `0xA0000000-0xA008FFFF` (gp `0xA0045140`).

### Il BIOS

Flash `SPGM029020060728` "Rev 2.1 NTSC". Al reset (`PC = 0x9F000000`, offset 0 del
flash) si copia in RAM e gira da lì. Contiene: driver CD servo, ISO9660 (`cdfs`), zlib
1.2.3, API file (`spgread/spgclose`), `memalloc`, un sistema sprite/char, USB, RFID,
menu di test di fabbrica. La ROM interna `spg290.bin` **non viene mai eseguita** (nessun PC
in `0x8B/0xAB…`): serve solo il flash.

### Servizi usati (statico, chiamate per gioco) e funzione probabile

Classificazione in base ai blocchi I/O toccati e alla frequenza di chiamata. I nomi
vanno confermati in M2.

| Voce | → BIOS | IWL | XM | MH | I/O / note | Probabile |
|-|-|-|-|-|-|-|
| 80c | a0019760 | 1 | 1 | 2 | GPIO 0x8820 | carta: init |
| 818, 824 | a001a990, a001aa18 | 1-2 | 4 | 3 | GPIO, attese su tick | carta: power on/off |
| 830, 83c, 848, 854 | a001aa44… | 1-2 | 0-4 | 0-4 | helper | carta: protocollo |
| 860, 86c | a001a7ec, a001a8a4 | 2-3 | 4 | 4-13 | PPU + timer | sync video / attesa |
| 890, 8cc, b18 | a0013xxx | 0-1 | 1 | 0-1 | nessun I/O | utility |
| **89c** | a0025f44 | 2 | 1 | 0 | CD servo, percorsi | **open file** |
| **8a8** | a00262ec | 2 | 1 | 0 | ~600/s nei caricamenti | **read** |
| **8c0** | a002664c | 2 | 1 | 0 | | **close** |
| 8f0, 8fc, 908, 914 | a0019078… | 1 | 1 | 1-3 | I2C | pad (8fc/914 ogni frame) |
| 9b0 | a0013f68 | 0 | 1 | 0 | SPU | audio |
| a1c, a28 | a001ac74, a001ae98 | 4 | 2-3 | 2-22 | nessun I/O; a28 fino a 300k/s | utility (copia/decompressione?) |
| a4c, a58, a88, a94, aa0 | a000b598… | 0-1 | 0-2 | 1-10 | timer; a94 ~480/s | tick / timer |
| ac4 | a000c0c4 | 0 | 1 | 1 | tocca tutto | init di sistema |
| b00 | a00128d0 | 0 | 1 | 0 | PPU, MIU, MIU2 | modo video |

Unione: **29 servizi**, 19-25 per gioco. In gameplay le chiamate sono poche (X-Men ~30/s
più ~480 letture di tick; IWL ~80/s), quindi il costo di un HLE sarebbe trascurabile.
Marvel Heroes usa anche `0xA0000B18`, che gli altri no: ogni gioco nuovo può
allargare la lista.

### Scelta del boot

| | LLE (si esegue il BIOS) | HLE (servizi nativi) |
|-|-|-|
| Richiede | `hyperscan.bin` (come MAME) | niente |
| Lavoro | solo hardware, che va emulato comunque | + ISO9660, protocollo carta, pad, timer, init, gestione IRQ e lo stato RAM che il BIOS lascia |
| Rischio | basso: semantica vera | medio: 29 contratti da ricostruire |
| Boot | 22-75 s di intro (accorciabile in seguito) | istantaneo |

**Raccomandazione: LLE per le prime milestone.** Il BIOS fa anche da **oracolo per
l'HLE**: con l'LLE funzionante si registrano argomenti e risultati di ogni chiamata
alla tabella e poi si sostituiscono i servizi uno alla volta, confrontando le tracce.
L'HLE diventa una milestone a sé (obiettivo: distribuzione senza BIOS, come Undertow).

---

## H3: hardware effettivamente usato

Accessi I/O in 240 s per gioco (IWL e X-Men fino al combattimento, Marvel Heroes fino a
"Scan character or story").

**Mai toccati:** JPG (`0x0802`), APBDMA (`0x0808`; X-Men costruisce l'indirizzo ma non
lo usa), BUFCTL `P_TV_BUFFER_SEL` (`0x08090020`), LDMDMA, BLNDMA, SD, USB, MP4.

**PPU**, valori reali:
- controllo layer (datasheet SPG290 p. 65): `0x8b` bitmap ARGB1555, `0x18b` + blend,
  `0x100b` bitmap RGB565, `0x0a` modo carattere. Sempre `Rgm=1` (attributi da
  registro), **mai** `Mve` (scroll per riga), `HCmp`, `VCmp`;
- compressione verticale (`0x74-0x7c`) mai scritta; RAM di scroll H/V solo azzerate
  all'init; attributi dei layer senza flip;
- i tre `buf_start` di ogni layer ricevono lo stesso indirizzo a ogni frame, quindi la
  semplificazione di MAME (solo `buf_start[0]`) è corretta per questi giochi;
- usati: blend dei layer (`0x38/0x54/0x70`, rampe di dissolvenza), trasparenza RGB
  (`0x10`), sprite (17 M scritture in sprite RAM per X-Men), palette, fade TV
  (`0x0803000C`), `IRQTimingV/H` (solo Marvel Heroes), scanline `0x94` in polling (IWL).

**Non mappati in MAME ma scritti** (configurazione, ignorabili all'inizio): CKG
(clock), SFTCFG (pin mux, pull GPIO), MIU (`0x08070040-64`: indirizzi di framebuffer per
il TV, `0x94`, `0x104`), `P_INT_MASK_CTRL1`, `P_TV_SATURATION/VIDEODAC`, BUFCTL
`0x04/0x3c/0x4c/0x58`, MIU2 (X-Men). Più la **UART** (`0x08150000`, ~160 mila byte):
è il log di debug del driver CD servo del BIOS (`SEEK mode`, `NowQ=mm:ss:ff`,
`cddsp_pause`), utile per modellare i seek.

### Superficie da implementare

| Blocco | Base | Fonte | Note |
|-|-|-|-|
| CPU S+core7 | | MAME `score.cpp` (BSD-3) + nostre rotate | modello di timing nuovo |
| Bus, mirror | | driver | SDRAM 16 MB (`0x00…`, mirror `0x80/0xA0…`), SRAM 16 KB `0x0A000000`, flash 1 MB `0x10000000` (mirror fino a `0x1FFFFFFF`) |
| PPU | 0x0801 | `spg290_ppu.cpp` | sottoinsieme già sufficiente; + IRQTimingV/H |
| TV | 0x0803 | driver | modo, fade; vblank per semiquadro |
| SPU | 0x0805 | nostro `spg290_spu.cpp` | DAC FIFO + 24 voci ADPCM |
| CD servo | 0x0806 | `spg290_cdservo.cpp` | + tempo di seek |
| I2C + pad | 0x0813 | `spg290_i2c.cpp`, `hyperscan_ctrl` | |
| Timer ×6 | 0x0816, 0x0821 | `spg290_timer.cpp` | tick BIOS 2 kHz |
| GPIO carta | 0x0820 | `hyperscan_card.cpp` | + fix `m_resp_idx` |
| UART | 0x0815 | nuovo | solo TX su log |
| Resto | | stub | lettura = ultimo valore scritto |

Circa 4-5 mila righe di C partendo dal codice MAME (1487 CPU + 560 PPU + 821 SPU +
415 CD + 300 I2C/timer + driver).

---

## Progetto proposto

- **C99 + SDL2**, CMake + Ninja, MSYS2 mingw64; layout e stile di Undertow (README,
  THIRD_PARTY_NOTICES con attribuzione BSD-3 a Sandro Ronco e agli altri autori MAME,
  release zip).
- Opzioni: `--bios`, `--cd <cue|iso|chd>`, `--card <bin>`, `--headless --frames N`,
  `--screenshot`, `--trace`, `--keys`, `--cpi` (finché il timing non è tarato),
  `--log-uart`.
- Struttura:

```
src/
  main.c            CLI, loop, frontend SDL (video/audio/input)
  score7.c/.h       interprete S+core7 (port da MAME), score7_dasm.c
  bus.c             mappa di memoria, mirror, dispatch MMIO
  sched.c           scheduler a cicli (eventi: riga video, timer, CD, SPU)
  ppu.c tv.c        video
  spu.c             audio (port del nostro device)
  cdservo.c cdimg.c servo + immagini cue/bin/iso
  i2c.c pad.c timer.c rfid.c uart.c irq.c
tests/              test S+core auto-verificanti (rotate_test & co.), trace-diff vs MAME
tools/recon/        strumenti di questa ricognizione
```

- **Oracoli**: (1) trace-diff in lockstep con il fork MAME a CPI 6 (stesso formato di
  traccia da entrambi i lati); (2) test S+core scritti con la toolchain Sunplus;
  (3) catture di hardware reale; (4) frame rate dei giochi (X-Men in combattimento).
- **Prestazioni**: a CPI 1 servono 108 M istruzioni/s emulate. MAME strumentato, con
  dieci istanze in parallelo, ne dava ~65 M. Un interprete C semplice ci arriva, e
  i loop d'attesa (fino a metà del tempo) si possono saltare.

---

## Prima milestone (M1): il BIOS arriva al logo

Obiettivo: CPU + bus + timer + IRQ + TV/PPU minimi; boot dal flash fino alla schermata
"HyperScan", headless.

Criteri di uscita:
1. **trace-diff con MAME a CPI 6 identico** sui primi 50 M istruzioni del boot (PC,
   registri, scritture MMIO);
2. screenshot del logo identico al pixel a quello di MAME allo stesso istante emulato;
3. `.gitignore`, README e THIRD_PARTY_NOTICES dal primo commit; build da PowerShell
   con MSYS2.

Le milestone successive, in sintesi:
- **M2**: CD servo + I2C + pad + PPU completa → X-Men e IWL giocabili muti, catalogo dei
  29 servizi con nomi e firme (dalle tracce LLE);
- **M3**: SPU (port), carta RFID, Marvel Heroes;
- **M4**: timing (CPI per classe d'istruzione e stalli, vblank per semiquadro, seek CD)
  tarato su catture reali, con vincoli simultanei: frame rate di X-Men, lettura carta,
  durata dei caricamenti;
- **M5**: HLE del BIOS (boot senza `hyperscan.bin`), validato contro l'LLE.

---

## Domande aperte (servono catture da hardware reale)

1. **Frame rate di X-Men in combattimento** su console reale (60 / 30 / 15 fps, contando
   i frame unici in una cattura). Discrimina CPI ≤ 1,9 (60) da 1,9-3,7 (30), e conferma
   o smentisce il vblank per semiquadro.
2. **Tempo dall'accensione al titolo** (X-Men: 25 s in MAME) e **durata della
   schermata LOADING** di X-Men (~40 s in MAME a CPI 6).
3. **Impulsi RFID** su GATE con analizzatore logico: dicono se le soglie di MAME sono
   vere e quanto costa davvero il codice RFID del BIOS.
4. ~~Marvel Heroes fermo su "Scan character or story"~~ **Risolto.** Le carte "baddump"
   di `hash/hyperscan_card.xml` si ricostruiscono esattamente (CRC e SHA1 coincidono):
   UID a zero, byte 8-11 = ID gioco + numero carta, resto a zero. X-Men scrive i due campi
   in **little-endian** (`03 00 | 01 00`), Marvel Heroes in **big-endian**
   (`00 04 | 00 01` per `marvh001`). Con la carta così il gioco arriva al gameplay.

Risposte raccolte (2026-10-04): la console fisica non è disponibile. Dai video reali il
frame rate non si legge facilmente, perché gli sprite pre-renderizzati hanno pochi frame
di animazione. Il discriminante migliore è quindi la **velocità di movimento** (scroll,
attraversamento dello schermo, se la logica avanza a frame e non a tempo) confrontata in
A/B con la registrazione a 30 e 60 fps. Su console vera, dal logo HyperScan ai primi loghi
Marvel passa **più di un minuto**; in MAME (CPI 6, seek gratuiti) il titolo Marvel
Heroes appare a ~60 s.

## Aggiunta: Spider-Man (2026-10-04, sera)

Quarto titolo, `Spider-Man (USA)` (bin/cue MODE1, 44 MB, volume `SM_V1_0_USE1`, gennaio
2007). `HYPER.EXE` da 790 KB con il gioco intero. Analisi statica: **22 servizi BIOS, tutti
già fra i 29** (nessuna voce nuova). Come Marvel Heroes non chiama in modo statico
open/read/close (`89c/8a8/8c0`): legge i file per un'altra via, da capire in M2 dalle tracce.

## Riproduzione

`tools/mame/recon.patch` (patch del fork, branch `recon`, con anche `HS_TRACE`); `tools/recon/`: `recon.lua`,
`run.sh` (percorsi dello scratchpad di questa sessione, da adattare), `h1.py` (fps e
tempo libero per secondo), `h3.py` (registri non mappati), `stubs2.py` (chiamate
statiche ai servizi BIOS), `svc.py` (classificazione dei servizi dal dump RAM),
`mkxm.py` (ISO di X-Men dall'estratto). Dischi, BIOS, dump ed estratti restano fuori dal
progetto (`.gitignore`).
