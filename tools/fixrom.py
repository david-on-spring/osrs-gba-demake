#!/usr/bin/env python3
"""Fix the GBA header checksum (and copy a Nintendo logo from a reference ROM if given)."""
import sys

rom = bytearray(open(sys.argv[1], 'rb').read())
if len(sys.argv) > 2:
    rom[4:160] = open(sys.argv[2], 'rb').read()[4:160]
chk = 0
for b in rom[0xA0:0xBD]:
    chk = (chk - b) & 0xFF
rom[0xBD] = (chk - 0x19) & 0xFF
while len(rom) % 1024:
    rom.append(0xFF)
open(sys.argv[1], 'wb').write(rom)
