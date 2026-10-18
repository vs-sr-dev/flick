#!/bin/bash
# Windows release package: dist/flick-<version>-win64.zip with flick.exe, SDL2.dll, the README
# and the licences. A clean Release build in build-release/ with the version suffix removed.
# Needs MSYS2 mingw64 (gcc, cmake, ninja, SDL2) and a Python (only its zipfile module).
# Refuses to package uncommitted changes unless ALLOW_DIRTY=1.
set -e
PY=${PY:-$(command -v python)}      # resolve before mingw64 joins PATH
cd "$(dirname "$0")/.."
export PATH=/c/msys64/mingw64/bin:$PATH
MINGW=/c/msys64/mingw64

if [ -n "$(git status --porcelain)" ] && [ "${ALLOW_DIRTY:-0}" != 1 ]; then
  echo "uncommitted changes: commit them first (or ALLOW_DIRTY=1)" >&2
  exit 1
fi

rm -rf build-release
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DFLICK_VERSION_SUFFIX= >/dev/null
cmake --build build-release
VER=$(build-release/flick.exe --version | sed 's/^Flick //')
NAME=flick-$VER-win64
OUT=dist/$NAME

rm -rf "$OUT" "dist/$NAME.zip"
mkdir -p "$OUT"
cp build-release/flick.exe "$OUT/"
strip "$OUT/flick.exe"
cp "$MINGW/bin/SDL2.dll" "$OUT/"
cp README.md LICENSE THIRD_PARTY_NOTICES.md "$OUT/"
cp "$MINGW/share/licenses/SDL2/LICENSE.txt" "$OUT/LICENSE-SDL2.txt"

(cd dist && "$PY" -m zipfile -c "$NAME.zip" "$NAME")
echo "dist/$NAME.zip ($(git rev-parse --short HEAD))"
ls -l "$OUT"
