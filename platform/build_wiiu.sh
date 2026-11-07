#!/bin/sh
# Builds recompiled Panic Park for Wii U (Aroma) and prepares the folder to copy to the SD card.
#   sudo dkp-pacman -S wiiu-dev wiiu-sdl2          (once)
#   sh platform/build_wiiu.sh                       -> dist/wiiu/sd/wiiu/apps/panicprk/
# Variables: JOBS (parallel compilations, default = number of cores), OPT (default -O2)
set -e
cd "$(dirname "$0")/.."
if command -v python3 >/dev/null 2>&1; then python3 tools/make_icons.py || true; fi   # app icons from platform/ui/icon.png (yours, or the repository one)
# a build folder made from another path (WSL /mnt/c/... vs the devkitPro bash /c/...) has dependency files that point
# to the old path: make would stop with "No rule to make target"; start again from clean
if [ -d build_wiiu ] && [ "$(cat build_wiiu/.srcdir 2>/dev/null)" != "$(pwd)" ]; then rm -rf build_wiiu; fi
mkdir -p build_wiiu && pwd > build_wiiu/.srcdir
: "${DEVKITPRO:=/opt/devkitpro}"; export DEVKITPRO
export DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"
[ -f "$DEVKITPRO/wut/share/wut_rules" ] || { echo "wut missing: sudo dkp-pacman -S wiiu-dev wiiu-sdl2"; exit 1; }
[ -f "$DEVKITPRO/portlibs/wiiu/lib/libSDL2.a" ] || { echo "SDL2 for Wii U missing: sudo dkp-pacman -S wiiu-sdl2"; exit 1; }
export PATH="$DEVKITPRO/tools/bin:$DEVKITPPC/bin:$PATH"
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 2)}
make -f Makefile.wiiu -j"$JOBS" ${OPT:+OPT="$OPT"}
SD=dist/wiiu/sd/wiiu/apps/panicprk
mkdir -p $SD/lang
cp panicprk.wuhb $SD/
cp platform/lang/*.json $SD/lang/
if ls platform/ui/*.png >/dev/null 2>&1; then mkdir -p $SD/ui && cp platform/ui/*.png $SD/ui/; fi   # menu background and logo (optional)
# game files: only if they are here (they are yours, they must not be distributed)
[ -f panicprk_main.elf ] && cp panicprk_main.elf $SD/
[ -f main_r4650_BE_4MB.bin ] && cp main_r4650_BE_4MB.bin $SD/
if [ -d panicprkrom ]; then mkdir -p $SD/panicprkrom && cp panicprkrom/* $SD/panicprkrom/ 2>/dev/null || true; fi
echo "BUILD_OK -> copy the contents of dist/wiiu/sd/ to the root of the Wii U SD card"
