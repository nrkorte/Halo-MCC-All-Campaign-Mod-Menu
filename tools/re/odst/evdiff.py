# Side by side: a HaloScript function's evaluator (and the first function it
# calls) in Halo 3 and in ODST, to read how struct offsets moved.
#   python evdiff.py <name> [n] [follow]
import re, struct, sys
from capstone import *

sys.path.insert(0, '..')
B = 0x180000000
md = Cs(CS_ARCH_X86, CS_MODE_64)
H3 = open(r'..\halo3.img', 'rb').read()
OD = open('odst.img', 'rb').read()
q3 = lambda a: struct.unpack_from('<Q', H3, a)[0]

# Halo 3: opcodes from Assembly's list, table at 0x7F6AD0, evaluator at +0x20.
xml = open(r'..\h3\H3MCC_U14_Scripting.xml').read()
def h3_eval(name):
    m = re.search(r'opcode="(0x[0-9A-Fa-f]+)" name="%s"' % re.escape(name), xml)
    if not m: return None
    d = q3(0x7F6AD0 + 8 * int(m.group(1), 16)) - B
    return q3(d + 0x20) - B

import hs as ohs
def od_eval(name): return ohs.F.get(name)


def lines(img, rva, n):
    out = []
    for i in md.disasm(img[rva:rva + n * 15], B + rva):
        out.append((i.address - B, f'{i.mnemonic} {i.op_str}'))
        if len(out) >= n or i.mnemonic in ('int3',): break
    return out


def callees(img, rva, n):
    return [int(t.split()[1], 16) - B for a, t in lines(img, rva, n) if t.startswith('call 0x18') or t.startswith('jmp 0x18')]


def show(a, b, n):
    la, lb = lines(H3, a, n), lines(OD, b, n)
    for k in range(max(len(la), len(lb))):
        x = f'{la[k][0]:08x}: {la[k][1]}' if k < len(la) else ''
        y = f'{lb[k][0]:08x}: {lb[k][1]}' if k < len(lb) else ''
        print(f'{x:60} | {y}')


if __name__ == '__main__':
    name = sys.argv[1]
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    a, b = h3_eval(name), od_eval(name)
    print(f'== {name}: H3 {a and hex(a)}  ODST {b and hex(b)}')
    if a is None or b is None: sys.exit()
    show(a, b, n)
    if len(sys.argv) > 3:
        for k, (ca, cb) in enumerate(zip(callees(H3, a, n), callees(OD, b, n))):
            if k >= int(sys.argv[3]): break
            print(f'-- callee {k}: H3 {ca:x}  ODST {cb:x}')
            show(ca, cb, n)
