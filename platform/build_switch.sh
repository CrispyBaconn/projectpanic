#!/bin/sh
# Compila Panic Park ricompilato per Nintendo Switch (homebrew) e prepara la cartella da copiare sulla SD.
#   sudo dkp-pacman -S switch-dev switch-sdl2      (una volta)
#   sh platform/build_switch.sh                     -> dist/switch/sd/switch/panicprk/
# Variabili: JOBS (compilazioni in parallelo, default = numero di core), OPT (default -O2)
set -e
cd "$(dirname "$0")/.."
: "${DEVKITPRO:=/opt/devkitpro}"; export DEVKITPRO
export DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"
[ -f "$DEVKITPRO/libnx/switch_rules" ] || { echo "manca libnx: sudo dkp-pacman -S switch-dev switch-sdl2"; exit 1; }
[ -f "$DEVKITPRO/portlibs/switch/lib/libSDL2.a" ] || { echo "manca SDL2 per Switch: sudo dkp-pacman -S switch-sdl2"; exit 1; }
export PATH="$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH"
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 2)}
make -f Makefile.switch -j"$JOBS" ${OPT:+OPT="$OPT"}
SD=dist/switch/sd/switch/panicprk
mkdir -p $SD/lang
cp panicprk.nro $SD/
cp platform/lang/*.json $SD/lang/
if ls platform/ui/*.png >/dev/null 2>&1; then mkdir -p $SD/ui && cp platform/ui/*.png $SD/ui/; fi   # sfondo e logo del menu (facoltativi)
# file del gioco: solo se sono qui (sono i tuoi, non vanno distribuiti)
[ -f panicprk_main.elf ] && cp panicprk_main.elf $SD/
[ -f main_r4650_BE_4MB.bin ] && cp main_r4650_BE_4MB.bin $SD/
if [ -d panicprkrom ]; then mkdir -p $SD/panicprkrom && cp panicprkrom/* $SD/panicprkrom/ 2>/dev/null || true; fi
# gli asset gia' estratti dalla versione PC vanno bene anche qui (stesso ordine dei byte): risparmiano il primo avvio lento
for a in assets dist/windows/assets dist/linux/assets; do
  if [ -f $a/tex.bin ]; then mkdir -p $SD/assets && cp $a/*.bin $SD/assets/ && echo "asset copiati da $a"; break; fi
done
echo "BUILD_OK -> copia il contenuto di dist/switch/sd/ nella radice della SD della Switch"
