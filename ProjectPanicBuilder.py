#!/usr/bin/env python3
"""Project Panic Builder: a small window that turns your Panic Park ROM set into the game for the devices you pick.

    Windows:      double-click ProjectPanicBuilder.py   (needs Python 3 from python.org; no WSL needed)
    Linux / WSL:  python3 ProjectPanicBuilder.py      (needs  sudo apt install python3-tk)

On Windows without WSL the Windows build is done here in Python with LLVM-MinGW (downloaded the first time), and the
Switch / Wii U / 3DS builds run the same scripts through the bash of the devkitPro installer. Linux and Android
need Linux or WSL.

Steps: extract the ROM archive (.zip or .7z) into panicprkrom/, rebuild panicprk_main.elf + main_r4650_BE_4MB.bin
(tools/extract_rom.py), generate the C++ code (mips2cpp.py), then run platform/build_<device>.sh for every device chosen.
"""
import glob, json, os, queue, re, shutil, signal, subprocess, sys, tarfile, threading, time, urllib.request, zipfile
from concurrent.futures import ThreadPoolExecutor, as_completed
try:
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
except ImportError:                                    # Linux without Tk for Python
    print('The builder window needs Tk for Python:  sudo apt install python3-tk')
    sys.exit(1)

ROOT = os.path.dirname(os.path.abspath(__file__))
CFG = os.path.join(os.path.expanduser('~'), '.projectpanic_builder.json')
WIN = os.name == 'nt'
FONT, MONO = ('Segoe UI', 'Consolas') if WIN else ('DejaVu Sans', 'DejaVu Sans Mono')
# devkitPro: /opt/devkitpro on Linux; on Windows its installer puts it (with its own MSYS2 bash) in C:\devkitPro.
# The Windows one is used also when the builder runs in WSL or in an MSYS2 Python (/mnt/c/devkitPro, /c/devkitPro).
def find_dkp():
    env = [os.environ.get(k, '') for k in ('DEVKITPRO_WIN', 'DEVKITPRO')]
    if WIN: cands = [e for e in env if re.match(r'^[A-Za-z]:', e)] + [r'C:\devkitPro', r'D:\devkitPro']
    else: cands = [e for e in env if e.startswith('/')] + ['/opt/devkitpro', '/mnt/c/devkitPro', '/c/devkitPro',
                                                         '/cygdrive/c/devkitPro', '/mnt/d/devkitPro', '/d/devkitPro']
    for c in cands:
        if any(os.path.isdir(os.path.join(c, d)) for d in ('devkitARM', 'devkitA64', 'devkitPPC')): return c
    return cands[0] if WIN or not cands else '/opt/devkitpro'
DKP = find_dkp()
DKP_BASH = os.path.join(DKP, 'msys2', 'usr', 'bin', 'bash.exe')
DKP_MAKE = os.path.join(DKP, 'msys2', 'usr', 'bin', 'make.exe')
DKP_WIN = os.path.isfile(DKP_BASH)                   # the Windows devkitPro: its bash runs the scripts
SDLV = '2.30.9'
LLVM_MINGW_PIN = '20240619'          # used if GitHub's "latest release" lookup fails
ROM_KEY = ('pnp2vera.ic1', 'pnp2vera.ic2')               # the two halves of the main program ROM

