#!/usr/bin/env python3
"""Adds entry records to functions.csv for code addresses that data words point at.

A computed jump can only reach an address the recompiler made an entry for. SLPM_653.03 kept its
relocations, so every jump table word and code pointer in data is known exactly: each absolute 32-bit
relocation applied outside a function whose value lands inside one is a jump target. Ghidra's switch
analysis finds most of them, not all, and a missed one stops the thread at run time.

Usage: add_reloc_targets.py <elf> <functions.csv>     (rewrites the csv, safe to run again)
"""
import bisect
import struct
import sys

R_MIPS_32 = 2

elf_path, csv_path = sys.argv[1:3]
d = open(elf_path, 'rb').read()
shoff = struct.unpack_from('<I', d, 32)[0]
shentsize, shnum = struct.unpack_from('<2H', d, 46)
secs = [struct.unpack_from('<10I', d, shoff + i * shentsize) for i in range(shnum)]
symtab = next(s for s in secs if s[1] == 2)
rel = next(s for s in secs if s[1] == 9)
main = next(s for s in secs if s[1] == 1 and s[3] == 0x100000)


def word_at(va):
    return struct.unpack_from('<I', d, va - main[3] + main[4])[0]


ranges = []
for i in range(symtab[5] // 16):
    _, value, size, info, _, _ = struct.unpack_from('<IIIBBH', d, symtab[4] + i * 16)
    if info & 15 == 2 and size:
        ranges.append((value, value + size))
ranges.sort()
starts = [r[0] for r in ranges]


def in_function(va):
    i = bisect.bisect_right(starts, va) - 1
    return i >= 0 and va < ranges[i][1]


targets = set()
for i in range(rel[5] // 8):
    offset, info = struct.unpack_from('<II', d, rel[4] + i * 8)
    if info & 0xff != R_MIPS_32 or not main[3] <= offset < main[3] + main[5] - 3 or in_function(offset):
        continue
    value = word_at(offset)
    if value & 3 == 0 and in_function(value):
        targets.add(value)

with open(csv_path, encoding='utf-8', newline='') as f:
    lines = f.read().splitlines()
header, records = lines[0], []
for line in lines[1:]:
    name, start, end, size = line.rsplit(',', 3)
    records.append([name, int(start, 16), int(end, 16)])
known = {r[1] for r in records}
missing = sorted(targets - known)

# Entry records partition a function, so a new one takes the tail of the record it lands in.
entries = sorted((r for r in records if r[0].startswith('entry_')), key=lambda r: r[1])
functions = sorted((r for r in records if not r[0].startswith('entry_')), key=lambda r: r[1])
entry_starts = [r[1] for r in entries]
function_starts = [r[1] for r in functions]
added = []
for target in missing:
    i = bisect.bisect_right(entry_starts, target) - 1
    if i >= 0 and target < entries[i][2]:
        end = entries[i][2]
        entries[i][2] = target
    else:
        j = bisect.bisect_right(function_starts, target) - 1
        end = functions[j][2]
    record = [f'entry_{target:08x}', target, end]
    added.append(record)
    entries.insert(i + 1, record)
    entry_starts.insert(i + 1, target)

records.extend(added)
records.sort(key=lambda r: (r[1], r[0].startswith('entry_')))
with open(csv_path, 'w', encoding='utf-8', newline='\n') as f:
    f.write(header + '\n')
    for name, start, end in records:
        f.write(f'{name},0x{start:08X},0x{end:08X},{end - start}\n')
print(f'data pointers into code: {len(targets)}, already entries: {len(targets) - len(missing)}, added: {len(added)}')
print('sample:', [hex(a[1]) for a in added[:12]])
