# Panic Park – static MIPS → C++ recompilation

Translates `panicprk_main.elf` (extracted from the ROM) into native C++.
It contains no game code or data: everything is generated from **your own** ROM.
Unofficial project, not affiliated with Bandai Namco. License: BSD-3-Clause (see `LICENSE`; parts derived from MAME).

The menu logo and background (`platform/ui/logo.png`, `platform/ui/background.png`) are not in the repository:
they are optional and, if you have them, must be placed in `platform/ui/` by hand.

## Pipeline

```
python3 mips2cpp.py --elf ../panicprk_main.elf --funcs functions.csv --out gen_full
sh platform/build_full.sh            # builds everything (-O0) and links the platform stub
S23_DSW=0xfd ./build_full/panicprk_stub ../panicprk_main.elf ../main_r4650_BE_4MB.bin 5000000 40 <ROM_folder>   # event limit, seconds, folder with pnp1mtah.2j and pnp1mtal.2h
```

Requirements: Python 3, `pyelftools`, `capstone` (only for the checks), g++ ≥ 9.
For the tests: `unicorn` and `numpy`; point to the ELF with `PANICPRK_ELF=/path/panicprk_main.elf`.

## Functions reached only through pointers
Besides the 2,748 functions found through `jal`, `mips2cpp.py` looks for pointers to `.text` in the data and for
`lui/addiu` constants in the code (excluding `switch` targets, and accepting only addresses right after an
unconditional jump): +170 functions (tiny callbacks, hardware accessors). Total **2,918 functions**.
Without this pass execution stopped at a `jalr` to `0x800C74B4`.

## Status
* The whole program **compiles, links and runs** and the game is **playable** on PC (Windows, Linux), with 3D
  (CPU or OpenGL), text layer, audio (C352 driven by the real H8 firmware), controls and settings menu.
* Ports: Wii U, Nintendo Switch, Android and Nintendo 3DS (see the sections below and the `README_*.md` files).
* Early bring-up notes are in `BRINGUP.md`.

## How it translates

| MIPS | C++ |
|---|---|
| a function | `void f_XXXXXXXX(CPU* c)` (2748 functions, 46 files) |
| internal jumps/branches | `goto L_XXXXXXXX` |
| `jal` | direct C++ call |
| `jr $ra` | `return` |
| `jr rX` (jump table) | `switch` over the targets read from the table (84 tables, all resolved) |
| `jalr` | `call_guest(c, addr)` (address→function table) |
| delay slot | expanded inline, condition evaluated first |
| branch-likely (`beql`…) | delay slot executed only if the branch is taken |
| backward branch | `BACKEDGE(c)` → safe point to deliver interrupts |
| `lwl/lwr/swl/swr` | big-endian helpers in `runtime.h` |
| `break`/CP0/`cache` | platform hooks |

## Validation (comparison with Unicorn/QEMU MIPS32 24KF, big endian)

* **Instruction level**: 2,600 random programs (2,362 comparable) (ALU, HI/LO, `mul`, `madd`, loads/stores including
  unaligned ones, single-precision FPU, all branches with delay slots): 0 differences.
  Cases with NaN are skipped (payloads differ between softfloat and SSE).
* **Real game functions**: 2,838 identical runs, 0 differences (registers, HI/LO, FPU,
  all written memory), over hundreds of functions run together with their call closure
  (last campaign, with the final boundaries: 922 identical over 186 functions).
* The decoder agrees with Capstone on 208,663 words out of 208,664 (the exception is data).
* All the generated C++ passes the syntax check.

Reproduce with: `cd test && python3 difftest_unit.py 1000 2` and `python3 difftest_game.py 300 6 2`.

## Assumptions and known limits (worth knowing before trusting it)