# ------------------------------------------------------------------------------------------------ texts
TXT = {
 'en': dict(title='Project Panic Builder', sub='Build Panic Park for your devices',
            s1='1  ROM set', s1h='Choose your panicprk archive (.zip or .7z)', browse='Browse…',
            s2='2  Devices', s2h='Tick the devices to build for (greyed out: install the tool shown first)', s3='3  Build', go='BUILD', stop='Stop',
            log='Show details', hidelog='Hide details', ready='Ready', missing='missing: ',
            ok='ready', st_wait='waiting', st_run='working…', st_ok='done', st_err='failed', st_skip='skipped',
            p_rom='Extract the ROM set', p_elf='Find the game program', p_gen='Generate the C++ code',
            no_rom='Choose the ROM archive first.', no_plat='Tick at least one device.',
            bad_rom='The archive does not contain the Panic Park ROMs (pnp2vera.ic1 / pnp2vera.ic2).',
            no7z='Could not open the .7z archive: install 7-Zip (7-zip.org, or  sudo apt install p7zip-full)',
            done='Finished! The files are in the dist folder.', failed='Something went wrong: open "Show details".',
            stopped='Stopped.', open='Open the dist folder', a_setup='first time: downloads Java and the Android SDK (~3 GB)', a_studio='uses Android Studio (its Java and SDK)', a_add='adds the missing SDK parts', a_ready='SDK already installed',
            n3ds='needs the assets folder made by the PC version: build Windows or Linux too and start the game once',
            confirm_stop='Stop the build?', reuse='already extracted: using panicprkrom/', no_file='file not found', recheck='Check again', w_dl='first time: downloads the compiler (LLVM-MinGW, ~150 MB)',
            no_elftools='pyelftools is missing:  sudo apt install python3-pyelftools',
            dkp_inst='Install the missing devkitPro packages', dkp_busy='installing the devkitPro packages…', dkp_fail='Installation failed: open "Show details".'),
 'it': dict(title='Project Panic Builder', sub='Compila Panic Park per i tuoi dispositivi',
            s1='1  ROM', s1h='Scegli il tuo archivio panicprk (.zip o .7z)', browse='Sfoglia…',
            s2='2  Dispositivi', s2h='Spunta i dispositivi (in grigio: prima installa lo strumento indicato)', s3='3  Compila', go='COMPILA', stop='Ferma',
            log='Mostra dettagli', hidelog='Nascondi dettagli', ready='Pronto', missing='manca: ',
            ok='pronto', st_wait='in attesa', st_run='in corso…', st_ok='fatto', st_err='errore', st_skip='saltato',
            p_rom='Estrai le ROM', p_elf='Trova il programma del gioco', p_gen='Genera il codice C++',
            no_rom='Prima scegli l’archivio delle ROM.', no_plat='Spunta almeno un dispositivo.',
            bad_rom="L'archivio non contiene le ROM di Panic Park (pnp2vera.ic1 / pnp2vera.ic2).",
            no7z='Impossibile aprire l’archivio .7z: installa 7-Zip (7-zip.org, oppure  sudo apt install p7zip-full)',
            done='Finito! I file sono nella cartella dist.', failed='Qualcosa è andato storto: apri "Mostra dettagli".',
            stopped='Fermato.', open='Apri la cartella dist', a_setup='la prima volta scarica Java e l’SDK Android (~3 GB)', a_studio='usa Android Studio (il suo Java e il suo SDK)', a_add='aggiunge le parti mancanti dell’SDK', a_ready='SDK già installato',
            n3ds='serve la cartella assets creata dalla versione PC: compila anche Windows o Linux e avvia il gioco una volta',
            confirm_stop='Fermare la compilazione?', reuse='già estratte: uso panicprkrom/', no_file='file non trovato', recheck='Ricontrolla', w_dl='la prima volta scarica il compilatore (LLVM-MinGW, ~150 MB)',
            no_elftools='manca pyelftools:  sudo apt install python3-pyelftools',
            dkp_inst='Installa i pacchetti devkitPro mancanti', dkp_busy='installo i pacchetti devkitPro…', dkp_fail='Installazione non riuscita: apri "Mostra dettagli".'),
 'de': dict(title='Project Panic Builder', sub='Panic Park für deine Geräte kompilieren',
            s1='1  ROM-Set', s1h='Wähle dein panicprk-Archiv (.zip oder .7z)', browse='Durchsuchen…',
            s2='2  Geräte', s2h='Hake die Geräte an (ausgegraut: erst das angezeigte Werkzeug installieren)', s3='3  Kompilieren', go='KOMPILIEREN', stop='Stopp',
            log='Details anzeigen', hidelog='Details ausblenden', ready='Bereit', missing='fehlt: ',
            ok='bereit', st_wait='wartet', st_run='läuft…', st_ok='fertig', st_err='Fehler', st_skip='übersprungen',
            p_rom='ROM-Set entpacken', p_elf='Spielprogramm finden', p_gen='C++-Code erzeugen',
            no_rom='Wähle zuerst das ROM-Archiv.', no_plat='Hake mindestens ein Gerät an.',
            bad_rom='Das Archiv enthält nicht die Panic-Park-ROMs (pnp2vera.ic1 / pnp2vera.ic2).',
            no7z='Das .7z-Archiv lässt sich nicht öffnen: installiere 7-Zip (7-zip.org, oder  sudo apt install p7zip-full)',
            done='Fertig! Die Dateien sind im Ordner dist.', failed='Etwas ist schiefgelaufen: öffne „Details anzeigen“.',
            stopped='Gestoppt.', open='Ordner dist öffnen', a_setup='beim ersten Mal werden Java und das Android-SDK geladen (~3 GB)', a_studio='nutzt Android Studio (dessen Java und SDK)', a_add='ergänzt die fehlenden SDK-Teile', a_ready='SDK schon installiert',
            n3ds='braucht den Ordner assets der PC-Version: kompiliere auch Windows oder Linux und starte das Spiel einmal',
            confirm_stop='Kompilieren abbrechen?', reuse='schon entpackt: nutze panicprkrom/', no_file='Datei nicht gefunden', recheck='Erneut prüfen', w_dl='beim ersten Mal wird der Compiler geladen (LLVM-MinGW, ~150 MB)',
            no_elftools='pyelftools fehlt:  sudo apt install python3-pyelftools',
            dkp_inst='Fehlende devkitPro-Pakete installieren', dkp_busy='devkitPro-Pakete werden installiert…', dkp_fail='Installation fehlgeschlagen: öffne „Details anzeigen“.'),
 'fr': dict(title='Project Panic Builder', sub='Compile Panic Park pour tes appareils',
            s1='1  ROM', s1h='Choisis ton archive panicprk (.zip ou .7z)', browse='Parcourir…',
            s2='2  Appareils', s2h='Coche les appareils (grisés : installe d’abord l’outil indiqué)', s3='3  Compiler', go='COMPILER', stop='Arrêter',
            log='Afficher les détails', hidelog='Masquer les détails', ready='Prêt', missing='manque : ',
            ok='prêt', st_wait='en attente', st_run='en cours…', st_ok='terminé', st_err='erreur', st_skip='ignoré',
            p_rom='Extraire les ROM', p_elf='Trouver le programme du jeu', p_gen='Générer le code C++',
            no_rom='Choisis d’abord l’archive des ROM.', no_plat='Coche au moins un appareil.',
            bad_rom='L’archive ne contient pas les ROM de Panic Park (pnp2vera.ic1 / pnp2vera.ic2).',
            no7z='Impossible d’ouvrir l’archive .7z : installe 7-Zip (7-zip.org, ou  sudo apt install p7zip-full)',
            done='Terminé ! Les fichiers sont dans le dossier dist.', failed='Un problème est survenu : ouvre « Afficher les détails ».',
            stopped='Arrêté.', open='Ouvrir le dossier dist', a_setup='la première fois, télécharge Java et le SDK Android (~3 Go)', a_studio='utilise Android Studio (son Java et son SDK)', a_add='ajoute les parties manquantes du SDK', a_ready='SDK déjà installé',
            n3ds='il faut le dossier assets créé par la version PC : compile aussi Windows ou Linux et lance le jeu une fois',
            confirm_stop='Arrêter la compilation ?', reuse='déjà extraites : j’utilise panicprkrom/', no_file='fichier introuvable', recheck='Revérifier', w_dl='la première fois, télécharge le compilateur (LLVM-MinGW, ~150 Mo)',
            no_elftools='pyelftools manque :  sudo apt install python3-pyelftools',
            dkp_inst='Installer les paquets devkitPro manquants', dkp_busy='installation des paquets devkitPro…', dkp_fail='Échec de l’installation : ouvre « Afficher les détails ».'),
 'es': dict(title='Project Panic Builder', sub='Compila Panic Park para tus dispositivos',
            s1='1  ROM', s1h='Elige tu archivo panicprk (.zip o .7z)', browse='Examinar…',
            s2='2  Dispositivos', s2h='Marca los dispositivos (en gris: instala primero la herramienta indicada)', s3='3  Compilar', go='COMPILAR', stop='Detener',
            log='Ver detalles', hidelog='Ocultar detalles', ready='Listo', missing='falta: ',
            ok='listo', st_wait='en espera', st_run='en curso…', st_ok='hecho', st_err='error', st_skip='omitido',
            p_rom='Extraer las ROM', p_elf='Encontrar el programa del juego', p_gen='Generar el código C++',
            no_rom='Primero elige el archivo de las ROM.', no_plat='Marca al menos un dispositivo.',
            bad_rom='El archivo no contiene las ROM de Panic Park (pnp2vera.ic1 / pnp2vera.ic2).',
            no7z='No se puede abrir el archivo .7z: instala 7-Zip (7-zip.org, o  sudo apt install p7zip-full)',
            done='¡Terminado! Los archivos están en la carpeta dist.', failed='Algo salió mal: abre «Ver detalles».',
            stopped='Detenido.', open='Abrir la carpeta dist', a_setup='la primera vez descarga Java y el SDK de Android (~3 GB)', a_studio='usa Android Studio (su Java y su SDK)', a_add='añade las partes que faltan del SDK', a_ready='SDK ya instalado',
            n3ds='necesita la carpeta assets creada por la versión de PC: compila también Windows o Linux y abre el juego una vez',
            confirm_stop='¿Detener la compilación?', reuse='ya extraídas: uso panicprkrom/', no_file='archivo no encontrado', recheck='Comprobar de nuevo', w_dl='la primera vez descarga el compilador (LLVM-MinGW, ~150 MB)',
            no_elftools='falta pyelftools:  sudo apt install python3-pyelftools',
            dkp_inst='Instalar los paquetes de devkitPro que faltan', dkp_busy='instalando los paquetes de devkitPro…', dkp_fail='La instalación falló: abre «Ver detalles».'),
 'pt': dict(title='Project Panic Builder', sub='Compile Panic Park para os seus dispositivos',
            s1='1  ROM', s1h='Escolha o seu arquivo panicprk (.zip ou .7z)', browse='Procurar…',
            s2='2  Dispositivos', s2h='Marque os dispositivos (em cinza: instale antes a ferramenta indicada)', s3='3  Compilar', go='COMPILAR', stop='Parar',
            log='Mostrar detalhes', hidelog='Ocultar detalhes', ready='Pronto', missing='falta: ',
            ok='pronto', st_wait='aguardando', st_run='em andamento…', st_ok='feito', st_err='erro', st_skip='ignorado',
            p_rom='Extrair as ROMs', p_elf='Encontrar o programa do jogo', p_gen='Gerar o código C++',
            no_rom='Escolha primeiro o arquivo das ROMs.', no_plat='Marque pelo menos um dispositivo.',
            bad_rom='O arquivo não contém as ROMs de Panic Park (pnp2vera.ic1 / pnp2vera.ic2).',
            no7z='Não foi possível abrir o arquivo .7z: instale o 7-Zip (7-zip.org, ou  sudo apt install p7zip-full)',
            done='Pronto! Os arquivos estão na pasta dist.', failed='Algo deu errado: abra "Mostrar detalhes".',
            stopped='Parado.', open='Abrir a pasta dist', a_setup='na primeira vez baixa o Java e o SDK do Android (~3 GB)', a_studio='usa o Android Studio (o Java e o SDK dele)', a_add='adiciona as partes que faltam do SDK', a_ready='SDK já instalado',
            n3ds='precisa da pasta assets criada pela versão de PC: compile também Windows ou Linux e abra o jogo uma vez',
            confirm_stop='Parar a compilação?', reuse='já extraídas: usando panicprkrom/', no_file='arquivo não encontrado', recheck='Verificar de novo', w_dl='na primeira vez baixa o compilador (LLVM-MinGW, ~150 MB)',
            no_elftools='falta o pyelftools:  sudo apt install python3-pyelftools',
            dkp_inst='Instalar os pacotes do devkitPro que faltam', dkp_busy='instalando os pacotes do devkitPro…', dkp_fail='A instalação falhou: abra "Mostrar detalhes".'),
 'ru': dict(title='Project Panic Builder', sub='Сборка Panic Park для ваших устройств',
            s1='1  ROM', s1h='Выберите архив panicprk (.zip или .7z)', browse='Обзор…',
            s2='2  Устройства', s2h='Отметьте устройства (серые: сначала установите указанный инструмент)', s3='3  Сборка', go='СОБРАТЬ', stop='Стоп',
            log='Показать подробности', hidelog='Скрыть подробности', ready='Готово', missing='нет: ',
            ok='готово', st_wait='ожидание', st_run='выполняется…', st_ok='готово', st_err='ошибка', st_skip='пропущено',
            p_rom='Распаковать ROM', p_elf='Найти программу игры', p_gen='Создать код C++',
            no_rom='Сначала выберите архив с ROM.', no_plat='Отметьте хотя бы одно устройство.',
            bad_rom='В архиве нет ROM Panic Park (pnp2vera.ic1 / pnp2vera.ic2).',
            no7z='Не удалось открыть архив .7z: установите 7-Zip (7-zip.org или  sudo apt install p7zip-full)',
            done='Готово! Файлы в папке dist.', failed='Что-то пошло не так: откройте «Показать подробности».',
            stopped='Остановлено.', open='Открыть папку dist', a_setup='в первый раз скачивает Java и Android SDK (~3 ГБ)', a_studio='использует Android Studio (его Java и SDK)', a_add='докачивает недостающие части SDK', a_ready='SDK уже установлен',
            n3ds='нужна папка assets, которую создаёт версия для ПК: соберите также Windows или Linux и запустите игру один раз',
            confirm_stop='Остановить сборку?', reuse='уже распакованы: используется panicprkrom/', no_file='файл не найден', recheck='Проверить снова', w_dl='в первый раз скачивает компилятор (LLVM-MinGW, ~150 МБ)',
            no_elftools='нет pyelftools:  sudo apt install python3-pyelftools',
            dkp_inst='Установить недостающие пакеты devkitPro', dkp_busy='устанавливаю пакеты devkitPro…', dkp_fail='Установка не удалась: откройте «Показать подробности».'),
 'ja': dict(title='Project Panic Builder', sub='Panic Park をお使いの機器向けにビルド',
            s1='1  ROM セット', s1h='panicprk のアーカイブ (.zip または .7z) を選んでください', browse='参照…',
            s2='2  機器', s2h='ビルドする機器にチェック (灰色: 先に表示されたツールをインストール)', s3='3  ビルド', go='ビルド', stop='停止',
            log='詳細を表示', hidelog='詳細を隠す', ready='準備完了', missing='不足: ',
            ok='準備完了', st_wait='待機中', st_run='実行中…', st_ok='完了', st_err='失敗', st_skip='スキップ',
            p_rom='ROM セットを展開', p_elf='ゲームのプログラムを探す', p_gen='C++ コードを生成',
            no_rom='先に ROM のアーカイブを選んでください。', no_plat='機器を1つ以上チェックしてください。',
            bad_rom='アーカイブに Panic Park の ROM (pnp2vera.ic1 / pnp2vera.ic2) がありません。',
            no7z='.7z アーカイブを開けません: 7-Zip をインストールしてください (7-zip.org、または  sudo apt install p7zip-full)',
            done='完了! ファイルは dist フォルダーにあります。', failed='問題が発生しました: 「詳細を表示」を開いてください。',
            stopped='停止しました。', open='dist フォルダーを開く', a_setup='初回は Java と Android SDK をダウンロード (~3 GB)', a_studio='Android Studio (その Java と SDK) を使用', a_add='足りない SDK の部品を追加', a_ready='SDK はインストール済み',
            n3ds='PC 版が作る assets フォルダーが必要: Windows か Linux もビルドして、ゲームを一度起動してください',
            confirm_stop='ビルドを停止しますか?', reuse='展開済み: panicprkrom/ を使用', no_file='ファイルが見つかりません', recheck='再確認', w_dl='初回はコンパイラーをダウンロード (LLVM-MinGW, ~150 MB)',
            no_elftools='pyelftools がありません:  sudo apt install python3-pyelftools',
            dkp_inst='足りない devkitPro パッケージをインストール', dkp_busy='devkitPro パッケージをインストール中…', dkp_fail='インストールに失敗しました: 「詳細を表示」を開いてください。'),
 'zh': dict(title='Project Panic Builder', sub='为你的设备编译 Panic Park',
            s1='1  ROM', s1h='选择你的 panicprk 压缩包 (.zip 或 .7z)', browse='浏览…',
            s2='2  设备', s2h='勾选要编译的设备 (灰色: 请先安装提示的工具)', s3='3  编译', go='编译', stop='停止',
            log='显示详情', hidelog='隐藏详情', ready='就绪', missing='缺少: ',
            ok='就绪', st_wait='等待中', st_run='进行中…', st_ok='完成', st_err='失败', st_skip='已跳过',
            p_rom='解压 ROM', p_elf='查找游戏程序', p_gen='生成 C++ 代码',
            no_rom='请先选择 ROM 压缩包。', no_plat='请至少勾选一个设备。',
            bad_rom='压缩包里没有 Panic Park 的 ROM (pnp2vera.ic1 / pnp2vera.ic2)。',
            no7z='无法打开 .7z 压缩包: 请安装 7-Zip (7-zip.org，或  sudo apt install p7zip-full)',
            done='完成! 文件在 dist 文件夹中。', failed='出了点问题: 请打开“显示详情”。',
            stopped='已停止。', open='打开 dist 文件夹', a_setup='首次会下载 Java 和 Android SDK (~3 GB)', a_studio='使用 Android Studio (它的 Java 和 SDK)', a_add='补充缺少的 SDK 组件', a_ready='SDK 已安装',
            n3ds='需要 PC 版生成的 assets 文件夹: 请同时编译 Windows 或 Linux 并运行一次游戏',
            confirm_stop='停止编译?', reuse='已解压: 使用 panicprkrom/', no_file='找不到文件', recheck='重新检查', w_dl='首次会下载编译器 (LLVM-MinGW, ~150 MB)',
            no_elftools='缺少 pyelftools:  sudo apt install python3-pyelftools',
            dkp_inst='安装缺少的 devkitPro 软件包', dkp_busy='正在安装 devkitPro 软件包…', dkp_fail='安装失败: 请打开“显示详情”。'),
}
LANGS = [('en', 'English'), ('it', 'Italiano'), ('de', 'Deutsch'), ('fr', 'Français'), ('es', 'Español'),
         ('pt', 'Português'), ('ru', 'Русский'), ('ja', '日本語'), ('zh', '简体中文')]

