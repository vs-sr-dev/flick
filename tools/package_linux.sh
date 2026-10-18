#!/bin/bash
# Linux release package: dist/flick-<version>-linux-x86_64.tar.gz with the flick binary, the
# README and the licences. Run it on Linux (WSL works): a clean Release build in build-linux/
# with the version suffix removed. Needs gcc, cmake, pkg-config and libsdl2-dev; the binary
# uses the system's SDL2 (libsdl2-2.0-0) and glibc 2.34 or later, as built on Ubuntu 24.04.
# Refuses to package uncommitted changes unless ALLOW_DIRTY=1.
set -e
cd "$(dirname "$0")/.."
GIT="git -c safe.directory=* -c core.fileMode=false"   # also from WSL on a Windows checkout

if [ -n "$($GIT status --porcelain)" ] && [ "${ALLOW_DIRTY:-0}" != 1 ]; then
  echo "uncommitted changes: commit them first (or ALLOW_DIRTY=1)" >&2
  exit 1
fi

rm -rf build-linux
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release -DFLICK_VERSION_SUFFIX= >/dev/null
cmake --build build-linux -j"$(nproc)"
VER=$(build-linux/flick --version | sed 's/^Flick //')
NAME=flick-$VER-linux-x86_64

# staged on a Linux file system, so that the permissions are real ones (not WSL's 777 on /mnt)
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
mkdir "$STAGE/$NAME"
cp build-linux/flick "$STAGE/$NAME/"
strip "$STAGE/$NAME/flick"
cp README.md LICENSE THIRD_PARTY_NOTICES.md res/flick.png "$STAGE/$NAME/"
chmod 644 "$STAGE/$NAME"/*
chmod 755 "$STAGE/$NAME" "$STAGE/$NAME/flick"

mkdir -p dist
rm -f "dist/$NAME.tar.gz"
tar -C "$STAGE" -czf "dist/$NAME.tar.gz" --owner=0 --group=0 "$NAME"
echo "dist/$NAME.tar.gz ($($GIT rev-parse --short HEAD))"
tar tvzf "dist/$NAME.tar.gz"
