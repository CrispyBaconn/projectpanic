#!/usr/bin/env python3
"""Project Panic Builder: a small window that turns your Panic Park ROM set into the game for the devices you pick.

    python3 tools/builder_gui.py        (Linux / WSL; needs  sudo apt install python3-tk)

On Windows double-click ProjectPanicBuilder.bat in the project folder (it starts this window inside WSL).

Steps: extract the ROM archive (.zip or .7z) into panicprkrom/, rebuild panicprk_main.elf + main_r4650_BE_4MB.bin
(tools/extract_rom.py), generate the C++ code (mips2cpp.py), then run platform/build_<device>.sh for every device chosen.
"""
import json, os, queue, re, shutil, signal, subprocess, sys, threading, time, zipfile
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CFG = os.path.join(os.path.expanduser('~'), '.projectpanic_builder.json')
DKP = os.environ.get('DEVKITPRO', '/opt/devkitpro')
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
            no7z='To open .7z files install 7-Zip:  sudo apt install p7zip-full',
            done='Finished! The files are in the dist folder.', failed='Something went wrong: open "Show details".',
            stopped='Stopped.', open='Open the dist folder', a_setup='first time: downloads the Android SDK (~3 GB)',
            n3ds='needs the assets folder made by the PC version: build Windows or Linux too and start the game once',
            confirm_stop='Stop the build?', reuse='already extracted: using panicprkrom/', no_file='file not found', recheck='Check again',
            no_elftools='pyelftools is missing:  sudo apt install python3-pyelftools'),
 'it': dict(title='Project Panic Builder', sub='Compila Panic Park per i tuoi dispositivi',
            s1='1  ROM', s1h='Scegli il tuo archivio panicprk (.zip o .7z)', browse='Sfoglia…',
            s2='2  Dispositivi', s2h='Spunta i dispositivi (in grigio: prima installa lo strumento indicato)', s3='3  Compila', go='COMPILA', stop='Ferma',
            log='Mostra dettagli', hidelog='Nascondi dettagli', ready='Pronto', missing='manca: ',
            ok='pronto', st_wait='in attesa', st_run='in corso…', st_ok='fatto', st_err='errore', st_skip='saltato',
            p_rom='Estrai le ROM', p_elf='Trova il programma del gioco', p_gen='Genera il codice C++',
            no_rom='Prima scegli l’archivio delle ROM.', no_plat='Spunta almeno un dispositivo.',
            bad_rom="L'archivio non contiene le ROM di Panic Park (pnp2vera.ic1 / pnp2vera.ic2).",
            no7z='Per aprire i file .7z installa 7-Zip:  sudo apt install p7zip-full',
            done='Finito! I file sono nella cartella dist.', failed='Qualcosa è andato storto: apri "Mostra dettagli".',
            stopped='Fermato.', open='Apri la cartella dist', a_setup='la prima volta scarica l’SDK Android (~3 GB)',
            n3ds='serve la cartella assets creata dalla versione PC: compila anche Windows o Linux e avvia il gioco una volta',
            confirm_stop='Fermare la compilazione?', reuse='già estratte: uso panicprkrom/', no_file='file non trovato', recheck='Ricontrolla',
            no_elftools='manca pyelftools:  sudo apt install python3-pyelftools'),
}

def have(cmd): return shutil.which(cmd) is not None

# device: (label, build script, check -> missing-tool hint or None, extra note)
def chk_windows():
    return None if have('x86_64-w64-mingw32-g++-posix') or have('x86_64-w64-mingw32-g++') else 'sudo apt install g++-mingw-w64-x86-64-posix'
def chk_linux():
    return None if have('g++') and have('sdl2-config') else 'sudo apt install g++ libsdl2-dev'
def chk_android():
    return None if have('java') and have('unzip') else 'sudo apt install openjdk-17-jdk unzip wget'
def chk_switch():
    ok = os.path.isfile(f'{DKP}/libnx/switch_rules') and os.path.isfile(f'{DKP}/portlibs/switch/lib/libSDL2.a')
    return None if ok else 'devkitPro + sudo dkp-pacman -S switch-dev switch-sdl2'
def chk_wiiu():
    ok = os.path.isfile(f'{DKP}/wut/share/wut_rules') and os.path.isfile(f'{DKP}/portlibs/wiiu/lib/libSDL2.a')
    return None if ok else 'devkitPro + sudo dkp-pacman -S wiiu-dev wiiu-sdl2'
