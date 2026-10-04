#!/bin/bash
# Record the M1 reference from the MAME fork (branch `recon`, see recon.patch):
#   dumps/ref/bios-boot-72M.trace  first 72M instructions of the BIOS boot (no CD, no card),
#                                  = exactly 4.000 s at MAME's flat CPI 6 (432M cycles)
#   dumps/ref/bios-logo-4s.png     MAME's screen at 4.000 s (static HyperScan logo)
# Env: MAME (fork binary), ROMPATH (MAME rompath containing hyprscan/). MAME is deterministic:
# two runs give byte-identical traces.
set -e
MAME=${MAME:-D:/Homebrew6/mame-hslba/hyprscan.exe}
: "${ROMPATH:?set ROMPATH to the MAME rompath containing hyprscan/}"
OUT=$(cd "$(dirname "$0")/../.." && pwd)/dumps/ref
mkdir -p "$OUT" && cd "$OUT"
common=(hyprscan -rompath "$ROMPATH" -nothrottle -video none -sound none -skip_gameinfo)
HS_TRACE="$OUT/bios-boot-72M.trace:72000000" "$MAME" "${common[@]}" -seconds_to_run 30 > trace.log 2>&1
rm -rf snap && "$MAME" "${common[@]}" -seconds_to_run 4 -snapshot_directory "$OUT/snap" > snap.log 2>&1
mv snap/hyprscan/0000.png bios-logo-4s.png && rm -rf snap cfg
tail -1 bios-boot-72M.trace | cut -c1-40
