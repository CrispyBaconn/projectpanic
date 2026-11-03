#!/usr/bin/env python3
"""
mips2cpp.py - static translator MIPS (R4650 / MIPS III subset) -> C++.

Execution model:
  * one C++ function per MIPS function, signature  void f_XXXXXXXX(CPU* c)
  * internal branches become goto, jal becomes a C++ call, jr $ra becomes return
  * delay slots are expanded inline; branch-likely instructions execute the delay slot only when taken
  * jr (not $ra) -> switch over the jump table targets (or tail call)
  * jalr -> call_guest(c, address)  (generated address->function table)
  * BACKEDGE(c) is inserted on backward branches: it is used to deliver interrupts
"""
import argparse, collections, csv, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mipsdec import decode, BRANCH, LIKELY

TEXT_LO = 0x80000320          # below: exception vectors, replaced by the runtime
JUMPS = {'j', 'jal', 'jr', 'jalr'}


def is_ctrl(d): return d['op'] in BRANCH or d['op'] in JUMPS
def fname(a): return f'f_{a:08x}'
def rr(n): return '0u' if n == 0 else f'c->r[{n}]'
def wr(n, e): return f'(void)({e});' if n == 0 else f'c->r[{n}] = {e};'
def imm32(v): return f'{v & 0xffffffff:#x}u'
def ea(d): return f'{rr(d["rs"])} + {imm32(d["imm"])}'
def sgn(n): return f'(int32_t){rr(n)}'


def branch_cond(d):
    op, rs, rt = d['op'], rr(d['rs']), rr(d['rt'])
    base = {'beql': 'beq', 'bnel': 'bne', 'blezl': 'blez', 'bgtzl': 'bgtz', 'bltzl': 'bltz',
            'bgezl': 'bgez', 'bc1fl': 'bc1f', 'bc1tl': 'bc1t'}.get(op, op)
    return {
        'beq': f'{rs} == {rt}', 'bne': f'{rs} != {rt}',
        'blez': f'(int32_t){rs} <= 0', 'bgtz': f'(int32_t){rs} > 0',
        'bltz': f'(int32_t){rs} < 0', 'bgez': f'(int32_t){rs} >= 0',
        'bc1t': '((c->fcsr >> 23) & 1u) != 0', 'bc1f': '((c->fcsr >> 23) & 1u) == 0',
    }[base]


def always_taken(d):
    return d['op'] in ('beq', 'beql') and d['rs'] == d['rt']


