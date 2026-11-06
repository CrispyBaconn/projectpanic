# Project Panic

An unofficial native port of **Panic Park** (Namco, 1997, System 23 arcade hardware), built by statically recompiling
the game's MIPS code to C++. The sub-CPU (H8), the sound chip (C352) and the 3D hardware are emulated.
Runs on Windows, Linux, Android, Nintendo Switch, Wii U and New 3DS.

This repository contains no game code or data: everything is generated from **your own** Panic Park ROM set.
Not affiliated with Bandai Namco. License: BSD-3-Clause (parts derived from MAME, see `LICENSE`).

## Easy way: Project Panic Builder

`ProjectPanicBuilder.py` is a window that does all the steps below for you, on **Windows** and **Linux** alike:

1. choose your `panicprk.7z` (or `.zip`),
2. tick the devices,
3. press **BUILD** and follow the percentage.

| Choose the ROM and the devices | Building | Done |
|---|---|---|
| ![Builder: start](docs/builder_start.png) | ![Builder: building](docs/builder_progress.png) | ![Builder: done](docs/builder_done.png) |

Start it:
- **Windows**: install [Python 3](https://www.python.org/downloads/) (tick "Add python.exe to PATH"), then double-click
  `ProjectPanicBuilder.py`. No WSL needed: the first Windows build downloads the compiler (LLVM-MinGW) by itself.
  Android works on Windows too: if Android Studio is installed it uses its Java and SDK, otherwise the first build
  downloads them (~3 GB) by itself.
  Switch, Wii U and 3DS work from Windows with the [devkitPro installer](https://devkitpro.org/wiki/Getting_Started)
  (in `C:\devkitPro`, also when the builder runs in WSL). If some packages are missing (for example `switch-sdl2`,
  `wiiu-sdl2`), the "Install the missing devkitPro packages" button installs them. Only the Linux build needs Linux or WSL.
- **Linux / WSL**: `sudo apt install python3-tk p7zip-full`, then `python3 ProjectPanicBuilder.py`.

Devices whose tools are missing are greyed out, with what to install. If a build fails the window says why, and
"Show details" opens the full log. The results end up in `dist/`; "Open the dist folder" opens it.
The window speaks the language of Windows (or of the Linux system) when it is one of English, Italian, German, French,
Spanish, Portuguese, Russian, Japanese or Simplified Chinese, and English otherwise; the menu at the top right changes it
and the choice is remembered.

## Languages

The game menu has the same 9 languages (`platform/lang/*.json`) and starts in the language of the system; change it in
Settings → Game → Menu language. To add a language, copy `en.json`, translate it and, if it uses letters the menu fonts
lack, run `python3 tools/bake_ui_font.py` (needs Pillow and `fonts-noto-cjk`). On 3DS, Chinese appears only on
Chinese consoles, whose system font has the simplified characters.

Optional: put your own square picture in `platform/ui/icon.png` and it becomes the icon everywhere: the Windows
executable, the game and builder windows, Android, Switch, Wii U and 3DS (`tools/make_icons.py`, run by the builds,
makes every size; it needs Pillow, which the builder installs by itself). Like the menu logo and background, the icon
is not part of the repository.

## Manual build

### 1. Generate the code (all platforms)

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

### 2. Build

The commands below are for Linux or WSL (on Windows without WSL use the builder above). The result ends up in `dist/`.

| Device | Install once | Build | Output |
|---|---|---|---|
| Linux | `sudo apt install g++ libsdl2-dev` | `sh platform/build_linux.sh` | `dist/linux/` |
| Windows | `sudo apt install g++-mingw-w64-x86-64-posix` | `sh platform/build_windows.sh` | `dist/windows/PanicPark.exe` |
| Android | nothing: uses Android Studio if installed, otherwise downloads Java and the SDK (~3 GB) | `python3 tools/build_android.py` (also on Windows) | `dist/android/PanicPark.apk` |
| Switch | [devkitPro](https://devkitpro.org/wiki/Getting_Started) + `sudo dkp-pacman -S switch-dev switch-sdl2` | `sh platform/build_switch.sh` | `dist/switch/sd/` |
| Wii U (Aroma) | devkitPro + `sudo dkp-pacman -S wiiu-dev wiiu-sdl2` | `sh platform/build_wiiu.sh` | `dist/wiiu/sd/` |
| New 3DS | devkitPro + `sudo dkp-pacman -S 3ds-dev` | `sh platform/build_3ds.sh` | `dist/3ds/sd/` |

`bash platform/build_all.sh` builds Windows, Switch, Wii U and Android in one go.

### 3. Run

Copy `panicprk_main.elf`, `main_r4650_BE_4MB.bin` and a `panicprkrom/` folder with all the Panic Park ROMs next to the program.

- **Windows**: double-click `PanicPark.exe` (it finds the files and the ROMs by itself).
- **Linux**: `ROMDIR=panicprkrom ./run.sh`.
- **Android**: copy the files into the `PanicPark` folder in phone storage (created on the first launch).
- **Switch / Wii U / 3DS**: copy the contents of `dist/<console>/sd/` to the root of the SD card. The build scripts already
  add the files above if they are in the project folder. The 3DS also needs the `assets/` folder that the PC version
  creates on its first run.
