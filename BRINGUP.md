# Bring-up: the recompiled game reaches attract mode

Historical notes from the first bring-up, run with `platform/platform_stub.cpp` before video, audio and inputs existed
(hardware accesses were logged and the text in VRAM was printed).
Values observed while running the recompiled code; register names and semantics from the MAME driver (`namcos23.cpp`).

```
S23_DSW=0xfd ./build_full/panicprk_stub panicprk_main.elf main_r4650_BE_4MB.bin 5000000 40 <folder_with_the_ROMs>
```
`DSW=0xFD` = "Skip POST" on. The ROM folder must contain `pnp1mtah.2j` and `pnp1mtal.2h` (data ROM, byte interleave).

## SDL2 window (live)
Build: `sudo apt install libsdl2-dev` then `sh platform/build_full.sh` (if `sdl2-config` is present the stub is built with `-DUSE_SDL`; `NO_SDL=1` excludes it).
Run: `S23_WINDOW=1 S23_DSW=0xfd ./build_full/panicprk_stub panicprk_main.elf main_r4650_BE_4MB.bin 5000000 0 <ROM_folder>`
(the fourth argument `0` = no time limit). Keys: `Esc` exits, `F11` full screen, `F12` saves `screenshot_NNN.png`.
Architecture: the game runs in a thread with a 256 MB stack (only it receives the timer signals), the main thread handles SDL
(events, 640x480 RGB24 texture updated every iteration, presentation with vsync, 640x480 logical scale -> 4:3). The title shows the game frames per second.
Non-interactive test: `SDL_VIDEODRIVER=dummy S23_WINDOW=1 S23_WINDOW_SECS=9 S23_WINDOW_SHOT=out.png ...` saves the pixels actually presented by the renderer.
At that stage there were no inputs: the game buttons (coins, Start, levers) go through the H8/JVS, which wasn't emulated yet.

## Video (text layer)
With `S23_FRAMES=<folder> S23_FRAME_EVERY=300` the stub saves a 640x480 PNG every N vblanks (C361 text layer + C404 palette + gamma).
The renderer replicates `draw_text_layer()` and the gamma of `screen_update()` from the MAME driver: 16x16 tiles at 4 bpp, pixel 0xF transparent,
colour = low byte of the BE half-word at `plane + 2*pen` (R/G/B planes at +0x0000/+0x10000/+0x20000), pen = `palbase | pal<<4 | val`.
Screens observed after ~40 s: "PANIC KING mode" instructions, "HALL OF FAME" (Shark/Buffalo/Missile/Jumping/Mole/Fire Panic), "KING STAGE".

Scroll and scanline interrupts (checked with `S23_TRACE_SCROLL=1`): in the attract screens the game writes the C361 registers
only at startup (`xscroll` = `0x035C` then `0xFF5C`, both = scroll 0; `irq_scanline` = 0 then `0x1FF` = disabled) and **never enables IP3**
(`Status` stays `0xB401`). Per-line scroll (`S23_ROWSCROLL=1`) and the scanline IRQ are implemented as in MAME but were never
exercised by the game in these screens; that's why a single scroll per frame is used by default.
Captions that looked overlapped were transition frames between two screens. Execution is not deterministic (timers tied
to the PC clock), so the same number of vblanks can fall at different phases of the animation.

Observed sequence: logo `PANIC PARK © 1997 NAMCO`, `PRESS START!` + TOP 10, animated tutorial (hand, arrow, coin), "WHAT'S THE PANIC KING MODE?",
"TO TRY THE PANIC KING MODE…", HALL OF FAME (Shark/Buffalo/Missile/Jumping/Mole/Fire Panic), KING STAGE.

## Result (40 s)
2,303 frames (~57/s), 222,813 C435 DMAs (4.9 M words), 130,951 `render`, 75,720 `flush`, 1.2 M data ROM reads,
interrupts: vblank (IP2) 2,322, C435 (IP4) 22,539, timer (IP7) 65. Text on screen: `COIN(S)`.

## What it takes to get there (all already in the stub)
| Element | Behaviour required by the game |
|---|---|
| Boot | The game is `main(boot_ROM_services_table)`; a returning `main` calls boot ROM service 0 (= `exit`). |
| CP0 | `Count` at clock/2 (84.672 MHz); `Compare` → IP7; final `Status` `0xB401` (IM7, IM5, IM4, IM2, IE). |
| Vblank | IP2 (IRQ0) at 59.9 Hz, stays active until the game writes `0x0D00000A` (ack). |
| C361 | `0x0682000A` = `vpos×2 \| vblank`; `0x0682000C` = vblank (525 lines, blanking from 480). |
| CTL | `0x0D000002` = DSW; `0x0D000004/06` = 12-bit shift registers (write = latch, 0xFFFF = bit 1). |
| C417 | `0x02000000` status = `0x008E` (units not busy); `0x02000006` test_done = 0; `0x0200000E` C435 IRQ ack. |
| C435 | `0x01000028` busy = 1; DMA (`1C` addr, `20` size, `24` start) = stream of 16-bit commands; end of list → IRQ IP4. |
| Memories | NVRAM, C361 char/text, C404 (+gamma), palette, C422: RAM that keeps its values. Data ROM at `0x08000000`. |

## Protocol with the H8 (C416, shared memory at `0x04400000`)
**Correction:** the 64 KB the game writes to C416 are not a firmware upload: they are a **clear** (the H8 has its own ROM, `pnp1vera.ic3`).
`MCUEN` (`0x04C3FF0A`): 0 = H8 in reset, ≠0 = started.
"Mailbox" scheme: the game writes a code into a cell, the H8 answers by changing it. Observed:

| Cell | The game writes | The (fake) H8 does |
|---|---|---|
| `0x3002` | `0x3163` | answers `0x7601`; writes the version to `0x3000`; `0x3018 = 0x301A = 0xFFFF` (= RAM check "OK") |
| `0x306A` | `0x3163` | clears it |
| `0x3078` | `0x3165`, `0x3164` | clears it |

The H8 version (`0x3000`) was an **assumption** (`0x0100`): the real value is written by the firmware. The fake H8 was a **minimal HLE**;
it was later replaced by the real firmware (`pnp1vera.ic3`, H8/3002) running on an H8/300H interpreter, validated by checking
that it produces the same replies (`0x7601`, `0xFFFF`).

## Observed 3D stream (C435) – needed for the renderer
Framing as in MAME's `c435_pio_w`: header `h`; if `(h & 0x4000)` length = `h & 0xFF`, otherwise `h & 0xF`.
Classes: `0x0000` matrices (`0x00/10/40/50` observed), `0x4000` states (priority, tx/ty, scale, light, viewport `0x0F46`, clip `0x0FC8`,
`0x0F09`, `0x0F42`, interrupt `0x0F01`), `0x8000` render, `0xC000` flush. Types and parameter meanings come from the MAME driver.

## Limits at that stage
* No video or audio; fake H8; inputs idle.
* The timer period and the fake H8 were assumptions; FPU flags are not modelled.
* Built at `-O0`.
