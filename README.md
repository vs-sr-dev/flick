# HyperScanEmu

An emulator for the **Mattel HyperScan** (2006), the Sunplus SPG290-based console that
reads game discs and RFID character cards. Work in progress.

The goal is an emulator that runs the retail games at the right speed with sound. MAME's
`hyprscan` driver is the starting point and the reference. Its CPU and video code is ported
here under BSD-3-Clause (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)), and every
step is checked against an instruction trace of MAME before any deliberate change.

## Status

**M1 (done):** S+core 7 CPU, memory map, timers, I2C with controllers, PPU and TV timing.
The original BIOS boots to the HyperScan logo. The first 72 million instructions (4 emulated
seconds) match MAME's trace exactly, down to the cycle of every interrupt, and the screen at
4.000 s is pixel-identical to MAME's.

**M2 (in progress):** CD servo and disc images, SPU (sound, the DAC FIFO used by the games
and the 24 hardware voices). X-Men, Spider-Man, Marvel Heroes and IWL boot from their discs
through the BIOS and load their game data. Over the first 60-90 seconds of each, every I/O
access and interrupt matches MAME's, and a hash of all registers taken every 100 000
instructions matches too. The RFID card reader works: X-Men accepts a Wolverine card and
reaches gameplay. Not yet: save states, a disassembler for traces.

Later milestones: a timing model fitted to captures from real hardware, since MAME's flat
6 cycles per instruction makes the heavy games run at half speed (M4); a boot without the
BIOS (M5). The analysis behind this plan is in [RECON.md](RECON.md).

## Legal

This repository contains **no** BIOS, game code or disc data. You need your own dump of
the HyperScan flash (`hyperscan.bin`, 1 MB, the same file MAME uses). HyperScanEmu is an
independent project, not affiliated with Mattel or Sunplus.

## Usage

    hyperscanemu --bios hyperscan.bin --cd game.cue    # window, sound, real time
    hyperscanemu --bios hyperscan.bin --headless --seconds 4 --screenshot logo.png

| Option | |
|-|-|
| `--bios FILE` | HyperScan flash image (required) |
| `--cd FILE` | disc image: `.cue` with one BINARY file (MODE1/2352, MODE1/2048, AUDIO tracks) or `.iso` |
| `--headless` | no window |
| `--seconds S`, `--frames N` | stop after S emulated seconds / N frames |
| `--screenshot FILE` | save the last frame as PNG on exit |
| `--wav FILE` | record the sound output as a WAV file (window or headless) |
| `--trace FILE[:N[:K[:FROM]]]` | trace the first N instructions (default 50M), same format as the MAME fork; with K > 0 a sparse trace (I/O, interrupts, a register hash every K instructions, full lines from FROM on) |
| `--input SPEC` | scripted presses, `T:button[:dur][@pad],...` with times in seconds |
| `--timing fast\|mame` | timing model (default `fast`, see below); `--trace` forces `mame` |
| `--cpi N` | cycles per instruction, overriding the preset |
| `--log-uart` | print the BIOS debug UART on stderr |
| `-v` | verbose log |

Pad: arrows = stick, Z/X/C/V = green/red/yellow/blue, Enter = start, Backspace = select,
A/S = shoulders, Q/W = triggers.

Moving the mouse shows a toolbar along the top of the window; it hides again after a few
seconds. Its buttons, with their keys:

| Button | Key | |
|-|-|-|
| disc | Ctrl+O | open a `.cue` or `.iso` (or drop it on the window): the console restarts with it |
| card | F1 | the card panel: pick a card and put it on the reader |
| eject | F2 | take the card off the reader |
| pause | P, Pause | pause and resume |
| reset | F5 | restart the console (the disc and the card stay) |
| FAST / MAME | F3 | switch the timing preset (see below) |
| camera | F12 | save a screenshot in `screenshots/` next to the executable |
| 1x / 2x | | window size |
| full screen | F11, Alt+Enter | full screen and back (Esc also leaves it) |
| speaker, slider | | mute, volume (the mouse wheel works on them too) |

Esc quits. Cards that a game writes to are saved in `cards/` next to the executable
(`--cards-dir` to change it).

### Timing

MAME charges 6 cycles to every instruction, so X-Men fights at 15 fps. The default `fast`
preset charges 1 cycle per instruction and keeps MAME's PPU vblank per frame in the
interlaced video modes: X-Men fights at 30 fps, as on captures from real consoles. It also
decodes the card reader's pulses by their ratios rather than MAME's absolute widths, which
only work at 6 cycles per instruction, and fast-forwards idle loops (exactly: whole
iterations, never past the next event). It also plays the BIOS boot jingle, which MAME
leaves silent: the BIOS starts the DAC FIFO before giving it a sample clock, and feeds it
only while its frame loop waits for the vblank, which it never does when the controllers'
I2C is clocked as in MAME (27 MHz); `fast` clocks it from the 108 MHz system clock.
`--timing mame` (or F3 in the window) gives
MAME's behaviour back, cycle for cycle. `--field-vblank` raises a vblank per field instead,
which lets X-Men reach 60 fps: real captures say it does not. Fitting the rest of the
timing (loading times, CD seeks) to the real console is milestone M4.

## Build (Windows, MSYS2 mingw64)

Packages: `mingw-w64-x86_64-gcc`, `-cmake`, `-ninja`, `-SDL2` (SDL2 is optional: without
it the build is headless only).

    export PATH=/c/msys64/mingw64/bin:$PATH
    cmake -S . -B build -G Ninja
    cmake --build build

The code is C99 with one GCC/Clang extension (`unsigned __int128` for exact emulated
time). Other platforms should only need build tweaks.

## Testing against MAME

[tools/mame/](tools/mame/) holds the patch for a MAME fork that writes the reference
trace (`HS_TRACE`). `record-ref.sh` records the BIOS boot reference and `record-games.sh`
sparse references for the games listed in `tools/games.sh`. `tools/regress.sh` compares
HyperScanEmu with all of them; `tools/tracediff.sh` shows the first divergence between two
traces, with context.