def have(cmd): return shutil.which(cmd) is not None

# Windows primary language id -> language of the window (LANG_GERMAN 0x07, LANG_FRENCH 0x0c, ...)
WIN_LANGS = {0x10: 'it', 0x07: 'de', 0x0c: 'fr', 0x0a: 'es', 0x16: 'pt', 0x19: 'ru', 0x11: 'ja', 0x04: 'zh'}

def lang_of(name):
    """'de_DE.UTF-8', 'pt-BR', 'zh-CN' ... -> one of the window's languages ('en' if not translated)."""
    c = name.lower().replace('-', '_')[:2]
    return c if c in TXT and not name.lower().startswith(('zh_tw', 'zh_hk', 'zh_mo', 'zh-tw', 'zh-hk', 'zh-mo', 'zh_hant', 'zh-hant')) else 'en'

def system_lang():
    """The language of the system (Windows display language, or the Linux locale) if the window has it, else 'en'."""
    try:
        if WIN:
            import ctypes
            lid = ctypes.windll.kernel32.GetUserDefaultUILanguage()
            if lid & 0x3ff == 0x04 and lid not in (0x0804, 0x1004): return 'en'      # traditional Chinese
            return WIN_LANGS.get(lid & 0x3ff, 'en')
        try:                                                  # WSL: the language of Windows itself
            if 'microsoft' in open('/proc/version').read().lower() and have('powershell.exe'):
                r = subprocess.run(['powershell.exe', '-NoProfile', '-Command', '(Get-UICulture).Name'],
                                   capture_output=True, text=True, timeout=8)
                if r.returncode == 0 and r.stdout.strip(): return lang_of(r.stdout.strip())
        except Exception: pass
        for k in ('LC_ALL', 'LC_MESSAGES', 'LANGUAGE', 'LANG'):
            v = os.environ.get(k, '')
            if v and v not in ('C', 'POSIX') and not v.startswith('C.'): return lang_of(v.split(':')[0])
    except Exception: pass
    return 'en'

def win_cxx():
    """Native Windows C++ compiler: LLVM-MinGW downloaded into tools/, else a MinGW g++ (e.g. MSYS2) in PATH."""
    for d in sorted(glob.glob(os.path.join(ROOT, 'tools', 'llvm-mingw-*', 'bin')), reverse=True):
        if os.path.isfile(os.path.join(d, 'clang++.exe')): return os.path.join(d, 'clang++.exe')
    for c in ('x86_64-w64-mingw32-g++', 'g++'):
        if have(c): return shutil.which(c)
    return None

