#!/bin/sh
# Compila Panic Park ricompilato per Linux (64 bit), ottimizzato, in dist/linux/.
#   sudo apt install g++ libsdl2-dev
#   sh platform/build_linux.sh              -> dist/linux/panicprk (+ avvia.sh)
# Variabili: OPT (default -O2), JOBS (compilazioni in parallelo, default = numero di core)
set -e
cd "$(dirname "$0")/.."
CXX=${CXX:-g++}; OPT=${OPT:--O2}; BD=build_linux; OUT=dist/linux
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 2)}
command -v sdl2-config >/dev/null 2>&1 || { echo "manca SDL2: sudo apt install libsdl2-dev"; exit 1; }
mkdir -p $BD $OUT
ls gen_full/gen_*.cpp gen_full/dispatch.cpp | while read f; do
  o=$BD/$(basename $f .cpp).o
  [ -f $o ] && [ $o -nt $f ] && [ $o -nt runtime.h ] && continue
  echo "$f"
done | xargs -r -P "$JOBS" -I{} sh -c 'f={}; o='"$BD"'/$(basename $f .cpp).o; echo "  $f"; '"$CXX"' -std=c++17 '"$OPT"' -w -I. -Igen_full -c $f -o $o'
$CXX -std=c++17 -O2 -w -I. -Igen_full -DUSE_SDL $(sdl2-config --cflags) -c platform/platform_stub.cpp -o $BD/platform_stub.o
$CXX $BD/*.o -o $OUT/panicprk -lpthread $(sdl2-config --libs)
mkdir -p $OUT/lang && cp platform/lang/*.json $OUT/lang/          # traduzioni dei menu
if ls platform/ui/*.png >/dev/null 2>&1; then mkdir -p $OUT/ui && cp platform/ui/*.png $OUT/ui/; fi   # sfondo e logo del menu (facoltativi)
cat > $OUT/avvia.sh <<'EOF'
#!/bin/sh
# Avvia Panic Park. Senza argomenti cerca panicprk_main.elf, main_r4650_BE_4MB.bin e le ROM nella cartella corrente.
# ROMDIR=cartella per indicare dove sono le ROM.
cd "$(dirname "$0")"
S23_WINDOW=1 exec ./panicprk panicprk_main.elf main_r4650_BE_4MB.bin 1000000000 0 "${ROMDIR:-.}"
EOF
chmod +x $OUT/avvia.sh
echo "BUILD_OK -> $OUT/panicprk"