1. **32-bit registers.** The code uses 64-bit instructions only for `daddiu` (6 times, `gp`/`sp` init)
   and `daddu` as `move`. With sign-extended values they match `addiu`/`addu`. Not tested on Unicorn
   (it doesn't run them in MIPS32 mode).
2. **FCSR**: FPU exception flags/causes are not modelled. The game reads FCSR (`cfc1`) only when
   saving the interrupt context, which is replaced. Out-of-range `trunc.w.s` → `0x7fffffff`
   (not verified on hardware).
3. **Division by zero**: undefined result on hardware, the game avoids it with `break`; here `lo=a, hi=0`.
4. **Exception vectors (0x80000000–0x8000031F) are NOT translated.** They are the game's interrupt dispatcher:
   it reads Cause/Status, finds the highest IP line and calls the function pointer at
   `0x8028D8B0 + 4×(IP+1)` (IP0..IP7), saving/restoring registers and FCSR. The platform has to
   reproduce this by calling that pointer at the `BACKEDGE` points, honouring Status.IE/IM.
   Real exceptions (`ExcCode ≠ 0`) originally ended up in the boot ROM monitor (`0xBFC00380`).
5. **CP0 used by the game**: Count(9)/Compare(11) (timer), Status(12), Cause(13), EPC(14), Config(16),
   TagLo(28), `cache` (once, init). Emulated in `cp0_read/cp0_write`.
6. **Synchronisation**: delivering interrupts only at backward branches assumes the game waits for flags
   in a loop. If some wait is a straight-line sequence without a loop, an extra point is needed.
7. **Hardware** (`hw_read/hw_write`): 3D C417/C435, C404/C361 (palette/text), C412/C421,
   C416 (memory shared with the H8), CTL (input/LED/vblank), data ROM at `0x08000000`.
8. The boot ROM (32 KB) is not executed: the game reads it in 5 places (header/ID), served by `hw_read`.

## Legal notes
The ROM is copyrighted. Do not distribute the generated code or the resulting binaries.

## Real sub-CPU and controls
- The original firmware of the H8/3002 (`pnp1vera.ic3`) and of the ASCA-3A I/O board (`asca-3a.ic14`, H8/3334) are
  run by interpreters generated from MAME's listing (`h8/`, `h8/h8gen.py`). The I/O board is now native (see Optimisations).
  To go back to the old fake H8: `S23_FAKEH8=1`. Diagnostics: `S23_H8DBG=1`.
- `./play.sh` first opens the settings screen (saved in `~/.local/share/panicprk/recomp/panicprk.ini`), then the game.
  `GUI=0 ./play.sh` skips it; `DSW=0xfd ./play.sh` skips the POST.
- Default controls: left stick/arrows/A-D = left-right, pad button A / key 5 = coin,
  START / key 1 = player 1, Y / key 2 = player 2, F2 = test, **F3 = config DIP on/off**, F11 full screen, F12 screenshot.

## 3D graphics (platform/gpu3d.h)
C435/C417/C412/C421 pipeline ported from MAME (commands, matrices, models from the point ROM, direct/immediate polygons,
near-plane clipping, Z sorting, textures from pnp1cg*/pnp1ccr*, shading, fade, alpha), software rasteriser.
The graphics ROMs must be in the ROM folder (the same as pnp1vera.ic3). Can be disabled with S23_NO3D=1.
Compositing order: background -> 3D -> text -> C404 gamma. Known limits: some coloured streaks at the floor edges.

### Fixes compared to MAME (3D captions, exploding star)
- **List segments.** The game splits every frame with "end of list" commands (render of model 0,
  or 4f38 with sub-type 1). Segment 1 (the second) is an overlay: the cabinet draws it on top of
  everything else, which is instead sorted by z as in MAME. This way the yellow captions ("A FUN 1 PLAYER GAME",
  "THE 2 PLAYER GAME IS FULL OF LAUGHS!", the PANIC PARK logo) are in front of the star, cacti and characters,
  and the tutorial stays correct. Rule derived by comparing frames from the cabinet: `S23_NOSEG=1` turns it off.
  In windowed mode: F9 changes the mode (0 = MAME, 1 = segment 1 on top, 2 = by priority, then segments from last
  to first, then z); F10 saves `segdump_NNN.txt` + `.png` with the structure of the frame's list.
- **Last slice of the star.** The game sends the last quad of the star with command 4f38 sub-type
  0x0001: MAME takes it only as end of list and discards it, here it is also drawn.
- Old heuristic that sent flat immediate quads to the back: now off, `S23_FLATBG=1` to re-enable it.

## Audio (h8/c352.h)
The Namco C352 sound chip (32 PCM voices, ROMs `pnp1wavel.2c` + `pnp1waveh.2a` in the ROM folder) is emulated
as in MAME and driven by the real H8 firmware: it advances at 88200 samples/s based on the H8 cycles (192 cycles per
sample), then it is downsampled to 44100 Hz stereo and sent to SDL (at most ~170 ms queued).
- `S23_NOSOUND=1` disables audio; `S23_WAV=file.wav` records the output to a file (useful without a window too).

## Building for Linux and Windows
- **Linux** (`sudo apt install g++ libsdl2-dev`): `sh platform/build_linux.sh` -> `dist/linux/panicprk` and `dist/linux/run.sh`.
- **Windows 64-bit**, built from Linux/WSL (`sudo apt install g++-mingw-w64-x86-64-posix`):
  `sh platform/build_windows.sh` -> `dist/windows/PanicPark.exe` (a single file, static SDL2; the first time it
  downloads SDL2 for MinGW from GitHub into `deps/`). On Windows the 1 kHz tick is a thread that suspends the game
  thread, runs `on_tick` and resumes it (instead of SIGALRM).
- Both: `OPT` (default -O2) and `JOBS` (parallel compilations). With -O2 the first build takes several minutes.
- The executable started without arguments (double click) looks for `panicprk_main.elf`, `main_r4650_BE_4MB.bin` and the ROMs
  in the current folder and opens the window.

## Main CPU speed
The game measures some timeouts with empty loops (e.g. 98,000 checks separated by 4,096 iterations to wait for the sub-CPU:
~9 s on the R4650). Compiled with -O2 on a modern PC that count finishes in a few tenths of a second and
"SUBCPU INITIALIZE TIME OUT" appears. `BACKEDGE` counts backward branches and `backedge_throttle` enforces at least 22.5 ns
per branch (3 cycles at 133 MHz): wait loops go back to their original length, everything else is not slowed down.
`S23_BENS=ns` changes the value (0 = no limit).

## Extracted, memory-mapped assets (platform/assets.h)
On the first launch the big ROMs are converted to the format used in memory and saved in the `assets` folder
(next to where the game is started; `S23_ASSETS=folder` to change it): `tex.bin` (textures, 32 MB), `pt.bin`
(3D models), `tm.bin` (texture map), `data.bin` (data ROM), `wave.bin` (audio samples). From the following launches
they are mapped read-only (mmap / MapViewOfFile): the system keeps only the pages in use in RAM and can release them
without swap. Delete the folder to regenerate them; `S23_NOASSETS=1` loads everything into RAM as before.

## NVRAM, resolution and refresh
- The backup RAM (service menu settings opened with F3, controller calibration, records) is saved to
  `nvram.bin` in the start folder every 2 s if it changed and on exit, and reloaded at startup (windowed mode only;
  `S23_NVRAM=file` for another path, `S23_NONVRAM=1` to not use it).
- "3D resolution" in the start screen: "like the screen" rasterises the 3D directly at the window resolution
  keeping 4:3 (up to 4x, i.e. 2560x1920); the text stays the original one, scaled up.
- The game always runs at its own speed (60 vblanks/s, real time); the window presents with the monitor's vsync.
- "Format (experimental)": 16:9 widens the 3D at the sides without distorting it (same projection, more is visible); the
  text stays 4:3 in the centre. Scenes built for 4:3 may show bands or objects appearing at the edges.

## Languages
- **Game language**: Panic Park already contains 8 languages (JPN, ENG, GER, FRA, SPA, POR, ITA, KOR), chosen from the
  service menu (F3 > OTHERS > LANGUAGE) and saved in the backup RAM (byte 307 of the data). The "Game language" entry of the
  start screen sets it directly. The Italian version translates the minigame rules and the in-game messages;
  some attract captions exist only in English in the ROMs.
- **Menu language**: JSON files in `lang/` (next to the executable; `S23_LANGDIR` for another folder). Each file
  is a flat object `{"key": "text"}`; `language_name` is the name shown. To add a language just
  copy `en.json` under another name and translate the values. Missing keys fall back to English.
- **Game texts translated from the JSON**: the `"game:<ENGLISH TEXT>"` keys of the game-language file (or, if the
  game language is "the one chosen in the game", of the menu-language file) replace the texts in the program's records
  (small and large font, max 36 characters). `"ascii:<text>"` replaces fixed-length ASCII strings.
  Available characters: A-Z, digits, space, `! ( ) :` and in the small font also `, . ' - ? "`; anything else becomes a space.
  `S23_LISTTEXT=1` prints all the available keys. Only captions that are 3D graphics or
  textures stay in English (e.g. "A FUN 1 PLAYER GAME", the logo, NEXT/HARD/NORMAL/EASY).
- **Date and time**: the RTC4543 is read from the PC on every access (52-bit stream with a 4-bit weekday,
  continuous synchronous receive on SCI1), so the game's clock and calendar follow the system ones.
- **Screen refresh**: monitor VSync or 59.9 / 60 / 75 / 100 / 120 / 144 / 165 / 240 Hz or unlimited.
  It only changes how often the image is shown: the game stays at 59.9 Hz in real time.

## Wii U (Aroma)

`Makefile.wiiu` + `platform/build_wiiu.sh` produce `panicprk.wuhb` (wut + SDL2 for Wii U); instructions in `README_WIIU.md`.
Portability changes made for it: `runtime.h` doesn't swap bytes on big-endian hosts (`be32h`), the assets go to
`assets_be/` on big-endian hosts, `-DS23_POLL_TICK` replaces SIGALRM with a tick check at backward
branches, threads through `start_thread()` (pthread or coreinit). Verified with a static `powerpc-linux-gnu`
build under `qemu-ppc-static` (big endian, 32-bit): the game reaches gameplay with correct 3D and text.

## Dynamic lights and shadows (removed)
There was an experimental option for per-pixel lighting and projected shadows (F8). It reduced faithfulness to the original
and was expensive, so it was removed: the 3D uses only the C435's per-vertex lighting, like the cabinet.

## Nintendo Switch (homebrew)

`Makefile.switch` + `platform/build_switch.sh` produce `panicprk.nro` (libnx + SDL2 for Switch); instructions in
`README_SWITCH.md`. As for the Wii U: tick without signals (`S23_POLL_TICK`), assets read into memory, app folder
`sdmc:/switch/panicprk` (`S23_SWITCH_DIR`). `platform/switch_sys.cpp` reads the local time from the time service.
On all platforms: Select/− together with Start/+ on the controller closes the game.

## Optimisations (slow consoles)
- H8/ASCA: instruction decode cached by address (the code runs from ROM), fast instruction fetch
  from ROM, peripherals updated in blocks of 32 cycles (caught up before every register access), SLEEP in blocks
  of 64 cycles, no access statistics unless requested. Measured cost: from ~700 to ~360 ms of CPU per emulated
  second. `S23_H8SLOW=1` goes back to the previous behaviour.
- Native ASCA-3 I/O board (`platform/jvs_hle.h`): instead of emulating the second CPU (H8/3334 + `asca-3a.ic14` firmware)
  the H8's JVS packets are answered directly. The replies were derived from the original firmware and
  `test/jvs_probe.cpp` compares them byte by byte (identity, features, buttons, coins, analogs, encoders, Namco
  0x70 commands, sense line): identical, except that coins and analogs arrive a few milliseconds earlier. In an
  automated game the packets exchanged are the same 19 as in the emulated version. The `asca-3a.ic14` file is no longer needed.
  `S23_ASCA=1` = emulated firmware as before; `S23_JVSLOG=1` prints the traffic.
- H8 idle waits (~75% of its instructions): a short backward branch that comes back with identical registers, flags and
  memory is a pure wait (RAM shared with the main CPU, serial TDRE): time is advanced to the next
  peripheral event or the end of the ~32 us block. `dec.b / bne` delay loops are skipped by computing the
  iterations, and the wait with timeout at 0x2c0c (main CPU reply, read from TCNT3) is advanced to the timeout (only
  if the code bytes match). Instructions executed: from 3.8 to 0.9 million per second; identical audio (same
  spectrum, correlation 0.997-0.999 with the version without skipping). `S23_H8NOIDLE=1` = as before; `S23_H8PROF=1` writes
  `h8prof.txt` (instructions per address).
- When the H8 thread is ahead, it sleeps 500 us instead of 100 (fewer context switches; `S23_H8SLEEP=us`).
  Overall the H8 thread uses about a third of the CPU it used before (measured with perf: 7156 -> 2555 samples).
- 3D: parallel drawing in bands of lines (helper threads: PC = cores - 3, max 6; Switch/Wii U = 2 at lower
  priority on the game and H8 cores; `S23_RTHREADS=n`). Fast path without effects with a clipped range and perspective
  correction every `S23_PSPAN` pixels (PC 1 = exact, consoles 8).
- Consoles: the game thread sleeps instead of spinning when it waits.
- On-screen indicator (game Hz, screen Hz, 3D ms, H8 speed): F1 or left stick click; on by default on consoles.
- Screen refresh: new 30 Hz option (the game stays at 59.9 Hz) to halve the drawing work.

## Tutorial window (RULE / NEEDED / LIFE)
The C435 clip planes (command 0x00c8) for the monitor mini-screen are not normalised and are slanted (a trapezoid).
MAME's formula (vp_sy = |c|*fov, a = b = 1) overflowed int16 and the demo/captions ended up outside the mini-screen
(this happens in MAME too). Now vp_sx = |c/a|*fov, vp_sy = |c/b|*fov and the slanted left/right planes clip every line.
`S23_NOSLANT=1` disables the trapezoid clipping.
- 3D resolution "fast 320x240" (`res3d=2`): the 3D is drawn at half resolution and scaled up, the text stays
  at 640x480. Default on Switch/Wii U. On the Switch the render threads now have a higher priority than the game/H8:
  at a lower priority a thread suspended halfway through a band blocked the frame (3D at 148 ms).
- Drawing on a separate thread: the window presents at the monitor rate (VSync, e.g. 100 Hz) without waiting for the drawing,
  and the drawing doesn't wait for VSync (before, with 22 ms of drawing and a 100 Hz monitor, it dropped to ~29 images/s).
  Indicator: "img" = new images per second (at most 60, like the game), "screen" = window refresh.
  3D helper threads on PC: cores - 2 (max 6).
- Per-segment window: clip and viewport (commands 0x00c8 / 0x0046) are remembered for each list segment and
  restored at the start of the segment. In some levels (e.g. NEEDED/LIFE of WALL PANIC, missile) the game sends the
  mini-screen window only once, and segment 0 sends the full-screen one every frame: with a single
  state the icons and the demo came out of the monitor. `S23_NOSEGWIN=1` goes back to the previous behaviour.
  A window is remembered only if a real model follows in its segment, and it is forgotten when a frame has fewer
  segments (screen change): before, a stale window clipped the 3D of the "insert coins" screen.
  0x0046 with the low 3 bits of P[3] at zero (0x20) doesn't change the window if the one in use is the monitor's trapezoid:
  the game sends it before NEEDED/LIFE while the shutter comes down, and applying it made the captions full
  screen. In the other cases it is applied (attract "INTERACTIVE CONTROLS": without it, the full-screen characters disappeared and
  only the small scene remained). `S23_WINALL=1` = always apply it.
  The game recomputes the mini-screen window only during the pan towards the monitor, and often sends it only
  once halfway: when reusing a remembered trapezoid window, the still-camera one is used (always the
  same, taken from the command traces). `S23_NOCANON=1` disables it.
- "PANIC PARK" shutter that closes the monitor at the end of the tutorial (models 02c8/02c9): drawn with the full-screen
  window and in front of all the 3D, like on the cabinet (before, it was clipped in the mini-screen, behind NEEDED/LIFE).
  `S23_FULLWIN=list|none` (and `S23_FRONT`, which is now `2db,2c8,2c9`).
- Translation: strings that the game completes with a digit in a fixed column must have the same length
  as the original (Italian: "INSERT" -> "METTI "; before, "INSERISCI" got overwritten: "INSERIS1I").
- Texture coordinates without MAME's +0.5: our rasteriser samples at the pixel centre, and with the extra 0.5
  the last column/row of every box took the texels of the neighbouring box (coloured edges, e.g. the grid of the 25 games).
  `S23_UVHALF=1` = like MAME.
- Depth exceptions (`front_model`): the 3D START button (model 02db) of the PANIC KING screen has a
  depth offset of +20000 and with the reconstructed sorting it ended up behind the figurines; on the cabinet it is
  in front of all the 3D (the hand and the arrow are text-layer sprites, always on top). `S23_FRONT=list|none`.

## 3D on the GPU (OpenGL) — platform/gl3d.h
- New start-screen option "3D drawing": graphics card (OpenGL, default on PC) or processor.
  In the .ini file: `gpu=1/0`; from the environment: `S23_GL=1/0`. If OpenGL doesn't start it falls back to CPU drawing by itself.
- The CPU still prepares the polygons (g3d::prepare: transforms, per-vertex lighting, projection, sorting, monitor
  windows). The rasteriser however is on the GPU: a fragment shader redoes scanline() (tile map, 8-bit texrom,
  palette, C412 stencil, shading, alpha and average, rectangular and trapezoid clipping per line). The graphics ROMs
  (32 MB of texrom as a 2048x2048x8 texture array, tile map) are loaded once. Palette, stencil, polygon
  data and text are updated every frame. A second pass composes background, 3D, text and gamma into the texture
  that SDL shows, so the OSD and the touch buttons stay the same.
- It uses the OpenGL context of SDL's renderer ("opengl" on PC, "opengles2" with an ES 3.0 context on Android/Switch).
  The GL functions are loaded with SDL_GL_GetProcAddress (no extra headers or libraries).
- Verification (`S23_GLCMP=n`): every frame is drawn both by the GPU and by the CPU and compared pixel by pixel.
  Over more than 6000 frames (attract, menus, tutorials, 12 levels) 0.2-0.4% of the pixels differ: polygon edges
  and texel boundaries, scattered, not visible. Images with more than 0.1% of differences are saved
  (`glcmp_*_gpu.png` / `_cpu.png`; `S23_GLCMP_SAMPLES=1` = regular samples only). `S23_GLPROBE=x,y` tells which
  polygon covers a pixel in the two renderers.
- Texture coordinates: +1/1000 before truncation. Without it, a value constant along an edge (e.g. v = 2880) interpolated
  in float reached 2879.9998 and took the row of texels above (dark bands on flat faces).
- Render thread time at 960x720 on the test machine: from ~56 ms (CPU) to ~6 ms (CPU preparing + GPU), and from 25 to 60
  images/s.
- Consoles: Switch and Android have the code (OpenGL ES 3.0) but `gpu=0` by default until it is tested on hardware.
  Wii U: no OpenGL (a GX2 engine will be needed).

## Nintendo 3DS (New 3DS) with the GPU
3D drawn by the PICA200 GPU with a texture cache (`platform/pica3d.h`,
`platform/platform_3ds.h`), menu on the bottom screen, controls, audio. Build: `bash platform/build_all.sh 3ds`.
Details, files to copy and limits in `3ds/README_3DS.md`. Renderer test on PC: `S23_GLCMP=1 S23_PICATEST=60`
(with GPU drawing and `S23_RTHREADS=0`; add `S23_PICAATLAS=1` to use 1024x1024 atlases like on the 3DS) saves
`pica_NN_gpu.png` / `pica_NN_sw.png` and prints the differences.

## New menu (main screen and settings)
- At startup there is a Panic Park-style main screen with PLAY, SETTINGS and EXIT (in game: RESUME).
  The settings are split into tabs (Controls, Lever and touch, Video, Game) with large rows to tap.
- It works with everything: mouse or finger (tap to choose, drag to scroll the list), keyboard (arrows, Enter,
  Esc, Q/E or Tab for the tabs), controller (d-pad or stick, A confirms, B back, LB/RB tabs, Start = play).
  The bar at the bottom shows the right buttons for the last device used.
- To assign a key: Controls tab, choose the box (Keyboard 1, Keyboard 2 or Controller) and press the
  key/button; "Clear" (or Backspace) removes the assignment. "Reset" asks for a second confirmation.
- Background and logo: if the `ui/` folder (next to `lang/`) contains `background.png` and `logo.png` the menu uses them,
  otherwise it draws a spiral sea and the title with the font. The build scripts copy `platform/ui/*.png`
  into dist (Windows, Linux) and into the app folder on the SD card (Switch, Wii U); on Android copy `ui/` by hand.
- Fonts: Bangers and Lilita One (SIL OFL license, in `platform/fonts/`), already converted into `platform/ui_font.h`
  (regenerate with `python3 tools/bake_ui_font.py`). The PNGs are read with stb_image (`platform/third_party/`).
- Translations: the menu keys (play, settings, exit, tab_*, h_*, ...) are in `lang/en.json` and `lang/it.json`;
  accented letters are displayed (Giù, sì, modalità).

## In-game settings menu and on-screen buttons
- In game you go back to the settings with Esc or F4 (keyboard), Select/- together with Start/+ (controller, also on Switch and
  Wii U), the phone's Back button or the on-screen MENU button (top centre). The game pauses: emulation
  time stops (`emu_ns()`), so do the H8 and the audio, and on resume it continues where it was.
  "RESUME THE GAME" (or Esc / B / Back) returns to the game, "Exit" closes it.
- Option "On-screen buttons (touch)": yes/no (`touchui=` in the .ini). With "no", COIN, SERVICE, MENU and the
  service menu buttons disappear, and taps in those areas do nothing (finger steering remains).
- Fixed: with GPU drawing, after SDL had uploaded the OSD text or the on-screen buttons, texture uploads
  read rows that were too long (GL_UNPACK_ROW_LENGTH left set by SDL) and the game closed.

## Android
`platform/build_android.sh` (setup + build) creates a Gradle project from SDL2's template and produces
`dist/android/PanicPark.apk`; instructions in `README_ANDROID.md`. Touch controls (also on PCs with a touchscreen):
dragging the finger = player 1 steering, short tap = Start, top-left corner = coin.
The start screen works with mouse/touch too.
