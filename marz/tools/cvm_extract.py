#!/usr/bin/env python3
"""Descrambles and extracts a CRI ROFS CVM (ROFSVO4.CVM) with the cipher in the game's own ELF.

The CVM is a CVMH header followed by an ISO9660 image whose file system sectors (volume descriptors and
directories) are scrambled; file data is stored as is (the game only descrambles directory reads). Following the game's
ROFS code (RFMAI_Decipher -> RSU_Decipher -> rsu_Encipher, SKG_GenerateKey, rsu_MkFinalT0..8):
- fixed key: header bytes 0x25,0x20,0x26,0x21,... (8 bytes) when the BE32 at 0x30 has bit 0x10, then each
  16-bit pair p is replaced by SKG_GenerateKey(p)[0] (big-endian);
- per sector s (a 2 KB sector of the image), w = fixed[5]; for every 8-byte block:
  v = w * s; (k1, k2, k3) = SKG_GenerateKey(BE32(v * 0x100001)); n = k1 % 9; vari = BE16(k2) BE16(k3);
  final = rsu_MkFinalT<n>(fixed, vari); w = n + block offset; each byte b ^= final[i], w *= final[i].
SKG_GenerateKey(bytes) runs three chains over a 1024-entry table: c = T[256|512|768]; c = T[(c * T[128 + b]) % 1024].
The table and the nine byte mixers are read from the ELF (nothing from the game is in this file).

Usage: python3 cvm_extract.py <ELF> <CVM> <output dir> [--list] [name part ...]
Lists the files with --list, and extracts every file whose name contains one of the given parts
(case-insensitive) into <output dir>. Extracted files are game material: keep them out of the repository.
"""
import os
import struct
import sys

SKG_TABLE = 0x681210      # 1024 x int16 (SKG_GenerateKey)
FINAL_KEY_TABLE = 0x5F3D70  # 9 pointers to rsu_MkFinalT0..8
M32 = 0xFFFFFFFF


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


class Elf:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        ph, n = struct.unpack_from('<I', self.data, 0x1C)[0], struct.unpack_from('<H', self.data, 0x2C)[0]
        self.segs = [struct.unpack_from('<8I', self.data, ph + i * 32) for i in range(n)]

    def off(self, va):
        for t, o, v, _, fs, _, _, _ in self.segs:
            if t == 1 and v <= va < v + fs:
                return o + va - v
        raise SystemExit('VA %#x not in the ELF' % va)

    def words(self, va, n):
        return struct.unpack_from('<%dI' % n, self.data, self.off(va))


def read_mixer(elf, va):
    """rsu_MkFinalT<n>(fixed=$a0, vari=$a1, out=$a2): each out[i] is a byte of fixed or vari, or two of them
    combined with xor/addu/subu (kept to 8 bits by sb). Returns 8 expressions: ('f'|'v', index) or (op, a, b)."""
    regs, out = {}, [None] * 8
    ops = {0x26: 'xor', 0x21: 'add', 0x23: 'sub'}
    for w in elf.words(va, 32):
        op, funct = w >> 26, w & 0x3F
        rs, rt, rd, imm = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, w & 0xFFFF
        if op == 0x24:  # lbu rt, imm(rs)
            assert rs in (4, 5)
            regs[rt] = ('fv'[rs - 4], imm)
        elif op == 0 and funct in ops:  # xor/addu/subu rd, rs, rt
            regs[rd] = (ops[funct], regs[rs], regs[rt])
        elif op == 0x28:  # sb rt, imm(rs)
            assert rs == 6
            out[imm] = regs[rt]
        elif op == 0 and funct == 0x08:  # jr ra: the delay slot follows
            pass
        elif w != 0:
            raise SystemExit('unexpected instruction %08x in the mixer at %#x' % (w, va))
        if all(o is not None for o in out):
            return out
    raise SystemExit('could not read the mixer at %#x' % va)


