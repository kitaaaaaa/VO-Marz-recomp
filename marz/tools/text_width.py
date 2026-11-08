# Line widths in pixels as the game's PFont draws them (_scePFont_Putc: each glyph advances by its metrics
# field 6, plus the 2.5 pitch FontInit sets), for the original Japanese and for the narrowed English font.
import struct

PITCH = 2.5
FONT = 0x70  # the font block inside a .PF file
# What the SjisPuts override (marz_overrides.cpp) draws for ASCII.
ASCII = {' ': 0xFFE3, '"': 0x201D, "'": 0x2019, '-': 0x2010, '~': 0x301C}


class Font:
    def __init__(self, path, narrow=False):
        d = bytearray(open(path, 'rb').read())
        if narrow:
            from font_narrow import patch
            patch(d, FONT)
        d = bytes(d)
        b = FONT
        count, ranges = struct.unpack_from('<2I', d, b + 0x30)
        index_table = struct.unpack_from('<I', d, b + 0x3C)[0]
        metrics = struct.unpack_from('<I', d, b + 0x44)[0]
        self.ranges = [struct.unpack_from('<4I', d, b + ranges + i * 16) for i in range(count)]
        self.d, self.b, self.index_table, self.metrics = d, b, index_table, metrics

    def advance(self, ucs):
        g = 0  # codes without a glyph draw glyph 0
        for start, end, base, _ in self.ranges:
            if start <= ucs <= end:
                g = struct.unpack_from('<H', self.d, self.b + self.index_table + (base + ucs - start) * 2)[0]
                break
        return struct.unpack_from('<8h', self.d, self.b + self.metrics + g * 16)[6]

    def width(self, text, ascii_map=False):
        w = 0.0
        for c in text:
            if ascii_map and c < '\x7f':
                if c == '\\':
                    continue
                u = ASCII.get(c, 0xFF01 + ord(c) - 0x21)
            else:
                u = ord(c)
            w += self.advance(u) + PITCH
        return w
