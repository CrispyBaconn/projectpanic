#!/bin/sh
# Builds recompiled Panic Park for Nintendo 3DS (New 3DS / New 2DS XL) and prepares the folder to copy to the SD card.
#   sudo dkp-pacman -S 3ds-dev                      (once: devkitARM, libctru, citro3d, citro2d, picasso)
#   sh platform/build_3ds.sh                        -> dist/3ds/sd/3ds/panicprk.3dsx + dist/3ds/sd/3ds/panicprk/
# Variables: JOBS (parallel compilations, default = number of cores), OPT (default -O2)
set -e
cd "$(dirname "$0")/.."
: "${DEVKITPRO:=/opt/devkitpro}"; export DEVKITPRO
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
[ -f "$DEVKITARM/3ds_rules" ] || { echo "devkitARM missing: sudo dkp-pacman -S 3ds-dev"; exit 1; }
[ -f "$DEVKITPRO/libctru/lib/libcitro3d.a" ] || { echo "citro3d missing: sudo dkp-pacman -S 3ds-dev (or 3ds-citro3d 3ds-citro2d)"; exit 1; }
[ -f "$DEVKITPRO/libctru/lib/libcitro2d.a" ] || { echo "citro2d missing: sudo dkp-pacman -S 3ds-citro2d"; exit 1; }
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 2)}
make -f Makefile.3ds -j"$JOBS" ${OPT:+OPT="$OPT"}
SD=dist/3ds/sd/3ds/panicprk
mkdir -p $SD/lang
cp panicprk.3dsx dist/3ds/sd/3ds/
cp platform/lang/*.json $SD/lang/
if ls platform/ui/*.png >/dev/null 2>&1; then mkdir -p $SD/ui && cp platform/ui/*.png $SD/ui/; fi   # menu background and logo (optional)
# game files: only if they are here (they are yours, they must not be distributed)
[ -f panicprk_main.elf ] && cp panicprk_main.elf $SD/
[ -f main_r4650_BE_4MB.bin ] && cp main_r4650_BE_4MB.bin $SD/
if [ -d panicprkrom ]; then mkdir -p $SD/panicprkrom && cp panicprkrom/* $SD/panicprkrom/ 2>/dev/null || true; fi
# assets already extracted (same byte order as PC and Switch): on 3DS they are REQUIRED, converting the ROMs there doesn't fit in memory
ok=0
for a in assets dist/windows/assets dist/switch/sd/switch/panicprk/assets dist/linux/assets; do
  if [ -f $a/tex.bin ] && [ -f $a/data.bin ]; then mkdir -p $SD/assets && cp $a/*.bin $SD/assets/ && echo "assets copied from $a"; ok=1; break; fi
done
[ $ok = 1 ] || echo "WARNING: no assets folder found: run the PC or Switch version first (it creates assets/), then try again"
echo "BUILD_OK -> copy the contents of dist/3ds/sd/ to the root of the 3DS SD card"
