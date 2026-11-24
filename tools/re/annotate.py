#!/usr/bin/env python3
"""Annotate Ghidra output: DAT_xxx literal loads get their value and, for
pointers into the image, the string they point to.
usage: annotate.py in.c out.c [6rah.bin]"""
import re, struct, sys
c = open(sys.argv[3] if len(sys.argv) > 3 else '6rah.bin', 'rb').read(); B = 0x10000000
def s_at(off):
    if off < 0 or off >= len(c): return None
    # TLitC16: length word then utf16
    if off + 4 < len(c):
        ln = struct.unpack_from('<I', c, off)[0]
        if 0 < ln < 200 and off+4+2*ln <= len(c):
            t = c[off+4:off+4+2*ln]
            if all(t[2*i+1]==0 and (32<=t[2*i]<127) for i in range(ln)): return 'L16"'+t.decode('utf-16le')+'"'
        if 0 < ln < 200 and off+4+ln <= len(c):
            t = c[off+4:off+4+ln]
            if all(32<=x<127 for x in t): return 'L8"'+t.decode()+'"'
    t = c[off:off+120]
    if len(t) > 6 and t[1] == 0:
        w = t.decode('utf-16le', 'ignore').split('\0')[0]
        if len(w) >= 3 and all(32 <= ord(ch) < 127 for ch in w): return 'W"'+w+'"'
    a = t.split(b'\0')[0]
    if len(a) >= 4 and all(32<=x<127 for x in a): return '"'+a.decode()+'"'
    return None
def rep(m):
    name, a = m.group(1), int(m.group(2), 16)
    off = a - B
    if not (0 <= off < len(c)-3): return m.group(0)
    v = struct.unpack_from('<I', c, off)[0]
    note = '0x%x' % v
    if name == 'DAT' and B <= v < B+len(c):
        s = s_at(v - B)
        if s: note += ' ' + s
    elif name == 'PTR_DAT':
        pass
    return m.group(0) + '/*' + note + '*/'
src = open(sys.argv[1]).read()
src = re.sub(r'\b(DAT|PTR_DAT)_([0-9a-f]{8})\b(?!/\*)', rep, src)
open(sys.argv[2], 'w').write(src)
