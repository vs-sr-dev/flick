# Disc images used by the regression scripts (sourced). By default DISCS/<game>.cue, with
# DISCS = discs/ in the repository (ignored by git); set DISCS, or any XM_CUE..., in the
# environment or in tools/local.sh.
DISCS=${DISCS:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/discs}
declare -A GAME_CUE=(
  [xm]="${XM_CUE:-$DISCS/xm.cue}"
  [sm]="${SM_CUE:-$DISCS/sm.cue}"
  [iwl]="${IWL_CUE:-$DISCS/iwl.cue}"
  [mh]="${MH_CUE:-$DISCS/mh.cue}"
  [b10]="${B10_CUE:-$DISCS/b10.cue}"
  [xm2]="${XM2_CUE:-$DISCS/xm2.cue}"
)
# emulated seconds per game: past the BIOS boot and the first CD loads
# (xm2 is the raw MODE1/2352 dump of the disc that xm has as a 2048-byte ISO)
declare -A GAME_SEC=( [xm]=60 [sm]=60 [iwl]=90 [mh]=60 [b10]=120 [xm2]=60 )
GAMES="xm sm iwl mh b10 xm2"
