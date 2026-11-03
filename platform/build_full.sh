#!/bin/sh
# Compila tutto il programma ricompilato (-O0 per velocita' di compilazione) e lo collega con lo stub.
set -e
cd "$(dirname "$0")/.."
OPT=${OPT:--O0}; BD=${BD:-build_full}
mkdir -p $BD
SDLF=""; SDLL=""
if [ -z "$NO_SDL" ] && command -v sdl2-config >/dev/null 2>&1; then
  SDLF="-DUSE_SDL $(sdl2-config --cflags)"; SDLL="$(sdl2-config --libs)"; echo "SDL2 trovato: finestra abilitata (S23_WINDOW=1)"
else
  echo "SDL2 non trovato: build senza finestra (per abilitarla: sudo apt install libsdl2-dev)"
fi
for f in gen_full/gen_*.cpp gen_full/dispatch.cpp; do
  o=$BD/$(basename $f .cpp).o
  [ -f $o ] && [ $o -nt $f ] && [ $o -nt runtime.h ] && continue
  g++ -std=c++17 $OPT -w -I. -Igen_full -c $f -o $o
done
g++ -std=c++17 -O1 -w -I. -Igen_full $SDLF -c platform/platform_stub.cpp -o $BD/platform_stub.o     # lo stub si ricompila sempre
g++ $BD/*.o -o $BD/panicprk_stub -lpthread $SDLL
echo BUILD_OK
