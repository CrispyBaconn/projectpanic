# Recompiled Panic Park on Nintendo 3DS (New 3DS / New 2DS XL)

The 3D is drawn by the **3DS GPU** (PICA200, through citro3d). The PICA200 has no pixel shaders, so the
System 23's 8-bit palettised textures are converted piece by piece (cells of 128x128 texels, with the polygon's
palette) into a texture cache on the GPU (three 1024x1024 atlases, so neighbouring polygons share draw calls);
every polygon is clipped on the CPU against the cells and against the viewport window, and lighting, fades and
transparency become vertex colours and combiner stages.
The code is in `platform/pica3d.h` (platform-independent) and `platform/platform_3ds.h` (citro3d/citro2d).

- Top screen: the game (320x240 centred), with the text layer and gamma.
- Bottom screen: PLAY / SETTINGS / EXIT menu; in game, COIN and MENU to tap and the statistics.
- Controls: Circle Pad or d-pad left/right = lever, A = button, START = start 1, Y = start 2, B or SELECT = coin,
  d-pad up/down = service menu, L = test, R = service DIP, SELECT+START or MENU = pause.
- Cores: 0 = drawing, 1 = H8 + audio (80% of the core), 2 = game (R4650). Audio with ndsp (needs `sdmc:/3ds/dspfirm.cdc`,
  created with DSP1; without it the game still runs, but silent).

## Building (WSL with devkitPro)
    sudo dkp-pacman -S 3ds-dev          (once: devkitARM, libctru, citro3d, citro2d, picasso)
    bash platform/build_all.sh 3ds      (or: sh platform/build_3ds.sh)

## On the SD card
Copy the contents of `dist/3ds/sd/` to the root of the SD card. The result must be:
- `sd:/3ds/panicprk.3dsx`
- `sd:/3ds/panicprk/` with `panicprk_main.elf`, `main_r4650_BE_4MB.bin`, `panicprkrom/`, `lang/`, `ui/` and **`assets/`**

The `assets/` folder is required on the 3DS: converting the ROMs on the 3DS doesn't fit in memory. The script copies it from
`dist/windows/assets` or from the Switch one (same format); if it isn't there, run the PC version once.

## Memory
The New 3DS gives a homebrew app about 120 MB. The normal heap is capped at 96 MB; everything else becomes
"linear" memory, used by the GPU (texture atlases, command buffer) and by the asset files that fit there.
The data ROM (16 MB) and the audio samples stay on the SD card and are read in pages. It doesn't fit on the old 3DS.
The `MEM` lines in `log.txt` show how the memory was split ("granted at startup" should be about 120 MB).
If the Homebrew Launcher was started in the old 3DS memory mode (about 60 MB, for example with a game or app still
open in the HOME Menu), the game stops at once and says so: close the other app and start it again.

## Performance
The main game CPU (R4650) and the H8 are emulated on the 804 MHz ARM11 cores, so the game may run slower than the real one.
The bottom screen (option "Statistics") and `log.txt` (`PERF 3DS` lines) show where the time goes:
gather (text layer), geo (polygon preparation), cells (clipping and texture uploads), submit (GPU commands), GPU and wait.

## What has and hasn't been verified
- Done: the `pica3d.h` renderer was tested on PC with fixed-function OpenGL (same combiners as the PICA200,
  `S23_PICATEST`, `S23_PICAATLAS`): same images as the software renderer, apart from the sampling of distant textures.
- Done: the code compiles for ARM11 (armv6k, hard float) against the real libctru, citro3d and citro2d headers, and runs on a New 3DS.
- In progress: performance on the console.
