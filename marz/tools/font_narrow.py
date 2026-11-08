# Gives the full-width Latin glyphs (U+FF01-FF5E, and EXTRA) of the game's PFont fonts proportional widths, so English
# text drawn with them is as narrow as its ink, and blanks U+FFE3 to serve as the space.
# Patches the font files in place inside a CVM (same size), using the listing from cvm_extract.py --list.
#   python3 font_narrow.py <CVM> <cvm_list.txt>
import struct
import sys

SPACE = 0xFFE3
SPACE_ADVANCE = 5
# Punctuation the full-width range has no glyph for (" ' - ~), plus dashes and curly quotes for English prose.
EXTRA = [0x2010, 0x2015, 0x2018, 0x2019, 0x201C, 0x201D, 0x2212, 0x301C]
FONTS = ('SCE20I22_NO20.PF', 'SCE20I22_NO24.PF', 'SCE20I22_NO25.PF', 'SCE20I22_NO26.PF', 'SCE22.PF')


def glyph_index(d, b, code):
    count, ranges = struct.unpack_from('<2I', d, b + 0x30)
    index_table = struct.unpack_from('<I', d, b + 0x3C)[0]
    for i in range(count):
        start, end, base, _ = struct.unpack_from('<4I', d, b + ranges + i * 16)
        if start <= code <= end:
            return struct.unpack_from('<H', d, b + index_table + (base + code - start) * 2)[0]
    return None


def patch(d, b):
    cell_w, cell_h = struct.unpack_from('<2H', d, b + 0x1C)
    stride = (cell_w + 1) // 2
    bitmaps = struct.unpack_from('<I', d, b + 0x2C)[0]
    metrics = struct.unpack_from('<I', d, b + 0x44)[0]
    glyph_size = 256
    for code in list(range(0xFF01, 0xFF5F)) + EXTRA:
        g = glyph_index(d, b, code)
        if not g:
            continue
        bm = b + bitmaps + g * glyph_size
        cols = [x for x in range(cell_w)
                if any((d[bm + y * stride + x // 2] >> (4 * (x & 1))) & 0xF for y in range(cell_h))]
        if not cols:
            continue
        a, w = cols[0], cols[-1] - cols[0] + 1
        m = b + metrics + g * 16
        f = list(struct.unpack_from('<8h', d, m))
        # Texture column = f[0] + local x; the quad spans local f[2]-1..f[3]+1; the advance is f[6] (+ pitch).
        f[0], f[2], f[3], f[6] = a - 1, 1, w + 1, w
        struct.pack_into('<8h', d, m, *f)
    g = glyph_index(d, b, SPACE)
    bm = b + bitmaps + g * glyph_size
    d[bm:bm + glyph_size] = bytes(glyph_size)
    m = b + metrics + g * 16
    f = list(struct.unpack_from('<8h', d, m))
    f[0], f[2], f[3], f[6] = 1, 1, 1, SPACE_ADVANCE
    struct.pack_into('<8h', d, m, *f)


def main():
    cvm, listing = sys.argv[1], sys.argv[2]
    where = {}
    for line in open(listing):
        p = line.split()
        if len(p) == 5 and p[1] == 'lba':
            where[p[0]] = (int(p[2]), int(p[4]))
    with open(cvm, 'r+b') as f:
        for name in FONTS:
            lba, size = where[name]
            f.seek(0x1800 + lba * 0x800)
            d = bytearray(f.read(size))
            patch(d, 0x70)
            f.seek(0x1800 + lba * 0x800)
            f.write(d)
            print('patched', name)


if __name__ == '__main__':
    main()
