# Collects the Dramatic Mode text for translation into one JSON file (game material: keep it under build/).
#   python3 text_dump.py <extracted CVM dir> <ELF> <out.json>
# Units:
#   msd  a run of consecutive text records in the MSD_E/M/S*.BIN files (48-byte Shift-JIS field each, records 64
#        bytes apart in MSD_E, 60 in MSD_M and MSD_S); the translation keeps the same number of lines, each at most
#        48 bytes. Identical runs share one unit. MSD_TEST*.BIN holds no text records (file names and data only).
#   db   a DB_*.TXT file (first line: the number of text lines that follow).
#   elf  a string in the ELF data (battle chatter, tutorial, mission orders, database titles); in place, at most
#        'max' bytes.
import glob
import json
import os
import re
import sys

MSD_FIELD = 48
# (file prefix, offset of the first text field, record stride, last file number holding text).
# MSD_E101 and later hold leftover program data (debug strings), not text. MSD_E comes first so its unit ids
# stay as they were before MSD_M and MSD_S were added.
MSD_SETS = [('MSD_E', 28, 64, 100), ('MSD_M', 32, 60, None), ('MSD_S', 28, 60, None)]

# ELF strings (file offsets): table ranges, and how many bytes each string may use (None: the room up to the
# next string).
ELF_TABLES = [
    (0x5033C0, 0x503A00, 57),    # mission orders / success conditions (64-byte slots)
    (0x5413D0, 0x543600, 49),    # database category and entry titles (50-byte slots)
    (0x5440F0, 0x544160, 63),    # database messages
    (0x59B650, 0x59B6F8, None),  # episode names (16/48-byte slots)
    (0x59B940, 0x5AA5D0, 255),   # battle chatter (12-byte header + 256-byte text, 0x10C apart)
    (0x5AEB60, 0x5B27C0, 103),   # tutorial lines (0x70 apart)
    (0x575490, 0x575510, 31),    # controller settings help: the sticks work like the direction keys (OP_PadSetting)
    (0x582E00, 0x582E40, 63),    # memory card overwrite warning (MS_Menu)
]
# Never translate the icon.sys title at 0x5758C0: FL_CheckFiles compares the whole icon.sys it builds with the one
# on the card (0x36cc08), so a changed title makes every existing save fail the check.
SJIS = rb'(?:[\x81-\x9f\xe0-\xfc][\x40-\xfc]|[\x20-\x7e\n])+'
DOUBLE = rb'[\x81-\x9f\xe0-\xfc][\x40-\xfc]'
JAPANESE = '[ぁ-んァ-ヶ一-龥＀-￯]'


def file_number(name):
    return int(re.findall(r'\d+', name)[0])


def is_text(line):
    return re.search(JAPANESE, line) and sum(c < '\x80' for c in line) * 2 < len(line)


def msd_units(src):
    units = {}
    for prefix, first, stride, last in MSD_SETS:
        paths = sorted(glob.glob(os.path.join(src, prefix + '[0-9]*.BIN')), key=lambda p: file_number(os.path.basename(p)))
        for path in paths:
            name = os.path.basename(path)
            if last is not None and file_number(name) > last:
                continue
            msd_file_units(units, name, open(path, 'rb').read(), first, stride)
    out = []
    for u in units.values():
        u['id'] = 'msd%04d' % len(out)
        out.append(u)
    return out


def msd_file_units(units, name, d, first, stride):
    run = []
    for off in range(first, len(d) - MSD_FIELD + 1, stride):
        t = d[off:off + MSD_FIELD].split(b'\0')[0]
        line = None
        if t and re.fullmatch(SJIS, t):
            try:
                line = t.decode('cp932')
            except UnicodeDecodeError:
                pass
        if line and is_text(line):
            run.append((off, line))
            continue
        if run:
            key = '\n'.join(x[1] for x in run)
            u = units.setdefault(key, {'kind': 'msd', 'jp': [x[1] for x in run], 'max': MSD_FIELD, 'at': []})
            u['at'].append([name, [x[0] for x in run]])
            run = []


def db_units(src):
    out = []
    for path in sorted(glob.glob(os.path.join(src, 'DB_*.TXT'))):
        out.append({'kind': 'db', 'id': os.path.basename(path), 'jp': open(path, 'rb').read().decode('cp932')})
    return out


def elf_units(elf):
    d = open(elf, 'rb').read()
    out = []
    for start, end, cap in ELF_TABLES:
        for m in re.compile(rb'(?<=\x00)' + SJIS + rb'(?=\x00)').finditer(d, start, end):
            s = m.group()
            if not re.search(DOUBLE, s):
                continue
            off = m.start()
            room = off + len(s)
            while room < len(d) and d[room] == 0:
                room += 1
            room -= off + 1
            out.append({'kind': 'elf', 'id': 'elf%06x' % off, 'jp': s.decode('cp932'),
                        'max': min(room, cap) if cap else room})
    return out


def main():
    src, elf, dst = sys.argv[1:4]
    units = msd_units(src) + db_units(src) + elf_units(elf)
    json.dump(units, open(dst, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    for k in ('msd', 'db', 'elf'):
        print(k, sum(1 for u in units if u['kind'] == k))


main()
