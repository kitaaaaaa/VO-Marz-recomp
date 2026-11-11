# Writes translations into a copy of the game folder (never the original): MSD_E*.BIN records and DB_*.TXT
# files inside ROFSVO4.CVM, and strings in the ELF.
#   python3 text_insert.py <jp.json> <cvm_list.txt> <game dir> <translation.json> [...]
# DB files may grow into the free part of their last sector; their size in the (scrambled) ISO9660 directory
# record is updated to match.
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from text_check import check, encode, normalize  # noqa: E402

BASE = 0x1800  # the ISO9660 image follows the CVM header sectors
SECTOR = 0x800

# Pieces MS_MakeCommandText_ReservedTime / _EnemyMax (0x450310 / 0x450190) write into the mission screen
# templates at byte 10: full-width digits, then a 4-byte suffix copied without a terminator, or a word in place
# of the number. ELF file offset -> (Japanese, English), English no longer than the Japanese slot.
ELF_FIXED = {
    0x5C8A98: ('秒', ' sec'),     # after the time limit digits
    0x5C8A90: ('機', ' VRs'),     # after the enemy count digits
    0x503A70: ('無制限', 'None'),  # time limit: none
    0x5C8A88: ('不明', '???'),    # enemy count: unknown (99 or more)
    0x58B3E0: ('　', '￣'),    # leading-zero blank: U+3000 has no glyph; U+FFE3 is the blank (font_narrow.py)
}


def load_cipher(elf_path, f):
    import importlib.util
    spec = importlib.util.spec_from_file_location('cvm_extract', os.path.join(os.path.dirname(__file__), 'cvm_extract.py'))
    cvm = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(cvm)
    f.seek(0)
    return cvm.Cipher(cvm.Elf(elf_path), f.read(SECTOR))


def directory_records(f, cipher):
    """Yields (sector lba, offset in sector, record bytes) for every file record, descrambled."""
    def read(lba):
        f.seek(BASE + lba * SECTOR)
        return cipher.sector(f.read(SECTOR), lba)
    pvd = read(16)
    root = pvd[156:156 + 34]
    todo = [(struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0])]
    while todo:
        lba0, size = todo.pop()
        for s in range((size + SECTOR - 1) // SECTOR):
            data = read(lba0 + s)
            i = 0
            while i < SECTOR and data[i]:
                n = data[i]
                rec = data[i:i + n]
                name = rec[33:33 + rec[32]]
                if name not in (b'\x00', b'\x01'):
                    if rec[25] & 2:
                        todo.append((struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]))
                    else:
                        yield lba0 + s, i, rec
                i += n


def set_sizes(f, cipher, sizes):
    by_sector = {}
    for lba, off, rec in directory_records(f, cipher):
        name = rec[33:33 + rec[32]].decode('latin-1').split(';')[0]
        if name in sizes:
            by_sector.setdefault(lba, []).append((off, sizes[name]))
    for lba, items in by_sector.items():
        f.seek(BASE + lba * SECTOR)
        data = bytearray(cipher.sector(f.read(SECTOR), lba))
        for off, size in items:
            struct.pack_into('<I', data, off + 10, size)
            struct.pack_into('>I', data, off + 14, size)
        f.seek(BASE + lba * SECTOR)
        f.write(cipher.sector(bytes(data), lba))
    return sum(len(v) for v in by_sector.values())


def main():
    jp_path, list_path, game = sys.argv[1:4]
    units = {u['id']: u for u in json.load(open(jp_path, encoding='utf-8'))}
    caps = {}
    files = {}
    for line in open(list_path):
        p = line.split()
        if len(p) == 5 and p[1] == 'lba':
            files[p[0]] = (int(p[2]), int(p[4]))
    order = sorted(files.values())
    for name, (lba, size) in files.items():
        nxt = next(l for l, _ in order if l > lba) if any(l > lba for l, _ in order) else lba + (size + SECTOR - 1) // SECTOR
        caps[name] = (nxt - lba) * SECTOR
    tr = {}
    for path in sys.argv[4:]:
        tr.update(json.load(open(path, encoding='utf-8')))
    bad = [(uid, e) for uid, en in tr.items() for e in check(units[uid], en, caps.get(uid, 0))]
    if bad:
        for uid, e in bad:
            print(uid, ':', e)
        raise SystemExit('fix the problems above first')

    elf_path = os.path.join(game, 'SLPM_653.03')
    cvm_path = os.path.join(game, 'ROFSVO4.CVM')
    counts = {'msd': 0, 'db': 0, 'elf': 0}
    sizes = {}
    with open(cvm_path, 'r+b') as cvm, open(elf_path, 'r+b') as elf:
        for uid, en in tr.items():
            u = units[uid]
            if u['kind'] == 'msd':
                for name, offsets in u['at']:
                    lba = files[name][0]
                    for off, line in zip(offsets, en):
                        cvm.seek(BASE + lba * SECTOR + off)
                        cvm.write(encode(line).ljust(u['max'], b'\0'))
            elif u['kind'] == 'db':
                lines = normalize(en).rstrip('\n').split('\n')
                body = ('\r\n'.join([str(len(lines))] + lines) + '\r\n').encode('cp932')
                lba, old = files[uid]
                cvm.seek(BASE + lba * SECTOR)
                cvm.write(body.ljust(max(old, len(body)), b'\0'))
                sizes[uid] = len(body)
            else:
                off = int(uid[3:], 16)
                old = len(u['jp'].encode('cp932'))
                b = encode(en)
                elf.seek(off)
                # Out to the slot's end, so a longer earlier translation leaves nothing behind.
                elf.write(b.ljust(max(old, u['max']), b'\0'))
            counts[u['kind']] += 1
        for off, (jp, en) in ELF_FIXED.items():
            elf.seek(off)
            old = elf.read(len(jp.encode('cp932')))
            if old not in (jp.encode('cp932'), en.encode('cp932').ljust(len(old), b'\0')[:len(old)]):
                raise SystemExit('unexpected bytes at %#x' % off)
            elf.seek(off)
            elf.write(en.encode('cp932').ljust(len(jp.encode('cp932')), b'\0'))
        if sizes:
            n = set_sizes(cvm, load_cipher(elf_path, cvm), sizes)
            if n != len(sizes):
                raise SystemExit('updated %d directory records for %d DB files' % (n, len(sizes)))
    print('written:', counts)


main()
