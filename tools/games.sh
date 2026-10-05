# Disc images used by the regression scripts (sourced). Override any of them in the
# environment; a game whose image is missing is skipped.
declare -A GAME_CUE=(
  [xm]="${XM_CUE:-D:/Homebrew6/HyperScanEmu/discs/xm/xm.cue}"
  [sm]="${SM_CUE:-D:/Homebrew2/hyperscan-emulator-master/ISO/Spider-Man/Spider-Man (USA).cue}"
  [iwl]="${IWL_CUE:-D:/Homebrew2/hyperscan-emulator-master/ISO/IWL_USE2/IWL - Interstellar Wrestling League (USA) (USE2).cue}"
  [mh]="${MH_CUE:-D:/Homebrew6/HyperScanEmu/discs/mh/mh.cue}"
  [b10]="${B10_CUE:-J:/Users/utente/Downloads/roms/Ben 10 (USA) (USE2).cue}"
  [xm2]="${XM2_CUE:-J:/Users/utente/Downloads/roms/X-Men (USA) (USE2).cue}"
)
# emulated seconds per game: past the BIOS boot and the first CD loads
# (xm2 is the raw MODE1/2352 dump of the disc that xm has as a 2048-byte ISO)
declare -A GAME_SEC=( [xm]=60 [sm]=60 [iwl]=90 [mh]=60 [b10]=120 [xm2]=60 )
GAMES="xm sm iwl mh b10 xm2"
