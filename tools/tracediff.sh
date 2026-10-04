#!/bin/bash
# tracediff.sh <reference.trace> <ours.trace> [context lines]
# First divergence between two HS_TRACE-format traces, with context from both.
# A shorter trace that matches the head of the other counts as identical (up to its E line).
set -e
ref=$1; ours=$2; ctx=${3:-12}
out=$(cmp "$ref" "$ours" 2>&1) && { echo "IDENTICAL"; exit 0; }
case "$out" in
  *EOF*) echo "one trace is a prefix of the other: $out"; exit 0;;
esac
line=$(echo "$out" | sed -n 's/.*line \([0-9]*\).*/\1/p')
from=$(( line > ctx ? line - ctx : 1 ))
insn=$(head -n "$line" "$ours" | grep -cv '^[ #SLIXE]' || true)
echo "first difference at line $line (instruction ~$insn)"
echo "--- reference"; sed -n "${from},$((line + 3))p" "$ref"
echo "--- ours";      sed -n "${from},$((line + 3))p" "$ours"
exit 1
