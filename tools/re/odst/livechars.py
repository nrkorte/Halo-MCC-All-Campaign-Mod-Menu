# Live ODST (MCC running a campaign map): the loaded map's 'char' tags and the AI actors' characters.
#   set H3MEM_DLL=odst; python livechars.py
import os, struct, sys
os.environ['H3MEM_DLL'] = 'odst'
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'live'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import h3mem as m
from mapnames import names, MAPS
TAG_BASE, TAG_HEADER, MAP_HEADER = 0x2022AA8, 0x20F3068, 0x20EF068
mapname = m.rd(m.BASE + MAP_HEADER + 0xC0, 32).split(b'\0')[0].decode()
ns = names(os.path.join(MAPS, mapname + '.map'))
hd = m.q(m.BASE + TAG_HEADER); n = m.i32(hd + 0x10); inst = m.q(hd + 0x18); groups = m.q(hd + 8)
print('map', mapname)
chars = {}
for i in range(n):
    gi, salt, addr = struct.unpack('<HHI', m.rd(inst + 8 * i, 8))
    if addr and gi != 0xffff and struct.pack('>I', m.u32(groups + 16 * gi)) == b'char':
        chars[(salt << 16) | i] = ns[i]
for d, nme in chars.items(): print(hex(d), nme)
A = m.slot(1)
for i in range(m.i32(A + 0x3C)):
    e = m.entry(A, i); w = struct.unpack('<%dI' % (0xab0 // 4), m.rd(e, 0xab0))
    if w[0]: print('actor', i, 'char@0x68 =', chars.get(w[0x68 // 4], hex(w[0x68 // 4])), 'sq', w[0x30 // 4], 'team', w[0x20 // 4])
