# HyperScanEmu

An emulator for the **Mattel HyperScan** (2006), the Sunplus SPG290-based console that
reads game discs and RFID character cards. Work in progress.

The goal is an emulator that runs the retail games at the right speed with sound. MAME's
`hyprscan` driver is the starting point and the reference. Its CPU and video code is ported
here under BSD-3-Clause (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)), and every
step is checked against an instruction trace of MAME before any deliberate change.

## Status

**Milestone M1 (done):** S+core 7 CPU, memory map, timers, I2C with controllers, PPU and
TV timing. The original BIOS boots to the HyperScan logo.
- The first 72 million instructions (4 emulated seconds) match MAME's trace exactly:
  every register change, I/O access and interrupt lands on the same cycle.
- The screen at 4.000 s is pixel-identical to MAME's.
- The two traces part at about 7 s, on the first read of the CD servo, which is not
  emulated yet.

Next milestones: the CD servo and disc images, so the games load (M2); sound and the RFID
card (M3); a timing model fitted to captures from real hardware, since MAME's flat 6 cycles
per instruction makes the heavy games run at half speed (M4); a boot without the BIOS (M5).
The analysis behind this plan is in [RECON.md](RECON.md).

## Legal

This repository contains **no** BIOS, game code or disc data. You need your own dump of
the HyperScan flash (`hyperscan.bin`, 1 MB, the same file MAME uses). HyperScanEmu is an
independent project, not affiliated with Mattel or Sunplus.

## Usage

    hyperscanemu --bios hyperscan.bin                 # window, real time
    hyperscanemu --bios hyperscan.bin --headless --seconds 4 --screenshot logo.png

| Option | |
|-|-|
| `--bios FILE` | HyperScan flash image (required) |
| `--headless` | no window |
| `--seconds S`, `--frames N` | stop after S emulated seconds / N frames |
| `--screenshot FILE` | save the last frame as PNG on exit |
| `--trace FILE[:N]` | trace the first N instructions (default 50M), same format as the MAME fork |
| `--cpi N` | cycles per instruction (default 6, as MAME) |
| `--log-uart` | print the BIOS debug UART on stderr |
| `-v` | verbose log |

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
trace (`HS_TRACE`), and `record-ref.sh` records it. `tools/m1check.sh` then compares
HyperScanEmu with that reference (trace and screenshot). `tools/tracediff.sh` shows the
first divergence between two traces, with context.
