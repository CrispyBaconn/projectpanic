#!/usr/bin/env python3
"""Livello 2: funzioni REALI di Panic Park. Uso: difftest_game.py [n_funzioni] [casi_per_funzione] [seed]"""
import os, random, struct, subprocess, sys, csv, collections
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, ROOT)
import mips2cpp as m
from unicorn import *
from unicorn.mips_const import *

NF = int(sys.argv[1]) if len(sys.argv) > 1 else 120
NC = int(sys.argv[2]) if len(sys.argv) > 2 else 4
SEED = int(sys.argv[3]) if len(sys.argv) > 3 else 1
ELF = os.environ.get('PANICPRK_ELF', os.path.join(ROOT, '..', 'panicprk_main.elf'))
rnd = random.Random(SEED)
BUILD = os.path.join(HERE, 'build2'); os.makedirs(BUILD, exist_ok=True)

# ------------------------------------------------------------ ELF -> immagine RAM
from elftools.elf.elffile import ELFFile
elf = ELFFile(open(ELF, 'rb'))
IMG_SZ = 0x300000
img = bytearray(IMG_SZ)
for seg in elf.iter_segments():
    if seg['p_type'] == 'PT_LOAD':
        pa = seg['p_vaddr'] & 0x1fffffff
        d = seg.data()
        img[pa:pa + len(d)] = d
open(os.path.join(BUILD, 'ram_init.bin'), 'wb').write(img)

