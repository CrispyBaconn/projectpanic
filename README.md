# Panic Park – ricompilazione statica MIPS → C++

Traduce `panicprk_main.elf` (estratto dalla ROM, vedi `../ROM_MAP.md`) in C++ nativo.
Non contiene codice o dati del gioco: si genera dalla **tua** ROM.
Progetto non ufficiale, non affiliato a Bandai Namco. Licenza: BSD-3-Clause (vedi `LICENSE`; parti derivate da MAME).

Il logo e lo sfondo del menu (`platform/ui/logo.png`, `platform/ui/background.png`) non sono nel repository:
sono facoltativi e, se li hai, vanno messi a mano in `platform/ui/`.

## Pipeline

```
python3 mips2cpp.py --elf ../panicprk_main.elf --funcs functions.csv --out gen_full
sh platform/build_full.sh            # compila tutto (-O0) e collega lo stub di piattaforma
S23_DSW=0xfd ./build_full/panicprk_stub ../panicprk_main.elf ../main_r4650_BE_4MB.bin 5000000 40 <cartella_ROM>   # limite eventi, secondi, cartella con pnp1mtah.2j e pnp1mtal.2h
```

Requisiti: Python 3, `pyelftools`, `capstone` (solo per i controlli), g++ ≥ 9.
Per i test: `unicorn` e `numpy`; indica l'ELF con `PANICPRK_ELF=/percorso/panicprk_main.elf`.

## Funzioni raggiunte solo via puntatore
Oltre alle 2.748 funzioni trovate dalle `jal`, `mips2cpp.py` cerca puntatori a `.text` nei dati e costanti
`lui/addiu` nel codice (escludendo i target di `switch`, e accettando solo indirizzi subito dopo un salto
incondizionato): +170 funzioni (callback minuscoli, accessor hardware). Totale **2.918 funzioni**.
Senza questo passo l'esecuzione si fermava su una `jalr` verso `0x800C74B4`.

## Stato
* L'intero programma **compila, si collega ed esegue** con lo stub di piattaforma e **arriva in attract mode**,
  elaborando ~57 frame/s di comandi 3D: vedi `BRINGUP.md`.
