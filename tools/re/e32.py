#!/usr/bin/env python3
"""Minimal reader for uncompressed Symbian E32 images (EPOC 6.x, N-Gage)."""
import struct


class E32:
    def __init__(self, path):
        self.b = open(path, 'rb').read()
        h = struct.unpack_from('<31I', self.b, 0)
        (self.uid1, self.uid2, self.uid3, self.check, self.sig, self.cpu, self.cksum_code,
         self.cksum_data, self.version, self.time_lo, self.time_hi, self.flags, self.code_size,
         self.data_size, self.heap_min, self.heap_max, self.stack, self.bss, self.entry,
         self.code_base, self.data_base, self.dllref_count, self.export_off, self.export_count,
         self.text_size, self.code_off, self.data_off, self.import_off, self.coderel_off,
         self.datarel_off, self.prio) = h
        if self.sig != 0x434F5045:
            raise ValueError('not an EPOC image')
        self.code = self.b[self.code_off:self.code_off + self.code_size]
        self.exports = [struct.unpack_from('<I', self.b, self.export_off + 4 * i)[0]
                        for i in range(self.export_count)]
        self.imports = []
        if self.import_off:
            p = self.import_off + 4
            for _ in range(self.dllref_count):
                name_off, n = struct.unpack_from('<Ii', self.b, p)
                p += 8
                name = self.b[self.import_off + name_off:].split(b'\0')[0].decode()
                ents = list(struct.unpack_from('<%dI' % n, self.b, p))
                p += 4 * n
                self.imports.append((name, ents))
        self.relocs = []
        if self.coderel_off:
            size, count = struct.unpack_from('<II', self.b, self.coderel_off)
            p, end = self.coderel_off + 8, self.coderel_off + 8 + size
            while p < end:
                page, bsize = struct.unpack_from('<II', self.b, p)
                for i in range((bsize - 8) // 2):
                    e = struct.unpack_from('<H', self.b, p + 8 + 2 * i)[0]
                    if e >> 12:
                        self.relocs.append((page + (e & 0xfff), e >> 12))
                p += bsize
