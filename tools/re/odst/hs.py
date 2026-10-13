# ODST's HaloScript function table, found from a known name: definitions hold
# a name pointer at +8 and the evaluator at +0x20 (as in Halo 3).
#   python hs.py <name> [...]   prints each evaluator's RVA
import struct, sys

B = 0x180000000
img = open('odst.img', 'rb').read()
q = lambda a: struct.unpack_from('<Q', img, a)[0]


def find_table():
    s = img.find(b'object_cannot_take_damage\x00')
    target = struct.pack('<Q', B + s)
    d = img.find(target) - 8  # the definition: name pointer at +8
    entry = img.find(struct.pack('<Q', B + d))  # table slot pointing at it
    start = entry
    while True:
        p = q(start - 8) - B
        if not (0 < p < len(img)) or not (0 < q(p + 8) - B < len(img)): break
        start -= 8
    return start


T = find_table()


def _names():
    out = {}
    for op in range(4000):
        d = q(T + 8 * op) - B
        if not (0 < d < len(img)): break
        n = q(d + 8) - B
        if not (0 < n < len(img)): break
        name = img[n:img.find(b'\x00', n)].decode('ascii', 'ignore')
        out[name] = q(d + 0x20) - B
    return out


F = _names()

if __name__ == '__main__':
    print(f'table at RVA {T:x}, {len(F)} functions')
    for name in sys.argv[1:]:
        print(name, hex(F[name]) if name in F else 'not found')
