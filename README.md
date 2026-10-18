<p align="center"><img src="res/flick.png" width="160" alt="Flick"></p>

# Flick

An emulator for the **Mattel HyperScan** (2006), the Sunplus SPG290 console that reads
game discs and RFID character cards. It runs the five retail games with sound, cards and
two players. The name is the flick of the wrist that puts a card on the scanner.

**Work in progress.** Flick is built on MAME's `hyprscan` driver, which it follows cycle
for cycle where MAME is right, and goes further where MAME stops: sound, the card reader at
full speed, the games' real frame rate. It is an independent project, not affiliated with
Mattel or Sunplus.

## Games

| Game | |
|-|-|
| X-Men | playable with cards; the experience a character gains is saved on its card |
| Marvel Heroes | playable with cards, one or two players |
| Ben 10 | playable with cards, one or two players (about two minutes of loading at boot) |
| IWL - Interstellar Wrestling League | playable with cards |
| Spider-Man | reaches gameplay; not tried with cards yet |

## Getting started

1. Download Flick from [Releases](https://github.com/vs-sr-dev/flick/releases) and extract it
   anywhere:
   - **Windows** (64-bit): `flick-<version>-win64.zip`.
   - **Linux** (x86-64, glibc 2.34 or later: Ubuntu 22.04, Debian 12, Fedora 35 and
     newer): `flick-<version>-linux-x86_64.tar.gz`. It uses the system's SDL2
     (`libsdl2-2.0-0` on Debian and Ubuntu, `SDL2` on Fedora, `sdl2` on Arch) and, for the
     file dialogs, `zenity` or `kdialog`; with neither, start it once with
     `./flick --bios /path/to/hyperscan.bin` and drop discs on the window.
2. You need the **HyperScan BIOS**, `hyperscan.bin` (the console's 1 MB flash, the same
   file as in MAME's `hyprscan` set), and the **game discs** as `.cue`/`.bin` or `.iso`
   images. Flick includes neither: dump your own.
3. Start `flick.exe` (or `flick` on Linux). The first time it asks for the BIOS; then it
   offers a disc.
   You can also open a `.cue` with Flick, or drop it on the window.

## Playing

### Controls

| HyperScan | Keyboard | Controller (Xbox layout) |
|-|-|-|
| stick | arrows | left stick, d-pad |
| green, red, yellow, blue | Z, X, C, V | A, B, Y, X |
| start, select | Enter, Backspace | Menu, View |
| shoulders | A, S | LB, RB |
| triggers | Q, W | LT, RT |

These are the defaults: the **controls panel** (F4, or the controller button on the
toolbar) changes them. Click a cell (or Enter), then press the new key or button; one that
is already in use swaps places, Del clears one.

Any game controller SDL knows (Xbox, PlayStation and many others) works. The first one
plugged in is player 1, the second player 2. The keyboard plays the first player without a
controller, so one controller and the keyboard make two players.

### Cards

The HyperScan's games come alive with cards placed on the scanner. **F1** opens the card
panel, with the cards of the game in the drive (Tab shows the other games' cards; type to
search). Enter puts the selected card on the reader, **F2** takes it off. A card stays on
the reader until F2, as if the player left it there.

All 445 known cards are in the panel. When a game writes to a card (X-Men saves a
character's experience), the card is saved in `cards/` (see
[Settings](#settings)) and used again the next time it is placed.

With two players, take each card off (F2) once the game has read it, before the other
player places theirs: a game that reads quickly, such as Ben 10, would otherwise read the
first card again and give both players the same character.

### Toolbar and keys

Moving the mouse shows a toolbar along the top of the window; it hides after a few seconds.

| Button | Key | |
|-|-|-|
| disc | Ctrl+O | open a disc (or drop it on the window): the console restarts with it |
| card | F1 | the card panel |
| eject | F2 | take the card off the reader |
| pause | P, Pause | pause and resume |
| reset | F5 | restart the console (the disc and the card stay) |
| FAST / MAME | F3 | timing preset (see [How it works](#how-it-works)) |
| controller | F4 | the controls panel |
| camera | F12 | screenshot, in `screenshots/` (see [Settings](#settings)) |
| | Ctrl+F12 | debug dump in `debug/` (RAM, video state, frame): attach it when reporting a graphics glitch |
| 1x / 2x | | window size |
| full screen | F11, Alt+Enter | full screen and back (Esc also leaves it) |
| speaker, slider | | mute, volume (the mouse wheel works on them too) |

Esc quits. In the card and controls panels, a controller works too: d-pad, A, B.

### Settings

`flick.ini` keeps the BIOS, the folder of the last disc, the volume, the window size, the
timing preset and the controls. Flick rewrites it when the window closes, with what was
chosen in the window.

It lives next to the executable, together with `cards/`, `screenshots/` and `debug/`. On
Linux that holds when Flick is unpacked in a folder you can write to; otherwise (installed
system-wide, say) `flick.ini` goes in `~/.config/flick/` and the rest in
`~/.local/share/flick/`.

## Known issues

- **Loading times** follow MAME's CD drive, which answers instantly; the real console
  takes longer to seek. Fitting them to the hardware is still to do.
- On a slow PC a few stretches of IWL (loading, decompression) may not hold full speed.
- No save states yet.

## How it works

Flick is written in C. Its CPU (S+core 7), video, timers, controllers, CD drive and card
reader start as ports of MAME's code, and every step is checked against instruction traces
of a MAME build: the BIOS boot is identical to MAME's for its first 72 million
instructions, down to the cycle of every interrupt, with a pixel-identical logo, and the
first 60-120 seconds of each game match MAME's I/O accesses, interrupts and registers.

Then it departs from MAME where MAME is wrong for playing:

- **Sound.** MAME's driver has none; Flick emulates the SPG290's sound unit (24 hardware
  voices with PCM and ADPCM, and the DAC FIFO the games stream sound through), including
  the BIOS boot jingle.
- **Speed.** MAME charges every instruction 6 cycles, so X-Men fights at 15 fps. Flick's
  default `fast` preset charges 1 and keeps MAME's vblank per frame: X-Men fights at 30 fps,
  as on captures from real consoles. It decodes the card reader's pulses by their ratios
  (MAME's fixed widths only work at 6 cycles per instruction), and skips the games' waiting
  loops exactly: whole iterations, never past the next event, so nothing changes but the
  time it takes the host. **F3** (`--timing mame`) switches to MAME's timing, cycle for cycle.
- **Video.** The 640x240 mode starts 8 lines into the layers, which makes Ben 10's split
  screen clean.

The milestones ahead: a timing model fitted to the real console (loading times, CD seeks),
then a boot without the BIOS. The original analysis is in [RECON.md](RECON.md) (in Italian),
the running list in [TODO.md](TODO.md).

## Command line

    flick [options] [disc.cue|disc.iso]

| Option | |
|-|-|
| `--bios FILE` | HyperScan flash image (default: the one in `flick.ini`) |
| `--cd FILE` | disc image: `.cue` with one BINARY file (MODE1/2352, MODE1/2048, AUDIO tracks) or `.iso` |
| `--card FILE` | a 120-byte card image on the reader from the start |
| `--quickload FILE` | run a `HYPER.EXE` directly, as MAME's quickload does (homebrew such as [hs-lba](https://github.com/vs-sr-dev/hs-lba)); the BIOS does not run |
| `--card-at T:ID` | place catalogue card ID (e.g. `xmen001`) at T seconds |
| `--cards-dir DIR` | where written cards are saved (default `cards/`, see [Settings](#settings)) |
| `--headless` | no window, no sound |
| `--seconds S`, `--frames N` | stop after S emulated seconds / N frames |
| `--screenshot FILE` | save the last frame as PNG on exit |
| `--wav FILE` | record the sound output as WAV |
| `--debug-dump DIR` | on exit, save the frame's video state and RAM (as Ctrl+F12) |
| `--render-dump DIR` | render such a dump again into `DIR/render.png`; no BIOS needed |
| `--trace FILE[:N[:K[:FROM]]]` | trace the first N instructions (default 50M) in the MAME fork's format; K > 0: sparse (I/O, interrupts, a register hash every K instructions, full lines from FROM on) |
| `--input SPEC` | scripted presses, `T:button[:dur][@pad],...` (seconds) |
| `--timing fast\|mame` | timing preset (default `fast`); `--trace` forces `mame` |
| `--cpi N` | cycles per instruction, overriding the preset |
| `--field-vblank` | a vblank per field in interlaced modes (MAME: per frame) |
| `--log-uart` | print the BIOS debug UART |
| `-v` | verbose log |
| `--version` | print the version |

Headless runs read the BIOS from `flick.ini` but always start with the `fast` preset, and
command-line options are never saved. On Windows Flick is a GUI program: started from a
terminal it prints there, but cmd and PowerShell do not wait for it, so for headless runs
use bash, `start /wait flick ...`, or a pipe (`flick ... 2>&1 | Out-Host`).

## Building

Windows, with MSYS2's mingw64: packages `mingw-w64-x86_64-gcc`, `-cmake`, `-ninja`,
`-SDL2` (without SDL2 the build is headless only).

    export PATH=/c/msys64/mingw64/bin:$PATH
    cmake -S . -B build -G Ninja
    cmake --build build

Linux: `build-essential` (or gcc), `cmake`, `pkg-config` and `libsdl2-dev`, then the same
two `cmake` lines.

`tools/package_release.sh` (Windows) and `tools/package_linux.sh` (Linux, WSL included)
make the release packages in `dist/` from a clean Release build; the version is the one in
`CMakeLists.txt` (`-dev` in ordinary builds). The code is C99 with one GCC/Clang extension
(`unsigned __int128`, for exact emulated time). The Linux build boots the BIOS with an
instruction trace identical to MAME's, as the Windows one does; macOS should need little more than a package, but is untested.

## Testing against MAME

[tools/mame/](tools/mame/) holds the patch for the MAME fork that writes reference traces
(`HS_TRACE`). `record-ref.sh` records the BIOS boot, `record-games.sh` sparse references
for the games in `tools/games.sh`; `tools/regress.sh` compares Flick with all of them and
`tools/tracediff.sh` shows the first divergence between two traces. The scripts take the
BIOS, the disc images and the MAME binary from the environment or from `tools/local.sh`
(copy [tools/local.sh.example](tools/local.sh.example)).

## License

Flick is under the [MIT License](LICENSE). The files ported from MAME keep their
BSD-3-Clause license, the card catalogue comes from MAME's CC0 software list, and the
Windows package includes SDL2 (zlib license): see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This repository contains no BIOS, game code or disc data.

## Thanks

To Sandro Ronco and the MAME team for the `hyprscan` driver and the SPG290 devices this
emulator grew from, and to everyone who dumped and documented the HyperScan.