def android_note(t):
    """Says whether Android Studio (its Java and SDK) will be used, or what the first build downloads."""
    try:
        sys.path.insert(0, os.path.join(ROOT, 'tools')); import build_android as ba
        st = ba.studio_dirs(); miss = ba.missing_packages()
        if st: return t['a_studio'] + ('' if not miss else ' + ' + t['a_add'])
        if not miss and ba.find_java_home(): return t['a_ready']
    except Exception: pass
    return t['a_setup']

def find_7z():
    if have('7z'): return shutil.which('7z')
    for d in (os.environ.get('ProgramFiles', ''), os.environ.get('ProgramFiles(x86)', '')):
        if d and os.path.isfile(os.path.join(d, '7-Zip', '7z.exe')): return os.path.join(d, '7-Zip', '7z.exe')
    return None

def msys_path(p):                    # C:\a\b (or WSL /mnt/c/a/b) -> /c/a/b (for the devkitPro bash)
    p = os.path.abspath(p).replace('\\', '/')
    if re.match(r'^[A-Za-z]:', p): return '/' + p[0].lower() + p[2:]
    m = re.match(r'^/mnt/([a-z])(/.*)?$', p)
    return '/' + m.group(1) + (m.group(2) or '') if m else p

def dkp_check(files, pkgs):
    """None if devkitPro has the files, otherwise what to install (missing packages, or devkitPro itself)."""
    if not os.path.isdir(DKP) or not any(os.path.isdir(os.path.join(DKP, d)) for d in ('devkitARM', 'devkitA64', 'devkitPPC')):
        return 'devkitPro (devkitpro.org/wiki/Getting_Started) + ' + ' '.join(pkgs)
    if all(os.path.isfile(os.path.join(DKP, f)) for f in files) and (DKP_WIN or not WIN): return None
    if DKP_WIN: return 'devkitPro: ' + ' '.join(pkgs)
    return 'sudo dkp-pacman -S ' + ' '.join(pkgs)
DKP_PKGS = {'switch': ['switch-dev', 'switch-sdl2'], 'wiiu': ['wiiu-dev', 'wiiu-sdl2'], '3ds': ['3ds-dev']}

# device: (label, build script, check -> missing-tool hint or None, extra note)
def chk_windows():
    if WIN: return None                                   # the compiler is downloaded by the first build if missing
    return None if have('x86_64-w64-mingw32-g++-posix') or have('x86_64-w64-mingw32-g++') else 'sudo apt install g++-mingw-w64-x86-64-posix'
def chk_linux():
    if WIN: return 'Linux / WSL'
    return None if have('g++') and have('sdl2-config') else 'sudo apt install g++ libsdl2-dev'
def chk_android():
    return None                                           # tools/build_android.py downloads Java and the SDK itself
def chk_switch(): return dkp_check(['libnx/switch_rules', 'portlibs/switch/lib/libSDL2.a'], DKP_PKGS['switch'])
def chk_wiiu(): return dkp_check(['wut/share/wut_rules', 'portlibs/wiiu/lib/libSDL2.a'], DKP_PKGS['wiiu'])
def chk_3ds(): return dkp_check(['devkitARM/3ds_rules', 'libctru/lib/libcitro3d.a', 'libctru/lib/libcitro2d.a'], DKP_PKGS['3ds'])

DEVICES = [  # key, name, script, check
    ('windows', 'Windows', 'build_windows.sh', chk_windows),
    ('linux', 'Linux', 'build_linux.sh', chk_linux),
    ('android', 'Android', 'build_android.sh', chk_android),
    ('switch', 'Nintendo Switch', 'build_switch.sh', chk_switch),
    ('wiiu', 'Wii U', 'build_wiiu.sh', chk_wiiu),
    ('3ds', 'New 3DS', 'build_3ds.sh', chk_3ds),
]

# colours (Panic Park: deep blue sea, yellow/orange letters, red accents)
BG, PANEL, INK, MUTED = '#0d2a5c', '#163c7a', '#ffffff', '#b8c8e8'
YEL, ORA, RED, GRN = '#ffd23f', '#ff8c1a', '#e8322b', '#3ccf6e'

# ------------------------------------------------------------------------------------------------ build steps
class Cancelled(Exception): pass

# Windows: the tools run without a console window of their own (started from pythonw, or by double-clicking the .py, every
# compiler process would open one)
NOWIN = dict(creationflags=0x08000000) if WIN else {}                   # CREATE_NO_WINDOW
NEWGROUP = dict(creationflags=subprocess.CREATE_NEW_PROCESS_GROUP | 0x08000000) if WIN else dict(start_new_session=True)
def kill_tree(p):
    try:
        if WIN: subprocess.run(['taskkill', '/T', '/F', '/PID', str(p.pid)], capture_output=True, **NOWIN)
        else: os.killpg(p.pid, signal.SIGTERM)
    except Exception: pass

