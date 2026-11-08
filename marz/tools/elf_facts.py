#!/usr/bin/env python3
"""Prints the facts about SLUS_206.74 the port's config needs: compiler note, gp, VU1 uploads."""
import struct
import sys

d = open(sys.argv[1], 'rb').read()
BASE = 0x100000 - 0x80
END = 0x80 + 0x5db580
print('comment:', d[0x5db62d:0x5db658])
print('reginfo gp:', hex(struct.unpack_from('<I', d, 0x5db658 + 20)[0]))

# Entry stub: find "lui gp / addiu gp" as the crt0 sets it.
for o in range(0x88, 0x88 + 0x100, 4):
    w = struct.unpack_from('<I', d, o)[0]
    print(f'{o + BASE:#x}: {w:08x}')
    if (w >> 16) == 0x03e0:
        break

# MPG vifcode followed by plausible micro code: every pair's upper word has a valid top bit pattern
# and the block ends in an E-bit (upper bit 30) within the upload when it is a complete program.
hits = []
for o in range(0x80, END - 8, 4):
    w = struct.unpack_from('<I', d, o)[0]
    if (w >> 24) & 0x7f != 0x4a:
        continue
    num = (w >> 16) & 0xff or 256
    addr = w & 0xffff
    if num < 16 or addr >= 0x800:
        continue
    body = d[o + 4:o + 4 + num * 8]
    if len(body) < num * 8:
        continue
    ebit = 0
    nops = 0
    for p in range(0, len(body), 8):
        lower, upper = struct.unpack_from('<II', body, p)
        if upper & 0x40000000:
            ebit += 1
        if upper & 0x7ff == 0x2ff:
            nops += 1
    if nops * 4 < num:  # real code has plenty of upper NOPs
        continue
    hits.append((o + BASE, num, addr, ebit, nops))
print('VIF MPG uploads that look like code:', len(hits))
for va, num, addr, ebit, nops in hits:
    print(f'  va={va:#x} pairs={num} vuaddr={addr:#x} ebits={ebit} upper_nops={nops}')
