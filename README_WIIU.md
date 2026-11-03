# Recompiled Panic Park – Wii U (Aroma)

`.wuhb` version for a Wii U running the **Aroma** environment. It is the same code as the PC version (recompiled MIPS code,
emulated H8/3002, native ASCA I/O board, software 3D, C352 audio): only the operating system underneath and the byte order change.

## Building (in WSL)

You need devkitPro with the Wii U packages:

```sh
sudo dkp-pacman -S wiiu-dev wiiu-sdl2
cd /path/to/recomp
sh platform/build_wiiu.sh
```

The result is `panicprk.wuhb`; the script also prepares `dist/wiiu/sd/wiiu/apps/panicprk/` with everything that goes on the SD card.

## On the SD card

```
sd:/wiiu/apps/panicprk/
    panicprk.wuhb
    panicprk_main.elf
    main_r4650_BE_4MB.bin
    panicprkrom/            all the panicprk ROMs (including pnp1wavel.2c / pnp1waveh.2a for audio)
    lang/                   en.json, it.json
```

On the first launch `assets_be/` is created (≈85 MB: converted textures, models, audio samples and data ROM),
along with `nvram.bin`, `panicprk.ini` and `log.txt`. The first launch is slow because it has to write the assets to the SD card.
The PC assets (`assets/`) do **not** work: on the Wii U the data is big endian, which is why the folder is different.

## Controls (GamePad or Pro Controller)

| Button | Function |
|---|---|
| D-pad / left stick | movement |
| A | coin |
| + (Start) | Player 1 start |
| Y | Player 2 start |
| B | Enter / button 1 |
| L | Test |
| R | Config DIP (calibration) |
| HOME | back to Aroma |

Everything can be remapped from the start screen, like on PC.

## Differences from the PC

- The 3D starts at 640×480 (the "like the screen" option is there but the Wii U CPU can hardly cope).
- Always full screen; image on the TV and the GamePad.
- No POSIX signals/timers: the 1 kHz tick and the 59.9 Hz VBlank are checked by the recompiled code
  (same mechanism already tested on Linux with `-DS23_POLL_TICK`).
- Threads: MIPS code on core 2, H8 on core 0, window/3D/audio on core 1.
- The POST is skipped by default.

## Status

- Verified: a 32-bit big-endian PowerPC build (same kind of CPU as the Wii U) runs correctly under
  emulation: boot, attract, 3D, text, gameplay.
- Speed is the main risk. The Espresso (3 PowerPC 750 cores at 1.24 GHz) is much slower than a PC; in the log
  the line `H8: …% of real time` tells whether the sub-CPU keeps up (below ~95% communication errors
  or slowdowns appear). The on-screen indicator's second line shows how long each part of the frame takes.
