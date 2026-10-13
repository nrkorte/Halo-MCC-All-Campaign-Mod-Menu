# Lists tag names (from the .map file name tables) matching a substring, for every ODST map.
#   python mapnames.py <substring>
import os, struct, sys
MAPS = r'F:\SteamLibrary\steamapps\common\Halo The Master Chief Collection\halo3odst\maps'
def names(path):
    f = open(path, 'rb'); hd = f.read(0x4000); u = lambda o: struct.unpack_from('<I', hd, o)[0]
    m = u(0x4CC); cnt = u(0x20)
    f.seek((u(0x2C) + m) & 0xffffffff); offs = struct.unpack('<%di' % cnt, f.read(4 * cnt))
    f.seek((u(0x24) + m) & 0xffffffff); s = f.read(u(0x28))
    return [s[o:s.find(b'\0', o)].decode('latin1') if 0 <= o < len(s) else '' for o in offs]
if __name__ == '__main__':
    sub = sys.argv[1]
    for fn in sorted(os.listdir(MAPS)):
        if fn.endswith('.map') and fn != 'shared.map':
            try: ns = names(os.path.join(MAPS, fn))
            except Exception as e: print(fn, 'err', e); continue
            print('==', fn, len(ns))
            for n in ns:
                if sub in n: print('  ', n)