def emit_plain(d, pc):
    """Non-control instruction -> list of C++ lines."""
    op = d['op']; rs, rt, rd = d['rs'], d['rt'], d['rd']
    sa, imm, uimm = d['sa'], d['imm'], d['uimm']
    fs, ft, fd = d['fs'], d['ft'], d['fd']
    R = rr
    if op == 'nop' or op == 'sync': return []
    if op == 'sll':  return [wr(rd, f'{R(rt)} << {sa}')]
    if op == 'srl':  return [wr(rd, f'{R(rt)} >> {sa}')]
    if op == 'sra':  return [wr(rd, f'(uint32_t)((int32_t){R(rt)} >> {sa})')]
    if op == 'sllv': return [wr(rd, f'{R(rt)} << ({R(rs)} & 31u)')]
    if op == 'srlv': return [wr(rd, f'{R(rt)} >> ({R(rs)} & 31u)')]
    if op == 'srav': return [wr(rd, f'(uint32_t)((int32_t){R(rt)} >> ({R(rs)} & 31u))')]
    if op == 'mfhi': return [wr(rd, 'c->hi')]
    if op == 'mflo': return [wr(rd, 'c->lo')]
    if op == 'mthi': return [f'c->hi = {R(rs)};']
    if op == 'mtlo': return [f'c->lo = {R(rs)};']
    if op == 'mult':  return [f'do_mult(c, {R(rs)}, {R(rt)});']
    if op == 'multu': return [f'do_multu(c, {R(rs)}, {R(rt)});']
    if op == 'div':   return [f'do_div(c, {R(rs)}, {R(rt)});']
    if op == 'divu':  return [f'do_divu(c, {R(rs)}, {R(rt)});']
    if op == 'madd':  return [f'do_madd(c, {R(rs)}, {R(rt)});']
    if op == 'mul':   return [wr(rd, f'(uint32_t)((int64_t)(int32_t){R(rs)} * (int32_t){R(rt)})')]
    if op in ('add', 'addu', 'daddu'): return [wr(rd, f'{R(rs)} + {R(rt)}')]
    if op in ('sub', 'subu'):          return [wr(rd, f'{R(rs)} - {R(rt)}')]
    if op == 'and': return [wr(rd, f'{R(rs)} & {R(rt)}')]
    if op == 'or':  return [wr(rd, f'{R(rs)} | {R(rt)}')]
    if op == 'xor': return [wr(rd, f'{R(rs)} ^ {R(rt)}')]
    if op == 'nor': return [wr(rd, f'~({R(rs)} | {R(rt)})')]
    if op == 'slt':  return [wr(rd, f'(uint32_t)((int32_t){R(rs)} < (int32_t){R(rt)})')]
    if op == 'sltu': return [wr(rd, f'(uint32_t)({R(rs)} < {R(rt)})')]
    if op in ('addi', 'addiu', 'daddiu'): return [wr(rt, f'{R(rs)} + {imm32(imm)}')]
    if op == 'slti':  return [wr(rt, f'(uint32_t)((int32_t){R(rs)} < {imm})')]
    if op == 'sltiu': return [wr(rt, f'(uint32_t)({R(rs)} < {imm32(imm)})')]
    if op == 'andi': return [wr(rt, f'{R(rs)} & {uimm:#x}u')]
    if op == 'ori':  return [wr(rt, f'{R(rs)} | {uimm:#x}u')]
    if op == 'xori': return [wr(rt, f'{R(rs)} ^ {uimm:#x}u')]
    if op == 'lui':  return [wr(rt, f'{uimm << 16:#x}u')]
    a = ea(d)
    if op == 'lb':  return [wr(rt, f'(uint32_t)(int32_t)(int8_t)mrd8({a})')]
    if op == 'lbu': return [wr(rt, f'mrd8({a})')]
    if op == 'lh':  return [wr(rt, f'(uint32_t)(int32_t)(int16_t)mrd16({a})')]
    if op == 'lhu': return [wr(rt, f'mrd16({a})')]
    if op == 'lw':  return [wr(rt, f'mrd32({a})')]
    if op == 'lwl': return [wr(rt, f'lwl_({R(rt)}, {a})')]
    if op == 'lwr': return [wr(rt, f'lwr_({R(rt)}, {a})')]
    if op == 'sb':  return [f'mwr8({a}, {R(rt)});']
    if op == 'sh':  return [f'mwr16({a}, {R(rt)});']
    if op == 'sw':  return [f'mwr32({a}, {R(rt)});']
    if op == 'swl': return [f'swl_({a}, {R(rt)});']
    if op == 'swr': return [f'swr_({a}, {R(rt)});']
    if op == 'lwc1': return [f'c->f[{ft}] = mrd32({a});']
    if op == 'swc1': return [f'mwr32({a}, c->f[{ft}]);']
    if op == 'cache': return [f'cache_op(c, {rt}, {a});']
    if op == 'mfc0': return [wr(rt, f'cp0_read(c, {rd}, {d["w"] & 7})')]
    if op == 'mtc0': return [f'cp0_write(c, {rd}, {d["w"] & 7}, {R(rt)});']
    if op == 'eret': return ['guest_eret(c);', 'return;']
    if op == 'break':   return [f'guest_break(c, {pc:#x}u, {d["code"]:#x}u);']
    if op == 'syscall': return [f'guest_break(c, {pc:#x}u, {d["code"]:#x}u);']
    if op == 'mfc1': return [wr(rt, f'c->f[{fs}]')]
    if op == 'mtc1': return [f'c->f[{fs}] = {R(rt)};']
    if op == 'cfc1': return [wr(rt, f'cfc1_(c, {fs})')]
    if op == 'ctc1': return [f'ctc1_(c, {fs}, {R(rt)});']
    F = lambda n: f'fl(c->f[{n}])'
    if op == 'add.s':  return [f'c->f[{fd}] = fbits({F(fs)} + {F(ft)});']
    if op == 'sub.s':  return [f'c->f[{fd}] = fbits({F(fs)} - {F(ft)});']
    if op == 'mul.s':  return [f'c->f[{fd}] = fbits({F(fs)} * {F(ft)});']
    if op == 'div.s':  return [f'c->f[{fd}] = fbits({F(fs)} / {F(ft)});']
    if op == 'sqrt.s': return [f'c->f[{fd}] = fbits(sqrtf({F(fs)}));']
    if op == 'abs.s':  return [f'c->f[{fd}] = c->f[{fs}] & 0x7fffffffu;']
    if op == 'mov.s':  return [f'c->f[{fd}] = c->f[{fs}];']
    if op == 'neg.s':  return [f'c->f[{fd}] = c->f[{fs}] ^ 0x80000000u;']
    if op == 'trunc.w.s': return [f'c->f[{fd}] = trunc_w_s({F(fs)});']
    if op == 'cvt.s.w':   return [f'c->f[{fd}] = fbits((float)(int32_t)c->f[{fs}]);']
    if op == 'c.eq.s': return [f'set_fcc(c, {F(fs)} == {F(ft)});']
    if op == 'c.lt.s': return [f'set_fcc(c, {F(fs)} < {F(ft)});']
    if op == 'c.le.s': return [f'set_fcc(c, {F(fs)} <= {F(ft)});']
    return [f'guest_unimpl(c, {pc:#x}u, {d["w"]:#x}u);']


