# Maps Halo: Reach RVAs to Halo 4 (same method as ../odst/port.py, which
# maps Halo 3 to ODST): masked byte patterns of the Reach code, matched in
# halo4.dll. Names inside still say H3 (Reach) and OD (Halo 4).
#   python port.py <reach rva> [...]
#   python port.py global <reach rva> [...]   map the global an instruction uses
import re, struct, sys
from capstone import *
from capstone.x86 import *

B = 0x180000000
H3 = open(r'..\reach\reach.img', 'rb').read()
OD = open('h4.img', 'rb').read()
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True


def text_range(img):
    # .text starts at 0x1000; take the first 0xB78000 bytes, more than enough.
    return 0x1000, min(len(img), 0x1000 + 0xB78000)


LOOSE = False  # also mask every displacement and immediate (struct offsets may differ)


def pattern(img, rva, count):
    """Masked regex for `count` instructions starting at rva, plus the instructions."""
    parts, insns = [], []
    for ins in md.disasm(img[rva:rva + count * 16], B + rva):
        raw = bytearray(ins.bytes)
        mask = [False] * len(raw)
        for op in ins.operands:
            if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP and ins.disp_size:
                for k in range(ins.disp_offset, ins.disp_offset + ins.disp_size): mask[k] = True
            if op.type == X86_OP_IMM and (ins.group(CS_GRP_JUMP) or ins.group(CS_GRP_CALL)) and ins.imm_size:
                for k in range(ins.imm_offset, ins.imm_offset + ins.imm_size): mask[k] = True
            if LOOSE and op.type == X86_OP_MEM and ins.disp_size:
                for k in range(ins.disp_offset, ins.disp_offset + ins.disp_size): mask[k] = True
            if LOOSE and op.type == X86_OP_IMM and ins.imm_size:
                for k in range(ins.imm_offset, ins.imm_offset + ins.imm_size): mask[k] = True
        parts.append(b''.join(b'.' if m else re.escape(bytes([b])) for b, m in zip(raw, mask)))
        insns.append(ins)
        if len(insns) >= count: break
    return parts, insns


def find(rva, count=12, back=0):
    """ODST RVAs matching the Halo 3 code at rva (pattern may start `back` instructions earlier)."""
    parts, insns = pattern(H3, rva, count)
    rx = re.compile(b''.join(parts), re.S)
    lo, hi = text_range(OD)
    return [m.start() + lo for m in rx.finditer(OD[lo:hi])], insns


def instruction_starts_before(img, rva, n):
    """A start address roughly n instructions before rva (disassembling from a bit earlier)."""
    for back in range(n * 4, n * 12):
        starts = [i.address - B for i in md.disasm(img[rva - back:rva + 16], B + rva - back)]
        if rva in starts:
            k = starts.index(rva)
            if k >= n: return starts[k - n]
    return rva


def map_rva(rva, loose_too=True):
    """Unique ODST RVA for a Halo 3 code RVA, trying longer and earlier windows,
    then the same with every displacement and immediate masked."""
    global LOOSE
    LOOSE = False
    r = _map(rva)
    if r[0] is None and loose_too:
        LOOSE = True
        r = _map(rva)
        LOOSE = False
        if r[0] is not None: r = (r[0], r[1] + ', loose')
    return r


def _map(rva):
    for count in (12, 16, 24, 32):
        hits, _ = find(rva, count)
        if len(hits) == 1: return hits[0], f'{count} insns'
    for back in (4, 8, 16):
        start = instruction_starts_before(H3, rva, back)
        for count in (16, 24, 32):
            hits, _ = find(start, count)
            if len(hits) == 1: return hits[0] + (rva - start), f'{count} insns from -{back}'
    return None, 'no unique match'


def rip_target(img, rva):
    for ins in md.disasm(img[rva:rva + 16], B + rva):
        for op in ins.operands:
            if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                return ins.address + ins.size + op.mem.disp - B, ins
        return None, ins


