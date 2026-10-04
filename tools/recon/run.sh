#!/bin/bash
# run.sh <name> <cue> <card> <seconds> [extra mame args...]
# Env: SCR (work dir; recon.lua is taken from here), MAME (fork binary), ROMPATH (holds hyprscan/)
SCR=${SCR:-$(cd "$(dirname "$0")" && pwd)}
MAME=${MAME:-D:/Homebrew6/mame-hslba/hyprscan.exe}
: "${ROMPATH:?set ROMPATH to the MAME rompath containing hyprscan/}"
name=$1; cue=$2; card=$3; secs=$4; shift 4
D="$SCR/runs/$name"; rm -rf "$D"; mkdir -p "$D"; cd "$D"
[ -n "$card" ] && cp "$card" "$D/card.bin" && card="$D/card.bin"
export HS_PROF="$D/prof.txt" RECON_OUT="$D/r" RECON_SNAP=${RECON_SNAP:-5}
export RECON_MASH=${RECON_MASH:-":ctrl0:IN.1|Green|3|85|0.3;:ctrl0:IN.0|Start|11|86|0.3"}
start=$(date +%s)
timeout 3000 "$MAME" hyprscan -rompath "$ROMPATH" -cdrom "$cue" ${card:+-memc "$card"} \
  -autoboot_script "$SCR/recon.lua" -autoboot_delay 0 -nothrottle -video none -sound none -seconds_to_run $secs \
  -snapshot_directory "$D/snap" -skip_gameinfo "$@" > mame.log 2>&1
echo "$name exit $? wall $(( $(date +%s)-start ))s $(grep 'Average speed' mame.log)"
