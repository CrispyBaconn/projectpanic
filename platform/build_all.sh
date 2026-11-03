#!/bin/bash
# Builds all versions at once (from WSL, in the recomp folder):
#   bash platform/build_all.sh              Windows + Switch + Wii U + Android  (3DS separately: ... build_all.sh 3ds)
#   bash platform/build_all.sh windows wiiu only the ones listed
# If a platform fails it moves on to the next; at the end there is the summary.
# Requirements: mingw (Windows), devkitPro with switch-dev/switch-sdl2 and wiiu-dev/wiiu-sdl2, Android SDK/NDK (build_android.sh setup).
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
    *) echo "unknown platform: $t (windows switch wiiu android 3ds)"; RES[$t]="unknown"; continue ;;
  esac
  if grep -q "BUILD_OK" "$log"; then RES[$t]="OK"; grep "BUILD_OK" "$log" | tail -1
  else RES[$t]="ERROR (see $log)"; echo "--- last lines of $log:"; tail -15 "$log"; fi
done
echo; echo "================ summary ================"
for t in $TARGETS; do printf "%-8s %s\n" "$t" "${RES[$t]}"; done
echo
echo "Windows: dist/windows/PanicPark.exe"
echo "Switch:  copy dist/switch/sd/ to the root of the SD card"
echo "Wii U:   copy dist/wiiu/sd/ to the root of the SD card"
echo "Android: dist/android/PanicPark.apk"
echo "3DS:     copy dist/3ds/sd/ to the root of the SD card (bash platform/build_all.sh 3ds)"
