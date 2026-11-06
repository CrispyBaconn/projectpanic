#!/bin/sh
# Builds recompiled Panic Park for Windows (64-bit) from Linux/WSL, with MinGW-w64.
#   sudo apt install g++-mingw-w64-x86-64-posix
#   sh platform/build_windows.sh            -> dist/windows/PanicPark.exe (a single file, SDL2 included)
# Variables: OPT (default -O2), JOBS (parallel compilations, default = number of cores)
set -e
cd "$(dirname "$0")/.."
python3 tools/make_icons.py || true                  # app icons from platform/ui/icon.png (if you have it)
CXX=${CXX:-x86_64-w64-mingw32-g++-posix}
command -v $CXX >/dev/null 2>&1 || CXX=x86_64-w64-mingw32-g++
command -v $CXX >/dev/null 2>&1 || { echo "MinGW-w64 missing: sudo apt install g++-mingw-w64-x86-64-posix"; exit 1; }
OPT=${OPT:--O2}; BD=build_win; OUT=dist/windows
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 2)}
SDLV=2.30.9; SDLD=deps/SDL2-$SDLV/x86_64-w64-mingw32
if [ ! -f $SDLD/lib/libSDL2.a ]; then
  echo "downloading SDL2 $SDLV for MinGW..."
  mkdir -p deps
  curl -sSfL -o deps/sdl2-mingw.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-$SDLV/SDL2-devel-$SDLV-mingw.tar.gz
  tar xzf deps/sdl2-mingw.tar.gz -C deps
fi
mkdir -p $BD $OUT
# generated code: in parallel, only the changed files
ls gen_full/gen_*.cpp gen_full/dispatch.cpp | while read f; do
  o=$BD/$(basename $f .cpp).o
  [ -f $o ] && [ $o -nt $f ] && [ $o -nt runtime.h ] && continue
  echo "$f"
done | xargs -r -P "$JOBS" -I{} sh -c 'f={}; o='"$BD"'/$(basename $f .cpp).o; echo "  $f"; '"$CXX"' -std=c++17 '"$OPT"' -w -I. -Igen_full -c $f -o $o'
$CXX -std=c++17 -O2 -w -I. -Igen_full -DUSE_SDL -I$SDLD/include/SDL2 -c platform/platform_stub.cpp -o $BD/platform_stub.o
# executable icon (build_icons/icon.ico, made by tools/make_icons.py)
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
if [ -f build_icons/icon.ico ] && command -v $WINDRES >/dev/null 2>&1; then
  echo '1 ICON "build_icons/icon.ico"' > $BD/icon.rc && $WINDRES $BD/icon.rc -O coff -o $BD/icon_res.o
else rm -f $BD/icon_res.o; fi
$CXX $BD/*.o -o $OUT/PanicPark.exe -mconsole -static -static-libgcc -static-libstdc++ \
  -L$SDLD/lib -Wl,-Bstatic -lSDL2 -lwinpthread \
  -lsetupapi -lwinmm -limm32 -lversion -lole32 -loleaut32 -lgdi32 -luser32 -lshell32 -lcfgmgr32 -luuid -ladvapi32
mkdir -p $OUT/lang && cp platform/lang/*.json $OUT/lang/          # menu translations
if ls platform/ui/*.png >/dev/null 2>&1; then mkdir -p $OUT/ui && cp platform/ui/*.png $OUT/ui/; fi   # menu background and logo (optional)
cat > $OUT/README.txt <<'EOF'
Recompiled Panic Park - Windows
Double-click PanicPark.exe. It finds by itself:
  panicprk_main.elf and main_r4650_BE_4MB.bin next to PanicPark.exe (or two folders up, when it stays in the
  project's dist\windows), and the ROMs in the panicprkrom folder beside them (or in the same folder).
To use another ROM folder: PanicPark.exe panicprk_main.elf main_r4650_BE_4MB.bin 1000000000 0 C:\path\rom
(the window always opens; S23_WINDOW=0 to run without a window)
Keys: F2 test, F3 config DIP, F8 lights and shadows, F9 3D order, F10 3D dump, F11 full screen, F12 screenshot, Esc exit.
EOF
rm -f $OUT/start.bat                                  # no longer needed: PanicPark.exe finds the files by itself
sed -i 's/$/\r/' $OUT/README.txt
echo "BUILD_OK -> $OUT/PanicPark.exe"
