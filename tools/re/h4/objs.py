# Lists objects of the given types (0 biped, 1 vehicle, 2 weapon, ...) in the running game, with team and player fields.
#   python objs.py 0 1
import sys,struct; sys.path.insert(0,'../live'); import h4mem as h
ot=h.slot(3); n=h.i32(ot+0x44); data=h.q(ot+0x50)
cnt={}
for i in range(n):
    e=data+0x18*i; salt=struct.unpack('<H',h.rd(e,2))[0]
    if not salt: continue
    t=h.rd(e+4,1)[0]; o=h.q(e+0x10); cnt[t]=cnt.get(t,0)+1
    if t in [int(x) for x in sys.argv[1:]]:
        nm=h.names()[h.u32(o+8)&0xffff].split(chr(92))[-1]
        print(t,hex((salt<<16)|i),nm,'team',h.rd(o+0x498,1)[0],'player',hex(h.u32(o+0x49c)),[hex(h.u32(o+x)) for x in (0x47c,0x480,0x484,0x488,0x48c)],'parent',hex(h.u32(o+0x24)))
print(cnt)
