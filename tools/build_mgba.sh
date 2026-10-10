#!/bin/sh
# Download and build libmgba 0.10.5 (GBA core only, static, no frontends) into
# .toolchain/mgba-0.10.5/build/libmgba.a, for tools/build_harness.sh. Needs cmake and a C compiler.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MGBA="$ROOT/.toolchain/mgba-0.10.5"
if [ ! -f "$MGBA/CMakeLists.txt" ]; then
    mkdir -p "$ROOT/.toolchain"
    curl -fsSL https://github.com/mgba-emu/mgba/archive/refs/tags/0.10.5.tar.gz | tar xz -C "$ROOT/.toolchain"
fi
cmake -S "$MGBA" -B "$MGBA/build" -DCMAKE_BUILD_TYPE=Release -DBUILD_STATIC=ON -DBUILD_SHARED=OFF \
    -DM_CORE_GB=OFF -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_GL=OFF -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF \
    -DBUILD_LIBRETRO=OFF -DBUILD_TEST=OFF -DBUILD_SUITE=OFF -DBUILD_CINEMA=OFF -DBUILD_EXAMPLE=OFF \
    -DBUILD_PYTHON=OFF -DBUILD_DOCGEN=OFF -DUSE_DEBUGGERS=OFF -DUSE_GDB_STUB=OFF -DUSE_EDITLINE=OFF \
    -DUSE_ELF=OFF -DUSE_EPOXY=OFF -DUSE_FFMPEG=OFF -DUSE_LIBZIP=OFF -DUSE_MINIZIP=OFF -DUSE_LUA=OFF \
    -DUSE_PNG=OFF -DUSE_SQLITE3=OFF -DUSE_ZLIB=OFF -DUSE_DISCORD_RPC=OFF -DENABLE_SCRIPTING=OFF
cmake --build "$MGBA/build" --target mgba -j4
