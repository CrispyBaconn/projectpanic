# Panic Park ricompilato – Android

APK per telefoni/tablet Android 5.0+ (arm64). Stesso codice della versione PC, con SDL2 compilato da `platform/SDL2-src`.

## Compilare (in WSL)

```sh
sudo apt install openjdk-17-jdk unzip wget
cd /mnt/c/Users/mirko/Downloads/panicprk_recomp/recomp
bash platform/build_android.sh setup     # una volta: SDK e NDK Android in ~/android-sdk (~3 GB)
bash platform/build_android.sh           # -> dist/android/PanicPark.apk
```
Il progetto Gradle viene creato in `~/panicprk_android` (sul disco di WSL, molto piu' veloce di /mnt/c).
Per telefoni vecchi a 32 bit: `ABIS="arm64-v8a armeabi-v7a" bash platform/build_android.sh`.

## Sul telefono
1. Installa `PanicPark.apk` e avvialo.
2. Si apre la schermata "Accesso a tutti i file": attivalo per Panic Park e torna indietro.
   L'app crea la cartella **PanicPark** nella memoria del telefono (accanto a Download, DCIM...).
3. Copia in `PanicPark/`: `panicprk_main.elf`, `main_r4650_BE_4MB.bin`, la cartella `panicprkrom/`, la cartella
   `lang/` e (facoltativo) `dist/windows/assets` come `assets/`. Col file manager del telefono o da PC via USB.
4. Riapri l'app. Se manca qualcosa compare un messaggio con l'elenco dei file mancanti.
Senza il permesso funziona ancora la cartella privata `Android/data/com.recomp.panicpark/files/`.
Nella cartella usata vengono scritti anche `log.txt`, `nvram.bin`, `panicprk.ini`.

## Comandi a tocco
- **trascina il dito a destra/sinistra, come su un trackpad**: il dito sposta la leva del giocatore 1 (piu' veloce
  muovi il dito, piu' si sposta); sollevando il dito la leva resta dov'e'. La barretta in basso mostra la posizione.
  Con `touch=0` in `panicprk.ini` torna il modo joystick (la leva torna al centro quando sollevi il dito).
- **tocco breve**: Start + conferma (avvia la partita, sceglie nei menu del gioco)
- **angolo in alto a sinistra** (riquadro "MONETA"): inserisce una moneta
- **angolo in alto a destra** (riquadro "SERVIZIO"): DIP di configurazione (menu di servizio)
- **Indietro**: esce
- Schermata iniziale: tocca una riga per sceglierla, toccala di nuovo per cambiarla; DZ/sensibilita': meta' sinistra
  diminuisce, meta' destra aumenta. "AVVIA IL GIOCO" parte al primo tocco.
- Un controller Bluetooth funziona come su PC.

## Stato
Non verificato su un telefono vero (qui non c'e' l'SDK Android): controllato solo che il codice compili per AArch64.
Se qualcosa non va, `log.txt` nella cartella dell'app dice fin dove e' arrivato.

## 3D su GPU (sperimentale)
Il disegno del 3D con la GPU del telefono (OpenGL ES 3.0) c'e' ma e' spento di default: per provarlo scegli
"Disegno del 3D: scheda video" nella schermata iniziale (o `gpu=1` in `PanicPark/panicprk.ini`). Se non parte, il gioco
torna da solo al disegno della CPU.
