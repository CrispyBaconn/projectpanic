# Panic Park ricompilato su Nintendo 3DS (New 3DS / New 2DS XL)

Il 3D viene disegnato dalla **GPU del 3DS** (PICA200, con citro3d). La PICA200 non ha shader per i pixel, quindi la
texture a 8 bit con palette del System 23 viene convertita a pezzi (celle da 128x128 texel, con la palette del
poligono) in una cache di texture sulla GPU; ogni poligono viene ritagliato sulla CPU contro le celle e contro la
finestra della viewport, e luce, dissolvenze e trasparenza diventano colori dei vertici e stadi del combinatore.
Il codice e' in `platform/pica3d.h` (indipendente dalla piattaforma) e `platform/platform_3ds.h` (citro3d/citro2d).

- Schermo superiore: il gioco (320x240 al centro), con livello del testo e gamma.
- Schermo inferiore: menu GIOCA / IMPOSTAZIONI / ESCI; in gioco MONETA e MENU da toccare e le statistiche.
- Comandi: Circle Pad o croce sx/dx = leva, A = pulsante, START = start 1, Y = start 2, B o SELECT = moneta,
  croce su/giu' = menu di servizio, L = test, R = DIP di servizio, SELECT+START o MENU = pausa.
- Core: 0 = disegno, 1 = H8 + audio (80% del core), 2 = gioco (R4650). Audio con ndsp (serve `sdmc:/3ds/dspfirm.cdc`,
  creato con DSP1; senza, il gioco va lo stesso ma muto).

## Compilare (WSL con devkitPro)
    sudo dkp-pacman -S 3ds-dev          (una volta: devkitARM, libctru, citro3d, citro2d, picasso)
    bash platform/build_all.sh 3ds      (oppure: sh platform/build_3ds.sh)

## Sulla SD
Copia il contenuto di `dist/3ds/sd/` nella radice della SD. Deve risultare:
- `sd:/3ds/panicprk.3dsx`
- `sd:/3ds/panicprk/` con `panicprk_main.elf`, `main_r4650_BE_4MB.bin`, `panicprkrom/`, `lang/`, `ui/` e **`assets/`**

La cartella `assets/` e' obbligatoria sul 3DS: convertire le ROM sul 3DS non ci sta in memoria. Lo script la copia da
`dist/windows/assets` o da quella della Switch (stesso formato); se non c'e', avvia una volta la versione PC.

## Memoria
Circa 95 MB di heap + 14 MB per la GPU, sui ~124 MB che il New 3DS lascia a un'app homebrew: la data ROM (16 MB)
resta sulla SD e viene letta a pagine. Sul 3DS vecchio non ci sta.

## Prestazioni
La GPU risolve il disegno, ma il processore principale del gioco (R4650) e l'H8 vanno emulati sui core ARM11 a 804 MHz:
sulla Wii U il solo R4650 occupa quasi un core da 1,24 GHz, quindi qui il gioco andra' piu' lento del vero.
Sullo schermo inferiore (opzione "Statistiche") e nel `log.txt` (righe `PERF 3DS`) ci sono i numeri per capire dove.

## Verifiche fatte e non fatte
- Fatto: il renderer `pica3d.h` e' stato provato su PC con OpenGL a funzioni fisse (stessi combinatori della PICA200,
  `S23_PICATEST`): stesse immagini del disegno software, a parte il campionamento delle texture lontane.
- Fatto: tutto il codice compila per ARM11 (armv6k, hard float) contro gli header veri di libctru, citro3d e citro2d.
- Non fatto: link con devkitARM ed esecuzione su un 3DS vero o su un emulatore.
