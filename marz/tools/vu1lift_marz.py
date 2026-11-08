#!/usr/bin/env python3
"""Lifts every VU1 microprogram image Virtual-On Marz was seen running to C++.

tools/vu1lift (Gitaroo Man) expects one program per game; Marz swaps whole images, so each image from
PS2X_VU1_CENSUS runs is lifted on its own, with the jump targets that census saw execute in it.
An image not listed here keeps running in the interpreter.

Usage: python3 tools/vu1lift_marz.py <census dir> [<census dir> ...]
Writes the runtime's src/lib/vu/generated/ps2_vu1_lifted_marz.inc (derived from the game: never commit it).
"""
import json
import os
import re
import sys

MARZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNTIME = os.path.join(MARZ, "..", "external", "gitaroo-man-native", "external", "PS2Recomp", "ps2xRuntime")
sys.path.insert(0, os.path.join(MARZ, "..", "external", "gitaroo-man-native", "tools", "vu1lift"))
import vu1lift  # noqa: E402


class ReachableLifter(vu1lift.Lifter):
    """Marz images keep other programs' leftovers and data past their code, so only reachable pairs are checked."""

    simd = True  # matches need the speed

    # Operand normalization (nrm/vnrm) is the identity on a value whose exponent is neither 0 nor 255, or on
    # a zero. Every FMAC result the lifted code stores is such a value (fmacStore keeps exponents 2..253 or
    # zero, or normalizes exactly like the interpreter), so within a block the lanes last written by an FMAC
    # op, ITOF, ABS/MAX/MINI of such lanes, or copies of them, need no normalizing when read again.
    # Anything else (loads, MFIR, MFP, FTOI, an interpreter call, a block entry) makes a lane unknown.
    # PS2X: set MARZ_LIFT_KEEP_NRM=1 to emit every normalization (the old code).
    elide_nrm = os.environ.get("MARZ_LIFT_KEEP_NRM") is None
    # Lower ops that write no VF register (everything not listed below them clears the knowledge).
    _NO_VF_OPS = {0x01, 0x04, 0x05, 0x08, 0x09, 0x10, 0x11, 0x12, 0x13, 0x1A, 0x1C, 0x20, 0x21, 0x24, 0x25,
                  0x28, 0x29, 0x2C, 0x2D, 0x2E, 0x2F}
    # SQI SQD DIV SQRT RSQRT WAITQ MTIR ILWR ISWR XTOP XITOP XGKICK WAITP ERLENG ESQRT (the EFU ones write P).
    _NO_VF_SPECIALS = {0x35, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3E, 0x3F, 0x68, 0x69, 0x6C, 0x7B, 0x73, 0x78}

    def _reset_known(self):
        self._known = {0: [True] * 4}  # VF0 is the constant (0, 0, 0, 1)
        self._acc = [False] * 4

    def _lane_known(self, reg, c):
        return reg == 0 or self._known.get(reg, [False] * 4)[c]

    def emit_block(self, start, w):
        self._reset_known()
        vu1lift.Lifter.emit_block(self, start, w)

    def emit_upper(self, p):
        lines = vu1lift.Lifter.emit_upper(self, p)
        if not self.elide_nrm:
            return lines
        dest = (p.uw >> 21) & 0xF
        lanes = [c for c in range(4) if dest & (8 >> c)]

        # A whole-register operand is used only in the written lanes; a lane operand is that lane.
        def vec(m):
            reg = m.group(1) or m.group(2)
            ok = all(self._acc[c] for c in lanes) if reg == "lacc" else all(self._lane_known(int(reg), c) for c in lanes)
            return m.group(0)[len("vnrm("):-1] if ok else m.group(0)

        def lane(m):
            reg, c = m.group(1) or m.group(2), int(m.group(3))
            ok = self._acc[c] if reg == "lacc" else self._lane_known(int(reg), c)
            return m.group(0)[len("nrm("):-1] if ok else m.group(0)

        out = []
        for line in lines:
            line = re.sub(r"vnrm\(_mm_load_ps\((?:VF\((\d+)\)|(lacc))\)\)", vec, line)
            line = re.sub(r"(?<!v)nrm\((?:VF\((\d+)\)|(lacc))\[(\d)\]\)", lane, line)
            out.append(line)
        return out

    def emit_pair(self, pc, w, branch=False, indent="    "):
        p = self.code[pc]
        vu1lift.Lifter.emit_pair(self, pc, w, branch, indent)
        if not self.elide_nrm:
            return
        dest = (p.lo >> 21) & 0xF
        lanes = [c for c in range(4) if dest & (8 >> c)]
        ft, fs = (p.lo >> 16) & 0x1F, (p.lo >> 11) & 0x1F
        known = self._known
        get = lambda r: list(known.get(r, [False] * 4)) if r else [True] * 4
        # Lower op first (the upper's result wins when both write one register).
        if p.ibit or p.lower_nop() or branch:
            pass
        elif p.op in self._NO_VF_OPS or (p.op == 0x40 and (p.lo & 0x3F) in (0x30, 0x31, 0x32, 0x34, 0x35)):
            pass
        elif p.op == 0x00 or p.special in (0x34, 0x36, 0x3D, 0x64):  # LQ LQI LQD MFIR MFP
            if ft:
                k = get(ft)
                for c in lanes:
                    k[c] = False
                known[ft] = k
        elif p.special == 0x30:  # MOVE
            if ft:
                k, src = get(ft), get(fs)
                for c in lanes:
                    k[c] = src[c]
                known[ft] = k
        elif p.special == 0x31:  # MR32
            if ft:
                k, src = get(ft), get(fs)
                for c in lanes:
                    k[c] = src[(c + 1) % 4]
                known[ft] = k
        elif p.special in self._NO_VF_SPECIALS:
            pass
        else:
            self._reset_known()
            known = self._known
        # Upper op.
        if p.upper_nop():
            return
        op = p.upper_op()
        uw = p.uw
        udest = (uw >> 21) & 0xF
        ulanes = [c for c in range(4) if udest & (8 >> c)]
        f = uw & 0x3F
        if op is None:
            if (f & 0x3C) == 0x3C and ((uw & 3) | ((uw >> 4) & 0x7C)) == 0x1F:  # CLIP writes no VF
                return
            self._reset_known()
            return
        if not udest:
            return
        kind, operand, to_acc = op
        ufd, ufs, uft = (uw >> 6) & 0x1F, (uw >> 11) & 0x1F, (uw >> 16) & 0x1F
        if kind in ("max", "mini"):
            if ufd:
                k, a, b = get(ufd), get(ufs), get(uft)
                if operand.startswith("bc"):
                    lane_b = "xyzw".index(operand[2]) if operand[2] in "xyzw" else int(operand[2])
                    b = [b[lane_b]] * 4
                elif operand in ("q", "i"):
                    b = [False] * 4
                for c in ulanes:
                    k[c] = a[c] and b[c]
                known[ufd] = k
        elif kind.startswith("itof") or kind.startswith("ftoi") or kind == "abs":
            if uft:
                k, src = get(uft), get(ufs)
                for c in ulanes:
                    k[c] = True if kind.startswith("itof") else (src[c] if kind == "abs" else False)
                known[uft] = k
        elif to_acc:
            for c in ulanes:
                self._acc[c] = True
        elif ufd:
            k = get(ufd)
            for c in ulanes:
                k[c] = True
            known[ufd] = k

    def check(self):
        pass

    def check_reachable(self):
        full = self.code
        reach = {pc for b in self.blocks.values() for pc in b[0]}
        self.code = {pc: p for pc, p in full.items() if pc in reach}
        # D and T bits only stop VU1 when FBRST enables them, and then tryExecute leaves it to the interpreter.
        for p in self.code.values():
            p.dbit = p.tbit = False
        vu1lift.Lifter.check(self)
        self.code = full


