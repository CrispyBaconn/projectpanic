"""Decoder MIPS (R4650 / MIPS III subset + madd/mul) per Panic Park.
decode(word) -> dict(op=..., rs, rt, rd, sa, imm(signed16), uimm, target, fs, ft, fd, code)"""
import struct

def sx16(v): return v - 0x10000 if v & 0x8000 else v

SPECIAL = {0:'sll',2:'srl',3:'sra',4:'sllv',6:'srlv',7:'srav',8:'jr',9:'jalr',12:'syscall',13:'break',
           15:'sync',16:'mfhi',17:'mthi',18:'mflo',19:'mtlo',24:'mult',25:'multu',26:'div',27:'divu',
           32:'add',33:'addu',34:'sub',35:'subu',36:'and',37:'or',38:'xor',39:'nor',42:'slt',43:'sltu',
           45:'daddu'}
REGIMM = {0:'bltz',1:'bgez',2:'bltzl',3:'bgezl'}
OPS = {2:'j',3:'jal',4:'beq',5:'bne',6:'blez',7:'bgtz',8:'addi',9:'addiu',10:'slti',11:'sltiu',
       12:'andi',13:'ori',14:'xori',15:'lui',20:'beql',21:'bnel',22:'blezl',23:'bgtzl',25:'daddiu',
       32:'lb',33:'lh',34:'lwl',35:'lw',36:'lbu',37:'lhu',38:'lwr',40:'sb',41:'sh',42:'swl',43:'sw',
       46:'swr',47:'cache',49:'lwc1',57:'swc1'}
FCMP = {0x32:'c.eq.s',0x3c:'c.lt.s',0x3e:'c.le.s'}
FARITH = {0:'add.s',1:'sub.s',2:'mul.s',3:'div.s',4:'sqrt.s',5:'abs.s',6:'mov.s',7:'neg.s',13:'trunc.w.s'}

def decode(w):
    op = w >> 26
    d = dict(w=w, rs=(w>>21)&31, rt=(w>>16)&31, rd=(w>>11)&31, sa=(w>>6)&31,
             imm=sx16(w&0xffff), uimm=w&0xffff, target=(w&0x3ffffff)<<2,
             fs=(w>>11)&31, ft=(w>>16)&31, fd=(w>>6)&31, code=(w>>6)&0xfffff, op='?')
    if op == 0:
        f = w & 63
        d['op'] = SPECIAL.get(f, '?')
        if f == 0 and w == 0: d['op'] = 'nop'
    elif op == 1:
        d['op'] = REGIMM.get(d['rt'], '?')
    elif op == 16:   # COP0
        if d['rs'] == 0: d['op'] = 'mfc0'
        elif d['rs'] == 4: d['op'] = 'mtc0'
        elif d['rs'] & 16 and (w & 63) == 0x18: d['op'] = 'eret'
    elif op == 17:   # COP1
        rs = d['rs']
        if rs == 0: d['op'] = 'mfc1'
        elif rs == 2: d['op'] = 'cfc1'
        elif rs == 4: d['op'] = 'mtc1'
        elif rs == 6: d['op'] = 'ctc1'
        elif rs == 8: d['op'] = {0:'bc1f',1:'bc1t',2:'bc1fl',3:'bc1tl'}.get(d['rt'], '?')
        elif rs == 16:
            f = w & 63
            d['op'] = FARITH.get(f) or FCMP.get(f, '?')
        elif rs == 20 and (w & 63) == 32: d['op'] = 'cvt.s.w'
    elif op == 28:
        f = w & 63
        d['op'] = {0:'madd',2:'mul'}.get(f, '?')
    else:
        d['op'] = OPS.get(op, '?')
        if d['op'] == 'lui' and d['rs'] != 0: d['op'] = '?'   # lui richiede rs=0
    return d

BRANCH = {'beq','bne','blez','bgtz','bltz','bgez','beql','bnel','blezl','bgtzl','bltzl','bgezl',
          'bc1f','bc1t','bc1fl','bc1tl'}
LIKELY = {'beql','bnel','blezl','bgtzl','bltzl','bgezl','bc1fl','bc1tl'}
