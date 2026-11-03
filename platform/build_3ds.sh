#!/bin/sh
# Compila Panic Park ricompilato per Nintendo 3DS (New 3DS / New 2DS XL) e prepara la cartella da copiare sulla SD.
#   sudo dkp-pacman -S 3ds-dev                      (una volta: devkitARM, libctru, citro3d, citro2d, picasso)
#   sh platform/build_3ds.sh                        -> dist/3ds/sd/3ds/panicprk.3dsx + dist/3ds/sd/3ds/panicprk/
# Variabili: JOBS (compilazioni in parallelo, default = numero di core), OPT (default -O2)
set -e
cd "$(dirname "$0")/.."
: "${DEVKITPRO:=/opt/devkitpro}"; export DEVKITPRO
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
[ -f "$DEVKITARM/3ds_rules" ] || { echo "manca devkitARM: sudo dkp-pacman -S 3ds-dev"; exit 1; }
[ -f "$DEVKITPRO/libctru/lib/libcitro3d.a" ] || { echo "manca citro3d: sudo dkp-pacman -S 3ds-dev (oppure 3ds-citro3d 3ds-citro2d)"; exit 1; }
[ -f "$DEVKITPRO/libctru/lib/libcitro2d.a" ] || { echo "manca citro2d: sudo dkp-pacman -S 3ds-citro2d"; exit 1; }
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 2)}
make -f Makefile.3ds -j"$JOBS" ${OPT:+OPT="$OPT"}
SD=dist/3ds/sd/3ds/panicprk
mkdir -p $SD/lang
cp panicprk.3dsx dist/3ds/sd/3ds/
cp platform/lang/*.json $SD/lang/
if ls platform/ui/*.png >/dev/null 2>&1; then mkdir -p $SD/ui && cp platform/ui/*.png $SD/ui/; fi   # sfondo e logo del menu (facoltativi)
# file del gioco: solo se sono qui (sono i tuoi, non vanno distribuiti)
[ -f panicprk_main.elf ] && cp panicprk_main.elf $SD/
[ -f main_r4650_BE_4MB.bin ] && cp main_r4650_BE_4MB.bin $SD/
if [ -d panicprkrom ]; then mkdir -p $SD/panicprkrom && cp panicprkrom/* $SD/panicprkrom/ 2>/dev/null || true; fi
# asset gia' estratti (stesso ordine dei byte di PC e Switch): sul 3DS sono NECESSARI, convertire le ROM li' non ci sta in memoria
ok=0
for a in assets dist/windows/assets dist/switch/sd/switch/panicprk/assets dist/linux/assets; do
  if [ -f $a/tex.bin ] && [ -f $a/data.bin ]; then mkdir -p $SD/assets && cp $a/*.bin $SD/assets/ && echo "asset copiati da $a"; ok=1; break; fi
done
[ $ok = 1 ] || echo "ATTENZIONE: nessuna cartella assets trovata: avvia prima la versione PC o Switch (crea assets/), poi rilancia"
echo "BUILD_OK -> copia il contenuto di dist/3ds/sd/ nella radice della SD del 3DS"
