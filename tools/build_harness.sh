#!/bin/sh
# Build the headless mGBA test harness (needs .toolchain/mgba-0.10.5/build/libmgba.a).
# Output: build/harness (run_scenario.py prefers it over .toolchain/harness).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MGBA="$ROOT/.toolchain/mgba-0.10.5"
mkdir -p "$ROOT/build"
cc -O2 -I"$MGBA/include" -I"$MGBA/build/include" -DM_CORE_GBA "$ROOT/tools/harness.c" "$MGBA/build/libmgba.a" \
   -o "$ROOT/build/harness" -lm -lpthread -framework CoreFoundation