* Finestra SDL2 opzionale (`S23_WINDOW=1`, vedi `BRINGUP.md`).
* Video: il livello testo viene disegnato (PNG con `S23_FRAMES=<cartella> S23_FRAME_EVERY=N`); il 3D no.
* Non è ancora giocabile: mancano video 3D/2D, audio, ingressi veri e un H8 reale (ora c'è un H8 finto minimo).

## Come traduce

| MIPS | C++ |
|---|---|
| una funzione | `void f_XXXXXXXX(CPU* c)` (2748 funzioni, 46 file) |
| salti/branch interni | `goto L_XXXXXXXX` |
| `jal` | chiamata C++ diretta |
| `jr $ra` | `return` |
| `jr rX` (tabella di salto) | `switch` sui target letti dalla tabella (84 tabelle, tutte risolte) |
| `jalr` | `call_guest(c, addr)` (tabella indirizzo→funzione) |
| delay slot | espanso in linea, condizione valutata prima |
| branch-likely (`beql`…) | delay slot eseguito solo se il salto è preso |
| arco all'indietro | `BACKEDGE(c)` → punto sicuro per consegnare gli interrupt |
| `lwl/lwr/swl/swr` | helper big endian in `runtime.h` |
| `break`/CP0/`cache` | hook della piattaforma |

## Validazione (confronto con Unicorn/QEMU MIPS32 24KF, big endian)

* **Livello istruzione**: 2.600 programmi casuali (2.362 confrontabili) (ALU, HI/LO, `mul`, `madd`, load/store compresi
  gli non allineati, FPU a singola precisione, tutte le branch con delay slot): 0 differenze.
  I casi con NaN sono saltati (payload diversi tra softfloat e SSE).
* **Funzioni reali del gioco**: 2.838 esecuzioni identiche, 0 differenze (registri, HI/LO, FPU,
  tutta la memoria scritta), su centinaia di funzioni eseguite con la loro chiusura di chiamate
  (ultima campagna, con i confini definitivi: 922 identiche su 186 funzioni).
* Il decoder concorda con Capstone su 208.663 word su 208.664 (l'eccezione è un dato).
* Tutto il C++ generato supera il controllo di sintassi.

Riproduzione: `cd test && python3 difftest_unit.py 1000 2` e `python3 difftest_game.py 300 6 2`.

## Ipotesi e limiti noti (da conoscere prima di fidarsi)

1. **Registri a 32 bit.** Il codice usa istruzioni a 64 bit solo per `daddiu` (6 volte, init di `gp`/`sp`)
   e `daddu` come `move`. Con valori sign-extended coincidono con `addiu`/`addu`. Non testato su Unicorn
   (in modalità MIPS32 non le esegue).
2. **FCSR**: non si modellano flag/cause delle eccezioni FPU. Il gioco legge FCSR (`cfc1`) solo nel
   salvataggio contesto dell'interrupt, che viene sostituito. `trunc.w.s` fuori range → `0x7fffffff`
   (non verificato su hardware).
3. **Divisione per zero**: risultato indefinito su hardware, il gioco la evita con `break`; qui `lo=a, hi=0`.
4. **Vettori di eccezione (0x80000000–0x8000031F) NON tradotti.** Sono il dispatcher interrupt del gioco:
   legge Cause/Status, trova la linea IP più alta e chiama il puntatore a funzione in
   `0x8028D8B0 + 4×(IP+1)` (IP0..IP7), con salvataggio/ripristino di registri e FCSR. La piattaforma deve
   riprodurlo chiamando quel puntatore ai punti `BACKEDGE`, rispettando Status.IE/IM.
   Le eccezioni vere (`ExcCode ≠ 0`) in origine finivano nel monitor in boot ROM (`0xBFC00380`).
5. **CP0 usato dal gioco**: Count(9)/Compare(11) (timer), Status(12), Cause(13), EPC(14), Config(16),
   TagLo(28), `cache` (una volta, init). Va emulato in `cp0_read/cp0_write`.
6. **Sincronizzazione**: la consegna degli interrupt ai soli archi all'indietro presuppone che il gioco
   attenda i flag in un loop. Se qualche attesa è una sequenza lineare senza loop, serve un punto in più.
7. **Hardware**: tutto da scrivere (`hw_read/hw_write`): 3D C417/C435, C404/C361 (palette/testo),
   C412/C421, C416 (memoria condivisa con l'H8), CTL (input/LED/vblank), data ROM a `0x08000000`.
8. Il boot ROM (32 KB) non viene eseguito: il gioco lo legge in 5 punti (header/ID), serviti da `hw_read`.

## Note legali
La ROM è protetta da copyright. Non distribuire né il codice generato né i binari risultanti.

## Sub-CPU reale e comandi (novita')
- Il firmware originale dell'H8/3002 (`pnp1vera.ic3`) e quello della scheda I/O ASCA-3A (`asca-3a.ic14`, H8/3334) vengono
  eseguiti da interpreti generati dal listato MAME (`h8/`, `h8/h8gen.py`). Ora la scheda I/O e' nativa (vedi Ottimizzazioni).
  Per tornare al vecchio H8 finto: `S23_FAKEH8=1`. Diagnostica: `S23_H8DBG=1`.
- `./play.sh` apre prima la schermata delle impostazioni (salvate in `~/.local/share/panicprk/recomp/panicprk.ini`), poi il gioco.
  `GUI=0 ./play.sh` la salta; `DSW=0xfd ./play.sh` salta il POST.
- Comandi predefiniti: stick sinistro/frecce/A-D = sinistra-destra, pulsante A del pad / tasto 5 = moneta,
  START / tasto 1 = giocatore 1, Y / tasto 2 = giocatore 2, F2 = test, **F3 = DIP configurazione on/off**, F11 schermo intero, F12 screenshot.
- Limiti attuali: nessun audio (C352) e nessun rendering 3D (solo livello testo/sprite).

## Grafica 3D (platform/gpu3d.h)
Pipeline C435/C417/C412/C421 portata da MAME (comandi, matrici, modelli dalla point ROM, poligoni diretti/immediati,
clip sul piano vicino, ordinamento per Z, texture da pnp1cg*/pnp1ccr*, shading, fade, alpha), rasterizzatore software.
Servono le ROM grafiche nella cartella ROM (stessa di pnp1vera.ic3). Disattivabile con S23_NO3D=1.
Ordine di composizione: sfondo -> 3D -> testo -> gamma C404. Limiti noti: qualche striscia colorata ai bordi del pavimento.

### Correzioni rispetto a MAME (scritte 3D, stella esplosa)
- **Segmenti della lista.** Il gioco divide ogni fotogramma con comandi di "fine lista" (render del modello 0,
  oppure 4f38 con sotto-tipo 1). Il segmento 1 (il secondo) e' un overlay: il cabinato lo disegna sopra a
  tutto il resto, che invece viene ordinato per z come in MAME. Cosi' le scritte gialle ("A FUN 1 PLAYER GAME",
  "THE 2 PLAYER GAME IS FULL OF LAUGHS!", il logo PANIC PARK) stanno davanti a stella, cactus e personaggi,
  e il tutorial resta corretto. Regola ricavata confrontando i fotogrammi del cabinato: `S23_NOSEG=1` la spegne.
  In finestra: F9 cambia il modo (0 = MAME, 1 = segmento 1 sopra, 2 = per priorita', poi segmenti dall'ultimo
  al primo, poi z); F10 salva `segdump_NNN.txt` + `.png` con la struttura della lista del fotogramma.
- **Ultimo spicchio della stella.** Il gioco manda l'ultimo quad della stella con il comando 4f38 sotto-tipo
  0x0001: MAME lo prende solo come fine lista e lo scarta, qui viene anche disegnato.
- Vecchia euristica dei quad immediati piatti mandati in fondo: ora spenta, `S23_FLATBG=1` per riattivarla.

## Audio (h8/c352.h)
Il chip sonoro Namco C352 (32 voci PCM, ROM `pnp1wavel.2c` + `pnp1waveh.2a` nella cartella delle ROM) e' emulato
come in MAME e pilotato dal firmware H8 reale: avanza a 88200 campioni/s in base ai cicli dell'H8 (192 cicli per
campione), poi viene ridotto a 44100 Hz stereo e mandato a SDL (al massimo ~170 ms in coda).
- `S23_NOSOUND=1` disattiva l'audio; `S23_WAV=file.wav` registra l'uscita su file (utile anche senza finestra).

## Compilare per Linux e Windows
- **Linux** (`sudo apt install g++ libsdl2-dev`): `sh platform/build_linux.sh` -> `dist/linux/panicprk` e `dist/linux/avvia.sh`.
- **Windows 64 bit**, compilato da Linux/WSL (`sudo apt install g++-mingw-w64-x86-64-posix`):
  `sh platform/build_windows.sh` -> `dist/windows/PanicPark.exe` (un solo file, SDL2 statico; la prima volta
  scarica SDL2 per MinGW da GitHub in `deps/`). Su Windows il tick a 1 kHz e' un thread che sospende il thread
  del gioco, esegue `on_tick` e lo riprende (al posto di SIGALRM).
- Entrambi: `OPT` (default -O2) e `JOBS` (compilazioni in parallelo). Con -O2 la prima compilazione richiede parecchi minuti.
- L'eseguibile lanciato senza argomenti (doppio clic) cerca `panicprk_main.elf`, `main_r4650_BE_4MB.bin` e le ROM
  nella cartella corrente e apre la finestra.

## Velocita' della CPU principale
Il gioco misura alcuni timeout con cicli vuoti (es. 98.000 controlli separati da 4.096 giri per aspettare la sub-CPU:
~9 s sul R4650). Compilato con -O2 su un PC moderno quel conteggio finisce in pochi decimi di secondo e compare
"SUBCPU INITIALIZE TIME OUT". `BACKEDGE` conta gli archi all'indietro e `backedge_throttle` impone almeno 22,5 ns
per arco (3 cicli a 133 MHz): i cicli di attesa tornano alla durata originale, il resto non rallenta.
`S23_BENS=ns` cambia il valore (0 = nessun limite).

## Asset estratti e mappati in memoria (platform/assets.h)
Al primo avvio le ROM grandi vengono convertite nel formato usato in memoria e salvate nella cartella `assets`
(accanto a dove si lancia il gioco; `S23_ASSETS=cartella` per cambiarla): `tex.bin` (texture, 32 MB), `pt.bin`
(modelli 3D), `tm.bin` (mappa delle texture), `data.bin` (data ROM), `wave.bin` (campioni audio). Dai lanci successivi
vengono mappati in sola lettura (mmap / MapViewOfFile): il sistema tiene in RAM solo le pagine usate e le puo' liberare
senza swap. Cancellare la cartella per rigenerarli; `S23_NOASSETS=1` carica tutto in RAM come prima.

## NVRAM, risoluzione e refresh
- La backup RAM (impostazioni del menu di servizio aperto con F3, calibrazione del controller, record) viene salvata in
  `nvram.bin` nella cartella di avvio ogni 2 s se cambia e alla chiusura, e ricaricata all'avvio (solo in finestra;
  `S23_NVRAM=file` per un altro percorso, `S23_NONVRAM=1` per non usarla).
