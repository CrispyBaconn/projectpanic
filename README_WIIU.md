# Panic Park ricompilato – Wii U (Aroma)

Versione `.wuhb` per Wii U con l'ambiente **Aroma**. È lo stesso codice della versione PC (codice MIPS ricompilato,
H8/3002 emulato, scheda I/O ASCA nativa, 3D software, audio C352): cambiano solo il sistema operativo sotto e l'ordine dei byte.

## Compilare (in WSL)

Serve devkitPro (lo hai già per il 3DS) con i pacchetti per Wii U:

```sh
sudo dkp-pacman -S wiiu-dev wiiu-sdl2
cd /mnt/c/Users/mirko/Downloads/panicprk_recomp/recomp
sh platform/build_wiiu.sh
```

Il risultato è `panicprk.wuhb`; lo script prepara anche `dist/wiiu/sd/wiiu/apps/panicprk/` con tutto quello che va sulla SD.

## Sulla SD

```
sd:/wiiu/apps/panicprk/
    panicprk.wuhb
    panicprk_main.elf
    main_r4650_BE_4MB.bin
    panicprkrom/            tutte le ROM di panicprk (anche pnp1wavel.2c / pnp1waveh.2a per l'audio)
    lang/                   it.json, en.json
```

Al primo avvio vengono create `assets_be/` (≈85 MB: texture, modelli, campioni audio e data ROM convertiti),
`nvram.bin`, `panicprk.ini` e `log.txt`. Il primo avvio è lento perché deve scrivere gli asset sulla SD.
Gli asset del PC (`assets/`) **non** vanno bene: sulla Wii U i dati sono big endian, per questo la cartella è un'altra.

## Comandi (GamePad o Pro Controller)

| Tasto | Funzione |
|---|---|
| Croce / stick sinistro | movimento |
| A | moneta |
| + (Start) | Start giocatore 1 |
| Y | Start giocatore 2 |
| B | Invio / pulsante 1 |
| L | Test |
| R | DIP di configurazione (calibrazione) |
| HOME | esce ad Aroma |

Tutto si rimappa dalla schermata iniziale, come su PC.

## Differenze rispetto al PC

- Il 3D parte a 640×480 (l'opzione "come lo schermo" c'è ma la CPU della Wii U difficilmente regge).
- Schermo sempre intero; immagine su TV e GamePad.
- Niente segnali/timer POSIX: il tick a 1 kHz e il VBlank a 59,9 Hz vengono controllati dal codice ricompilato
  (stesso meccanismo già provato su Linux con `-DS23_POLL_TICK`).
- Thread: codice MIPS sul core 2, H8 sul core 0, finestra/3D/audio sul core 1.
- Il POST è saltato di default.

## Stato

- Verificato: una build PowerPC big endian a 32 bit (stesso tipo di CPU della Wii U) gira correttamente sotto
  emulazione: avvio, attract, 3D, testi, partita.
- **Non verificato**: la compilazione con wut e l'esecuzione su una Wii U vera. Se qualcosa non va, il file
  `log.txt` nella cartella dell'app dice fin dove è arrivato.
- Rischio principale: la velocità. L'Espresso (3 core PowerPC 750 a 1,24 GHz) è molto più lento di un PC; nel log
  la riga `H8: …% del tempo reale` indica se la sub-CPU tiene il passo (sotto ~95% compaiono errori di comunicazione
  o rallentamenti). Ridurre il 3D o compilare con `OPT=-O3` può aiutare.
