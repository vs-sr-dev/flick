#!/bin/bash
# M1 exit check against the MAME reference made by tools/mame/record-ref.sh:
#   1. trace of the first 72M instructions identical to dumps/ref/bios-boot-72M.trace
#   2. screen at 4.000 s identical to dumps/ref/bios-logo-4s.png
# Env (or tools/local.sh): BIOS (hyperscan.bin). Needs build/flick.exe and a Python with Pillow.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$ROOT/tools/local.sh" ] && source "$ROOT/tools/local.sh"
: "${BIOS:?set BIOS to the HyperScan flash image (hyperscan.bin), e.g. in tools/local.sh}"
PY=${PY:-$(command -v python)}      # resolve before mingw64 (no Pillow there) joins PATH
cd "$(dirname "$0")/.."
export PATH=/c/msys64/mingw64/bin:$PATH
EMU=build/flick.exe
mkdir -p dumps
"$EMU" --bios "$BIOS" --headless --trace dumps/ours-72M.trace:72000000
bash tools/tracediff.sh dumps/ref/bios-boot-72M.trace dumps/ours-72M.trace
"$EMU" --bios "$BIOS" --headless --timing mame --seconds 4 --screenshot dumps/ours-logo-4s.png
"$PY" tools/pngdiff.py dumps/ref/bios-logo-4s.png dumps/ours-logo-4s.png
