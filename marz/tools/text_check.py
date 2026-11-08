# Checks translations against the limits of the slots they go into.
#   python3 text_check.py <jp.json> <db_capacity.json> <translation.json> [...]
# A translation file maps unit id -> text: a list of lines for msd units, a string (lines split by \n) otherwise.
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

LINE_MAX = {'msd': 48, 'db': 50, 'elf': 46}
# Characters drawn narrow besides printable ASCII (see font_narrow.py); others are allowed but draw full width.
NARROW = set('“”‘’―‐〜')
BANNED = set('\\')


def normalize(text):
    # cp932 has no U+2014 or U+2026; the font's dash is U+2015, and '...' reads better than a wide ellipsis.
    return text.replace('—', '―').replace('…', '...').replace('–', '-')


def encode(text):
    return normalize(text).encode('cp932')


def db_bytes(text):
    lines = normalize(text).split('\n')
    body = '\r\n'.join([str(len(lines))] + lines) + '\r\n'
    return len(body.encode('cp932'))


def check(unit, en, cap):
    errs = []
    kind = unit['kind']
    lines = en if kind == 'msd' else en.split('\n')
    if kind == 'msd':
        if not isinstance(en, list):
            return ['msd translation must be a list of lines']
        if len(en) != len(unit['jp']):
            errs.append('needs %d lines, has %d' % (len(unit['jp']), len(en)))
    for line in lines:
        try:
            n = len(encode(line))
        except UnicodeEncodeError as e:
            errs.append('not encodable in Shift-JIS: %r' % line[e.start:e.end])
            continue
        if kind == 'msd' and not line.strip():
            errs.append('empty line')
        if n > LINE_MAX[kind]:
            errs.append('line is %d bytes (max %d): %s' % (n, LINE_MAX[kind], line))
        if BANNED & set(line):
            errs.append('banned character in: %s' % line)
    if kind == 'elf':
        n = len(encode(en))
        if n > unit['max']:
            errs.append('%d bytes, slot holds %d' % (n, unit['max']))
    if kind == 'db':
        n = db_bytes(en)
        if n > cap:
            errs.append('%d bytes, file room %d' % (n, cap))
    return errs


FONT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'cvm', 'SCE20I22_NO26.PF')
# ELF tables with a display width of their own (the others share the database text box).
ELF_GROUPS = [(0x5033C0, 0x503A00, 'orders'), (0x59B940, 0x5AA5D0, 'chatter'), (0x5AEB60, 0x5B27C0, 'tutorial')]


def group(unit):
    if unit['kind'] != 'elf':
        return unit['kind']
    off = int(unit['id'][3:], 16)
    return next((g for a, b, g in ELF_GROUPS if a <= off < b), 'db')


def jp_lines(unit):
    if unit['kind'] == 'msd':
        return unit['jp']
    if unit['kind'] == 'db':
        return unit['jp'].splitlines()[1:]
    return unit['jp'].split('\n')


class Widths:
    """English lines may be no wider in pixels than the widest Japanese line drawn in the same place."""
    def __init__(self, units):
        from text_width import Font
        self.jp, self.en = Font(FONT_PATH), Font(FONT_PATH, narrow=True)
        self.limit = {}
        for u in units:
            g = group(u)
            for line in jp_lines(u):
                self.limit[g] = max(self.limit.get(g, 0), self.jp.width(line))

    def check(self, unit, lines):
        lim = self.limit[group(unit)]
        errs = []
        for line in lines:
            w = self.en.width(normalize(line), ascii_map=True)
            if w > lim:
                errs.append('line is %d px wide (max %d): %s' % (w, lim, line))
        return errs


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    units = {u['id']: u for u in json.load(open(sys.argv[1], encoding='utf-8'))}
    caps = json.load(open(sys.argv[2]))
    widths = Widths(units.values())
    bad = 0
    for path in sys.argv[3:]:
        tr = json.load(open(path, encoding='utf-8'))
        for uid, en in tr.items():
            if uid not in units:
                print(uid, ': unknown id')
                bad += 1
                continue
            lines = en if isinstance(en, list) else normalize(en).split('\n')
            for e in check(units[uid], en, caps.get(uid, 0)) + widths.check(units[uid], lines):
                print(uid, ':', e)
                bad += 1
    print('problems:', bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
