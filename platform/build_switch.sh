#!/bin/sh
# Builds recompiled Panic Park for Nintendo Switch (homebrew) and prepares the folder to copy to the SD card.
#   sudo dkp-pacman -S switch-dev switch-sdl2      (once)
#   sh platform/build_switch.sh                     -> dist/switch/sd/switch/panicprk/
# Variables: JOBS (parallel compilations, default = number of cores), OPT (default -O2)
set -e
cd "$(dirname "$0")/.."
if command -v python3 >/dev/null 2>&1; then python3 tools/make_icons.py || true; fi   # app icons from platform/ui/icon.png (if you have it)
# a build folder made from another path (WSL /mnt/c/... vs the devkitPro bash /c/...) has dependency files that point
# to the old path: make would stop with "No rule to make target"; start again from clean
if [ -d build_switch ] && [ "$(cat build_switch/.srcdir 2>/dev/null)" != "$(pwd)" ]; then rm -rf build_switch; fi
mkdir -p build_switch && pwd > build_switch/.srcdir
: "${DEVKITPRO:=/opt/devkitpro}"; export DEVKITPRO
export DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"
[ -f "$DEVKITPRO/libnx/switch_rules" ] || { echo "libnx missing: sudo dkp-pacman -S switch-dev switch-sdl2"; exit 1; }
[ -f "$DEVKITPRO/portlibs/switch/lib/libSDL2.a" ] || { echo "SDL2 for Switch missing: sudo dkp-pacman -S switch-sdl2"; exit 1; }
export PATH="$DEVKITPRO/tools/bin:$DEVKITA64/bin:$PATH"
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 2)}
make -f Makefile.switch -j"$JOBS" ${OPT:+OPT="$OPT"}
SD=dist/switch/sd/switch/panicprk
mkdir -p $SD/lang
cp panicprk.nro $SD/
cp platform/lang/*.json $SD/lang/
if ls platform/ui/*.png >/dev/null 2>&1; then mkdir -p $SD/ui && cp platform/ui/*.png $SD/ui/; fi   # menu background and logo (optional)
# game files: only if they are here (they are yours, they must not be distributed)
[ -f panicprk_main.elf ] && cp panicprk_main.elf $SD/
[ -f main_r4650_BE_4MB.bin ] && cp main_r4650_BE_4MB.bin $SD/
if [ -d panicprkrom ]; then mkdir -p $SD/panicprkrom && cp panicprkrom/* $SD/panicprkrom/ 2>/dev/null || true; fi
# the assets already extracted by the PC version work here too (same byte order): they save the slow first launch
for a in assets dist/windows/assets dist/linux/assets; do
  if [ -f $a/tex.bin ]; then mkdir -p $SD/assets && cp $a/*.bin $SD/assets/ && echo "assets copied from $a"; break; fi
done
echo "BUILD_OK -> copy the contents of dist/switch/sd/ to the root of the Switch SD card"
