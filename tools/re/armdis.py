#!/usr/bin/env python3
"""ARMv5TE (ARM state) disassembler for 6RAH.APP, for the parts Ghidra
gets wrong or for quick looks without a Ghidra project. Literal pool loads
show their value, calls show the label and the first line of the port's
source that names the address.

usage: armdis.py <addr> [+count | -end] ...
env:   PK_RE_BIN    6rah.bin from mk_ghidra_input.py (default ./6rah.bin)
       PK_RE_LABELS 6rah.labels (optional, default next to the binary)
       PK_REPO      repository root for the source names (optional)
Everything stays on your own machine: the output is derived from your copy
of the game."""
import os, re, struct, sys
BIN = os.environ.get('PK_RE_BIN', '6rah.bin')
c = open(BIN, 'rb').read(); B = 0x10000000
lab = {}
lp = os.environ.get('PK_RE_LABELS', os.path.join(os.path.dirname(os.path.abspath(BIN)), '6rah.labels'))
if os.path.exists(lp):
    for l in open(lp):
        a, n = l.split()
        lab[int(a, 16)] = n
pname = {}
R = os.environ.get('PK_REPO', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
for dp, _, fs in os.walk(os.path.join(R, 'src')):
    for f in fs:
        for l in open(os.path.join(dp, f), errors='replace'):
            for m in re.findall(r'(?:FUN_|0x)(10[0-9a-f]{6})\b', l):
                pname.setdefault(int(m, 16), l.strip()[:70])
S = sorted(lab)
size = {}
COND = ['eq','ne','cs','cc','mi','pl','vs','vc','hi','ls','ge','lt','gt','le','','nv']
DP = ['and','eor','sub','rsb','add','adc','sbc','rsc','tst','teq','cmp','cmn','orr','mov','bic','mvn']
SH = ['lsl','lsr','asr','ror']
RN = ['r0','r1','r2','r3','r4','r5','r6','r7','r8','r9','r10','r11','r12','sp','lr','pc']
def w32(a): return struct.unpack_from('<I', c, a - B)[0]
def rot(x):
    v = x & 0xff; r = ((x >> 8) & 0xf) * 2
    return ((v >> r) | (v << (32 - r))) & 0xffffffff if r else v
def shifter(x):
    rm = RN[x & 15]; st = (x >> 5) & 3
    if x & 0x10:
        return '%s, %s %s' % (rm, SH[st], RN[(x >> 8) & 15])
    imm = (x >> 7) & 31
    if imm == 0 and st == 0: return rm
    if imm == 0 and st == 3: return rm + ', rrx'
    if imm == 0: imm = 32
    return '%s, %s #%d' % (rm, SH[st], imm)
def name(t):
    n = lab.get(t, '')
    if n.startswith('vf_') or n == '': n = 'FUN_%08x' % t
    return n
def rl(x):
    l = [RN[i] for i in range(16) if x & (1 << i)]
    return '{' + ','.join(l) + '}'
def dis(a):
    x = w32(a); cd = COND[x >> 28]; note = ''
    if x >> 28 == 0xf:
        if (x & 0x0e000000) == 0x0a000000:
            off = x & 0xffffff; off -= (1 << 24) if off & 0x800000 else 0
            t = a + 8 + off * 4 + (2 if x & 0x01000000 else 0)
            return x, 'blx %#x' % t, ''
        return x, '.word %#010x' % x, ''
    op = (x >> 25) & 7
    if (x & 0x0ffffff0) == 0x012fff10: return x, 'bx%s %s' % (cd, RN[x & 15]), ''
    if (x & 0x0ffffff0) == 0x012fff30: return x, 'blx%s %s' % (cd, RN[x & 15]), ''
    if (x & 0x0fff0ff0) == 0x016f0f10: return x, 'clz%s %s, %s' % (cd, RN[(x >> 12) & 15], RN[x & 15]), ''
    if (x & 0x0fc000f0) == 0x00000090:
        rd, rn, rs, rm = (x >> 16) & 15, (x >> 12) & 15, (x >> 8) & 15, x & 15
        s = 's' if x & 0x100000 else ''
        if x & 0x200000: return x, 'mla%s%s %s, %s, %s, %s' % (cd, s, RN[rd], RN[rm], RN[rs], RN[rn]), ''
        return x, 'mul%s%s %s, %s, %s' % (cd, s, RN[rd], RN[rm], RN[rs]), ''
    if (x & 0x0f8000f0) == 0x00800090:
        nm = ['umull','umlal','smull','smlal'][(x >> 21) & 3]
        return x, '%s%s %s, %s, %s, %s' % (nm, cd, RN[(x >> 12) & 15], RN[(x >> 16) & 15], RN[x & 15], RN[(x >> 8) & 15]), ''
    if (x & 0x0fb00ff0) == 0x01000090:
        return x, 'swp%s%s %s, %s, [%s]' % (cd, 'b' if x & 0x400000 else '', RN[(x >> 12) & 15], RN[x & 15], RN[(x >> 16) & 15]), ''
    if (x & 0x0e000090) == 0x00000090 and (x & 0x60):
        L = x & 0x100000; sh = (x >> 5) & 3
        nm = {1: 'h', 2: 'sb', 3: 'sh'}[sh]
        if not L and sh == 2: nm = 'd'  # ldrd
        rd, rn = RN[(x >> 12) & 15], RN[(x >> 16) & 15]
        U = '' if x & 0x800000 else '-'
        if x & 0x400000: off = '#%s%#x' % (U, ((x >> 4) & 0xf0) | (x & 15))
        else: off = '%s%s' % (U, RN[x & 15])
        P, Wb = x & 0x1000000, x & 0x200000
        addr = '[%s, %s]%s' % (rn, off, '!' if Wb else '') if P else '[%s], %s' % (rn, off)
        if (x >> 16) & 15 == 15 and x & 0x400000 and P:
            o = ((x >> 4) & 0xf0) | (x & 15); t = a + 8 + (o if U == '' else -o); note = '@%#x' % t
        return x, '%s%s%s %s, %s' % ('ldr' if L else 'str', cd, nm, rd, addr), note
    if (x & 0x0fb00000) == 0x01000000 and (x & 0xf0) == 0:
        return x, 'mrs%s %s, %s' % (cd, RN[(x >> 12) & 15], 'spsr' if x & 0x400000 else 'cpsr'), ''
    if (x & 0x0db00000) == 0x01200000 and (x & 0xf0) == 0 or (x & 0x0fb00000) == 0x03200000:
        return x, 'msr%s ...' % cd, ''
    if op in (0, 1):
        opc = (x >> 21) & 15; s = 's' if x & 0x100000 and opc not in (8, 9, 10, 11) else ''
        rd, rn = RN[(x >> 12) & 15], RN[(x >> 16) & 15]
        op2 = '#%#x' % rot(x & 0xfff) if op == 1 else shifter(x)
        if opc in (13, 15): t = '%s%s%s %s, %s' % (DP[opc], cd, s, rd, op2)
        elif opc in (8, 9, 10, 11): t = '%s%s %s, %s' % (DP[opc], cd, rn, op2)
        else: t = '%s%s%s %s, %s, %s' % (DP[opc], cd, s, rd, rn, op2)
        if op == 1 and (x >> 16) & 15 == 15 and opc in (2, 4):
            v = rot(x & 0xfff); note = '=%#x' % (a + 8 + (v if opc == 4 else -v))
        return x, t, note
    if op in (2, 3):
        L = x & 0x100000; bb = 'b' if x & 0x400000 else ''
        P, U, Wb = x & 0x1000000, x & 0x800000, x & 0x200000
        rd, rn = RN[(x >> 12) & 15], RN[(x >> 16) & 15]
        sg = '' if U else '-'
        if op == 2: off = '#%s%#x' % (sg, x & 0xfff)
        else: off = sg + shifter(x)
        t = 'b' if (not P and Wb) else ''
        addr = '[%s, %s]%s' % (rn, off, '!' if Wb else '') if P else '[%s], %s' % (rn, off)
        if op == 2 and (x >> 16) & 15 == 15 and P:
            la = a + 8 + ((x & 0xfff) if U else -(x & 0xfff))
            v = w32(la); note = '=%#x' % v
            if v in pname or (B <= v < B + len(c) and v in lab): note += ' ' + name(v)
        return x, '%s%s%s%s %s, %s' % ('ldr' if L else 'str', cd, bb, t, rd, addr), note
    if op == 4:
        L = x & 0x100000; P, U = (x >> 24) & 1, (x >> 23) & 1
        mode = ['da','ia','db','ib'][U | (P << 1)]
        rn = RN[(x >> 16) & 15]
        return x, '%s%s%s %s%s, %s%s' % ('ldm' if L else 'stm', cd, mode, rn, '!' if x & 0x200000 else '', rl(x & 0xffff), '^' if x & 0x400000 else ''), ''
    if op == 5:
        off = x & 0xffffff; off -= (1 << 24) if off & 0x800000 else 0
        t = a + 8 + off * 4
        nm = name(t); note = pname.get(t, '')
        return x, '%s%s %s' % ('bl' if x & 0x1000000 else 'b', cd, nm if x & 0x1000000 else '%#x' % t), note[:60]
    if (x & 0x0f000000) == 0x0f000000: return x, 'swi%s %#x' % (cd, x & 0xffffff), ''
    return x, '.word %#010x' % x, ''
def run(a, e):
    for p in range(a, e, 4):
        if p in lab and p != a: print('%08x <%s>' % (p, name(p)))
        x, t, n = dis(p)
        print('%08x: %08x  %-40s %s' % (p, x, t, ('; ' + n) if n else ''))
if __name__ == '__main__':
    args = sys.argv[1:]; i = 0
    while i < len(args):
        a = int(args[i], 16)
        if i + 1 < len(args) and args[i+1].startswith('+'):
            e = a + 4 * int(args[i+1][1:]); i += 2
        elif i + 1 < len(args) and args[i+1].startswith('-'):
            e = int(args[i+1][1:], 16); i += 2
        else:
            e = a + size.get(a, 64); i += 1
        print('=== %08x %s' % (a, pname.get(a, '')))
        run(a, e)