if __name__ == '__main__':
    args = sys.argv[1:]
    glob = args and args[0] == 'global'
    if glob: args = args[1:]
    for a in args:
        rva = int(a, 16)
        od, how = map_rva(rva)
        if od is None:
            print(f'{rva:08x} -> ?? ({how})'); continue
        if glob:
            t3, i3 = rip_target(H3, rva); to, io = rip_target(OD, od)
            print(f'{rva:08x} -> {od:08x} ({how}); global {t3 and hex(t3)} -> {to and hex(to)} | {i3.mnemonic} {i3.op_str}')
        else:
            i3 = next(md.disasm(H3[rva:rva + 16], B + rva)); io = next(md.disasm(OD[od:od + 16], B + od))
            same = '' if (i3.mnemonic, i3.op_str.split('0x18')[0]) == (io.mnemonic, io.op_str.split('0x18')[0]) else '   <-- differs'
            print(f'{rva:08x} -> {od:08x} ({how}) | {i3.mnemonic} {i3.op_str} || {io.mnemonic} {io.op_str}{same}')


def func_start(img, rva):
    """Start of the function containing rva (first instruction after int3/ret padding)."""
    a = rva
    while a > 0x1000:
        if img[a - 1] == 0xCC and img[a - 2] == 0xCC: return a
        a -= 1
    return rva


def align(rva, n_after=2):
    """Maps rva by matching its function's start, then stepping both functions
    instruction by instruction to the same index. Returns (odst rva, h3 insn, odst insn)."""
    f3 = func_start(H3, rva)
    fo, how = map_rva(f3)
    if fo is None: return None, None, None
    l3 = list(md.disasm(H3[f3:rva + 32], B + f3))
    k = next((i for i, x in enumerate(l3) if x.address - B == rva), None)
    if k is None: return None, None, None
    lo = list(md.disasm(OD[fo:fo + (rva - f3) + 256], B + fo))
    if k >= len(lo): return None, None, None
    return lo[k].address - B, l3[k], lo[k]


def _shape(i):
    return i.mnemonic + ' ' + re.sub(r'0x[0-9a-f]+|\b\d+\b', 'N', i.op_str)


def func_insns(img, start, limit=6000):
    out = []
    for i in md.disasm(img[start:start + limit], B + start):
        out.append(i)
        if i.mnemonic == 'int3' and len(out) > 4: break
    return out


def align2(rva):
    """Like align, but lines the two functions up with a sequence diff of
    instruction shapes (numbers ignored), so inserted or removed instructions
    don't throw the index off."""
    import difflib
    f3 = func_start(H3, rva)
    fo, how = map_rva(f3)
    if fo is None: return None, None, None
    l3, lo = func_insns(H3, f3), func_insns(OD, fo)
    k = next((i for i, x in enumerate(l3) if x.address - B == rva), None)
    if k is None: return None, None, None
    sm = difflib.SequenceMatcher(None, [_shape(x) for x in l3], [_shape(x) for x in lo], autojunk=False)
    for a, b, size in sm.get_matching_blocks():
        if a <= k < a + size: return lo[b + k - a].address - B, l3[k], lo[b + k - a]
    return None, l3[k], None


_H3TEXT = None


def h3_refs(target, limit=40):
    """Halo 3 instructions that use the global at `target` (RIP-relative)."""
    global _H3TEXT
    import pickle
    if _H3TEXT is None: _H3TEXT = pickle.load(open(r'..\reach\text.pkl', 'rb'))
    out = []
    for i, (a, m, o) in enumerate(_H3TEXT):
        mm = re.search(r'\[rip ([+-]) (0x[0-9a-f]+)\]', o)
        if mm and i + 1 < len(_H3TEXT):
            t = _H3TEXT[i + 1][0] + int(mm.group(2), 16) * (1 if mm.group(1) == '+' else -1)
            if t == target:
                out.append(a)
                if len(out) >= limit: break
    return out


def map_global(target, samples=12):
    """ODST address of a Halo 3 global: maps code that uses it and takes the majority."""
    from collections import Counter
    votes = Counter()
    for h3 in h3_refs(target)[:samples]:
        od, how = map_rva(h3)
        if od is None: continue
        to, io = rip_target(OD, od)
        if to: votes[to] += 1
    return votes.most_common(3)