class Translator:
    def __init__(self, read_word, text_lo, text_hi, entries, read_data=None):
        self.rw = read_word            # address -> word (code)
        self.rd_data = read_data       # address -> word (data/rodata) for jump tables
        self.lo, self.hi = text_lo, text_hi
        self.entries = sorted(set(entries))
        self._dec = {}
        self.stats = collections.Counter()
        self.warn = []

    def dec(self, pc):
        d = self._dec.get(pc)
        if d is None:
            d = self._dec[pc] = decode(self.rw(pc))
        return d

    def func_end(self, start):
        import bisect
        i = bisect.bisect_right(self.entries, start)
        return self.entries[i] if i < len(self.entries) else self.hi

    # ---------------------------------------------------------- jump table
    DEST_RD = {'sll','srl','sra','sllv','srlv','srav','mfhi','mflo','add','addu','sub','subu','and','or',
               'xor','nor','slt','sltu','daddu','mul','jalr'}
    DEST_RT = {'addi','addiu','daddiu','slti','sltiu','andi','ori','xori','lui','lb','lbu','lh','lhu','lw',
               'lwl','lwr','mfc0','mfc1','cfc1'}

    @classmethod
    def dest(cls, d):
        if d['op'] in cls.DEST_RD: return d['rd']
        if d['op'] in cls.DEST_RT: return d['rt']
        if d['op'] == 'jal': return 31
        return None

    def find_table(self, start, end, pc):
        """jr rs <- lw rs,off(rb) <- addu rb,x,y <- (lui/addiu on x or y, even far away).
        Returns the targets if ALL entries (up to the limit from sltiu) fall inside the function."""
        if self.rd_data is None: return None
        jr = self.dec(pc); target_reg = jr['rs']
        lw = None
        for k in range(1, 12):
            p = pc - 4 * k
            if p < start: break
            d = self.dec(p)
            if d['op'] == 'lw' and d['rt'] == target_reg:
                lw = (p, d); break
            if self.dest(d) == target_reg: return None
        if lw is None: return None
        p0, dl = lw
        cands = {dl['rs']} - {0}; acc = dl['imm']; hi = None; widened = False
        p = p0 - 4
        while p >= start:
            d = self.dec(p)
            ds = self.dest(d)
            if ds in cands and ds != 0:
                if d['op'] in ('addu', 'add', 'daddu') and not widened:
                    cands = ({d['rs'], d['rt']}) - {0}; widened = True
                elif d['op'] in ('addiu', 'daddiu') and widened:
                    cands = {d['rs']} - {0}; acc += d['imm']
                elif d['op'] == 'ori' and widened:
                    cands = {d['rs']} - {0}; acc += d['uimm']
                elif d['op'] == 'lui' and widened:
                    hi = d['uimm'] << 16; break
                elif d['op'] == 'lui' and not widened:
                    hi = d['uimm'] << 16; break
                elif widened and len(cands) > 1:
                    cands.discard(ds)        # was the index (e.g. sll v0,v1,2): only the base remains
                else:
                    return None
            p -= 4
        if hi is None: return None
        tbl = (hi + acc) & 0xffffffff
        limit = 1024; exact = False
        for k in range(1, 40):
            q = pc - 4 * k
            if q < start: break
            d = self.dec(q)
            if d['op'] in ('sltiu',) and 0 < d['imm'] < 1024:
                limit = d['imm']; exact = True; break
        out = []
        for i in range(limit):
            try: v = self.rd_data(tbl + 4 * i)
            except Exception: break
            if v & 3 or not (start <= v < end): break
            out.append(v)
        if exact and len(out) != limit: return None
        return out or None

    # ---------------------------------------------------------- scan
    def scan(self, start, end):
        labels, ext, jrs = set(), set(), {}
        n = (end - start) // 4
        in_delay = False
        for i in range(n):
            pc = start + 4 * i
            d = self.dec(pc)
            if in_delay:
                in_delay = False
                continue
            if not is_ctrl(d): continue
            in_delay = True
            op = d['op']
            if op in BRANCH:
                t = (pc + 4 + (d['imm'] << 2)) & 0xffffffff
            elif op == 'j':
                t = ((pc + 4) & 0xf0000000) | d['target']
            elif op == 'jal':
                t = ((pc + 4) & 0xf0000000) | d['target']
                if t not in self.entries and self.lo <= t < self.hi: ext.add(t)
                continue
            elif op == 'jr' and d['rs'] != 31:
                tb = self.find_table(start, end, pc)
                jrs[pc] = tb
                if tb: labels.update(tb)
                continue
            else:
                continue
            if start <= t < end: labels.add(t)
            elif self.lo <= t < self.hi and t not in self.entries: ext.add(t)
        # label on the delay slot -> pc+8 is needed too
        in_delay = False
        for i in range(n):
            pc = start + 4 * i
            d = self.dec(pc)
            if in_delay: in_delay = False; continue
            if is_ctrl(d):
                in_delay = True
                if (pc + 4) in labels:
                    labels.add(pc + 8)
                    self.stats['label_on_delay_slot'] += 1
                    if d['op'] in ('j', 'jr', 'jalr') or always_taken(d):
                        self.warn.append(f'{pc:#x}: label on the delay slot of an unconditional jump')
        return labels, ext, jrs

    # ---------------------------------------------------------- emit
    def jump_to(self, t, start, end, pc, backedge_ok=True):
        if start <= t < end:
            if t <= pc and backedge_ok: return ['BACKEDGE(c);', f'goto L_{t:08x};']
            return [f'goto L_{t:08x};']
        if t in self.entries: return [f'{fname(t)}(c);', 'return;']
        return [f'call_guest(c, {t:#x}u);', 'return;']

    def call_to(self, t):
        if t in self.entries: return [f'{fname(t)}(c);']
        return [f'call_guest(c, {t:#x}u);']

    def emit_ctrl(self, d, pc, start, end, labels, jrs):
        dly = self.dec(pc + 4)
        if is_ctrl(dly):
            self.warn.append(f'{pc:#x}: branch in the delay slot')
            return [f'guest_unimpl(c, {pc:#x}u, {d["w"]:#x}u);']
        dl = emit_plain(dly, pc + 4)
        op = d['op']
        if op in BRANCH:
            t = (pc + 4 + (d['imm'] << 2)) & 0xffffffff
            jmp = self.jump_to(t, start, end, pc)
            C = branch_cond(d)
            if always_taken(d) and op == 'beq':
                return dl + jmp
            if op in LIKELY:
                return [f'if ({C}) {{', *dl, *jmp, '}']
            return ['{', f'bool b = {C};', *dl, 'if (b) {', *jmp, '}', '}']
        if op == 'j':
            t = ((pc + 4) & 0xf0000000) | d['target']
            return dl + self.jump_to(t, start, end, pc)
        if op == 'jal':
            t = ((pc + 4) & 0xf0000000) | d['target']
            return [wr(31, f'{pc + 8:#x}u'), *dl, *self.call_to(t)]
        if op == 'jalr':
            return ['{', f'uint32_t t = {rr(d["rs"])};', wr(d['rd'], f'{pc + 8:#x}u'), *dl,
                    'call_guest(c, t);', '}']
        if op == 'jr':
            if d['rs'] == 31:
                return dl + ['return;']
            tb = jrs.get(pc)
            if tb:
                cases = [f'case {a:#x}u: goto L_{a:08x};' for a in sorted(set(tb))]
                self.stats['jr_table'] += 1
            else:
                cases = []
                self.stats['jr_tailcall'] += 1
            return ['{', f'uint32_t t = {rr(d["rs"])};', *dl, 'switch (t) {', *cases,
                    'default: call_guest(c, t); return;', '}', '}']
        raise AssertionError(op)

    def emit_function(self, start, end=None):
        end = end if end is not None else self.func_end(start)
        labels, ext, jrs = self.scan(start, end)
        n = (end - start) // 4
        out = [f'void {fname(start)}(CPU* c) {{']
        i = 0
        last_ctrl_uncond = False
        while i < n:
            pc = start + 4 * i
            if pc in labels: out.append(f'L_{pc:08x}:')
            d = self.dec(pc)
            if is_ctrl(d):
                out += ['  ' + l for l in self.emit_ctrl(d, pc, start, end, labels, jrs)]
                last_ctrl_uncond = (d['op'] in ('j', 'jr') or always_taken(d))
                if (pc + 4) in labels and pc + 4 < end:
                    out.append(f'  goto L_{pc + 8:08x};')
                    out.append(f'L_{pc + 4:08x}:')
                    out += ['  ' + l for l in emit_plain(self.dec(pc + 4), pc + 4)]
                i += 2
                continue
            if d['op'] != 'nop': last_ctrl_uncond = False
            out += ['  ' + l for l in emit_plain(d, pc)]
            i += 1
        if not last_ctrl_uncond:
            if end in self.entries:
                out.append(f'  {fname(end)}(c);')
                self.stats['fallthrough'] += 1
        out.append('}')
        return out

    def closure(self, roots, limit_rounds=8):
        """Adds external branch targets to the entries until stable."""
        for _ in range(limit_rounds):
            new = set()
            for s in list(roots):
                e = self.func_end(s)
                _, ext, _ = self.scan(s, e)
                new |= ext
            new -= set(self.entries)
            if not new: return
            self.entries = sorted(set(self.entries) | new)
            roots = set(roots) | new
            self.stats['entries_added'] += len(new)



