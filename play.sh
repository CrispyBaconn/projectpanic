#!/bin/sh
# Avvia Panic Park. ROMDIR=cartella  indica dove sono le ROM (default: cartella corrente).
# panicprk_main.elf e main_r4650_BE_4MB.bin. Opzioni: GUI=0 salta la schermata iniziale; DSW=0xfd salta il POST.
cd "$(dirname "$0")"
[ -x build_full/panicprk_stub ] || sh platform/build_full.sh
[ "$GUI" = "0" ] && export S23_NOGUI=1
[ -n "$DSW" ] && export S23_DSW="$DSW"
S23_WINDOW=1 exec ./build_full/panicprk_stub panicprk_main.elf main_r4650_BE_4MB.bin 1000000000 0 "${ROMDIR:-.}"
