#!/bin/sh
# Builds recompiled Panic Park for Android (APK). The work is done by tools/build_android.py, which also runs on
# Windows without WSL; the first time it downloads Java 17 (if missing), the Android SDK/NDK and the SDL2 sources.
#   sh platform/build_android.sh            -> dist/android/PanicPark.apk
#   sh platform/build_android.sh setup      only installs the tools
# Variables: ANDROID_HOME (default ~/android-sdk), ABIS (default arm64-v8a), PROJ (default ~/panicprk_android)
cd "$(dirname "$0")/.."
exec python3 tools/build_android.py "$@"
