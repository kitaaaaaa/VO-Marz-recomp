#!/usr/bin/env python3
"""Copies every file on the Virtual-On Marz disc image into a directory, keeping the disc's layout.

Usage: extract_iso.py <disc.iso> <output dir>
The image is only ever read. Existing files of the right size are left alone.
"""
import os
import struct
import sys

SECTOR = 2048
CHUNK = 16 * 1024 * 1024


def walk(iso, lba, size, path, out):
    iso.seek(lba * SECTOR)
    data = iso.read(size)
    off = 0
    while off < len(data):
        length = data[off]
        if length == 0:
            off = (off // SECTOR + 1) * SECTOR
            continue
        rec = data[off:off + length]
        off += length
        name = rec[33:33 + rec[32]]
        if name in (b'\x00', b'\x01'):
            continue
        name = name.decode('ascii').split(';')[0]
        e_lba = struct.unpack('<I', rec[2:6])[0]
        e_size = struct.unpack('<I', rec[10:14])[0]
        full = f'{path}/{name}'
        if rec[25] & 2:
            walk(iso, e_lba, e_size, full, out)
        else:
            out.append((full.lstrip('/'), e_lba, e_size))


def main():
    iso_path, out_dir = sys.argv[1:3]
    with open(iso_path, 'rb') as iso:
        iso.seek(16 * SECTOR)
        pvd = iso.read(SECTOR)
        root = pvd[156:190]
        entries = []
        walk(iso, struct.unpack('<I', root[2:6])[0], struct.unpack('<I', root[10:14])[0], '', entries)
        for rel, lba, size in entries:
            dest = os.path.join(out_dir, rel)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            if os.path.exists(dest) and os.path.getsize(dest) == size:
                continue
            iso.seek(lba * SECTOR)
            with open(dest, 'wb') as f:
                left = size
                while left:
                    buf = iso.read(min(CHUNK, left))
                    f.write(buf)
                    left -= len(buf)
            print(rel, size)
    print('files:', len(entries))


if __name__ == '__main__':
    main()
