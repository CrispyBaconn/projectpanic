#!/bin/bash
# Compila tutte le versioni in una volta (da WSL, nella cartella recomp):
#   bash platform/build_all.sh              Windows + Switch + Wii U + Android  (il 3DS a parte: ... build_all.sh 3ds)
#   bash platform/build_all.sh windows wiiu solo quelle indicate
# Se una piattaforma fallisce si passa alla successiva; alla fine c'e' il riepilogo.
# Requisiti: mingw (Windows), devkitPro con switch-dev/switch-sdl2 e wiiu-dev/wiiu-sdl2, SDK/NDK Android (build_android.sh setup).
cd "$(dirname "$0")/.."
TARGETS=${*:-windows switch wiiu android}
: "${DEVKITPRO:=/opt/devkitpro}"; export DEVKITPRO
mkdir -p dist/log
declare -A RES
for t in $TARGETS; do
  echo; echo "================ $t ================"
  log="dist/log/build_$t.txt"
  case $t in
    windows) sh platform/build_windows.sh > "$log" 2>&1 ;;
    switch)  { make -f Makefile.switch clean && sh platform/build_switch.sh; } > "$log" 2>&1 ;;
    wiiu)    { make -f Makefile.wiiu clean && sh platform/build_wiiu.sh; } > "$log" 2>&1 ;;
    android) bash platform/build_android.sh > "$log" 2>&1 ;;
    3ds)     { make -f Makefile.3ds clean && sh platform/build_3ds.sh; } > "$log" 2>&1 ;;
    *) echo "piattaforma sconosciuta: $t (windows switch wiiu android 3ds)"; RES[$t]="sconosciuta"; continue ;;
  esac
  if grep -q "BUILD_OK" "$log"; then RES[$t]="OK"; grep "BUILD_OK" "$log" | tail -1
  else RES[$t]="ERRORE (vedi $log)"; echo "--- ultime righe di $log:"; tail -15 "$log"; fi
done
echo; echo "================ riepilogo ================"
for t in $TARGETS; do printf "%-8s %s\n" "$t" "${RES[$t]}"; done
echo
echo "Windows: dist/windows/PanicPark.exe"
echo "Switch:  copia dist/switch/sd/ nella radice della SD"
echo "Wii U:   copia dist/wiiu/sd/ nella radice della SD"
echo "Android: dist/android/PanicPark.apk"
echo "3DS:     copia dist/3ds/sd/ nella radice della SD (bash platform/build_all.sh 3ds)"
