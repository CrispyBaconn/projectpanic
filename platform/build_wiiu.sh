#!/bin/sh
# Compila Panic Park ricompilato per Wii U (Aroma) e prepara la cartella da copiare sulla SD.
#   sudo dkp-pacman -S wiiu-dev wiiu-sdl2          (una volta)
#   sh platform/build_wiiu.sh                       -> dist/wiiu/sd/wiiu/apps/panicprk/
# Variabili: JOBS (compilazioni in parallelo, default = numero di core), OPT (default -O2)
set -e
cd "$(dirname "$0")/.."
: "${DEVKITPRO:=/opt/devkitpro}"; export DEVKITPRO
export DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"
[ -f "$DEVKITPRO/wut/share/wut_rules" ] || { echo "manca wut: sudo dkp-pacman -S wiiu-dev wiiu-sdl2"; exit 1; }
[ -f "$DEVKITPRO/portlibs/wiiu/lib/libSDL2.a" ] || { echo "manca SDL2 per Wii U: sudo dkp-pacman -S wiiu-sdl2"; exit 1; }
export PATH="$DEVKITPRO/tools/bin:$DEVKITPPC/bin:$PATH"
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 2)}
make -f Makefile.wiiu -j"$JOBS" ${OPT:+OPT="$OPT"}
SD=dist/wiiu/sd/wiiu/apps/panicprk
mkdir -p $SD/lang
cp panicprk.wuhb $SD/
cp platform/lang/*.json $SD/lang/
if ls platform/ui/*.png >/dev/null 2>&1; then mkdir -p $SD/ui && cp platform/ui/*.png $SD/ui/; fi   # sfondo e logo del menu (facoltativi)
# file del gioco: solo se sono qui (sono i tuoi, non vanno distribuiti)
[ -f panicprk_main.elf ] && cp panicprk_main.elf $SD/
[ -f main_r4650_BE_4MB.bin ] && cp main_r4650_BE_4MB.bin $SD/
if [ -d panicprkrom ]; then mkdir -p $SD/panicprkrom && cp panicprkrom/* $SD/panicprkrom/ 2>/dev/null || true; fi
echo "BUILD_OK -> copia il contenuto di dist/wiiu/sd/ nella radice della SD della Wii U"
