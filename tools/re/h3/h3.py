# Halo 3 static helpers (halo3.img / text.pkl one folder up, from ../mkimg_h3.py).
#   python h3.py <script name or hex rva>[:lines] ...   disassembles an evaluator or function
#   python h3.py callers <rva> ...                      calls/jumps to a function
# In Python: evaluator(name), dis(rva, n), callers(rva), rng(start, end).
import os, re, struct, sys, bisect, pickle
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

HERE = os.path.dirname(os.path.abspath(__file__))
img = open(os.path.join(HERE, '..', 'halo3.img'), 'rb').read()
B = 0x180000000
T = 0x7F6AD0  # HaloScript function table; evaluator at definition +0x20
md = Cs(CS_ARCH_X86, CS_MODE_64)
q = lambda a: struct.unpack_from('<Q', img, a)[0]

# name -> opcodes, from Assembly's H3MCC_U14_Scripting.xml (overloads share a name)
OPS = {}
for m in re.finditer(r'<function opcode="(0x[0-9A-Fa-f]+)" name="([^"]+)"', open(os.path.join(HERE, 'H3MCC_U14_Scripting.xml')).read()):
    OPS.setdefault(m.group(2), []).append(int(m.group(1), 16))


def evaluator(name, overload=0):
    return q(q(T + 8 * OPS[name][overload]) - B + 0x20) - B


def dis(rva, n=60):
    out = []
    for ins in md.disasm(img[rva:rva + n * 15], B + rva):
        out.append(f"  {ins.address - B:08x}: {ins.mnemonic} {ins.op_str}")
        if len(out) >= n or ins.mnemonic == 'int3': break
    return '\n'.join(out)


_L = None
def _text():
    global _L, _K
    if _L is None:
        _L = pickle.load(open(os.path.join(HERE, '..', 'text.pkl'), 'rb'))
        _K = [a for a, _, _ in _L]
    return _L


def rng(a, b):
    L = _text(); i = bisect.bisect_left(_K, a); out = []
    while i < len(L) and L[i][0] < b:
        out.append(f"  {L[i][0]:08x}: {L[i][1]} {L[i][2]}"); i += 1
    return '\n'.join(out)


def callers(rva):
    t = hex(B + rva)
    return [a for a, m, o in _text() if o == t and m in ('call', 'jmp')]


if __name__ == '__main__':
    args = sys.argv[1:]
    if args and args[0] == 'callers':
        for a in args[1:]: print(a, [hex(x) for x in callers(int(a, 16))])
    else:
        for a in args:
            n = int(a.split(':')[1]) if ':' in a else 40; a = a.split(':')[0]
            r = evaluator(a) if a in OPS else int(a, 16)
            print('==', a, hex(r)); print(dis(r, n))


def calls(rva, limit=3000):
    # direct calls inside a function (up to the first int3 run)
    L = _text(); i = bisect.bisect_left(_K, rva); out = []
    while i < len(L) and L[i][1] != 'int3' and L[i][0] < rva + limit:
        if L[i][1] == 'call' and L[i][2].startswith('0x18'): out.append((L[i][0], int(L[i][2], 16) - B))
        i += 1
    return out
