# Project Panic

An unofficial native port of **Panic Park** (Namco, 1997, System 23 arcade hardware), built by statically recompiling
the game's MIPS code to C++. The sub-CPU (H8), the sound chip (C352) and the 3D hardware are emulated.
Runs on Windows, Linux, Android, Nintendo Switch, Wii U and New 3DS.

This repository contains no game code or data: everything is generated from **your own** Panic Park ROM set.
Not affiliated with Bandai Namco. License: BSD-3-Clause (parts derived from MAME, see `LICENSE`).

## Easy way: Project Panic Builder

A window that does everything below for you: choose your `panicprk.7z` (or `.zip`), tick the devices, press **BUILD**
and follow the percentage. Devices whose tools are not installed are greyed out, with the command to install them.

- **Windows**: double-click `ProjectPanicBuilder.bat` (it runs inside WSL; needs Windows 11 or WSLg).
- **Linux / WSL**: `sh tools/builder.sh`

The first time install Tk for Python in WSL/Linux: `sudo apt install python3-tk p7zip-full`.
The results end up in `dist/`, as with the manual steps.

## 1. Generate the code (all platforms)

You need the Panic Park ROM set **`panicprk.7z`** (not included). Extract it into a `panicprkrom/` folder inside the
project, so that `panicprkrom/` directly contains the ROM files (`pnp2vera.ic1`, `pnp2vera.ic2`, `pnp1cgll.4m`, ...). Then:

```sh
sudo apt install p7zip-full                  # once, to open .7z files
7z x panicprk.7z -opanicprkrom
python3 tools/extract_rom.py panicprkrom     # -> panicprk_main.elf + main_r4650_BE_4MB.bin
pip install pyelftools
python3 mips2cpp.py --elf panicprk_main.elf --funcs functions.csv --out gen_full
```

`extract_rom.py` joins the two halves of the main program ROM (`pnp2vera.ic2` + `pnp2vera.ic1`) into
`main_r4650_BE_4MB.bin` and takes the game's executable (`panicprk_main.elf`) out of it.

## 2. Build

All builds run on Linux or WSL. The result ends up in `dist/`.

| Device | Install once | Build | Output |
|---|---|---|---|
| Linux | `sudo apt install g++ libsdl2-dev` | `sh platform/build_linux.sh` | `dist/linux/` |
| Windows | `sudo apt install g++-mingw-w64-x86-64-posix` | `sh platform/build_windows.sh` | `dist/windows/PanicPark.exe` |
| Android | `sudo apt install openjdk-17-jdk unzip wget`<br>`bash platform/build_android.sh setup` | `bash platform/build_android.sh` | `dist/android/PanicPark.apk` |
| Switch | [devkitPro](https://devkitpro.org/wiki/Getting_Started) + `sudo dkp-pacman -S switch-dev switch-sdl2` | `sh platform/build_switch.sh` | `dist/switch/sd/` |
| Wii U (Aroma) | devkitPro + `sudo dkp-pacman -S wiiu-dev wiiu-sdl2` | `sh platform/build_wiiu.sh` | `dist/wiiu/sd/` |
| New 3DS | devkitPro + `sudo dkp-pacman -S 3ds-dev` | `sh platform/build_3ds.sh` | `dist/3ds/sd/` |

`bash platform/build_all.sh` builds Windows, Switch, Wii U and Android in one go.

## 3. Run

Copy `panicprk_main.elf`, `main_r4650_BE_4MB.bin` and a `panicprkrom/` folder with all the Panic Park ROMs next to the program.

- **Windows**: double-click `start.bat`.
- **Linux**: `ROMDIR=panicprkrom ./run.sh`.
- **Android**: copy the files into the `PanicPark` folder in phone storage (created on the first launch).
- **Switch / Wii U / 3DS**: copy the contents of `dist/<console>/sd/` to the root of the SD card. The build scripts already
  add the files above if they are in the project folder. The 3DS also needs the `assets/` folder that the PC version
  creates on its first run.