e_, secs, tbase, tdata = m.load_elf(ELF)
twords = struct.unpack('>%dI' % (len(tdata) // 4), tdata); thi = tbase + len(tdata)
rw = lambda a: twords[(a - tbase) // 4]
def rdata(a):
    for b, d in secs:
        if b <= a < b + len(d): return struct.unpack('>I', d[a - b:a - b + 4])[0]
    raise KeyError(a)
rows = list(csv.DictReader(open(os.path.join(ROOT, 'functions.csv'))))
ents = [int(r['addr'], 16) for r in rows]
hwf = {int(r['addr'], 16): bool(r['hw_blocks']) for r in rows}
ents = sorted({x for x in ents if x >= m.TEXT_LO} | {m.TEXT_LO})
tr = m.Translator(rw, m.TEXT_LO, thi, ents, rdata)
_extra, _ = m.discover_fn_pointers(tr, secs, tbase, thi, verbose=True)
tr.entries = sorted(set(tr.entries) | _extra)
ents = list(tr.entries)

# ------------------------------------------------------------ purezza
IMPURE_OPS = {'mfc0', 'mtc0', 'eret', 'cache', 'syscall', 'jalr', '?'}
info = {}
for s in ents:
    e = tr.func_end(s); calls = set(); imp = hwf.get(s, False)
    for p in range(s, e, 4):
        d = tr.dec(p)
        if d['op'] in IMPURE_OPS: imp = True
        if d['op'] == 'jal': calls.add(((p + 4) & 0xf0000000) | d['target'])
    info[s] = dict(imp=imp, calls=calls, n=(e - s) // 4)
memo = {}
def impure(s, stack=()):
    if s in memo: return memo[s]
    if s not in info: return True
    if s in stack: return False
    r = info[s]['imp'] or any(impure(t, stack + (s,)) for t in info[s]['calls'])
    memo[s] = r; return r
pure = [s for s in ents if not impure(s) and 6 <= info[s]['n'] <= 700]
withcalls = [s for s in pure if info[s]['calls']]
print(f'funzioni pure: {len([s for s in ents if not impure(s)])} / {len(ents)}; candidate {len(pure)} (con chiamate: {len(withcalls)})')
roots = rnd.sample(withcalls, min(NF // 2, len(withcalls))) + rnd.sample(pure, min(NF // 2, len(pure)))
roots = sorted(set(roots))
todo = set(roots); stack = list(roots)
while stack:
    s = stack.pop()
    for t in info[s]['calls']:
        if t not in todo: todo.add(t); stack.append(t)
tr.closure(set(todo))
todo = sorted(todo | {t for t in tr.entries if t in todo})
print(f'root: {len(roots)}, funzioni da emettere (con chiusura): {len(todo)}')

# ------------------------------------------------------------ genera C++ e compila
lines = ['#include "runtime.h"', '#pragma GCC diagnostic ignored "-Wunused-label"']
for s in todo: lines.append(f'void {m.fname(s)}(CPU* c);')
for s in todo: lines += tr.emit_function(s)
lines.append('void (*g_tests[])(CPU*) = {' + ','.join(m.fname(s) for s in roots) + '};')
lines.append(f'const int g_ntests = {len(roots)};')
open(os.path.join(BUILD, 'gen.cpp'), 'w').write('\n'.join(lines) + '\n')
exe = os.path.join(BUILD, 'harness2')
r = subprocess.run(['g++', '-std=c++17', '-O1', '-DRECOMP_TEST', '-ffp-contract=off', '-fno-fast-math', '-w', '-I' + ROOT,
                    os.path.join(HERE, 'harness2.cpp'), os.path.join(BUILD, 'gen.cpp'), '-o', exe], capture_output=True, text=True)
if r.returncode: print(r.stderr[:3000]); sys.exit(1)
print('compilato')

# ------------------------------------------------------------ casi
END, SP, GP = 0x80F20000, 0x80E00000, 0x80275040
def rand_ptr(): return 0x800dc700 + 4 * rnd.randrange(0, (0x802aa000 - 0x800dc700) // 4)
def rand_arg():
    k = rnd.random()
    if k < 0.40: return rand_ptr()
    if k < 0.75: return rnd.randrange(-4, 65) & 0xffffffff
    return rnd.getrandbits(32)
def rand_f():
    if rnd.random() < 0.85:
        return struct.unpack('>I', struct.pack('>f', rnd.choice([-1, 1]) * rnd.uniform(0.01, 500)))[0]
    return rnd.getrandbits(32)
cases = []
for ri, s in enumerate(roots):
    for _ in range(NC):
        r = [0] + [rnd.choice([rnd.getrandbits(32), rnd.randrange(0, 100), rand_ptr()]) for _ in range(31)]
        for k in (4, 5, 6, 7): r[k] = rand_arg()
        r[29], r[28], r[31] = SP, GP, END
        cases.append(dict(idx=ri, r=r, hi=rnd.getrandbits(32), lo=rnd.getrandbits(32),
                          f=[rand_f() for _ in range(32)], fcsr=0))
with open(os.path.join(BUILD, 'cases.bin'), 'wb') as f:
    f.write(struct.pack('<I', len(cases)))
    for c in cases:
        f.write(struct.pack('<I', c['idx']) + struct.pack('<32I', *c['r']) + struct.pack('<II', c['hi'], c['lo']) +
                struct.pack('<32I', *c['f']) + struct.pack('<I', 0))
subprocess.run([exe, os.path.join(BUILD, 'ram_init.bin'), os.path.join(BUILD, 'cases.bin'),
                os.path.join(BUILD, 'results.bin')], check=True)
raw = open(os.path.join(BUILD, 'results.bin'), 'rb').read()
HEAD = (1 + 32 + 2 + 32 + 1 + 1) * 4
cpp = []; pos = 0
for _ in cases:
    h = struct.unpack('<%dI' % (HEAD // 4), raw[pos:pos + HEAD]); pos += HEAD
    nlog = h[-1]; writes = {}
    for _ in range(nlog):
        a, v = struct.unpack('<II', raw[pos:pos + 8]); pos += 8; writes[a] = v
    cpp.append(dict(st=h[0], r=h[1:33], hi=h[33], lo=h[34], f=h[35:67], fcsr=h[67], w=writes))

# ------------------------------------------------------------ Unicorn
import numpy as np
GPRN = lambda n: getattr(sys.modules['unicorn.mips_const'], f'UC_MIPS_REG_{n}')
GPR = [GPRN(n) for n in range(32)]
def I(op, rs, rt, imm): return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
PRE, INIT, READ, STATE = 0x00F00000, 0x00F00400, 0x00F00800, 0x00F10000
T9 = 25
STK_LO, STK_LEN = 0x00DF0000, 0x20000
IMGA = np.frombuffer(bytes(img), dtype=np.uint8)
ZSTK = bytes(STK_LEN)
def new_uc():
    u = Uc(UC_ARCH_MIPS, UC_MODE_MIPS32 | UC_MODE_BIG_ENDIAN)
    u.ctl_set_cpu_model(UC_CPU_MIPS32_24KF)
    u.mem_map(0, 0x1000000)
    u.mem_write(0, bytes(img))
    def W(a, ws): u.mem_write(a, b''.join(struct.pack('>I', w) for w in ws))
    W(PRE, [I(15, 0, 8, 0x3040), I(13, 8, 8, 4), 0x40886000, 0])
    u.emu_start(0x80F00000, 0x80F00000 + 12)
    W(INIT, [I(49, T9, n, 4 * n) for n in range(32)] + [I(35, T9, 8, 128), (17 << 26) | (6 << 21) | (8 << 16) | (31 << 11), 0])
    W(READ, [I(57, T9, n, 4 * n) for n in range(32)] + [0])
    return u
uc = new_uc()
isnan = lambda b: (b & 0x7f800000) == 0x7f800000 and (b & 0x7fffff) != 0

ok = 0; bad = 0; shown = 0; skipped = collections.Counter(); per_func = collections.defaultdict(lambda: [0, 0])
for ci, (cs, cp) in enumerate(zip(cases, cpp)):
    fn = roots[cs['idx']]
    if cp['st'] != 0: skipped[f'cpp status {cp["st"]}'] += 1; continue
    uc.mem_write(STATE, struct.pack('>32I', *cs['f']) + struct.pack('>I', 0))
    uc.reg_write(GPR[T9], 0x80F10000)
    uc.emu_start(0x80F00400, 0x80F00400 + 4 * 34)
    for n in range(1, 32): uc.reg_write(GPR[n], cs['r'][n])
    uc.reg_write(UC_MIPS_REG_HI, cs['hi']); uc.reg_write(UC_MIPS_REG_LO, cs['lo'])
    err = None
    try: uc.emu_start(fn, END, count=400000)
    except UcError as e: err = e
    if err is not None:
        skipped['unicorn eccezione'] += 1; uc = new_uc(); continue
    if uc.reg_read(UC_MIPS_REG_PC) != END:
        skipped['unicorn non terminato'] += 1
        uc.mem_write(0, bytes(img)); uc.mem_write(STK_LO, ZSTK); continue
    ur = [0] + [uc.reg_read(GPR[n]) for n in range(1, 32)]
    uhi, ulo = uc.reg_read(UC_MIPS_REG_HI), uc.reg_read(UC_MIPS_REG_LO)
    cur = np.frombuffer(bytes(uc.mem_read(0, IMG_SZ)), dtype=np.uint8)
    stk = np.frombuffer(bytes(uc.mem_read(STK_LO, STK_LEN)), dtype=np.uint8)
    uni_final = {int(a): int(cur[a]) for a in np.nonzero(cur != IMGA)[0]}
    uni_final.update({int(STK_LO + a): int(stk[a]) for a in np.nonzero(stk)[0]})
    def uval(a):
        if a in uni_final: return uni_final[a]
        if a < IMG_SZ: return int(IMGA[a])
        if STK_LO <= a < STK_LO + STK_LEN: return 0
        return int(uc.mem_read(a, 1)[0])
    uc.mem_write(0, bytes(img)); uc.mem_write(STK_LO, ZSTK)       # ripristino completo
    uc.reg_write(GPR[T9], 0x80F10000)
    uc.emu_start(0x80F00800, 0x80F00800 + 4 * 33)
    uf = struct.unpack('>32I', bytes(uc.mem_read(STATE, 128)))
    if any(isnan(x) for x in uf) or any(isnan(x) for x in cp['f']): skipped['NaN FPU'] += 1; continue
    diffs = []
    for n in range(1, 32):
        if ur[n] != cp['r'][n]: diffs.append(f'r{n}: uni={ur[n]:#010x} cpp={cp["r"][n]:#010x}')
    if uhi != cp['hi']: diffs.append(f'hi uni={uhi:#x} cpp={cp["hi"]:#x}')
    if ulo != cp['lo']: diffs.append(f'lo uni={ulo:#x} cpp={cp["lo"]:#x}')
    for n in range(32):
        if uf[n] != cp['f'][n]: diffs.append(f'f{n}: uni={uf[n]:#010x} cpp={cp["f"][n]:#010x}')
    for a in sorted(set(uni_final) | set(cp['w'])):
        cv = cp['w'].get(a, int(IMGA[a]) if a < IMG_SZ else 0)
        if uval(a) != cv:
            diffs.append(f'mem[{a:#x}] uni={uval(a)} cpp={cv}'); break
    if diffs:
        bad += 1; per_func[fn][1] += 1
        if shown < 6:
            shown += 1
            print(f'--- DIFF in f_{fn:08x} (caso {ci})'); [print('     ', d) for d in diffs[:6]]
    else:
        ok += 1; per_func[fn][0] += 1

nfun = sum(1 for v in per_func.values() if v[0] + v[1])
print(f'\nRISULTATO: {ok} casi identici, {bad} diversi, {sum(skipped.values())} saltati {dict(skipped)}')
print(f'funzioni con almeno un confronto valido: {nfun}/{len(roots)}; con differenze: {sum(1 for v in per_func.values() if v[1])}')
