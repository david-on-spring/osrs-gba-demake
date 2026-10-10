#!/bin/sh
# Build (if needed) and open the ROM in mGBA (macOS).
set -e
cd "$(dirname "$0")"
./build.sh >/dev/null
open -a mGBA "$PWD/rs07demake.gba"
