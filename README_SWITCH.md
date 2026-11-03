# Panic Park ricompilato – Nintendo Switch (homebrew)

Versione `.nro` per Switch con CFW (Atmosphère + Homebrew Menu). È lo stesso codice della versione PC
(codice MIPS ricompilato, H8/3002 emulato, scheda I/O ASCA nativa, 3D software, audio C352); la Switch è little endian come il PC,
quindi gli asset estratti dalla versione PC vanno bene così come sono.

## Compilare (in WSL)

Serve devkitPro (lo hai già per il 3DS) con i pacchetti per Switch:

```sh
sudo dkp-pacman -S switch-dev switch-sdl2
cd /mnt/c/Users/mirko/Downloads/panicprk_recomp/recomp
sh platform/build_switch.sh
```

Il risultato è `panicprk.nro`; lo script prepara anche `dist/switch/sd/switch/panicprk/` con tutto quello che va
sulla SD (compresi gli asset già estratti da `dist/windows/assets`, se ci sono).

## Sulla SD

```
sd:/switch/panicprk/
    panicprk.nro
    panicprk_main.elf
    main_r4650_BE_4MB.bin
    panicprkrom/            tutte le ROM di panicprk (anche pnp1wavel.2c / pnp1waveh.2a per l'audio)
    lang/                   it.json, en.json
    assets/                 (facoltativo) gli stessi file .bin della versione PC
```

Senza `assets/` vengono creati al primo avvio (≈85 MB, lento). Vengono creati anche `nvram.bin`, `panicprk.ini`, `log.txt`.

**Avvialo in modalità "title takeover"** (tieni premuto R mentre apri un gioco qualsiasi): dall'album l'homebrew ha
poca memoria e meno CPU.

## Comandi (Joy-Con / Pro Controller)

| Tasto | Funzione |
|---|---|
| Croce / stick sinistro | movimento |
| A | moneta |
| + | Start giocatore 1 |
| Y | Start giocatore 2 |
| B | Invio / pulsante 1 |
| L | Test |
| R | DIP di configurazione (calibrazione) |
| − e + insieme | esci |

Tutto si rimappa dalla schermata iniziale, come su PC. (I nomi A/B seguono la mappatura di SDL: se risultano scambiati,
rimappali dalla schermata iniziale.)

## Differenze rispetto al PC

- Il 3D parte a 640×480 (l'opzione "come lo schermo" c'è, ma costa CPU); schermo sempre intero, 1280×720.
- Niente segnali/timer POSIX: il tick a 1 kHz e il VBlank a 59,9 Hz vengono controllati dal codice ricompilato
  (stesso meccanismo della Wii U, provato su Linux con `-DS23_POLL_TICK`).
- L'ora per l'RTC del gioco viene dal servizio time della Switch (fuso orario compreso).
- Il POST è saltato di default.

## Stato

- Verificato: il codice compila per AArch64 (controllo di sintassi con le intestazioni standard) e il meccanismo di
  tick senza segnali funziona su Linux.
- **Non verificato**: la compilazione con libnx e l'esecuzione su una Switch vera. Se qualcosa non va, `log.txt`
  nella cartella dell'app dice fin dove è arrivato; la riga `H8: …% del tempo reale` indica se la sub-CPU tiene il passo.

## 3D su GPU
Il 3D lo disegna la GPU della Switch (OpenGL ES 3): la CPU prepara solo i poligoni, cosi' resta tempo per il gioco e
l'H8. E' la scelta predefinita ("Disegno del 3D: scheda video" nella schermata iniziale, `gpu=1` in `panicprk.ini`).
Verificato su PC con lo stesso percorso OpenGL ES (stesso risultato del disegno CPU, 0,3% di pixel diversi ai bordi).
Se non parte, il gioco torna da solo al disegno della CPU e `log.txt` dice perche'.
Se in `panicprk.ini` c'e' gia' `gpu=0` (salvato da una versione precedente) va cambiato nella schermata iniziale.