class SegmentLifter(ReachableLifter):
    """One MPG upload, lifted at its VU address. Pairs outside it are not known: flow that leaves it hands off
    (the runtime continues in whichever lifted upload is resident there)."""

    def __init__(self, base, data, cfg):
        self.image = data
        self.base = base
        self.code = {}
        for off in range(0, len(data), 8):
            lo, up = vu1lift.struct.unpack_from("<II", data, off)
            self.code[base + off] = vu1lift.Pair(base + off, lo, up)
        # A branch, jump or E bit on the last pair has its delay slot in the next upload: leave that pair
        # to the interpreter.
        last = base + len(data) - 8
        if last in self.code and (self.code[last].is_branch or self.code[last].is_jump or self.code[last].ebit):
            del self.code[last]
        self.entries = [int(x, 16) for x in cfg["entries"]]
        self.jr_targets = [int(x, 16) for x in cfg["jr_targets"]]
        self.dynamic = {}
        self.check()
        self.build_cfg()

    def mac_liveness(self, exact):
        """Upper ops whose MAC/status can be read, as in vu1lift.Lifter.mac_liveness, with the whole VU1 program
        split into uploads. Whatever runs after flow leaves this upload (a handoff, a jump, the next program
        after an E bit) is a possible reader in the pair after the exit, so every exit is walked back like a
        reader. An entry's predecessors outside the upload are covered the same way by the code they are in
        (its exits), so the walk only follows the predecessors inside the upload."""
        producers = {pc for pc, p in self.code.items() if p.writes_mac()}
        if exact:
            return producers, True
        preds = {pc: set() for pc in self.code}
        for pc, p in self.code.items():
            prev = self.code.get(pc - 16)
            if pc - 8 in self.code and not (prev and (prev.op in (0x20, 0x21) or prev.is_jump or prev.ebit)):
                preds[pc].add(pc - 8)
            if p.is_branch or pc in self.dynamic:
                for t in self.successors(pc):
                    if t in preds:
                        preds[t].add(pc + 8)
        starts = [preds[pc] for pc, p in self.code.items() if p.reads_mac()]
        for pairs, succ, kind in self.blocks.values():
            leaves = kind in ("jump", "end", "off") or any(s not in self.blocks for s in succ)
            if kind == "fall" and pairs[-1] + 8 not in self.blocks:
                leaves = True
            if leaves:
                starts.append({pairs[-1]})
        live = set()
        for start in starts:
            work, seen = [(q, 1) for q in start], set()
            while work:
                pc, d = work.pop()
                if (pc, d) in seen:
                    continue
                seen.add((pc, d))
                if d > 64:
                    return producers, False
                if pc in producers:
                    live.add(pc)
                    if d >= 4:
                        continue
                work.extend((q, d + 1) for q in preds[pc])
        return live, False


