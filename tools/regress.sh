#!/bin/bash
# Regression check against the MAME references:
#   - M1: BIOS boot without a disc, full trace of 72M instructions + logo screenshot
#     (tools/m1check.sh, references from tools/mame/record-ref.sh)
#   - games: sparse traces of each game in tools/games.sh
#     (references from tools/mame/record-games.sh)
# Env: BIOS (hyperscan.bin). Needs build/flick.exe.
set -e
: "${BIOS:?set BIOS to the HyperScan flash image (hyperscan.bin)}"
ROOT=$(cd "$(dirname "$0")/.." && pwd)
source "$ROOT/tools/games.sh"
bash "$ROOT/tools/m1check.sh"
export PATH=/c/msys64/mingw64/bin:$PATH
S="$ROOT/dumps/sparse"
fail=0
for g in $GAMES; do
  cue=${GAME_CUE[$g]}
  if [ ! -f "$cue" ] || [ ! -f "$S/ref-$g.trace" ]; then echo "$g: skipped (no image or reference)"; continue; fi
  n=$(( GAME_SEC[$g] * 18000000 ))
  "$ROOT/build/flick.exe" --bios "$BIOS" --cd "$cue" --headless --trace "$(cygpath -m "$S")/ours-$g.trace:$n:100000" &
done
wait
for g in $GAMES; do
  [ -f "$S/ours-$g.trace" ] || continue
  printf '%s: ' "$g"
  bash "$ROOT/tools/tracediff.sh" "$S/ref-$g.trace" "$S/ours-$g.trace" 6 | cut -c1-160 || fail=1
done
exit $fail
