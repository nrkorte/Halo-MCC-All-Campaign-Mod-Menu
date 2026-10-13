import struct, sys
from capstone import *

# Halo CE (halo1.dll): HaloScript function table, indexed by the names the
# definitions themselves carry (Assembly's list only lines up for the first 343).
img = open('ce.img', 'rb').read()
B = 0x180000000
T = 0x18960f0   # function definition pointers
GT = 0x1b87e88  # globals {name, type, address}, 0x18 each
q = lambda a: struct.unpack_from('<Q', img, a)[0]
md = Cs(CS_ARCH_X86, CS_MODE_64)

def cstr(a):
    return img[a:img.find(b'\x00', a)].decode('ascii', 'ignore')

def _names():
    out = {}
    for op in range(2000):
        d = q(T + 8 * op) - B
        if not (0 < d < len(img)): break
        np = q(d + 8) - B
        if not (0 < np < len(img)): break
        out[cstr(np)] = op
    return out

F = _names()

def defn(name): return q(T + 8 * F[name]) - B
def evaluator(name): return q(defn(name) + 0x18) - B

def dis(rva, n=60):
    out = []
    for ins in md.disasm(img[rva:rva + n * 15], B + rva):
        out.append(f"  {ins.address - B:08x}: {ins.mnemonic} {ins.op_str}")
        if len(out) >= n or ins.mnemonic == 'int3': break
    return '\n'.join(out)

def ev(name, n=60): return dis(evaluator(name), n)

if __name__ == '__main__':
    for a in sys.argv[1:]:
        print(a, 'opcode', hex(F[a]), 'evaluator', hex(evaluator(a)))