def discover_fn_pointers(tr, secs, tbase, thi, verbose=True):
    """Functions reachable only through a pointer (callback tables): looks for pointers to .text
    in the data and lui/addiu|ori constants in the code. Excludes jump table targets and accepts
    only addresses that start right after an unconditional jump (function boundary)."""
    import struct as _s
    ents = set(tr.entries)
    jt = set()
    for s_ in sorted(ents):
        e_ = tr.func_end(s_)
        labels, _, jrs = tr.scan(s_, e_)
        for tb_ in jrs.values():
            if tb_: jt.update(tb_)
    cand = collections.Counter()
    for b, d in secs:
        if b == tbase: continue
        for off in range(0, len(d) - 3, 4):
            v = _s.unpack('>I', d[off:off + 4])[0]
            if TEXT_LO <= v < thi and v % 4 == 0: cand[('data', v)] += 1
    # constants in the code: lui rX,hi ; (within 4 instr) addiu/ori rX,rX,lo
    lo_, hi_ = tr.lo, tr.hi
    for pc in range(lo_, hi_ - 4, 4):
        d = tr.dec(pc)
        if d['op'] != 'lui': continue
        r = d['rt']; hi = d['uimm'] << 16
        if not (0x80000000 <= hi < 0x80300000): continue
        for k in range(1, 5):
            q = pc + 4 * k
            if q >= hi_: break
            e = tr.dec(q)
            if e['op'] in ('addiu', 'daddiu', 'ori') and e['rs'] == r:
                v = (hi + (e['imm'] if e['op'] != 'ori' else e['uimm'])) & 0xffffffff
                if TEXT_LO <= v < thi and v % 4 == 0: cand[('code', v)] += 1
                break
            if tr.dest(e) == r: break
    def terminal_before(c):
        q = c - 4
        while q >= tr.lo and tr.dec(q)['op'] == 'nop': q -= 4
        if q < tr.lo: return False
        x = tr.dec(q)
        if x['op'] in ('jr', 'j') or always_taken(x): return True
        y = tr.dec(q - 4)           # is q a non-nop delay slot?
        return y['op'] in ('jr', 'j') or always_taken(y)
    acc, rej_jt, rej_bound = set(), set(), []
    for (src, v), n in cand.items():
        if v in ents: continue
        if v in jt: rej_jt.add(v); continue
        if terminal_before(v): acc.add(v)
        else: rej_bound.append((v, src))
    if verbose:
        print(f'function pointers: {len({v for _, v in cand})} distinct; already entries {len({v for _, v in cand if v in ents})}; '
              f'switch targets excluded {len(rej_jt)}; NEW entries {len(acc)}; rejected (not on a boundary) {len(rej_bound)}')
    return acc, rej_bound