def chk_3ds():
    ok = os.path.isfile(f'{DKP}/devkitARM/3ds_rules') and os.path.isfile(f'{DKP}/libctru/lib/libcitro3d.a')
    return None if ok else 'devkitPro + sudo dkp-pacman -S 3ds-dev'

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

class Worker(threading.Thread):
    """Runs the steps and reports to the window through a queue: ('log', text) ('step', i, state) ('pct', i, 0..1)."""
    def __init__(self, q, archive, devices, t):
        super().__init__(daemon=True); self.q, self.archive, self.devices, self.t = q, archive, devices, t
        self.proc = None; self.cancel = False; self.last = ''

    def log(self, s): self.q.put(('log', s))

    def run_cmd(self, args, step, total=None, pattern=None, env=None):
        """Runs a command; with `total`, every output line matching `pattern` advances the step's progress."""
        if self.cancel: raise Cancelled()
        self.log('$ ' + ' '.join(args))
        e = dict(os.environ); e.update(env or {})
        self.proc = subprocess.Popen(args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                     errors='replace', env=e, start_new_session=True)
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
        if self.proc:
            try: os.killpg(self.proc.pid, signal.SIGTERM)
            except Exception: pass

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
            if not have('7z'): raise RuntimeError(self.t['no7z'])
            if self.run_cmd(['7z', 'x', '-y', '-bsp1', '-o' + tmp, self.archive], step, None, re.compile(r'(\d+)%')) != 0:
                raise RuntimeError('7z')
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
                        if subprocess.run([sys.executable, '-c', 'import elftools'], capture_output=True).returncode:
                            # mips2cpp.py needs pyelftools: install it for this user (pip refuses on recent Ubuntu
                            # without --break-system-packages, which only affects this user's packages here)
                            for extra in ([], ['--break-system-packages']):
                                if self.run_cmd([sys.executable, '-m', 'pip', 'install', '--user', 'pyelftools'] + extra, i) == 0: break
                            if subprocess.run([sys.executable, '-c', 'import elftools'], capture_output=True).returncode:
                                raise RuntimeError(self.t['no_elftools'])
                        ok = self.run_cmd([sys.executable, 'mips2cpp.py', '--elf', 'panicprk_main.elf',
                                           '--funcs', 'functions.csv', '--out', 'gen_full'], i) == 0
                    else:
                        script = next(d[2] for d in DEVICES if d[0] == key)
                        nsrc = len([f for f in os.listdir(os.path.join(ROOT, 'gen_full')) if f.endswith('.cpp')]) + 1
                        pat = re.compile(r'(gen_\d+|dispatch|platform_stub|switch_sys)\.cpp')
                        if key == 'android' and not os.path.isdir(os.path.expanduser(
                                os.environ.get('ANDROID_HOME', '~/android-sdk')) + '/ndk'):
                            ok = self.run_cmd(['bash', 'platform/build_android.sh', 'setup'], i) == 0
                        if ok:
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
        self.lang = self.cfg.get('lang') or ('it' if os.environ.get('LANG', '').lower().startswith('it') else 'en')
        root.configure(bg=BG); root.minsize(640, 0)
        st = ttk.Style(root); st.theme_use('clam')
        st.configure('P.Horizontal.TProgressbar', troughcolor='#0a1f45', background=YEL, bordercolor=BG,
                     lightcolor=YEL, darkcolor=ORA, thickness=26)
        st.configure('S.Horizontal.TProgressbar', troughcolor='#0a1f45', background=GRN, bordercolor=PANEL,
                     lightcolor=GRN, darkcolor=GRN, thickness=8)
        self.build_ui(); self.refresh_texts(); self.poll()
        root.protocol('WM_DELETE_WINDOW', self.on_close)

    @property
    def t(self): return TXT[self.lang]

    # -------- layout
    def lbl(self, parent, size=11, weight='normal', fg=INK, bg=None, **kw):
        return tk.Label(parent, font=('DejaVu Sans', size, weight), fg=fg, bg=bg or parent['bg'], **kw)

    def button(self, parent, cmd, bg=YEL, fg='#1a1a1a', size=11, **kw):
        return tk.Button(parent, command=cmd, font=('DejaVu Sans', size, 'bold'), bg=bg, fg=fg, activebackground=ORA,
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
        self.w_lang = self.button(top, self.toggle_lang, bg=PANEL, fg=INK, size=10); self.w_lang.pack(side='right')

        self.body = tk.Frame(r, bg=BG, padx=20); self.body.pack(fill='both', expand=True)
        self.body.columnconfigure(0, weight=1)

        # 1: ROM set
        p1 = self.panel(0)
        self.w_s1 = self.lbl(p1, 13, 'bold', YEL, anchor='w'); self.w_s1.grid(row=0, column=0, columnspan=2, sticky='w')
        self.w_s1h = self.lbl(p1, 10, fg=MUTED, anchor='w'); self.w_s1h.grid(row=1, column=0, columnspan=2, sticky='w')
        self.rom = tk.StringVar(value=self.cfg.get('rom', ''))
        self.w_rom = tk.Entry(p1, textvariable=self.rom, font=('DejaVu Sans', 11), relief='flat', bg='#ffffff', fg='#111')
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
            cb = tk.Checkbutton(grid, text=name, variable=v, font=('DejaVu Sans', 12, 'bold'), fg=INK, bg=PANEL,
                                selectcolor='#0a1f45', activebackground=PANEL, activeforeground=YEL, anchor='w', width=15,
                                cursor='hand2', highlightthickness=0, bd=0, command=self.save_cfg)
            cb.grid(row=i, column=0, sticky='nw', pady=3)
            note = self.lbl(grid, 9, fg=MUTED, anchor='w', justify='left', wraplength=360); note.grid(row=i, column=1, sticky='w', padx=(8, 0))
            pb = ttk.Progressbar(grid, style='S.Horizontal.TProgressbar', maximum=1000, length=200)
            stl = self.lbl(grid, 10, 'bold', fg=MUTED, anchor='e', width=11)
            self.dev[key] = dict(var=v, cb=cb, note=note, pb=pb, st=stl, chk=chk, row=i)
        self.dev_vars = {k: d['var'] for k, d in self.dev.items()}

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
                            font=('DejaVu Sans Mono', 9), wrap='none')
        sb = tk.Scrollbar(self.logf, command=self.logt.yview); self.logt.configure(yscrollcommand=sb.set)
        self.logt.pack(side='left', fill='both', expand=True); sb.pack(side='right', fill='y')
        self.log_on = False
        tk.Frame(r, bg=BG, height=10).pack(side='bottom')

    def refresh_texts(self):
        t = self.t; self.root.title(t['title'])
        self.w_title['text'] = t['title']; self.w_sub['text'] = t['sub']
        self.w_lang['text'] = 'English' if self.lang == 'it' else 'Italiano'
        self.w_s1['text'], self.w_s1h['text'], self.w_browse['text'] = t['s1'], t['s1h'], t['browse']
        self.w_s2['text'], self.w_s2h['text'], self.w_s3['text'] = t['s2'], t['s2h'], t['s3']
        self.w_recheck['text'] = t['recheck']
        self.w_go['text'], self.w_stop['text'], self.w_open['text'] = t['go'], t['stop'], t['open']
        self.w_logbtn['text'] = t['hidelog'] if self.log_on else t['log']
        self.w_now['text'] = t[self.status] if self.status in t else (self.status or t['ready'])
        for key, d in self.dev.items():
            miss = d['chk'](); note = (t['missing'] + miss) if miss else ('✓ ' + t['ok'])
            if key == 'android' and not miss and not os.path.isdir(os.path.expanduser('~/android-sdk/ndk')): note += '  ·  ' + t['a_setup']
            if key == '3ds' and not miss: note += '\n' + t['n3ds']
            d['note'].configure(text=note, fg=(ORA if miss else MUTED))
            if not self.worker:
                if miss: d['var'].set(False)
                d['cb'].configure(state='disabled' if miss else 'normal')
        self.check_rom()

    def toggle_lang(self):
        self.lang = 'it' if self.lang == 'en' else 'en'; self.save_cfg(); self.refresh_texts()
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
        if rom.lower().endswith('.7z') and not have('7z'): messagebox.showwarning(t['title'], t['no7z']); return
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
        if have('explorer.exe') and have('wslpath'):                     # WSL: open it in the Windows file manager
            w = subprocess.run(['wslpath', '-w', d], capture_output=True, text=True).stdout.strip()
            subprocess.Popen(['explorer.exe', w])
        elif have('xdg-open'): subprocess.Popen(['xdg-open', d])

def main():
    root = tk.Tk()
    App(root)
    root.mainloop()

if __name__ == '__main__':
    main()
