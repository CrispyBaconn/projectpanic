# Recompiled Panic Park – Android

APK for Android 5.0+ phones/tablets (arm64). Same code as the PC version, with SDL2 built from `platform/SDL2-src`.

## Building (in WSL)

```sh
sudo apt install openjdk-17-jdk unzip wget
cd /path/to/recomp
bash platform/build_android.sh setup     # once: Android SDK and NDK in ~/android-sdk (~3 GB)
bash platform/build_android.sh           # -> dist/android/PanicPark.apk
```
The Gradle project is created in `~/panicprk_android` (on the WSL disk, much faster than /mnt/c).
For old 32-bit phones: `ABIS="arm64-v8a armeabi-v7a" bash platform/build_android.sh`.

## On the phone
1. Install `PanicPark.apk` and start it.
2. The "All files access" screen opens: enable it for Panic Park and go back.
   The app creates the **PanicPark** folder in phone storage (next to Download, DCIM...).
3. Copy into `PanicPark/`: `panicprk_main.elf`, `main_r4650_BE_4MB.bin`, the `panicprkrom/` folder, the
   `lang/` folder and (optionally) `dist/windows/assets` as `assets/`. With the phone's file manager or from a PC over USB.
4. Reopen the app. If something is missing a message lists the missing files.
Without the permission the private folder `Android/data/com.recomp.panicpark/files/` still works.
`log.txt`, `nvram.bin` and `panicprk.ini` are also written to the folder in use.

## Touch controls
- **drag the finger right/left, like on a trackpad**: the finger moves player 1's lever (the faster
  you move the finger, the further it moves); lifting the finger leaves the lever where it is. The small bar at the bottom shows the position.
  With `touch=0` in `panicprk.ini` you get joystick mode (the lever returns to the centre when you lift the finger).
- **short tap**: Start + confirm (starts the game, selects in the game menus)
- **top-left corner** ("COIN" box): inserts a coin
- **top-right corner** ("SERVICE" box): config DIP (service menu)
- **Back**: exits
- Start screen: tap a row to select it, tap it again to change it; dead zone/sensitivity: the left half
  decreases, the right half increases. "START THE GAME" starts on the first tap.
- A Bluetooth controller works like on PC.

## Status
Not tested on a real phone: only checked that the code compiles for AArch64.
If something goes wrong, `log.txt` in the app folder says how far it got.

## 3D on the GPU (experimental)
3D drawing with the phone's GPU (OpenGL ES 3.0) is there but off by default: to try it choose
"3D drawing: graphics card" in the start screen (or `gpu=1` in `PanicPark/panicprk.ini`). If it doesn't start, the game
falls back to CPU drawing by itself.
