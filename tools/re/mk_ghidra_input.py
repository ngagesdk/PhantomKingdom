#!/usr/bin/env python3
"""Turn 6RAH.APP into inputs for Ghidra's headless analyzer.

usage: mk_ghidra_input.py <6rah.app> <workdir>
writes: 6rah.bin (code at 0x10000000), 6rah.labels (entry, exports,
import thunks named DLL_ordinal, likely virtual functions from relocated
pointers). Everything is written to <workdir>, which must stay outside
the repository: the files are derived from the game.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from e32 import E32  # noqa: E402


def main():
    e = E32(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    base = e.code_base
    c = e.code
    open(os.path.join(out, '6rah.bin'), 'wb').write(c)
    labels = [(e.entry, 'entry')]
    labels += [(a - base if a >= base else a, 'exp%02d' % (i + 1)) for i, a in enumerate(e.exports)]
    iat, idx = {}, 0
    for dll, ents in e.imports:
        short = dll.split('[')[0].split('.')[0].upper()
        for o in ents:
            iat[e.text_size + 4 * idx] = '%s_%d' % (short, o)
            idx += 1
    for i in range(0, e.text_size - 12, 4):
        w = struct.unpack_from('<4I', c, i)
        if w[:3] == (0xe59fc004, 0xe59cc000, 0xe12fff1c) and (w[3] - base) in iat:
            labels.append((i, iat[w[3] - base]))
    have = {a for a, _ in labels}
    for off, _t in e.relocs:
        if off >= len(c) - 4:
            continue
        w = struct.unpack_from('<I', c, off)[0] - base
        if 0 <= w < e.text_size and w % 4 == 0 and w not in have:
            ins = struct.unpack_from('<I', c, w)[0]
            if (ins >> 16) == 0xe92d or ins == 0xe12fff1e or (ins & 0xfff00000) in (
                    0xe3a00000, 0xe5900000, 0xe1a00000, 0xe59f0000, 0xe1d00000):
                labels.append((w, 'vf_%x' % w))
                have.add(w)
    with open(os.path.join(out, '6rah.labels'), 'w') as f:
        for a, n in labels:
            f.write('%x %s\n' % (a + base, n))
    print('uid3=%#x code checksum=%#x, %d labels' % (e.uid3, e.cksum_code, len(labels)))


if __name__ == '__main__':
    main()
