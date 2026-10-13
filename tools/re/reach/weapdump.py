# Live: dumps a weapon tag's magazines/triggers/barrels and every live weapon
# object of it (state fields the mod touches). Usage: python weapdump.py target_laser
import sys,struct; from reachlive import *
def res(a): return h.q(BASE+0x4E39F20+8*(a>>28))+a*4 if a else 0
def blk(t,o,size):
    n=h.u32(t+o); a=res(h.u32(t+o+4)); return [a+size*i for i in range(n)]
def f(a): return struct.unpack('<f',h.rd(a,4))[0]
def s16(a): return struct.unpack('<h',h.rd(a,2))[0]
want=sys.argv[1]
n=names(); inst=h.q(BASE+0xC1A600)
tags=[i for i,x in enumerate(n) if x.endswith(want)]
for ti in tags:
    g,salt,addr=struct.unpack('<hHI',h.rd(inst+8*ti,8))
    if not addr: continue
    d=(salt<<16)|ti; t=tag_addr(d)
    grp=h.rd(h.q(BASE+0x263EA48)+0x3FFFC+16*g,4)[::-1]
    if grp!=b'weap': continue
    print(n[ti],hex(d),'flags',hex(h.u32(t+0x204)),hex(h.u32(t+0x208)),'age fields',[round(f(t+o),3) for o in (0x364,0x368,0x36c,0x370)])
    for i,m in enumerate(blk(t,0x3F0,0x60)):
        print(' mag',i,'flags',hex(h.u32(m)),'recharged/initial/total/loaded/inv/reloaded',[s16(m+o) for o in range(4,0x10,2)],'raw',h.rd(m+0x10,0x14).hex())
    for i,tr in enumerate(blk(t,0x3FC,0x8C)):
        print(' trig',i,'flags',hex(h.u32(tr)),'input/behavior',s16(tr+4),s16(tr+6),'barrels',s16(tr+8),s16(tr+10),'autofire',f(tr+0x10),f(tr+0x14),'charging',f(tr+0x1C),f(tr+0x20),'overcharge',h.rd(tr+0x24,2).hex(),'drain',f(tr+0x5C))
    for i,b in enumerate(blk(t,0x408,0x184)):
        print(' barrel',i,'flags',hex(h.u32(b)),'rps',f(b+4),f(b+8),'shots',s16(b+0x24),s16(b+0x26),'recovery',f(b+0x28),'mag',s16(b+0x38),'rounds/shot',s16(b+0x3a),'min loaded',s16(b+0x3c),'age/round',f(b+0x158),f(b+0x15c))
        print('   proj datum',hex(h.u32(b+0xA0)) if False else '')
    objs=slot(2)
    for i in range(h.u32(objs+0x3C) if False else 2048):
        try:
            e=entry(objs,i); o=h.q(e+0x10)
            if not o or h.u32(o)!=d: continue
        except Exception: continue
        print(' object',i,'parent',hex(h.u32(o+0x14)),'heat',f(o+0x1E0),'age',f(o+0x1E4))
        print('   pre-barrels 0x1E0..0x240',h.rd(o+0x1E0,0x60).hex())
        for b in range(2): print('   barrel',b,h.rd(o+0x240+0x34*b,0x34).hex())
        for k in range(2): print('   trigger',k,h.rd(o+0x2A8+0xC*k,0xC).hex())
        for k in range(2): print('   mag',k,h.rd(o+0x2C0+0x1A*k,0x1A).hex())