- "Risoluzione del 3D" nella schermata iniziale: "come lo schermo" rasterizza il 3D direttamente alla risoluzione della
  finestra mantenendo il 4:3 (fino a 4x, cioe' 2560x1920); il testo resta quello originale ingrandito.
- Il gioco va sempre alla sua velocita' (60 vblank/s, tempo reale); la finestra presenta con il vsync del monitor.
- "Formato (sperimentale)": 16:9 allarga il 3D ai lati senza deformarlo (stessa proiezione, si vede di piu'); il
  testo resta 4:3 al centro. Le scene costruite per il 4:3 possono avere bande o oggetti che compaiono ai bordi.

## Lingue
- **Lingua del gioco**: Panic Park contiene gia' 8 lingue (JPN, ENG, GER, FRA, SPA, POR, ITA, KOR), scelte dal menu di
  servizio (F3 > OTHERS > LANGUAGE) e salvate nella backup RAM (byte 307 dei dati). La voce "Lingua del gioco" della
  schermata iniziale la imposta direttamente. In italiano sono tradotte le regole dei minigiochi e i messaggi di
  gioco; alcune scritte dell'attract esistono solo in inglese nelle ROM.
- **Lingua dei menu**: file JSON in `lang/` (accanto all'eseguibile; `S23_LANGDIR` per un'altra cartella). Ogni file
  e' un oggetto piatto `{"chiave": "testo"}`; `language_name` e' il nome mostrato. Per aggiungere una lingua basta
  copiare `en.json` con un altro nome e tradurre i valori. Le chiavi mancanti restano in italiano; le lettere
  accentate vengono mostrate come `e'` (il font del menu e' ASCII).
- **Testi del gioco tradotti dal JSON**: le chiavi `"game:<TESTO INGLESE>"` del file della lingua del gioco (o, se la
  lingua del gioco e' "quella scelta nel gioco", di quello dei menu) sostituiscono i testi nei record del programma
  (font piccolo e font grande, max 36 caratteri). `"ascii:<testo>"` sostituisce stringhe ASCII a lunghezza fissa.
  Caratteri disponibili: A-Z, cifre, spazio, `! ( ) :` e nel font piccolo anche `, . ' - ? "`; il resto diventa spazio.
  `S23_LISTTEXT=1` stampa tutte le chiavi disponibili. Restano in inglese solo le scritte che sono grafica 3D o
  texture (es. "A FUN 1 PLAYER GAME", il logo, NEXT/HARD/NORMAL/EASY).
- **Data e ora**: l'RTC4543 viene letto dal PC a ogni accesso (flusso a 52 bit con giorno della settimana a 4 bit,
  ricezione sincrona continua della SCI1), quindi orologio e calendario del gioco seguono quelli di sistema.
- **Aggiornamento schermo**: VSync del monitor oppure 59,9 / 60 / 75 / 100 / 120 / 144 / 165 / 240 Hz o senza limite.
  Cambia solo quante volte viene mostrata l'immagine: il gioco resta a 59,9 Hz in tempo reale.

## Wii U (Aroma)

`Makefile.wiiu` + `platform/build_wiiu.sh` producono `panicprk.wuhb` (wut + SDL2 per Wii U); istruzioni in `README_WIIU.md`.
Modifiche portabili fatte per questo: `runtime.h` non gira i byte su host big endian (`be32h`), gli asset vanno in
`assets_be/` sugli host big endian, `-DS23_POLL_TICK` sostituisce SIGALRM con un controllo del tick dagli archi
all'indietro, thread tramite `start_thread()` (pthread o coreinit). Verificato con una build `powerpc-linux-gnu`
statica sotto `qemu-ppc-static` (big endian, 32 bit): il gioco arriva in partita con 3D e testi corretti.

## Luci e ombre dinamiche (rimosse)
C'era un'opzione sperimentale di luce per pixel e ombre proiettate (F8). Toglieva fedelta' all'originale e costava
molto, quindi e' stata rimossa: il 3D usa solo la luce per vertice del C435, come il cabinato.

## Nintendo Switch (homebrew)

`Makefile.switch` + `platform/build_switch.sh` producono `panicprk.nro` (libnx + SDL2 per Switch); istruzioni in
`README_SWITCH.md`. Come per la Wii U: tick senza segnali (`S23_POLL_TICK`), asset letti in memoria, cartella dell'app
`sdmc:/switch/panicprk` (`S23_SWITCH_DIR`). `platform/switch_sys.cpp` legge l'ora locale dal servizio time.
Su tutte le piattaforme: Select/− insieme a Start/+ sul controller chiude il gioco.

## Ottimizzazioni (console lente)
- H8/ASCA: decodifica delle istruzioni in cache per indirizzo (il codice gira da ROM), lettura veloce delle istruzioni
  dalla ROM, periferiche aggiornate a blocchi di 32 cicli (recuperate prima di ogni accesso ai registri), SLEEP a blocchi
  di 64 cicli, niente statistiche degli accessi se non richieste. Costo misurato: da ~700 a ~360 ms di CPU per secondo
  emulato. `S23_H8SLOW=1` torna al comportamento precedente.
- Scheda I/O ASCA-3 nativa (`platform/jvs_hle.h`): invece di emulare la seconda CPU (H8/3334 + firmware `asca-3a.ic14`)
  si risponde direttamente ai pacchetti JVS dell'H8. Le risposte sono state ricavate dal firmware originale e
  `test/jvs_probe.cpp` le confronta byte per byte (identita', funzioni, tasti, monete, analogici, encoder, comandi Namco
  0x70, linea sense): uguali, a parte le monete e gli analogici che arrivano qualche millisecondo prima. In una partita
  automatica i pacchetti scambiati sono gli stessi 19 della versione emulata. Il file `asca-3a.ic14` non serve piu'.
  `S23_ASCA=1` = firmware emulato come prima; `S23_JVSLOG=1` stampa il traffico.
- Attese a vuoto dell'H8 (~75% delle sue istruzioni): un salto all'indietro corto che ritorna con registri, flag e memoria
  identici e' un'attesa pura (RAM condivisa con la CPU principale, seriale TDRE): si avanza il tempo fino al prossimo
  evento delle periferiche o alla fine del blocco di ~32 us. I cicli di ritardo `dec.b / bne` si saltano calcolando i
  giri, e l'attesa con timeout a 0x2c0c (risposta della CPU principale, letta da TCNT3) si avanza fino al timeout (solo
  se i byte del codice corrispondono). Istruzioni eseguite: da 3,8 a 0,9 milioni al secondo; audio identico (stesso
  spettro, correlazione 0,997-0,999 con la versione senza salti). `S23_H8NOIDLE=1` = come prima; `S23_H8PROF=1` scrive
  `h8prof.txt` (istruzioni per indirizzo).
- Il thread dell'H8, quando e' avanti, dorme 500 us invece di 100 (meno cambi di contesto; `S23_H8SLEEP=us`).
  In totale il thread H8 usa circa un terzo della CPU di prima (misurato con perf: 7156 -> 2555 campioni).
- 3D: disegno in parallelo a fasce di righe (thread di supporto: PC = core - 3, max 6; Switch/Wii U = 2 a priorita' piu'
  bassa sui core del gioco e dell'H8; `S23_RTHREADS=n`). Via veloce senza effetti con intervallo ritagliato e correzione
  prospettica ogni `S23_PSPAN` pixel (PC 1 = esatta, console 8).
- Console: il thread del gioco dorme invece di girare a vuoto quando aspetta.
- Indicatore a schermo (gioco Hz, schermo Hz, ms del 3D, velocita' H8): F1 o clic dello stick sinistro; acceso di default su console.
- Aggiornamento schermo: nuova opzione 30 Hz (il gioco resta a 59,9 Hz) per dimezzare il lavoro del disegno.

## Finestra dei tutorial (RULE / NEEDED / LIFE)
I piani di clip del C435 (comando 0x00c8) per lo schermino del monitor non sono normalizzati e sono inclinati (trapezio).
La formula di MAME (vp_sy = |c|*fov, a = b = 1) traboccava l'int16 e la demo/le scritte uscivano fuori dallo schermino
(succede anche in MAME). Ora vp_sx = |c/a|*fov, vp_sy = |c/b|*fov e i piani sinistro/destro inclinati ritagliano ogni riga.
`S23_NOSLANT=1` disattiva il ritaglio a trapezio.
- Risoluzione del 3D "veloce 320x240" (`res3d=2`): il 3D viene disegnato a meta' risoluzione e ingrandito, il testo resta
  a 640x480. Default su Switch/Wii U. Sulla Switch i thread del disegno ora hanno priorita' piu' alta del gioco/H8:
  a priorita' piu' bassa un thread sospeso a meta' fascia bloccava il fotogramma (3D a 148 ms).
- Disegno in un thread a parte: la finestra presenta al ritmo del monitor (VSync, es. 100 Hz) senza aspettare il disegno,
  e il disegno non aspetta il VSync (prima, con disegno da 22 ms e monitor a 100 Hz, si scendeva a ~29 immagini/s).
  Indicatore: "img" = immagini nuove al secondo (al massimo 60, come il gioco), "schermo" = refresh della finestra.
  Thread di supporto del 3D su PC: core - 2 (max 6).
- Finestra per segmento: clip e viewport (comandi 0x00c8 / 0x0046) vengono ricordati per ogni segmento della lista e
  ripristinati all'inizio del segmento. In alcuni livelli (es. NEEDED/LIFE di WALL PANIC, missile) il gioco manda la
  finestra dello schermino una volta sola, e il segmento 0 rimanda ogni fotogramma quella a tutto schermo: con uno stato
  unico le icone e la demo uscivano dal monitor. `S23_NOSEGWIN=1` torna al comportamento precedente.
  Una finestra si ricorda solo se nel suo segmento segue un modello vero, e si dimentica quando un fotogramma ha meno
  segmenti (cambio schermata): prima una finestra vecchia tagliava il 3D della schermata "inserisci monete".
  0x0046 con i 3 bit bassi di P[3] a zero (0x20) non cambia la finestra se quella in uso e' il trapezio del monitor:
  il gioco la manda prima di NEEDED/LIFE mentre scende la saracinesca, e applicandola le scritte uscivano a tutto
  schermo. Negli altri casi si applica (attract "INTERACTIVE CONTROLS": senza, i personaggi a tutto schermo sparivano e
  restava solo la scena piccola). `S23_WINALL=1` = applicarla sempre.
  Il gioco ricalcola la finestra dello schermino solo durante la panoramica verso il monitor, e spesso la manda una
  volta sola a meta' percorso: riusando una finestra a trapezio ricordata si usa quella a telecamera ferma (sempre la
  stessa, presa dalle tracce dei comandi). `S23_NOCANON=1` la disattiva.
- Saracinesca "PANIC PARK" che chiude il monitor a fine tutorial (modelli 02c8/02c9): disegnata con la finestra a tutto
  schermo e davanti a tutto il 3D, come sul cabinato (prima restava ritagliata nello schermino, dietro a NEEDED/LIFE).
  `S23_FULLWIN=lista|none` (e `S23_FRONT`, che ora vale `2db,2c8,2c9`).
- Traduzione: le stringhe che il gioco completa con una cifra in una colonna fissa devono avere la stessa lunghezza
  dell'originale ("INSERT" -> "METTI ", prima "INSERISCI" veniva sovrascritto: "INSERIS1I").
- Coordinate di texture senza il +0,5 di MAME: il nostro rasterizzatore campiona al centro del pixel, e con lo 0,5 in piu'
  l'ultima colonna/riga di ogni riquadro prendeva i texel del riquadro vicino (bordi colorati, es. la griglia dei 25 giochi).
  `S23_UVHALF=1` = come MAME.
- Eccezioni di profondita' (`front_model`): il pulsante START 3D (modello 02db) della schermata PANIC KING ha uno
  spostamento di profondita' di +20000 e con l'ordinamento ricostruito finiva dietro alle statuine; sul cabinato e'
  davanti a tutto il 3D (la mano e la freccia sono sprite del livello testo, sempre sopra). `S23_FRONT=lista|none`.

## 3D su GPU (OpenGL) — platform/gl3d.h
- Nuova opzione della schermata iniziale "Disegno del 3D": scheda video (OpenGL, predefinita su PC) o processore.
  Nel file .ini: `gpu=1/0`; da variabile: `S23_GL=1/0`. Se OpenGL non parte si torna da soli al disegno della CPU.
- La CPU prepara ancora i poligoni (g3d::prepare: trasformazioni, luce per vertice, proiezione, ordinamento, finestre
  del monitor). Il rasterizzatore pero' e' sulla GPU: un fragment shader rifa' scanline() (mappa dei tile, texrom a 8 bit,
  palette, stencil del C412, sfumature, alpha e media, ritaglio rettangolare e a trapezio per riga). Le ROM grafiche
  (32 MB di texrom come array di texture 2048x2048x8, mappa dei tile) si caricano una volta. Palette, stencil, dati dei
  poligoni e testo si aggiornano a ogni fotogramma. Una seconda passata compone sfondo, 3D, testo e gamma nella texture
  che SDL mostra, quindi OSD e tasti a tocco restano uguali.
- Usa il contesto OpenGL del renderer di SDL ("opengl" su PC, "opengles2" con contesto ES 3.0 su Android/Switch).
  Le funzioni GL si caricano con SDL_GL_GetProcAddress (nessun header o libreria in piu').
- Verifica (`S23_GLCMP=n`): ogni fotogramma viene disegnato sia dalla GPU sia dalla CPU e confrontato pixel per pixel.
  Su oltre 6000 fotogrammi (attract, menu, tutorial, 12 livelli) i pixel diversi sono lo 0,2-0,4%: bordi dei poligoni
  e confini tra texel, sparsi, non visibili. Le immagini con piu' dello 0,1% di differenze vengono salvate
  (`glcmp_*_gpu.png` / `_cpu.png`; `S23_GLCMP_SAMPLES=1` = solo campioni regolari). `S23_GLPROBE=x,y` dice quale
  poligono copre un pixel nei due disegni.
- Coordinate di texture: +1/1000 prima del troncamento. Senza, un valore costante lungo un lato (es. v = 2880) interpolato
  in float arrivava a 2879,9998 e prendeva la riga di texel sopra (bande scure sulle facce piatte).
- Tempo del thread di disegno a 960x720 su questa macchina di prova: da ~56 ms (CPU) a ~6 ms (CPU che prepara + GPU), e da 25 a 60
  immagini/s.
- Console: Switch e Android hanno il codice (OpenGL ES 3.0) ma `gpu=0` di default finche' non e' provato sull'hardware.
  Wii U: niente OpenGL (servira' un motore GX2).

## Nintendo 3DS (New 3DS) con la GPU
Port nuovo sul codice attuale: 3D disegnato dalla GPU PICA200 con una cache di texture (`platform/pica3d.h`,
`platform/platform_3ds.h`), menu sullo schermo inferiore, comandi, audio. Compilazione: `bash platform/build_all.sh 3ds`.
Dettagli, file da copiare e limiti in `3ds/README_3DS.md`. Test del renderer su PC: `S23_GLCMP=1 S23_PICATEST=60`
(con il disegno su GPU e `S23_RTHREADS=0`) salva `pica_NN_gpu.png` / `pica_NN_sw.png` e stampa le differenze.

## Menu nuovo (schermata principale e impostazioni)
- All'avvio c'e' la schermata principale in stile Panic Park con GIOCA, IMPOSTAZIONI ed ESCI (in gioco: RIPRENDI).
  Le impostazioni sono divise in schede (Comandi, Leva e tocco, Video, Gioco) con righe grandi da toccare.
- Si usa con tutto: mouse o dito (tocca per scegliere, trascina per scorrere la lista), tastiera (frecce, Invio,
  Esc, Q/E o Tab per le schede), controller (croce o stick, A conferma, B indietro, LB/RB schede, Start = gioca).
  La barra in basso mostra i tasti giusti per l'ultimo dispositivo usato.
- Per assegnare un tasto: scheda Comandi, scegli la casella (Tastiera 1, Tastiera 2 o Controller) e premi il
  tasto/pulsante; "Cancella" (o Backspace) toglie l'assegnazione. "Ripristina" chiede una seconda conferma.
- Sfondo e logo: se nella cartella `ui/` (accanto a `lang/`) ci sono `background.png` e `logo.png` il menu li usa,
  altrimenti disegna un mare a spirale e il titolo col font. Gli script di compilazione copiano `platform/ui/*.png`
  in dist (Windows, Linux) e nella cartella dell'app sulla SD (Switch, Wii U); su Android copiare `ui/` a mano.
- Font: Bangers e Lilita One (licenza SIL OFL, in `platform/fonts/`), gia' convertiti in `platform/ui_font.h`
  (si rigenera con `python3 tools/bake_ui_font.py`). Le PNG si leggono con stb_image (`platform/third_party/`).
- Traduzioni: le chiavi nuove (play, settings, exit, tab_*, h_*, ...) sono in `lang/it.json` e `lang/en.json`;
  ora le lettere accentate si vedono (Giù, sì, modalità).

## Menu delle impostazioni in gioco e pulsanti a schermo
- In gioco si torna alle impostazioni con Esc o F4 (tastiera), Select/- insieme a Start/+ (controller, anche su Switch e
  Wii U), il tasto Indietro del telefono o il pulsante MENU a schermo (in alto al centro). Il gioco va in pausa: il
  tempo dell'emulazione si ferma (`emu_ns()`), l'H8 e l'audio pure, e alla ripresa riparte da dove era.
  "RIPRENDI IL GIOCO" (o Esc / B / Indietro) torna al gioco, "Esci" chiude. Prima Esc e Select+Start chiudevano subito.
- Nuova opzione "Pulsanti a schermo (tocco)": si'/no (`touchui=` nel .ini). Con "no" spariscono MONETA, SERVIZIO, MENU e i
  tasti del menu di servizio, e i tocchi in quelle zone non fanno nulla (resta lo sterzo col dito).
- Corretto: con il disegno su GPU, dopo che SDL aveva caricato il testo dell'OSD o dei tasti a schermo, il caricamento
  delle texture leggeva righe troppo lunghe (GL_UNPACK_ROW_LENGTH lasciato da SDL) e il gioco si chiudeva.

## Android
`platform/build_android.sh` (setup + build) crea un progetto Gradle dal modello di SDL2 e produce
`dist/android/PanicPark.apk`; istruzioni in `README_ANDROID.md`. Comandi a tocco (anche su PC con touchscreen):
trascinare il dito = sterzo del giocatore 1, tocco breve = Start, angolo in alto a sinistra = moneta.
La schermata iniziale ora si usa anche con mouse/tocco.
