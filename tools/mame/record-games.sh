#!/bin/bash
# Record sparse MAME references for the games in tools/games.sh into dumps/sparse/ref-<game>.trace:
# every I/O access and interrupt, plus a register hash every 100k instructions, for the
# first GAME_SEC seconds (CPI 6: 18M instructions per second). Runs the games in parallel.
# Env (or tools/local.sh): MAME (fork binary), ROMPATH (MAME rompath containing hyprscan/).
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
[ -f "$ROOT/tools/local.sh" ] && source "$ROOT/tools/local.sh"
: "${MAME:?set MAME to the MAME fork binary (hyprscan.exe), e.g. in tools/local.sh}"
: "${ROMPATH:?set ROMPATH to the MAME rompath containing hyprscan/}"
source "$ROOT/tools/games.sh"
OUT="$ROOT/dumps/sparse"
mkdir -p "$OUT"
for g in $GAMES; do
  cue=${GAME_CUE[$g]}
  [ -f "$cue" ] || { echo "$g: no image, skipped"; continue; }
  n=$(( GAME_SEC[$g] * 18000000 ))
  mkdir -p "$OUT/m$g"
  (cd "$OUT/m$g" && HS_TRACE="$(cygpath -m "$OUT")/ref-$g.trace:$n:100000" "$MAME" hyprscan -rompath "$ROMPATH" \
     -cdrom "$cue" -nothrottle -video none -sound none -seconds_to_run $(( GAME_SEC[$g] + 60 )) -skip_gameinfo > mame.log 2>&1) &
done
wait
rm -rf "$OUT"/m*
ls -la "$OUT"/ref-*.trace