class Worker(threading.Thread):
    """Runs the steps and reports to the window through a queue: ('log', text) ('step', i, state) ('pct', i, 0..1)."""
    def __init__(self, q, archive, devices, t):
        super().__init__(daemon=True); self.q, self.archive, self.devices, self.t = q, archive, devices, t
        self.proc = None; self.cancel = False; self.last = ''; self.procs = set(); self.plock = threading.Lock()

    def log(self, s): self.q.put(('log', s))

    def run_cmd(self, args, step, total=None, pattern=None, env=None):
        """Runs a command; with `total`, every output line matching `pattern` advances the step's progress."""
        if self.cancel: raise Cancelled()
        self.log('$ ' + ' '.join(args))
        e = dict(os.environ); e.update(env or {})
        self.proc = subprocess.Popen(args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                     errors='replace', env=e, **NEWGROUP)
        n = 0
        for line in self.proc.stdout:
            self.log(line.rstrip('\n'))
            if line.strip(): self.last = line.strip()
            if total and pattern and pattern.search(line):
                n += 1; self.q.put(('pct', step, min(0.97, n / total)))
            elif total is None and pattern is not None:          # tools that print their own percentage (7-Zip)
                m = pattern.search(line)
                if m: self.q.put(('pct', step, int(m.group(1)) / 100))
        rc = self.proc.wait(); self.proc = None
        if self.cancel: raise Cancelled()
        return rc

    def stop(self):
        self.cancel = True
        for p in [self.proc] + list(self.procs):
            if p: kill_tree(p)

    def pip(self, step, module, package):
        """Makes sure a Python module is there, installing it for this user with pip if needed."""
        test = lambda: subprocess.run([sys.executable, '-c', 'import ' + module], capture_output=True, **NOWIN).returncode == 0
        if test(): return True
        # recent Ubuntu refuses pip without --break-system-packages (with --user it only touches this user's packages)
        for extra in ([], ['--break-system-packages']):
            if self.run_cmd([sys.executable, '-m', 'pip', 'install', '--user', package] + extra, step) == 0: break
        return test()

    def download(self, url, dest, step, lo, hi):
        self.log('download ' + url)
        req = urllib.request.Request(url, headers={'User-Agent': 'ProjectPanicBuilder'})
        with urllib.request.urlopen(req, timeout=60) as r, open(dest + '.part', 'wb') as f:
            size = int(r.headers.get('Content-Length') or 0); got = 0
            while True:
                if self.cancel: raise Cancelled()
                b = r.read(1 << 20)
                if not b: break
                f.write(b); got += len(b)
                if size: self.q.put(('pct', step, lo + (hi - lo) * got / size))
        os.replace(dest + '.part', dest)

    # ---------------------------------------------------------------- Windows build without WSL (same steps as build_windows.sh)
    def get_compiler(self, step):
        cxx = win_cxx()
        if cxx: return cxx
        url = None
        try:                                                 # newest LLVM-MinGW (msvcrt, like the SDL2 MinGW libraries)
            req = urllib.request.Request('https://api.github.com/repos/mstorsjo/llvm-mingw/releases/latest',
                                         headers={'User-Agent': 'ProjectPanicBuilder'})
            rel = json.load(urllib.request.urlopen(req, timeout=30))
            url = next(a['browser_download_url'] for a in rel['assets'] if re.fullmatch(r'llvm-mingw-\w+-msvcrt-x86_64\.zip', a['name']))
        except Exception as ex: self.log(f'latest LLVM-MinGW not found ({ex}), using {LLVM_MINGW_PIN}')
        url = url or (f'https://github.com/mstorsjo/llvm-mingw/releases/download/{LLVM_MINGW_PIN}/'
                      f'llvm-mingw-{LLVM_MINGW_PIN}-msvcrt-x86_64.zip')
        z = os.path.join(ROOT, 'tools', 'llvm-mingw.zip')
        self.download(url, z, step, 0.0, 0.25)
        self.log('extracting the compiler...')
        with zipfile.ZipFile(z) as f: f.extractall(os.path.join(ROOT, 'tools'))
        os.remove(z)
        cxx = win_cxx()
        if not cxx: raise RuntimeError('compiler not found after the download')
        return cxx

    def get_sdl(self, step):
        d = os.path.join(ROOT, 'deps', f'SDL2-{SDLV}', 'x86_64-w64-mingw32')
        if not os.path.isfile(os.path.join(d, 'lib', 'libSDL2.a')):
            os.makedirs(os.path.join(ROOT, 'deps'), exist_ok=True)
            tgz = os.path.join(ROOT, 'deps', 'sdl2-mingw.tar.gz')
            self.download(f'https://github.com/libsdl-org/SDL/releases/download/release-{SDLV}/SDL2-devel-{SDLV}-mingw.tar.gz',
                          tgz, step, 0.25, 0.3)
            with tarfile.open(tgz) as t:
                try: t.extractall(os.path.join(ROOT, 'deps'), filter='data')
                except TypeError: t.extractall(os.path.join(ROOT, 'deps'))
        return d

    def compile(self, args):
        if self.cancel: return 1, ''
        p = subprocess.Popen(args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace', **NEWGROUP)
        with self.plock: self.procs.add(p)
        out = p.communicate()[0]
        with self.plock: self.procs.discard(p)
        return p.returncode, out

    def build_windows_native(self, step):
        cxx = self.get_compiler(step); sdl = self.get_sdl(step)
        clang = 'clang' in os.path.basename(cxx).lower()
        self.log('compiler: ' + cxx)
        bd = os.path.join(ROOT, 'build_win'); out = os.path.join(ROOT, 'dist', 'windows')
        os.makedirs(bd, exist_ok=True); os.makedirs(out, exist_ok=True)
        srcs = sorted(glob.glob(os.path.join(ROOT, 'gen_full', 'gen_*.cpp'))) + [os.path.join(ROOT, 'gen_full', 'dispatch.cpp')]
        obj = lambda s: os.path.join(bd, os.path.splitext(os.path.basename(s))[0] + '.o')
        rt = os.path.getmtime(os.path.join(ROOT, 'runtime.h'))
        todo = [s for s in srcs if not (os.path.isfile(obj(s)) and os.path.getmtime(obj(s)) > max(os.path.getmtime(s), rt))]
        flags = ['-std=c++17', os.environ.get('OPT', '-O2'), '-w', '-I.', '-Igen_full']
        total = len(srcs) + 2; done = len(srcs) - len(todo); err = None
        with ThreadPoolExecutor(max(1, os.cpu_count() or 2)) as ex:
            futs = {ex.submit(self.compile, [cxx] + flags + ['-c', s, '-o', obj(s)]): s for s in todo}
            for f in as_completed(futs):
                rc, o = f.result(); s = os.path.relpath(futs[f], ROOT)
                self.log('  ' + s + ('' if not o.strip() else '\n' + o.rstrip()))
                if rc and not err: err = s + ': ' + next((l for l in o.splitlines() if 'error' in l), o.strip()[:200])
                done += 1; self.q.put(('pct', step, 0.3 + 0.65 * done / total))
        if self.cancel: raise Cancelled()
        if err: self.last = err; return False
        # every object must be there: an antivirus can remove a freshly written .o (then the link misses its functions)
        for s in srcs:
            if not (os.path.isfile(obj(s)) and os.path.getsize(obj(s)) > 0):
                self.log('  ' + os.path.relpath(obj(s), ROOT) + ' missing: compiling it again')
                rc, o = self.compile([cxx] + flags + ['-c', s, '-o', obj(s)])
                if rc or not os.path.isfile(obj(s)) or os.path.getsize(obj(s)) == 0:
                    self.last = os.path.relpath(obj(s), ROOT) + ' was not created (antivirus? add the project folder to its exclusions) ' + o.strip()[:200]
                    return False
        sdl_inc = os.path.join(sdl, 'include', 'SDL2'); sdl_lib = os.path.join(sdl, 'lib')
        rc, o = self.compile([cxx, '-std=c++17', '-O2', '-w', '-I.', '-Igen_full', '-DUSE_SDL', '-I' + sdl_inc,
                              '-c', 'platform/platform_stub.cpp', '-o', os.path.join(bd, 'platform_stub.o')])
        self.log('  platform/platform_stub.cpp' + ('\n' + o.rstrip() if o.strip() else ''))
        if rc: self.last = next((l for l in o.splitlines() if 'error' in l), 'platform_stub.cpp'); return False
        self.q.put(('pct', step, 0.97))
        res = os.path.join(bd, 'icon_res.o')                 # executable icon (build_icons/icon.ico)
        if os.path.isfile(res): os.remove(res)
        ico = os.path.join(ROOT, 'build_icons', 'icon.ico')
        wr = next((w for w in (os.path.join(os.path.dirname(cxx), n) for n in
                   ('x86_64-w64-mingw32-windres.exe', 'llvm-windres.exe', 'windres.exe', 'x86_64-w64-mingw32-windres', 'windres'))
                   if os.path.isfile(w)), None) or shutil.which('windres') or shutil.which('x86_64-w64-mingw32-windres')
        if os.path.isfile(ico) and wr:
            open(os.path.join(bd, 'icon.rc'), 'w').write('1 ICON "build_icons/icon.ico"\n')
            rc, o = self.compile([wr, os.path.join(bd, 'icon.rc'), '-O', 'coff', '-o', res])
            self.log('icon: ' + ('ok' if rc == 0 else o.strip()))
            if rc and os.path.isfile(res): os.remove(res)
        objs = [obj(s) for s in srcs] + [os.path.join(bd, 'platform_stub.o')] + ([res] if os.path.isfile(res) else [])
        link = [cxx] + objs + ['-o', os.path.join(out, 'PanicPark.exe'), '-mconsole', '-static'] + \
               ([] if clang else ['-static-libgcc', '-static-libstdc++']) + \
               ['-L' + sdl_lib, '-Wl,-Bstatic', '-lSDL2', '-lwinpthread', '-lsetupapi', '-lwinmm', '-limm32', '-lversion',
                '-lole32', '-loleaut32', '-lgdi32', '-luser32', '-lshell32', '-lcfgmgr32', '-luuid', '-ladvapi32']
        self.log('linking PanicPark.exe...')
        rc, o = self.compile(link)
        if o.strip(): self.log(o.rstrip())
        if rc: self.last = next((l for l in o.splitlines() if 'error' in l or 'undefined' in l), 'link failed'); return False
        # the same extra files as build_windows.sh (texts taken from the script, so they stay in one place)
        for sub in ('lang', 'ui'):
            files = glob.glob(os.path.join(ROOT, 'platform', sub, '*.json' if sub == 'lang' else '*.png'))
            if files: os.makedirs(os.path.join(out, sub), exist_ok=True)
            for f in files: shutil.copy(f, os.path.join(out, sub))
        sh = open(os.path.join(ROOT, 'platform', 'build_windows.sh'), encoding='utf-8').read()
        old = os.path.join(out, 'start.bat')                     # no longer needed: PanicPark.exe finds the files itself
        if os.path.isfile(old): os.remove(old)
        for name in ('README.txt',):
            m = re.search(r"cat > \$OUT/" + re.escape(name) + r" <<'EOF'\n(.*?)\nEOF", sh, re.S)
            if m: open(os.path.join(out, name), 'w', newline='\r\n').write(m.group(1) + '\n')
        self.log('BUILD_OK -> ' + os.path.join(out, 'PanicPark.exe'))
        return True

    # step 1: archive -> panicprkrom/
    def extract(self, step):
        dst = os.path.join(ROOT, 'panicprkrom')
        if self.archive == '@reuse':
            self.log(self.t['reuse']); return
        tmp = os.path.join(ROOT, '.rom_extract'); shutil.rmtree(tmp, ignore_errors=True); os.makedirs(tmp)
        if self.archive.lower().endswith('.zip'):
            with zipfile.ZipFile(self.archive) as z:
                names = z.namelist()
                for i, n in enumerate(names):
                    if self.cancel: raise Cancelled()
                    z.extract(n, tmp); self.q.put(('pct', step, (i + 1) / len(names)))
        else:
            z7 = find_7z()
            if z7:
                if self.run_cmd([z7, 'x', '-y', '-bsp1', '-o' + tmp, self.archive], step, None, re.compile(r'(\d+)%')) != 0:
                    raise RuntimeError('7z')
            else:                                           # no 7-Zip installed: the py7zr Python module does it
                self.pip(step, 'py7zr', 'py7zr')
                self.log('py7zr: ' + self.archive)
                r = subprocess.run([sys.executable, '-c', 'import py7zr,sys; py7zr.SevenZipFile(sys.argv[1]).extractall(sys.argv[2])',
                                    self.archive, tmp], capture_output=True, text=True, **NOWIN)
                if r.returncode: self.log(r.stdout + r.stderr); raise RuntimeError(self.t['no7z'])
        src = None                                          # the folder that holds the ROM files (maybe a subfolder)
        for d, _, files in os.walk(tmp):
            if all(k in files for k in ROM_KEY): src = d; break
        if not src:
            for d, _, files in os.walk(tmp):                # a MAME set zipped inside the archive
                for f in files:
                    if f.lower().endswith('.zip') and zipfile.is_zipfile(os.path.join(d, f)):
                        with zipfile.ZipFile(os.path.join(d, f)) as z:
                            if all(k in z.namelist() for k in ROM_KEY): z.extractall(os.path.join(tmp, '_inner')); src = os.path.join(tmp, '_inner')
        if not src: shutil.rmtree(tmp, ignore_errors=True); raise RuntimeError(self.t['bad_rom'])
        os.makedirs(dst, exist_ok=True)
        for f in os.listdir(src):
            if os.path.isfile(os.path.join(src, f)): shutil.move(os.path.join(src, f), os.path.join(dst, f))
        shutil.rmtree(tmp, ignore_errors=True)
        self.log(f'ROMs -> {dst} ({len(os.listdir(dst))} files)')

    def run(self):
        steps = ['rom', 'elf', 'gen'] + self.devices
        failed = False
        try:
            for i, key in enumerate(steps):
                if self.cancel: raise Cancelled()
                self.q.put(('step', i, 'run'))
                ok = True
                try:
                    if key == 'rom': self.extract(i)
                    elif key == 'elf':
                        ok = self.run_cmd([sys.executable, 'tools/extract_rom.py', 'panicprkrom', '.'], i) == 0
                    elif key == 'gen':
                        if not self.pip(i, 'elftools', 'pyelftools'): raise RuntimeError(self.t['no_elftools'])
                        ok = self.run_cmd([sys.executable, 'mips2cpp.py', '--elf', 'panicprk_main.elf',
                                           '--funcs', 'functions.csv', '--out', 'gen_full'], i) == 0
                        if ok:
                            # app icons for every device from platform/ui/icon.png, or the repository's icon (needs Pillow)
                            if self.pip(i, 'PIL', 'pillow'): self.run_cmd([sys.executable, 'tools/make_icons.py'], i)
                    else:
                        script = next(d[2] for d in DEVICES if d[0] == key)
                        nsrc = len([f for f in os.listdir(os.path.join(ROOT, 'gen_full')) if f.endswith('.cpp')]) + 1
                        pat = re.compile(r'(gen_\d+|dispatch|platform_stub|switch_sys)\.cpp')
                        if key == 'android':                 # same Python script on Windows and Linux
                            ok = self.run_cmd([sys.executable, 'tools/build_android.py'], i, nsrc, pat) == 0
                        elif ok and WIN and key == 'windows':
                            ok = self.build_windows_native(i)
                        elif ok and DKP_WIN and key in DKP_PKGS:   # Switch / Wii U / 3DS: the scripts, with devkitPro's bash
                            ok = self.run_cmd([DKP_BASH, '-lc', f'cd "{msys_path(ROOT)}" && sh platform/{script}'], i, nsrc, pat,
                                              env={'MSYSTEM': 'MSYS', 'CHERE_INVOKING': '1'}) == 0
                        elif ok:
                            ok = self.run_cmd(['bash', 'platform/' + script], i, nsrc, pat) == 0
                except Cancelled: raise
                except Exception as ex:
                    self.log(f'ERROR: {ex}'); ok = False; self.last = str(ex)
                    if key in ('rom', 'elf', 'gen'):
                        self.q.put(('step', i, 'err')); self.q.put(('msg', str(ex))); failed = True
                        for j in range(i + 1, len(steps)): self.q.put(('step', j, 'skip'))
                        break
                if not ok: self.q.put(('why', i, self.last))
                self.q.put(('step', i, 'ok' if ok else 'err'))
                if not ok:
                    failed = True
                    if key in ('rom', 'elf', 'gen'):
                        for j in range(i + 1, len(steps)): self.q.put(('step', j, 'skip'))
                        break
            self.q.put(('end', 'failed' if failed else 'done'))
        except Cancelled:
            self.q.put(('end', 'stopped'))

# ------------------------------------------------------------------------------------------------ window
class App:
    def __init__(self, root):
        self.root = root; self.q = queue.Queue(); self.worker = None; self.status = None
        try: self.cfg = json.load(open(CFG))
        except Exception: self.cfg = {}
        self.lang = self.cfg.get('lang') or system_lang()          # the system's language; the menu's choice is remembered
        if self.lang not in TXT: self.lang = 'en'
        root.configure(bg=BG); root.minsize(640, 0)
        st = ttk.Style(root); st.theme_use('clam')
        st.configure('P.Horizontal.TProgressbar', troughcolor='#0a1f45', background=YEL, bordercolor=BG,
                     lightcolor=YEL, darkcolor=ORA, thickness=26)
        st.configure('S.Horizontal.TProgressbar', troughcolor='#0a1f45', background=GRN, bordercolor=PANEL,
                     lightcolor=GRN, darkcolor=GRN, thickness=8)
        try:                                                 # window icon: your platform/ui/icon.png, if you have it
            ic = os.path.join(ROOT, 'platform', 'ui', 'icon.png')
            if not os.path.isfile(ic): ic = os.path.join(ROOT, 'platform', 'icon_default.png')
            self.icon = tk.PhotoImage(file=ic); root.iconphoto(True, self.icon)
        except Exception: pass
        self.build_ui(); self.refresh_texts(); self.poll()
        root.protocol('WM_DELETE_WINDOW', self.on_close)

    @property
    def t(self): return TXT[self.lang]

    # -------- layout
    def lbl(self, parent, size=11, weight='normal', fg=INK, bg=None, **kw):
        return tk.Label(parent, font=(FONT, size, weight), fg=fg, bg=bg or parent['bg'], **kw)

    def button(self, parent, cmd, bg=YEL, fg='#1a1a1a', size=11, **kw):
        return tk.Button(parent, command=cmd, font=(FONT, size, 'bold'), bg=bg, fg=fg, activebackground=ORA,
                         activeforeground='#1a1a1a', relief='flat', bd=0, cursor='hand2', padx=14, pady=6, **kw)

    def panel(self, row):
        f = tk.Frame(self.body, bg=PANEL, padx=16, pady=10); f.grid(row=row, column=0, sticky='ew', pady=(0, 10))
        f.columnconfigure(0, weight=1); return f

    def build_ui(self):
        r = self.root
        top = tk.Frame(r, bg=BG, padx=20, pady=12); top.pack(fill='x')
        tt = tk.Frame(top, bg=BG); tt.pack(side='left')
        self.w_title = self.lbl(tt, 22, 'bold', YEL, anchor='w'); self.w_title.pack(anchor='w')
        self.w_sub = self.lbl(tt, 10, fg=MUTED, anchor='w'); self.w_sub.pack(anchor='w')
        self.w_lang = tk.Menubutton(top, font=(FONT, 10, 'bold'), bg=PANEL, fg=INK, activebackground=ORA, activeforeground='#1a1a1a',
                                    relief='flat', bd=0, cursor='hand2', padx=14, pady=6)
        m = tk.Menu(self.w_lang, tearoff=0, font=(FONT, 10), bg=PANEL, fg=INK, activebackground=ORA, activeforeground='#1a1a1a')
        for code, name in LANGS: m.add_command(label=name, command=lambda c=code: self.set_lang(c))
        self.w_lang['menu'] = m; self.w_lang.pack(side='right', anchor='n')

        self.body = tk.Frame(r, bg=BG, padx=20); self.body.pack(fill='both', expand=True)
        self.body.columnconfigure(0, weight=1)

        # 1: ROM set
        p1 = self.panel(0)
        self.w_s1 = self.lbl(p1, 13, 'bold', YEL, anchor='w'); self.w_s1.grid(row=0, column=0, columnspan=2, sticky='w')
        self.w_s1h = self.lbl(p1, 10, fg=MUTED, anchor='w'); self.w_s1h.grid(row=1, column=0, columnspan=2, sticky='w')
        self.rom = tk.StringVar(value=self.cfg.get('rom', ''))
        self.w_rom = tk.Entry(p1, textvariable=self.rom, font=(FONT, 11), relief='flat', bg='#ffffff', fg='#111')
        self.w_rom.grid(row=2, column=0, sticky='ew', pady=(8, 0), ipady=6)
        self.w_browse = self.button(p1, self.pick_rom); self.w_browse.grid(row=2, column=1, padx=(8, 0), pady=(8, 0))
        self.w_romst = self.lbl(p1, 10, fg=MUTED, anchor='w'); self.w_romst.grid(row=3, column=0, columnspan=2, sticky='w', pady=(4, 0))
        self.rom.trace_add('write', lambda *a: self.check_rom())

        # 2: devices (each row also shows its own progress while building)
        p2 = self.panel(1)
        self.w_s2 = self.lbl(p2, 13, 'bold', YEL, anchor='w'); self.w_s2.grid(row=0, column=0, sticky='w')
        self.w_s2h = self.lbl(p2, 10, fg=MUTED, anchor='w'); self.w_s2h.grid(row=1, column=0, sticky='w')
        self.w_recheck = self.button(p2, self.refresh_texts, bg='#0a1f45', fg=INK, size=9); self.w_recheck.grid(row=0, column=1, rowspan=2, sticky='e')
        grid = tk.Frame(p2, bg=PANEL); grid.grid(row=2, column=0, sticky='ew', pady=(6, 0))
        grid.columnconfigure(1, weight=1)
        self.dev = {}
        sel = self.cfg.get('devices', ['windows'])
        for i, (key, name, _, chk) in enumerate(DEVICES):
            v = tk.BooleanVar(value=key in sel)
            cb = tk.Checkbutton(grid, text=name, variable=v, font=(FONT, 12, 'bold'), fg=INK, bg=PANEL,
                                selectcolor='#0a1f45', activebackground=PANEL, activeforeground=YEL, anchor='w', width=15,
                                cursor='hand2', highlightthickness=0, bd=0, command=self.save_cfg)
            cb.grid(row=i, column=0, sticky='nw', pady=3)
            note = self.lbl(grid, 9, fg=MUTED, anchor='w', justify='left', wraplength=360); note.grid(row=i, column=1, sticky='w', padx=(8, 0))
            pb = ttk.Progressbar(grid, style='S.Horizontal.TProgressbar', maximum=1000, length=200)
            stl = self.lbl(grid, 10, 'bold', fg=MUTED, anchor='e', width=11)
            self.dev[key] = dict(var=v, cb=cb, note=note, pb=pb, st=stl, chk=chk, row=i)
        self.dev_vars = {k: d['var'] for k, d in self.dev.items()}
        # Windows devkitPro without some packages: one click installs them with its pacman
        self.w_dkp = self.button(p2, self.install_dkp, size=9); self.dkp_busy = False

        # 3: build
        p3 = self.panel(2)
        self.w_s3 = self.lbl(p3, 13, 'bold', YEL, anchor='w'); self.w_s3.grid(row=0, column=0, sticky='w')
        bar = tk.Frame(p3, bg=PANEL); bar.grid(row=1, column=0, sticky='ew', pady=(8, 0)); bar.columnconfigure(0, weight=1)
        self.pb = ttk.Progressbar(bar, style='P.Horizontal.TProgressbar', maximum=1000)
        self.pb.grid(row=0, column=0, sticky='ew')
        self.w_pct = self.lbl(bar, 18, 'bold', YEL, width=5); self.w_pct.grid(row=0, column=1, padx=(10, 0))
        self.w_now = self.lbl(p3, 10, fg=INK, anchor='w', justify='left', wraplength=560); self.w_now.grid(row=2, column=0, sticky='w', pady=(6, 0))
        self.prep = tk.Frame(p3, bg=PANEL); self.prep.grid(row=3, column=0, sticky='w', pady=(4, 0))
        self.prep_w = [self.lbl(self.prep, 9, fg=MUTED) for _ in range(3)]
        for i, w in enumerate(self.prep_w): w.grid(row=0, column=i, padx=(0, 16))
        btns = tk.Frame(p3, bg=PANEL); btns.grid(row=4, column=0, sticky='ew', pady=(10, 0))
        self.w_go = self.button(btns, self.start, size=14); self.w_go.pack(side='left')
        self.w_stop = self.button(btns, self.stop, bg=RED, fg=INK); self.w_open = self.button(btns, self.open_dist, bg=GRN)
        self.w_logbtn = self.button(btns, self.toggle_log, bg='#0a1f45', fg=INK, size=10); self.w_logbtn.pack(side='right')

        self.logf = tk.Frame(r, bg=BG, padx=20)
        self.logt = tk.Text(self.logf, height=12, width=60, bg='#07152f', fg='#cfe0ff', insertbackground=INK, relief='flat',
                            font=(MONO, 9), wrap='none')
        sb = tk.Scrollbar(self.logf, command=self.logt.yview); self.logt.configure(yscrollcommand=sb.set)
        self.logt.pack(side='left', fill='both', expand=True); sb.pack(side='right', fill='y')
        self.log_on = False
        tk.Frame(r, bg=BG, height=10).pack(side='bottom')

    def refresh_texts(self):
        t = self.t; self.root.title(t['title'])
        self.w_title['text'] = t['title']; self.w_sub['text'] = t['sub']
        self.w_lang['text'] = dict(LANGS)[self.lang] + '  ▾'
        self.w_s1['text'], self.w_s1h['text'], self.w_browse['text'] = t['s1'], t['s1h'], t['browse']
        self.w_s2['text'], self.w_s2h['text'], self.w_s3['text'] = t['s2'], t['s2h'], t['s3']
        self.w_recheck['text'] = t['recheck']
        self.w_go['text'], self.w_stop['text'], self.w_open['text'] = t['go'], t['stop'], t['open']
        self.w_logbtn['text'] = t['hidelog'] if self.log_on else t['log']
        self.w_now['text'] = t[self.status] if self.status in t else (self.status or t['ready'])
        dkp_missing = []
        for key, d in self.dev.items():
            miss = d['chk'](); note = (t['missing'] + miss) if miss else ('✓ ' + t['ok'])
            if key == 'windows' and WIN and not win_cxx(): note += '  ·  ' + t['w_dl']
            if key == 'android' and not miss: note += '  ·  ' + android_note(t)
            if key == '3ds' and not miss: note += '\n' + t['n3ds']
            d['note'].configure(text=note, fg=(ORA if miss else MUTED))
            if key in DKP_PKGS and miss and DKP_WIN: dkp_missing += DKP_PKGS[key]
            if not self.worker:
                if miss: d['var'].set(False)
                d['cb'].configure(state='disabled' if miss else 'normal')
        self.dkp_missing = dkp_missing
        self.w_dkp['text'] = t['dkp_busy'] if self.dkp_busy else t['dkp_inst']
        if dkp_missing or self.dkp_busy: self.w_dkp.grid(row=3, column=0, columnspan=2, sticky='w', pady=(8, 0))
        else: self.w_dkp.grid_remove()
        self.check_rom()

    def install_dkp(self):
        if self.dkp_busy or self.worker or not self.dkp_missing: return
        self.dkp_busy = True; self.w_dkp.configure(state='disabled'); self.refresh_texts()
        pk = ' '.join(sorted(set(self.dkp_missing)))
        def work():
            try:
                self.q.put(('log', '$ pacman -Sy --needed --noconfirm ' + pk))
                r = subprocess.run([DKP_BASH, '-lc', 'pacman -Sy --needed --noconfirm ' + pk], capture_output=True, text=True,
                                   errors='replace', **NEWGROUP)
                self.q.put(('log', r.stdout + r.stderr)); ok = r.returncode == 0
            except Exception as ex: self.q.put(('log', str(ex))); ok = False
            self.q.put(('dkp', ok))
        threading.Thread(target=work, daemon=True).start()

    def dkp_done(self, ok):
        self.dkp_busy = False; self.w_dkp.configure(state='normal'); self.refresh_texts()
        if not ok: self.status = 'dkp_fail'; self.w_now.configure(text=self.t['dkp_fail'], fg=ORA)

    def set_lang(self, code):
        self.lang = code; self.save_cfg(); self.refresh_texts()
        if getattr(self, 'state', None): [self.set_step(i, s, quiet=True) for i, s in enumerate(self.state) if s != 'wait']

    def toggle_log(self):
        self.log_on = not self.log_on
        if self.log_on: self.logf.pack(fill='both', expand=True, pady=(0, 6))
        else: self.logf.pack_forget()
        self.refresh_texts()

    def check_rom(self):
        p = self.rom.get().strip()
        if p and os.path.isfile(p): self.w_romst.configure(text='✓ ' + os.path.basename(p), fg=GRN)
        elif not p and all(os.path.isfile(os.path.join(ROOT, 'panicprkrom', k)) for k in ROM_KEY):
            self.w_romst.configure(text='✓ ' + self.t['reuse'], fg=GRN)
        elif p: self.w_romst.configure(text='✗ ' + self.t['no_file'], fg=ORA)
        else: self.w_romst.configure(text='', fg=MUTED)

    def pick_rom(self):
        p = filedialog.askopenfilename(title=self.t['s1h'], filetypes=[('ROM set', '*.zip *.7z *.ZIP *.7Z'), ('*', '*')],
                                       initialdir=os.path.dirname(self.rom.get()) or os.path.expanduser('~'))
        if p: self.rom.set(p); self.save_cfg()

    def save_cfg(self):
        self.cfg.update(lang=self.lang, rom=self.rom.get(), devices=[k for k, d in self.dev.items() if d['var'].get()])
        try: json.dump(self.cfg, open(CFG, 'w'))
        except Exception: pass

    def lock(self, on):
        for w in [self.w_rom, self.w_browse, self.w_recheck] + [d['cb'] for d in self.dev.values()]: w.configure(state='disabled' if on else 'normal')
        if not on: self.refresh_texts()

    # -------- running
    def start(self):
        t = self.t; rom = self.rom.get().strip()
        reuse = not rom and all(os.path.isfile(os.path.join(ROOT, 'panicprkrom', k)) for k in ROM_KEY)
        if not reuse and not os.path.isfile(rom): messagebox.showwarning(t['title'], t['no_rom']); return
        devs = [k for k, _, _, _ in DEVICES if self.dev[k]['var'].get()]
        if not devs: messagebox.showwarning(t['title'], t['no_plat']); return
        self.save_cfg()
        self.keys = ['rom', 'elf', 'gen'] + devs
        self.weights = [0.04, 0.01, 0.03] + [0.92 / len(devs)] * len(devs)    # the device builds are the long part
        self.prog = [0.0] * len(self.keys); self.state = ['wait'] * len(self.keys); self.why = []
        for d in self.dev.values(): d['pb'].grid_forget(); d['st'].grid_forget(); d['note'].grid(); d['pb']['value'] = 0
        for k in devs:
            d = self.dev[k]; d['note'].grid_remove()
            d['pb'].grid(row=d['row'], column=1, sticky='ew', padx=(8, 8)); d['st'].grid(row=d['row'], column=2, sticky='e')
        for w in self.prep_w: w['text'] = ''
        self.logt.delete('1.0', 'end'); self.t0 = time.time()
        self.w_go.pack_forget(); self.w_open.pack_forget(); self.w_stop.pack(side='left'); self.lock(True)
        for i in range(len(self.keys)): self.set_step(i, 'wait', quiet=True)
        self.worker = Worker(self.q, '@reuse' if reuse else rom, devs, t); self.worker.start()
        self.update_total()

    def stop(self):
        if self.worker and messagebox.askyesno(self.t['title'], self.t['confirm_stop']): self.worker.stop()

    def on_close(self):
        if self.worker:
            if not messagebox.askyesno(self.t['title'], self.t['confirm_stop']): return
            self.worker.stop()
        self.root.destroy()

    def update_total(self):
        tot = sum(w * (1.0 if s == 'ok' else p) for w, p, s in zip(self.weights, self.prog, self.state))
        self.pb['value'] = tot * 1000; self.w_pct['text'] = f'{int(tot * 100)}%'

    def step_name(self, i):
        t = self.t; return [t['p_rom'], t['p_elf'], t['p_gen']][i] if i < 3 else next(x[1] for x in DEVICES if x[0] == self.keys[i])

    def set_step(self, i, state, quiet=False):
        t = self.t; key = self.keys[i]; self.state[i] = state
        txt = {'wait': t['st_wait'], 'run': t['st_run'], 'ok': '✓ ' + t['st_ok'], 'err': '✗ ' + t['st_err'], 'skip': t['st_skip']}[state]
        col = {'wait': MUTED, 'run': YEL, 'ok': GRN, 'err': RED, 'skip': MUTED}[state]
        if i < 3:
            name = [t['p_rom'], t['p_elf'], t['p_gen']][i]
            sym = {'wait': '○', 'run': '…', 'ok': '✓', 'err': '✗', 'skip': '–'}[state]
            self.prep_w[i].configure(text=f'{sym} {name}', fg=col)
        else:
            d = self.dev[key]; d['st'].configure(text=txt, fg=col)
            if state == 'ok': d['pb']['value'] = 1000
        if state == 'run' and not quiet:
            name = [t['p_rom'], t['p_elf'], t['p_gen']][i] if i < 3 else next(x[1] for x in DEVICES if x[0] == key)
            self.status = name + '…'; self.w_now.configure(text=self.status, fg=INK)
        self.update_total()

    def poll(self):
        try:
            for _ in range(400):
                m = self.q.get_nowait()
                if m[0] == 'log':
                    self.logt.insert('end', m[1] + '\n')
                    if int(self.logt.index('end-1c').split('.')[0]) > 5000: self.logt.delete('1.0', '1000.0')
                    self.logt.see('end')
                elif m[0] == 'pct':
                    i, v = m[1], m[2]; self.prog[i] = max(self.prog[i], v)
                    if i >= 3: self.dev[self.keys[i]]['pb']['value'] = self.prog[i] * 1000
                    self.update_total()
                elif m[0] == 'step': self.set_step(m[1], m[2])
                elif m[0] == 'dkp': self.dkp_done(m[1])
                elif m[0] == 'why': self.why.append(self.step_name(m[1]) + ': ' + m[2])
                elif m[0] == 'msg': messagebox.showerror(self.t['title'], m[1])
                elif m[0] == 'end': self.finish(m[1])
        except queue.Empty: pass
        self.root.after(80, self.poll)

    def finish(self, how):
        t = self.t; self.worker = None; self.lock(False)
        self.w_stop.pack_forget(); self.w_go.pack(side='left')
        el = int(time.time() - self.t0); dur = f'  ({el // 60}:{el % 60:02d})'
        if how == 'done':
            self.pb['value'] = 1000; self.w_pct['text'] = '100%'
            self.status = t['done'] + dur; self.w_now.configure(text=self.status, fg=GRN)
            self.w_open.pack(side='left', padx=(8, 0))
        elif how == 'failed':
            self.status = t['failed'] + dur + ''.join('\n• ' + w[:160] for w in self.why); self.w_now.configure(text=self.status, fg=ORA)
            if any(s == 'ok' for s in self.state[3:]): self.w_open.pack(side='left', padx=(8, 0))
            if not self.log_on: self.toggle_log()
        else: self.status = t['stopped']; self.w_now.configure(text=self.status, fg=ORA)

    def open_dist(self):
        d = os.path.join(ROOT, 'dist')
        if WIN: os.startfile(d); return
        if have('explorer.exe') and have('wslpath'):                     # WSL: open it in the Windows file manager
            w = subprocess.run(['wslpath', '-w', d], capture_output=True, text=True).stdout.strip()
            subprocess.Popen(['explorer.exe', w])
        elif have('xdg-open'): subprocess.Popen(['xdg-open', d])

def main():
    try:
        root = tk.Tk()
    except tk.TclError as ex:                          # e.g. Linux without a display
        print(f'Cannot open the window: {ex}'); sys.exit(1)
    try:
        App(root)
        root.mainloop()
    except Exception:                                  # started with a double click: show the error instead of closing
        import traceback; err = traceback.format_exc(); print(err)
        try: messagebox.showerror('Project Panic Builder', err)
        except Exception: pass

if __name__ == '__main__':
    main()
