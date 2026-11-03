#!/bin/sh
# Compila Panic Park ricompilato per Windows (64 bit) partendo da Linux/WSL, con MinGW-w64.
#   sudo apt install g++-mingw-w64-x86-64-posix
#   sh platform/build_windows.sh            -> dist/windows/PanicPark.exe (un solo file, SDL2 incluso)
# Variabili: OPT (default -O2), JOBS (compilazioni in parallelo, default = numero di core)
set -e
cd "$(dirname "$0")/.."
CXX=${CXX:-x86_64-w64-mingw32-g++-posix}
command -v $CXX >/dev/null 2>&1 || CXX=x86_64-w64-mingw32-g++
command -v $CXX >/dev/null 2>&1 || { echo "manca MinGW-w64: sudo apt install g++-mingw-w64-x86-64-posix"; exit 1; }
OPT=${OPT:--O2}; BD=build_win; OUT=dist/windows
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 2)}
SDLV=2.30.9; SDLD=deps/SDL2-$SDLV/x86_64-w64-mingw32
if [ ! -f $SDLD/lib/libSDL2.a ]; then
  echo "scarico SDL2 $SDLV per MinGW..."
  mkdir -p deps
  curl -sSfL -o deps/sdl2-mingw.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-$SDLV/SDL2-devel-$SDLV-mingw.tar.gz
  tar xzf deps/sdl2-mingw.tar.gz -C deps
fi
mkdir -p $BD $OUT
# codice generato: in parallelo, solo i file cambiati
ls gen_full/gen_*.cpp gen_full/dispatch.cpp | while read f; do
  o=$BD/$(basename $f .cpp).o
  [ -f $o ] && [ $o -nt $f ] && [ $o -nt runtime.h ] && continue
  echo "$f"
done | xargs -r -P "$JOBS" -I{} sh -c 'f={}; o='"$BD"'/$(basename $f .cpp).o; echo "  $f"; '"$CXX"' -std=c++17 '"$OPT"' -w -I. -Igen_full -c $f -o $o'
$CXX -std=c++17 -O2 -w -I. -Igen_full -DUSE_SDL -I$SDLD/include/SDL2 -c platform/platform_stub.cpp -o $BD/platform_stub.o
$CXX $BD/*.o -o $OUT/PanicPark.exe -mconsole -static -static-libgcc -static-libstdc++ \
  -L$SDLD/lib -Wl,-Bstatic -lSDL2 -lwinpthread \
  -lsetupapi -lwinmm -limm32 -lversion -lole32 -loleaut32 -lgdi32 -luser32 -lshell32 -lcfgmgr32 -luuid -ladvapi32
mkdir -p $OUT/lang && cp platform/lang/*.json $OUT/lang/          # traduzioni dei menu
if ls platform/ui/*.png >/dev/null 2>&1; then mkdir -p $OUT/ui && cp platform/ui/*.png $OUT/ui/; fi   # sfondo e logo del menu (facoltativi)
cat > $OUT/LEGGIMI.txt <<'EOF'
Panic Park ricompilato - Windows
Metti in questa cartella (accanto a PanicPark.exe):
  panicprk_main.elf, main_r4650_BE_4MB.bin e tutte le ROM di panicprk (anche pnp1wavel.2c / pnp1waveh.2a per l'audio).
Poi fai doppio clic su avvio.bat (o su PanicPark.exe).
Se restano in dist\windows, avvio.bat prende elf/bin dalla cartella recomp e le ROM da recomp\panicprkrom.
Per usare un'altra cartella di ROM: PanicPark.exe panicprk_main.elf main_r4650_BE_4MB.bin 1000000000 0 C:\percorso\rom
(la finestra si apre sempre; S23_WINDOW=0 per usarlo senza finestra)
Tasti: F2 test, F3 DIP config, F8 luci e ombre, F9 ordine 3D, F10 dump 3D, F11 schermo intero, F12 screenshot, Esc esci.
EOF
# avvio.bat: cerca elf/bin/ROM accanto all'exe, altrimenti nella cartella del progetto (due livelli sopra)
cat > $OUT/avvio.bat <<'EOF'
@echo off
cd /d "%~dp0"
set BASE=.
if not exist "panicprk_main.elf" set BASE=..\..
set ROM=%BASE%
if exist "%BASE%\panicprkrom\pnp1vera.ic3" set ROM=%BASE%\panicprkrom
if not exist "%BASE%\panicprk_main.elf" (echo panicprk_main.elf non trovato & pause & exit /b 1)
echo ELF/BIN: %BASE%   ROM: %ROM%
start "" /wait /high PanicPark.exe "%BASE%\panicprk_main.elf" "%BASE%\main_r4650_BE_4MB.bin" 1000000000 0 "%ROM%"
pause
EOF
sed -i 's/$/\r/' $OUT/avvio.bat $OUT/LEGGIMI.txt
echo "BUILD_OK -> $OUT/PanicPark.exe (+ avvio.bat)"