def elf_uploads(path):
    """Every VIF MPG in the ELF's VU1 microcode packages: (package, VU byte address, code bytes, label addresses)."""
    st = vu1lift.struct
    elf = open(path, "rb").read()
    shoff, shn = st.unpack_from("<I", elf, 0x20)[0], st.unpack_from("<H", elf, 0x30)[0]
    sh = [st.unpack_from("<10I", elf, shoff + i * 40) for i in range(shn)]
    symtab = next(s for s in sh if s[1] == 2)
    strtab = sh[symtab[6]]
    phoff, phn = st.unpack_from("<I", elf, 0x1C)[0], st.unpack_from("<H", elf, 0x2C)[0]
    loads = [st.unpack_from("<8I", elf, phoff + i * 32) for i in range(phn)]

    def off(va):
        for t, o, v, _, fs, _, _, _ in loads:
            if t == 1 and v <= va < v + fs:
                return o + va - v
        raise SystemExit("VA %#x not in the ELF" % va)

    syms = []
    for i in range(symtab[5] // 16):
        n, v = st.unpack_from("<II", elf, symtab[4] + i * 16)
        e = elf.index(b"\0", strtab[4] + n)
        syms.append((v, elf[strtab[4] + n:e].decode(errors="replace")))
    labels = [v for v, n in syms if n.startswith("_$")]
    packages = [v for v, n in syms if n.endswith("_code_") or n.startswith("Vu1Code_")]
    out = []
    for v, n in sorted(syms):
        if not n.startswith(".vif.") or not any(p <= v for p in packages):
            continue
        p = v
        for _ in range(8):
            cmd = st.unpack_from("<I", elf, off(p))[0]
            p += 4
            if (cmd >> 24) & 0x7F == 0x4A:  # MPG
                num, addr = ((cmd >> 16) & 0xFF) or 256, (cmd & 0xFFFF) * 8
                # The Ninja package holds ten variants under one symbol, each starting again at VU address 0.
                package = max(q for q in packages if q <= v)
                if not out or out[-1][0][0] != package or addr == 0:
                    group = (package, v)
                else:
                    group = out[-1][0]
                out.append((group, addr, elf[off(p):off(p) + num * 8], {lv - p + addr for lv in labels if p <= lv < p + num * 8}))
                break
    return out


def lift_uploads(elf, census_dirs, out):
    """Appends one program per distinct upload and returns their table rows."""
    images = collect(census_dirs) if census_dirs else {}
    uploads = elf_uploads(elf)
    # FSAND/FSOR/FSEQ read the sticky status bits, which every FMAC op since the last FSSET can set, in any
    # upload of the package. An upload of a package with such a reader keeps MAC/status for every FMAC op.
    readers = {pkg for pkg, _, data, _ in uploads for off in range(0, len(data), 8)
               if not vu1lift.struct.unpack_from("<I", data, off + 4)[0] & 0x80000000
               and vu1lift.struct.unpack_from("<I", data, off)[0] >> 25 in (0x14, 0x16, 0x17)}
    exact = {(base, data) for pkg, base, data, _ in uploads if pkg in readers}
    # The runtime leaves out the product sticky bits (US/OS set by an intermediate product, 0x300): refuse a reader of them.
    for _, base, data, _ in uploads:
        for off in range(0, len(data), 8):
            lo, up = vu1lift.struct.unpack_from("<II", data, off)
            imm = ((lo >> 10) & 0x800) | (lo & 0x7FF)
            if not up & 0x80000000 and (lo >> 25 == 0x14 or (lo >> 25 in (0x16, 0x17) and imm & 0x300)):
                raise SystemExit("upload %04X: status read at %04X can see the product sticky bits" % (base, base + off))
    seen, table = set(), []
    for _, base, data, labels in uploads:
        if (base, data) in seen:
            continue
        seen.add((base, data))
        index = len(table)
        end = base + len(data)
        entries, targets = {base} | labels, set()
        # Census runs of images holding this upload add the entries and jump targets that executed in it.
        for im in images.values():
            if im["bytes"][base:end] == data:
                entries |= {e for e in im["entries"] if base <= e < end}
                targets |= {t for t in im["targets"] if base <= t < end}
        # MSCNT continues after the pair that ended the last run.
        for off in range(0, len(data) - 8, 8):
            if vu1lift.struct.unpack_from("<I", data, off + 4)[0] & (1 << 30):
                entries.add(base + off + 16)
        entries = {e for e in entries if base <= e < end}
        cfg = {"entries": ["0x%X" % e for e in sorted(entries)], "jr_targets": ["0x%X" % t for t in sorted(targets)],
               "dynamic_branches": {}}
        lifter = SegmentLifter(base, data, cfg)
        lifter.check_reachable()
        text = lifter.emit((base, data) in exact)
        head, body = text.split("int VU1Lifted::runProgram(VU1Interpreter &vu, LiftedRun &run, uint32_t startPc)\n")
        head = head.replace("static const uint16_t kDynamicPairs[] = {};\n", "")
        head += "static const uint16_t kBlocks[] = {%s};\n" % ", ".join("0x%04X" % b for b in sorted(lifter.blocks))
        # A VI write in the pair before the entry still shows its old value to a branch here (the interpreter's
        # branch backup, which a run ending or leaving the upload leaves in the VU state, as the interpreter does).
        assert body.count("    BranchBackup bk{};\n") == 1
        body = body.replace("    BranchBackup bk{};\n", "    BranchBackup bk{};\n    if (vu.m_viBranchBackupValid)\n"
                            "        bk.record(vu.m_viBranchBackupReg, vu.m_viBranchBackupValue, vu.m_cycle - 1u);\n")
        body = body.replace("    writeBack();\n    return kLiftEnd;\n", "    writeBack();\n    handoff(vu, bk, vu.m_state.pc);\n    return kLiftEnd;\n")
        out.append("namespace marz_vu1_%d\n{\n%s}\n" % (index, head))
        out.append("template <>\nint VU1Lifted::runProgramN<%d>(VU1Interpreter &vu, LiftedRun &run, uint32_t startPc)\n%s" % (index, body))
        table.append("    {marz_vu1_%d::kImage, marz_vu1_%d::kImageBytes, &marz_vu1_%d::isBlockStart, &VU1Lifted::runProgramN<%d>, 0x%04Xu, "
                     "marz_vu1_%d::kBlocks, sizeof(marz_vu1_%d::kBlocks) / sizeof(uint16_t)}," % (index, index, index, index, base, index, index))
        reach = {pc for b in lifter.blocks.values() for pc in b[0]}
        print("upload %04X+%04X: %d blocks, %d/%d pairs reachable, %d entries, %d jump targets, MAC/status kept for %d FMAC ops%s"
              % (base, len(data), len(lifter.blocks), len(reach), len(data) // 8, len(entries), len(targets), len(lifter.mac_live),
                 " (all: status read in the package)" if (base, data) in exact else ""))
    return table


def collect(dirs):
    images = {}
    for d in dirs:
        manifest = json.load(open(os.path.join(d, "manifest.json")))
        census = json.load(open(os.path.join(d, "gitaroo_census.json")))
        counts = {im["crc"]: {int(k) for k in im["pc_count"]} for im in census["images"]}
        for p in manifest["programs"]:
            im = images.setdefault(p["crc"], {"bytes": open(os.path.join(d, p["image"]), "rb").read(), "entries": set(),
                                              "targets": set(), "executed": set(), "code_hi": 0, "instructions": 0})
            im["executed"] |= counts[p["crc"]]
            im["entries"].update(e["pc"] for e in p["entries"])
            im["code_hi"] = max(im["code_hi"], p["code_hi"])
            im["instructions"] += p["instructions"]
            # The census pools jumps across images: keep the targets that executed in this one.
            im["targets"].update(t for _, t, _ in census["jumps"] if t in counts[p["crc"]])
    return images


def lift_all(images, first_index, out):
    """Appends the programs to out and returns their table rows."""
    table = []
    for index, (crc, im) in enumerate(sorted(images.items(), key=lambda kv: -kv[1]["instructions"]), first_index):
        cfg = {"entries": ["0x%X" % e for e in sorted(im["entries"])],
               "jr_targets": ["0x%X" % t for t in sorted(im["targets"])],
               "dynamic_branches": {}}
        image = im["bytes"][:im["code_hi"]]
        # MSCNT continues after the pair that ended the last run, so those pairs are entries too.
        while True:
            lifter = ReachableLifter(image, cfg)
            resumes = {b[0][-1] + 8 for b in lifter.blocks.values() if b[2] == "end"} & im["executed"]
            new = resumes - im["entries"]
            if not new:
                break
            im["entries"] |= new
            cfg["entries"] = ["0x%X" % e for e in sorted(im["entries"])]
        lifter.check_reachable()
        for pc in sorted(pc for b in lifter.blocks.values() for pc in b[0]):
            if lifter.code[pc].op in (0x14, 0x15, 0x16, 0x17):  # FSEQ FSSET FSAND FSOR read or write status
                raise SystemExit("image %s: status flag op at %04X, but the runtime elides sticky flags" % (crc, pc))
        text = lifter.emit(False)
        head, body = text.split("int VU1Lifted::runProgram(VU1Interpreter &vu, LiftedRun &run, uint32_t startPc)\n")
        head = head.replace("static const uint16_t kDynamicPairs[] = {};\n", "")
        out.append("namespace marz_vu1_%d\n{\n%s}\n" % (index, head))
        out.append("template <>\nint VU1Lifted::runProgramN<%d>(VU1Interpreter &vu, LiftedRun &run, uint32_t startPc)\n%s" % (index, body))
        table.append("    {marz_vu1_%d::kImage, marz_vu1_%d::kImageBytes, &marz_vu1_%d::isBlockStart, &VU1Lifted::runProgramN<%d>}," % (index, index, index, index))
        reach = {pc for b in lifter.blocks.values() for pc in b[0]}
        print("image %s: %d blocks, %d/%d pairs reachable (%d executed pairs not reached), %d entries, %d jump targets, %d MInstr in census"
              % (crc, len(lifter.blocks), len(reach), len(lifter.code), len(im["executed"] - reach), len(im["entries"]),
                 len(im["targets"]), im["instructions"] // 1000000))
    return table


def main():
    args = sys.argv[1:]
    elf = None
    if args[:1] == ["--elf"]:
        elf, args = args[1], args[2:]
    vu1_dirs = [d for d in args if not d.startswith("vu0:")]
    vu0_dirs = [d[4:] for d in args if d.startswith("vu0:")]
    out = ["// Generated by marz/tools/vu1lift_marz.py -- do not edit.", ""]
    print("VU1")
    if elf:
        # Every microcode upload in the ELF, matched one by one at run time.
        table = lift_uploads(elf, vu1_dirs, out)
        out.insert(1, "#define PS2X_LIFTED_UPLOADS 1")
    else:
        table = lift_all(collect(vu1_dirs), 0, out)
    print("VU0")
    vu0_images = collect(vu0_dirs)
    vu0_table = lift_all(vu0_images, len(table), out)
    out.append("constexpr bool kLiftedFlagsExact = false;")
    out.append("static const LiftedProgram kPrograms[] = {")
    out.extend(table)
    out.append("};")
    out.append("static const LiftedProgram kProgramsVu0[] = {")
    out.extend(vu0_table or ["    {nullptr, 0u, nullptr, nullptr},"])
    out.append("};")
    path = os.path.join(RUNTIME, "src", "lib", "vu", "generated", "ps2_vu1_lifted_marz.inc")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("wrote", os.path.normpath(path), len(table) + len(vu0_images), "programs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
