#!/usr/bin/env python3
"""Builds the recompiler's config: the Ghidra exporter's TOML with the paths and hand-kept lists set.

Usage: gen_toml.py <ghidra_export.toml> <marz_overrides.toml> <output.toml> <marz dir>
marz_overrides.toml holds a `native = [...]` list: names that must run as original guest code even
though the runtime has a handler of the same name, and optionally a [patches] table of instruction words
the recompiler substitutes (copied as is).
"""
import re
import sys

export_path, overrides_path, out_path, marz = sys.argv[1:5]
marz = marz.replace('\\', '/')
text = open(export_path, encoding='utf-8').read()
overrides = open(overrides_path, encoding='utf-8').read()


def set_key(key, value):
    global text
    text, n = re.subn(rf'^{key}\s*=.*$', f'{key} = "{value}"', text, count=1, flags=re.M)
    if not n:
        raise SystemExit(f'{key} missing from {export_path}')


set_key('input', f'{marz}/iso_jp/SLPM_653.03')
set_key('output', f'{marz}/build/recomp.new')
set_key('ghidra_output', f'{marz}/build/functions.csv')

native = re.search(r'^native\s*=\s*\[.*?^\]', overrides, flags=re.M | re.S).group(0)
text = re.sub(r'^native\s*=\s*\[.*?\]\s*$', '', text, flags=re.M | re.S)
# Keys of [general] must come before the next table.
text = text.replace('[ghidra_export]', native + '\n\n[ghidra_export]', 1) if '[ghidra_export]' in text else text + '\n' + native + '\n'
patches = re.search(r'^\[patches\].*?(?=^\[(?!\[)|\Z)', overrides, flags=re.M | re.S)
if patches:
    text = text.rstrip('\n') + '\n\n' + patches.group(0).rstrip('\n') + '\n'
open(out_path, 'w', encoding='utf-8').write(text)
print('wrote', out_path)