def load_elf(path):
    from elftools.elf.elffile import ELFFile
    e = ELFFile(open(path, 'rb'))
    secs = []
    for s in e.iter_sections():
        if s['sh_type'] == 'SHT_PROGBITS' and s['sh_addr']:
            secs.append((s['sh_addr'], s.data()))
    text = e.get_section_by_name('.text')
    return e, secs, text['sh_addr'], text.data()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', required=True)
    ap.add_argument('--funcs', required=True, help='functions.csv')
    ap.add_argument('--out', required=True)
    ap.add_argument('--per-file', type=int, default=60)
    ap.add_argument('--no-fnptr', action='store_true', help='do not look for function pointers')
    ap.add_argument('--only', help='comma-separated list of hex addresses (+ closure of the callees)')
    ap.add_argument('--closure-calls', action='store_true', help='with --only: include the callees (jal)')
    a = ap.parse_args()

    e, secs, tbase, tdata = load_elf(a.elf)
    twords = struct.unpack('>%dI' % (len(tdata) // 4), tdata)
    thi = tbase + len(tdata)

    def rw(addr): return twords[(addr - tbase) // 4]
    def rdata(addr):
        for b, d in secs:
            if b <= addr < b + len(d): return struct.unpack('>I', d[addr - b:addr - b + 4])[0]
        raise KeyError(addr)

    ents = [int(r['addr'], 16) for r in csv.DictReader(open(a.funcs))]
    ents = [x for x in ents if x >= TEXT_LO] + [TEXT_LO]
    tr = Translator(rw, TEXT_LO, thi, ents, rdata)
    if not a.no_fnptr:
        extra, _rej = discover_fn_pointers(tr, secs, tbase, thi)
        tr.entries = sorted(set(tr.entries) | extra)

    if a.only:
        roots = {int(x, 16) for x in a.only.split(',')}
        if a.closure_calls:
            todo = list(roots)
            while todo:
                s = todo.pop()
                for p in range(s, tr.func_end(s), 4):
                    d = tr.dec(p)
                    if d['op'] == 'jal':
                        t = ((p + 4) & 0xf0000000) | d['target']
                        if t not in roots and TEXT_LO <= t < thi: roots.add(t); todo.append(t)
        tr.closure(roots)
        todo = sorted(roots)
    else:
        tr.closure(set(tr.entries))
        todo = list(tr.entries)

    os.makedirs(a.out, exist_ok=True)
    hdr = ['// generated by mips2cpp.py - do NOT edit by hand', '#pragma once', '#include "runtime.h"']
    for s in todo: hdr.append(f'void {fname(s)}(CPU* c);')
    # reachable but not emitted functions (only with --only): declare them and resolve them with call_guest
    open(os.path.join(a.out, 'funcs.h'), 'w').write('\n'.join(hdr) + '\n')

    files = []
    for k in range(0, len(todo), a.per_file):
        chunk = todo[k:k + a.per_file]
        name = f'gen_{k // a.per_file:03d}.cpp'
        lines = ['// generated by mips2cpp.py', '#include "funcs.h"',
                 '#pragma GCC diagnostic ignored "-Wunused-label"', '']
        for s in chunk: lines += tr.emit_function(s) + ['']
        open(os.path.join(a.out, name), 'w').write('\n'.join(lines))
        files.append(name)
    tab = ['#include "funcs.h"', 'struct Ent { uint32_t a; void (*f)(CPU*); };', 'static const Ent g_tab[] = {']
    for s in sorted(todo): tab.append(f'  {{ {s:#x}u, {fname(s)} }},')
    tab += ['};', 'void call_guest(CPU* c, uint32_t addr) {',
            '  size_t lo = 0, hi = sizeof(g_tab) / sizeof(g_tab[0]);',
            '  while (lo < hi) { size_t m = (lo + hi) / 2; if (g_tab[m].a < addr) lo = m + 1; else hi = m; }',
            '  if (lo < sizeof(g_tab) / sizeof(g_tab[0]) && g_tab[lo].a == addr) { g_tab[lo].f(c); return; }',
            '  guest_unimpl(c, addr, 0xdeadc0deu);', '}']
    open(os.path.join(a.out, 'dispatch.cpp'), 'w').write('\n'.join(tab) + '\n')
    print(f'{len(todo)} functions in {len(files)} files; stats: {dict(tr.stats)}')
    for w in tr.warn[:20]: print('WARN', w)
    if len(tr.warn) > 20: print('...', len(tr.warn) - 20, 'more warnings')


if __name__ == '__main__':
    main()
