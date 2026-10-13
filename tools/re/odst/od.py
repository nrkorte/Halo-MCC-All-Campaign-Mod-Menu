# ODST disassembly helpers (odst.img / text.pkl here), like ../h3/h3.py.
#   python od.py <hex rva>[:lines] ...   |   python od.py callers <rva> ...
import bisect, pickle, struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
img = open('odst.img', 'rb').read(); B = 0x180000000
md = Cs(CS_ARCH_X86, CS_MODE_64)
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
        _L = pickle.load(open('text.pkl', 'rb')); _K = [a for a, _, _ in _L]
    return _L
def callers(rva):
    t = hex(B + rva); return [a for a, m, o in _text() if o == t and m in ('call', 'jmp')]
def rng(a, b):
    L = _text(); i = bisect.bisect_left(_K, a); out = []
    while i < len(L) and L[i][0] < b: out.append(f"  {L[i][0]:08x}: {L[i][1]} {L[i][2]}"); i += 1
    return '\n'.join(out)
if __name__ == '__main__':
    a = sys.argv[1:]
    if a and a[0] == 'callers':
        for x in a[1:]: print(x, [hex(c) for c in callers(int(x, 16))])
    else:
        for x in a:
            n = int(x.split(':')[1]) if ':' in x else 40; r = int(x.split(':')[0], 16)
            print('==', hex(r)); print(dis(r, n))