def evaluate(expr, fixed, vari):
    if expr[0] in ('f', 'v'):
        return (fixed if expr[0] == 'f' else vari)[expr[1]]
    a, b = evaluate(expr[1], fixed, vari), evaluate(expr[2], fixed, vari)
    return {'xor': a ^ b, 'add': a + b, 'sub': a - b}[expr[0]] & 0xFF


class Cipher:
    def __init__(self, elf, header):
        self.T = struct.unpack_from('<1024h', elf.data, elf.off(SKG_TABLE))
        self.mixers = [read_mixer(elf, va) for va in elf.words(FINAL_KEY_TABLE, 9)]
        flags = struct.unpack_from('>I', header, 0x30)[0]
        if not flags & 0x10:
            raise SystemExit('CVM header says the image is not scrambled')
        a, b = header[0x25:0x29], header[0x20:0x24]
        key = bytearray(x for pair in zip(a, b) for x in pair)
        for k in range(0, 8, 2):
            struct.pack_into('>H', key, k, self.generate(key[k:k + 2])[0] & 0xFFFF)
        self.fixed = bytes(key)

    def generate(self, data):
        T, res = self.T, []
        for start in (256, 512, 768):
            c = T[start]
            for b in data:
                x = s32(c * T[128 + (b - 256 if b & 0x80 else b)])
                c = T[x - int(x / 1024) * 1024]
            res.append(c)
        return res

    def sector(self, buf, sct):
        out = bytearray(buf)
        w = self.fixed[5]
        for j in range(0, len(out), 8):
            v = (w * sct) & M32
            k1, k2, k3 = self.generate(struct.pack('>I', (v + (v << 20)) & M32))
            n = k1 % 9
            vari = struct.pack('>HH', k2 & 0xFFFF, k3 & 0xFFFF)
            final = [evaluate(e, self.fixed, vari) for e in self.mixers[n]]
            w = n + j
            for i in range(8):
                out[j + i] ^= final[i]
                w = (w * final[i]) & M32
        return bytes(out)


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    elf, cvm_path, outdir = Elf(sys.argv[1]), sys.argv[2], sys.argv[3]
    f = open(cvm_path, 'rb')
    header = f.read(0x800)
    if header[:4] != b'CVMH':
        raise SystemExit('not a CVM')
    cipher = Cipher(elf, header)
    base = 0x1800  # the ISO9660 image follows the CVM header sectors
    def read(lba, count=1):
        f.seek(base + lba * 0x800)
        data = f.read(count * 0x800)
        return b''.join(cipher.sector(data[i:i + 0x800], lba + i // 0x800) for i in range(0, len(data), 0x800))
    pvd = read(16)
    if pvd[1:6] != b'CD001':
        raise SystemExit('descrambling did not produce an ISO9660 descriptor')
    files = []

    def walk(lba, size, path):
        data = read(lba, (size + 0x7FF) // 0x800)[:size]
        i = 0
        while i < len(data):
            n = data[i]
            if n == 0:
                i = (i // 0x800 + 1) * 0x800
                continue
            rec = data[i:i + n]
            i += n
            elba, esize, flags, nl = struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0], rec[25], rec[32]
            name = rec[33:33 + nl]
            if name in (b'\x00', b'\x01'):
                continue
            name = name.decode('latin-1').split(';')[0]
            if flags & 2:
                walk(elba, esize, path + name + '/')
            else:
                files.append((path + name, elba, esize))

    root = pvd[156:156 + 34]
    walk(struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0], '')
    print(len(files), 'files')
    wanted = [a.lower() for a in sys.argv[4:] if not a.startswith('--')]
    if '--list' in sys.argv:
        for name, lba, size in files:
            print('%-40s lba %7d size %9d' % (name, lba, size))
    for name, lba, size in files:
        if not any(w in name.lower() for w in wanted):
            continue
        out = os.path.join(outdir, name)
        os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
        # Only the file system is scrambled: the game descrambles directory sectors alone, file data is stored as is.
        f.seek(base + lba * 0x800)
        open(out, 'wb').write(f.read(size))
        print('extracted', name, size)
    return 0


if __name__ == '__main__':
    sys.exit(main())
