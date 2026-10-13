# Votes on how Halo 3 displacements map to ODST, across every HaloScript
# evaluator (plus its first callee) the two games share: instructions are
# aligned with a sequence diff of their shapes, and each aligned pair whose
# Halo 3 operand uses one of the wanted displacements records the ODST one.
import difflib, re, sys
from collections import Counter, defaultdict
import evdiff as e
from port import _shape

WANT = [int(x, 16) for x in sys.argv[1:]]
H3_NAMES = re.findall(r'name="([a-z0-9_]+)" returnType', e.xml)
votes = defaultdict(Counter)
examples = {}


def disp(op_str):
    return [int(x, 16) for x in re.findall(r'\+ (0x[0-9a-f]+)\]', op_str)]


def insns(img, rva, n=60):
    return list(e.md.disasm(img[rva:rva + n * 15], e.B + rva))[:n]


for name in H3_NAMES:
    a, b = e.h3_eval(name), e.od_eval(name)
    if not a or not b: continue
    pairs = [(a, b)]
    ca, cb = e.callees(e.H3, a, 30), e.callees(e.OD, b, 30)
    pairs += list(zip(ca[1:3], cb[1:3]))
    for x, y in pairs:
        l3, lo = insns(e.H3, x), insns(e.OD, y)
        sm = difflib.SequenceMatcher(None, [_shape(i) for i in l3], [_shape(i) for i in lo], autojunk=False)
        for i, j, size in sm.get_matching_blocks():
            for k in range(size):
                d3, do = disp(l3[i + k].op_str), disp(lo[j + k].op_str)
                for v3, vo in zip(d3, do):
                    if v3 in WANT:
                        votes[v3][vo] += 1
                        examples.setdefault((v3, vo), f'{name}: {l3[i+k].mnemonic} {l3[i+k].op_str} || {lo[j+k].mnemonic} {lo[j+k].op_str}')

for v in WANT:
    print(f'{v:#x}:', ', '.join(f'{vo:#x} x{n}' for vo, n in votes[v].most_common(4)))
    for vo, n in votes[v].most_common(2): print('    e.g.', examples[(v, vo)])
