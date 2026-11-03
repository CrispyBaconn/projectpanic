#!/bin/sh
# Starts Panic Park. ROMDIR=folder  tells where the ROMs are (default: current folder).
# panicprk_main.elf and main_r4650_BE_4MB.bin. Options: GUI=0 skips the start screen; DSW=0xfd skips the POST.
cd "$(dirname "$0")"
[ -x build_full/panicprk_stub ] || sh platform/build_full.sh
[ "$GUI" = "0" ] && export S23_NOGUI=1
[ -n "$DSW" ] && export S23_DSW="$DSW"
S23_WINDOW=1 exec ./build_full/panicprk_stub panicprk_main.elf main_r4650_BE_4MB.bin 1000000000 0 "${ROMDIR:-.}"
