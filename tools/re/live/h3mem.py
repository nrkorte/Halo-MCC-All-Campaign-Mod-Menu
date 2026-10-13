# Live reader/writer for halo3.dll (or halo3odst.dll: H3MEM_DLL=odst) in a
# running MCC. Process access comes from h4mem.py (same MCC process).
import os, struct
import h4mem as h

ODST = os.environ.get('H3MEM_DLL') == 'odst'
DLL = b'halo3odst.dll' if ODST else b'halo3.dll'
TLS_INDEX_VAR = 0xA8FB1C if ODST else 0xA39F9C  # the PE TLS directory's AddressOfIndex
_, BASE, SIZE = h._find(DLL)
rd, q, u32 = h.rd, h.q, h.u32
i32 = lambda a: struct.unpack('<i', rd(a, 4))[0]
i16 = lambda a: struct.unpack('<h', rd(a, 2))[0]
f32 = lambda a: struct.unpack('<f', rd(a, 4))[0]
def wr(a, data): return h.wr(a, data)

_state = None
def state():
    # The TLS block (game state slots) of the thread whose players/objects are set.
    global _state
    if _state: return _state
    idx = u32(BASE + TLS_INDEX_VAR)
    for tid in h._threads():
        try:
            tls = q(h._teb(tid) + 0x58)
            blk = q(tls + 8 * idx) if tls else 0
            if blk and q(blk + 8 * 2) and q(blk + 8 * 7): _state = blk; break
        except Exception: pass
    return _state
def slot(n): return q(state() + 8 * n)

# Data arrays (blam.h): entry size +0x24, high water +0x3C, data +0x48.
def entry(arr, i): return q(arr + 0x48) + i32(arr + 0x24) * i
def obj(datum):
    e = entry(slot(7), datum & 0xffff)
    return q(e + 0x10) if struct.unpack('<H', rd(e, 2))[0] == (datum >> 16) else 0
def player_unit(i=0): return u32(entry(slot(2), i) + 0x28)

# Tags: data = *tag_base + address * 4.
TAG_BASE, TAG_HEADER, MAP_HEADER = (0x1FCF4C8, 0x20AD058, 0x20A9058) if not ODST else (None, None, None)
def tag(datum):
    hd = q(BASE + TAG_HEADER); g, salt, addr = struct.unpack('<HHI', rd(q(hd + 0x18) + 8 * (datum & 0xffff), 8))
    return q(BASE + TAG_BASE) + addr * 4 if addr and salt == (datum >> 16) else 0
def group(datum):
    hd = q(BASE + TAG_HEADER); g = struct.unpack('<H', rd(q(hd + 0x18) + 8 * (datum & 0xffff), 2))[0]
    return struct.pack('>I', u32(q(hd + 8) + 16 * g)).decode('latin1')
def block(t, off, size, i):
    n = i32(t + off); return q(BASE + TAG_BASE) + u32(t + off + 4) * 4 + size * i if 0 <= i < n else 0
def mapname(): return rd(q(BASE + MAP_HEADER) if False else BASE + MAP_HEADER + 0xC0, 32).split(b'\0')[0].decode()

MAPS = r'F:\SteamLibrary\steamapps\common\Halo The Master Chief Collection\halo3\maps'
_names = None
def names():
    # Tag names by index, from the .map (header file table +0x20.., offset mask +0x4CC).
    global _names
    if _names: return _names
    f = open(os.path.join(MAPS, mapname() + '.map'), 'rb'); hd = f.read(0x4000); u = lambda o: struct.unpack_from('<I', hd, o)[0]
    m = u(0x4CC); cnt = u(0x20)
    f.seek((u(0x2C) + m) & 0xffffffff); offs = struct.unpack('<%di' % cnt, f.read(4 * cnt))
    f.seek((u(0x24) + m) & 0xffffffff); s = f.read(u(0x28))
    _names = [s[o:s.find(b'\0', o)].decode('latin1') if 0 <= o < len(s) else '' for o in offs]; return _names
def nm(d): return names()[d & 0xffff].split('\\')[-1] if d != 0xffffffff else '-'
def tags_of(g):
    hd = q(BASE + TAG_HEADER); n = i32(hd + 0x10); inst = q(hd + 0x18); groups = q(hd + 8); out = []
    for i in range(n):
        gi, salt, addr = struct.unpack('<HHI', rd(inst + 8 * i, 8))
        if addr and gi != 0xffff and struct.pack('>I', u32(groups + 16 * gi)).decode('latin1') == g: out.append((salt << 16) | i)
    return out
