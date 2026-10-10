#!/bin/sh
# Build the ROM using the toolchain bundled in .toolchain/ (or arm-none-eabi-* on PATH).
set -e
cd "$(dirname "$0")"
TC=$(ls -d .toolchain/arm-gnu-toolchain-*/bin 2>/dev/null | head -1)
[ -n "$TC" ] && PATH="$PWD/$TC:$PATH"
make -j8 "$@"
