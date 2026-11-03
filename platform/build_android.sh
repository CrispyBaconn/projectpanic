#!/bin/bash
# Builds recompiled Panic Park for Android (APK, arm64) from WSL/Linux, with SDL2 (platform/SDL2-src).
#
#   bash platform/build_android.sh setup     once: downloads the JDK-free Android tools (SDK, NDK) into ~/android-sdk
#   bash platform/build_android.sh           builds -> dist/android/PanicPark.apk
#
# Needs Java 17:  sudo apt install openjdk-17-jdk unzip
# The Gradle project is created in ~/panicprk_android (WSL disk: on /mnt/c Gradle is very slow);
# the sources stay in the recomp folder and are compiled from there.
# Variables: ANDROID_HOME (default ~/android-sdk), ABIS (default arm64-v8a; e.g. "arm64-v8a armeabi-v7a")
set -e
cd "$(dirname "$0")/.."
SRC=$(pwd)
: "${ANDROID_HOME:=$HOME/android-sdk}"; export ANDROID_HOME ANDROID_SDK_ROOT=$ANDROID_HOME
NDK_VER=27.2.12479018
PROJ=${PROJ:-$HOME/panicprk_android}
ABIS=${ABIS:-arm64-v8a}
APPID=com.recomp.panicpark

if [ "$1" = "setup" ]; then
  command -v java >/dev/null || { echo "Java missing: sudo apt install openjdk-17-jdk unzip"; exit 1; }
  mkdir -p "$ANDROID_HOME/cmdline-tools"
  if [ ! -x "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" ]; then
    echo "downloading the Android tools..."
    wget -q --show-progress -O /tmp/cmdtools.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
    rm -rf /tmp/cmdtools && unzip -q /tmp/cmdtools.zip -d /tmp/cmdtools && rm -rf "$ANDROID_HOME/cmdline-tools/latest"
    mv /tmp/cmdtools/cmdline-tools "$ANDROID_HOME/cmdline-tools/latest"
  fi
  SDKM="$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager"
  yes | "$SDKM" --licenses >/dev/null || true
  "$SDKM" "platform-tools" "platforms;android-35" "build-tools;35.0.0" "ndk;$NDK_VER"
  echo "SETUP_OK: now run  bash platform/build_android.sh"
  exit 0
fi

[ -d "$ANDROID_HOME/ndk/$NDK_VER" ] || { echo "Android SDK/NDK missing: first run  bash platform/build_android.sh setup"; exit 1; }
[ -d platform/SDL2-src/android-project ] || { echo "platform/SDL2-src missing (SDL2 sources)"; exit 1; }
command -v java >/dev/null || { echo "Java missing: sudo apt install openjdk-17-jdk"; exit 1; }

# ---- project: copy of SDL's template (first time only), then our files are rewritten every time
if [ ! -f "$PROJ/gradlew" ]; then
  echo "creating the project in $PROJ"
  mkdir -p "$PROJ"
  (cd platform/SDL2-src/android-project && tar --exclude=.gradle --exclude=build -cf - .) | (cd "$PROJ" && tar xf -)
  chmod +x "$PROJ/gradlew"
fi
rm -rf "$PROJ/app/jni/SDL"; ln -s "$SRC/platform/SDL2-src" "$PROJ/app/jni/SDL"
echo "sdk.dir=$ANDROID_HOME" > "$PROJ/local.properties"

# app/build.gradle: app identifier, ABI, NDK
G="$PROJ/app/build.gradle"
grep -q "applicationId" "$G" || sed -i "s/minSdkVersion 21/applicationId \"$APPID\"\n        minSdkVersion 21/" "$G"
grep -q "ndkVersion" "$G" || sed -i "s/compileSdkVersion 35/compileSdkVersion 35\n    ndkVersion \"$NDK_VER\"/" "$G"
Q=$(for a in $ABIS; do printf "'%s', " "$a"; done | sed 's/, $//')
sed -i "s/abiFilters .*/abiFilters $Q/" "$G"
# our activity (asks for all files access for the public PanicPark folder) and permissions
mkdir -p "$PROJ/app/src/main/java/com/recomp/panicpark"
cp "$SRC/platform/android/PanicParkActivity.java" "$PROJ/app/src/main/java/com/recomp/panicpark/"
M="$PROJ/app/src/main/AndroidManifest.xml"
sed -i 's#android:name="SDLActivity"#android:name="com.recomp.panicpark.PanicParkActivity"#' "$M"
grep -q MANAGE_EXTERNAL_STORAGE "$M" || sed -i 's#<uses-permission android:name="android.permission.VIBRATE" />#<uses-permission android:name="android.permission.VIBRATE" />\n    <uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE" />\n    <uses-permission android:name="android.permission.WRITE_EXTERNAL_STORAGE" />\n    <uses-permission android:name="android.permission.MANAGE_EXTERNAL_STORAGE" />#' "$M"
grep -q requestLegacyExternalStorage "$M" || sed -i 's#<application android:label="@string/app_name"#<application android:label="@string/app_name" android:requestLegacyExternalStorage="true"#' "$M"

# display name
sed -i 's#<string name="app_name">.*</string>#<string name="app_name">Panic Park</string>#' "$PROJ/app/src/main/res/values/strings.xml"

# jni: SDL + our "main" module (built from the sources in recomp)
cat > "$PROJ/app/jni/Application.mk" <<EOF
APP_ABI := $ABIS
APP_PLATFORM := android-21
APP_STL := c++_shared
APP_OPTIM := release
EOF
GEN=$(cd "$SRC" && ls gen_full/gen_*.cpp gen_full/dispatch.cpp | tr '\n' ' ')
cat > "$PROJ/app/jni/src/Android.mk" <<EOF
LOCAL_PATH := $SRC
include \$(CLEAR_VARS)
LOCAL_MODULE := main
LOCAL_C_INCLUDES := $SRC $SRC/gen_full $SRC/platform/SDL2-src/include
LOCAL_SRC_FILES := $GEN platform/platform_stub.cpp
LOCAL_CPPFLAGS := -std=c++17 -O2 -w -DUSE_SDL -DS23_POLL_TICK -fno-strict-aliasing
LOCAL_SHARED_LIBRARIES := SDL2
LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -lOpenSLES -llog -landroid
include \$(BUILD_SHARED_LIBRARY)
EOF

cd "$PROJ"
./gradlew --no-daemon assembleDebug
cd "$SRC"
mkdir -p dist/android
cp "$PROJ/app/build/outputs/apk/debug/app-debug.apk" dist/android/PanicPark.apk
cat > dist/android/README.txt <<'EOF'
Recompiled Panic Park - Android
1. Install PanicPark.apk (allow "apps from unknown sources").
2. Start it.
3. On the first launch grant "All files access" (the screen opens by itself), then go back to the app:
   the PanicPark folder is created in phone storage (next to Download, DCIM...).
4. Copy into PanicPark/: panicprk_main.elf, main_r4650_BE_4MB.bin, the panicprkrom/ folder and the lang/ and ui/ folders (menu background and logo, optional)
   (with the phone's file manager or from a PC over USB). Optional: dist/windows/assets as assets/.
   Without permission the private folder Android/data/com.recomp.panicpark/files/ works too.
Controls: drag the finger right/left = movement; short tap = Start/confirm;
top-left corner = coin; Back button = exit. A Bluetooth controller works like on PC.
EOF
echo "BUILD_OK -> dist/android/PanicPark.apk"
