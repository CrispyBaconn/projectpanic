# Recompiled Panic Park – Nintendo Switch (homebrew)

`.nro` version for a Switch with CFW (Atmosphère + Homebrew Menu). It is the same code as the PC version
(recompiled MIPS code, emulated H8/3002, native ASCA I/O board, 3D, C352 audio); the Switch is little endian like the PC,
so the assets extracted by the PC version work as they are.

## Building (in WSL)

You need devkitPro with the Switch packages:

```sh
sudo dkp-pacman -S switch-dev switch-sdl2
cd /path/to/recomp
sh platform/build_switch.sh
```

The result is `panicprk.nro`; the script also prepares `dist/switch/sd/switch/panicprk/` with everything that goes
on the SD card (including the assets already extracted into `dist/windows/assets`, if present).

## On the SD card

```
sd:/switch/panicprk/
    panicprk.nro
    panicprk_main.elf
    main_r4650_BE_4MB.bin
    panicprkrom/            all the panicprk ROMs (including pnp1wavel.2c / pnp1waveh.2a for audio)
    lang/                   en.json, it.json
    assets/                 (optional) the same .bin files as the PC version
```

Without `assets/` they are created on the first launch (≈85 MB, slow). `nvram.bin`, `panicprk.ini` and `log.txt` are created too.

**Start it in "title takeover" mode** (hold R while opening any game): from the album, homebrew gets
little memory and less CPU.

## Controls (Joy-Con / Pro Controller)

| Button | Function |
|---|---|
| D-pad / left stick | movement |
| A | coin |
| + | Player 1 start |
| Y | Player 2 start |
| B | Enter / button 1 |
| L | Test |
| R | Config DIP (calibration) |
| − and + together | exit |

Everything can be remapped from the start screen, like on PC. (The A/B names follow SDL's mapping: if they look swapped,
remap them from the start screen.)

## Differences from the PC

- The 3D starts at 640×480 (the "like the screen" option is there, but it costs CPU); always full screen, 1280×720.
- No POSIX signals/timers: the 1 kHz tick and the 59.9 Hz VBlank are checked by the recompiled code
  (same mechanism as on the Wii U, tested on Linux with `-DS23_POLL_TICK`).
- The time for the game's RTC comes from the Switch time service (time zone included).
- The POST is skipped by default.

## Status

- Verified: the code compiles for AArch64 (syntax check with the standard headers) and the signal-free
  tick mechanism works on Linux.
- **Not verified**: building with libnx and running on a real Switch. If something goes wrong, `log.txt`
  in the app folder says how far it got; the line `H8: …% of real time` tells whether the sub-CPU keeps up.

## 3D on the GPU
The Switch GPU draws the 3D (OpenGL ES 3): the CPU only prepares the polygons, which leaves time for the game and
the H8. It is the default choice ("3D drawing: graphics card" in the start screen, `gpu=1` in `panicprk.ini`).
Verified on PC with the same OpenGL ES path (same result as the CPU renderer, 0.3% of the pixels differ at the edges).
If it doesn't start, the game falls back to CPU drawing by itself and `log.txt` says why.
If `panicprk.ini` already contains `gpu=0` (saved by an earlier version) it has to be changed in the start screen.
