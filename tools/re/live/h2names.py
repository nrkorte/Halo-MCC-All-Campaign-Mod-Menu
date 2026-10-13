# Tag names from a Halo 2 MCC .map (zlib chunks of 0x40000 after a 0x380 header).
import struct, zlib
MAPS = r'F:\SteamLibrary\steamapps\common\Halo The Master Chief Collection\halo2\h2_maps_win64_dx11'
def names(map_name, strings=False):
    # strings=True: the string id table (header +0x30) instead of tag names.
    f = open(rf'{MAPS}\{map_name}.map', 'rb'); h = f.read(0x380)
    count, toff, tsize, ioff = struct.unpack_from('<4i', h, 0x20)
    if strings:
        count, toff, tsize, ioff = struct.unpack_from('<4i', h, 0x30)
    csize, = struct.unpack_from('<i', h, 0x308); ctab, ccount = struct.unpack_from('<ii', h, 0x310)
    f.seek(ctab); chunks = [struct.unpack('<ii', f.read(8)) for _ in range(ccount)]
    cache = {}
    def read(off, n):
        out = b''; off -= 0x380
        while n > 0:
            ci, co = divmod(off, csize)
            if ci not in cache:
                sz, fo = chunks[ci]; f.seek(fo); cache[ci] = zlib.decompress(f.read(sz))
            part = cache[ci][co:co + n]; out += part; n -= len(part); off += len(part)
        return out
    idx = read(ioff, count * 4); tab = read(toff, tsize)
    return [tab[o:tab.index(b'\0', o)].decode() for o in struct.unpack(f'<{count}i', idx)]
