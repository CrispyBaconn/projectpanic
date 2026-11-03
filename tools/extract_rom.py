#!/usr/bin/env python3
"""Builds main_r4650_BE_4MB.bin and panicprk_main.elf from the Panic Park ROM set.

Usage: python3 tools/extract_rom.py <rom_folder> [output_folder]

The main R4650 program ROM is split over two 16-bit-wide chips: pnp2vera.ic2 holds the even bytes and
pnp2vera.ic1 the odd bytes (like ROM_LOAD16_BYTE in MAME). Interleaving them gives the 4 MB big-endian image;
the game's ELF executable is stored inside it.
"""
import os, struct, sys

def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    rom, out = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else '.'
    try:
        even = open(os.path.join(rom, 'pnp2vera.ic2'), 'rb').read()
        odd = open(os.path.join(rom, 'pnp2vera.ic1'), 'rb').read()
    except OSError as e:
        sys.exit(f'missing ROM: {e.filename}')
    if len(even) != 0x200000 or len(odd) != 0x200000:
        sys.exit('pnp2vera.ic1 / pnp2vera.ic2 must be 2 MB each')
    img = bytearray(0x400000)
    img[0::2], img[1::2] = even, odd

    at = img.find(b'\x7fELF')
    if at < 0:
        sys.exit('no ELF found in the program ROM (wrong ROM set?)')
    h = img[at:at + 52]
    phoff, shoff = struct.unpack_from('>II', h, 28)
    phentsize, phnum, shentsize, shnum = struct.unpack_from('>HHHH', h, 42)
    end = shoff + shentsize * shnum
    for i in range(phnum):
        off, _, _, filesz = struct.unpack_from('>IIII', img, at + phoff + i * phentsize + 4)
        end = max(end, off + filesz)
    for i in range(shnum):
        typ, = struct.unpack_from('>I', img, at + shoff + i * shentsize + 4)
        off, size = struct.unpack_from('>II', img, at + shoff + i * shentsize + 16)
        if typ != 8:                                     # SHT_NOBITS (.bss) takes no space in the file
            end = max(end, off + size)

    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, 'main_r4650_BE_4MB.bin'), 'wb').write(img)
    open(os.path.join(out, 'panicprk_main.elf'), 'wb').write(img[at:at + end])
    print(f'main_r4650_BE_4MB.bin: {len(img)} bytes')
    print(f'panicprk_main.elf: {end} bytes (offset {at:#x} in the program ROM)')

if __name__ == '__main__':
    main()
