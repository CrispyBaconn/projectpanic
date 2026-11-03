# Bring-up: il gioco ricompilato arriva in attract mode

Eseguito con `platform/platform_stub.cpp` (nessun video: si registrano gli accessi hardware e si stampa il testo in VRAM).
Valori osservati dall'esecuzione del codice ricompilato; nomi e semantica dei registri dal driver MAME (`namcos23.cpp`).

```
S23_DSW=0xfd ./build_full/panicprk_stub panicprk_main.elf main_r4650_BE_4MB.bin 5000000 40 <cartella_con_le_ROM>
```
`DSW=0xFD` = "Skip POST" attivo. La cartella ROM deve contenere `pnp1mtah.2j` e `pnp1mtal.2h` (data ROM, interleave byte).

## Finestra SDL2 (dal vivo)
Build: `sudo apt install libsdl2-dev` poi `sh platform/build_full.sh` (se `sdl2-config` c'è, lo stub viene compilato con `-DUSE_SDL`; `NO_SDL=1` la esclude).
Esecuzione: `S23_WINDOW=1 S23_DSW=0xfd ./build_full/panicprk_stub panicprk_main.elf main_r4650_BE_4MB.bin 5000000 0 <cartella_ROM>`
(il quarto argomento `0` = nessun limite di tempo). Tasti: `Esc` esce, `F11` schermo intero, `F12` salva `screenshot_NNN.png`.
Architettura: il gioco gira in un thread con stack da 256 MB (solo lui riceve i segnali dei timer), il thread principale gestisce SDL
(eventi, texture RGB24 640x480 aggiornata a ogni giro, presentazione con vsync, scala logica 640x480 -> 4:3). Il titolo mostra i frame di gioco al secondo.
Test non interattivo: `SDL_VIDEODRIVER=dummy S23_WINDOW=1 S23_WINDOW_SECS=9 S23_WINDOW_SHOT=out.png ...` salva i pixel realmente presentati dal renderer.
**Non ci sono ingressi:** i tasti di gioco (monete, Start, leve) passano dall'H8/JVS, non ancora emulato.

## Video (livello testo)
Con `S23_FRAMES=<cartella> S23_FRAME_EVERY=300` lo stub salva un PNG 640x480 ogni N vblank (livello testo C361 + palette C404 + gamma).
Il renderer replica `draw_text_layer()` e la gamma di `screen_update()` del driver MAME: tile 16x16 a 4 bpp, pixel 0xF trasparente,
colore = byte basso della mezza parola BE a `piano + 2*pen` (piani R/G/B a +0x0000/+0x10000/+0x20000), pen = `palbase | pal<<4 | val`.
Schermate osservate dopo ~40 s: istruzioni "PANIC KING mode", "HALL OF FAME" (Shark/Buffalo/Missile/Jumping/Mole/Fire Panic), "KING STAGE".
**Non c'è ancora il 3D.**

Scroll e interrupt di scanline (verificato con `S23_TRACE_SCROLL=1`): nelle schermate di attract il gioco scrive i registri C361
solo all'avvio (`xscroll` = `0x035C` poi `0xFF5C`, entrambi = scroll 0; `irq_scanline` = 0 poi `0x1FF` = disattivato) e **non abilita mai IP3**
(`Status` resta `0xB401`). Lo scroll per riga (`S23_ROWSCROLL=1`) e l'IRQ di scanline sono implementati come in MAME ma non sono mai stati
esercitati dal gioco in queste schermate; per questo di default si usa un unico scroll per frame.
Le scritte che sembravano sovrapposte erano frame di transizione tra due schermate. L'esecuzione non è deterministica (timer legati
all'orologio del PC), quindi lo stesso numero di vblank può cadere in fasi diverse dell'animazione.

Sequenza osservata: logo `PANIC PARK © 1997 NAMCO`, `PRESS START!` + TOP 10, tutorial animato (mano, freccia, moneta), "WHAT'S THE PANIC KING MODE?",
"TO TRY THE PANIC KING MODE…", HALL OF FAME (Shark/Buffalo/Missile/Jumping/Mole/Fire Panic), KING STAGE.

## Risultato (40 s)
2.303 frame (~57/s), 222.813 DMA C435 (4,9 M parole), 130.951 `render`, 75.720 `flush`, 1,2 M letture data ROM,
interrupt: vblank (IP2) 2.322, C435 (IP4) 22.539, timer (IP7) 65. Testo a schermo: `COIN(S)`.

## Cosa serve per arrivare lì (tutto già nello stub)
| Elemento | Comportamento richiesto dal gioco |
|---|---|
| Boot | Il gioco è `main(tabella_servizi_boot_ROM)`; `main` che ritorna chiama il servizio 0 del boot ROM (= `exit`). |
| CP0 | `Count` a clock/2 (84,672 MHz); `Compare` → IP7; `Status` finale `0xB401` (IM7, IM5, IM4, IM2, IE). |
| Vblank | IP2 (IRQ0) a 59,9 Hz, resta attivo finché il gioco scrive `0x0D00000A` (ack). |
| C361 | `0x0682000A` = `vpos×2 \| vblank`; `0x0682000C` = vblank (525 linee, blanking dalla 480). |
| CTL | `0x0D000002` = DSW; `0x0D000004/06` = registri a scorrimento a 12 bit (scrittura = aggancio, 0xFFFF = bit 1). |
| C417 | `0x02000000` stato = `0x008E` (unità non occupate); `0x02000006` test_done = 0; `0x0200000E` ack IRQ C435. |
| C435 | `0x01000028` busy = 1; DMA (`1C` addr, `20` size, `24` start) = flusso di comandi a 16 bit; fine lista → IRQ IP4. |
| Memorie | NVRAM, C361 char/text, C404 (+gamma), palette, C422: RAM che mantiene i valori. Data ROM a `0x08000000`. |

## Protocollo con l'H8 (C416, memoria condivisa a `0x04400000`)
**Correzione:** i 64 KB scritti dal gioco in C416 non sono un upload di firmware: sono un **azzeramento** (l'H8 ha una ROM propria, `pnp1vera.ic3`).
`MCUEN` (`0x04C3FF0A`): 0 = H8 in reset, ≠0 = avviato.
Schema "mailbox": il gioco scrive un codice in una cella, l'H8 risponde cambiandola. Osservato:

| Cella | Il gioco scrive | L'H8 (finto) fa |
|---|---|---|
| `0x3002` | `0x3163` | risponde `0x7601`; scrive versione in `0x3000`; `0x3018 = 0x301A = 0xFFFF` (= RAM-check "OK") |
| `0x306A` | `0x3163` | la azzera |
| `0x3078` | `0x3165`, `0x3164` | la azzera |

La versione dell'H8 (`0x3000`) è un'**ipotesi** (`0x0100`): il valore vero lo scrive il firmware. L'H8 finto è un **HLE minimo**:
sound, I/O JVS e altri comandi non sono implementati. Prossimo passo robusto: eseguire il vero firmware (`pnp1vera.ic3`, H8/3002)
con un interprete H8/300H, validandolo sul fatto che produca le stesse risposte (`0x7601`, `0xFFFF`).

## Flusso 3D osservato (C435) – serve per il renderer
Framing come in MAME `c435_pio_w`: header `h`; se `(h & 0x4000)` lunghezza = `h & 0xFF`, altrimenti `h & 0xF`.
Classi: `0x0000` matrici (`0x00/10/40/50` osservate), `0x4000` stati (priorità, tx/ty, scala, luce, viewport `0x0F46`, clip `0x0FC8`,
`0x0F09`, `0x0F42`, interrupt `0x0F01`), `0x8000` render, `0xC000` flush. I tipi e il significato dei parametri vanno presi dal driver MAME.

## Limiti attuali
* Nessun video né audio; H8 finto; ingressi a riposo.
* Il periodo del timer e l'H8 finto sono ipotesi; i flag FPU non sono modellati.
* Compilato a `-O0`.
