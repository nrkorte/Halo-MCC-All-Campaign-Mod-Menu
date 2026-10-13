# Dumps the scenario's designer zones and zone sets (which designer zones each allows), live.
from reachlive import *
def tptr(addr): return h.q(BASE+0x4E39F20+8*(addr>>28))+addr*4 if addr else 0
def block(base,off):
    c,a=struct.unpack('<iI',h.rd(base+off,8)); return c,tptr(a)
scnr=h.q(BASE+0xC1A230)
def palette(off):
    c,p=block(scnr,off); return [h.u32(p+0x10*i+0xC) for i in range(c)]
groups={0x8:0x120,0x14:0x138,0x20:0x168,0x2c:0x150,0xa4:0x3ec}  # bipeds,vehicles,weapons,equipment,characters
gname={0x8:'bipd',0x14:'vehi',0x20:'weap',0x2c:'eqip',0xa4:'char'}
pals={k:palette(v) for k,v in groups.items()}
c,p=block(scnr,0x740)
print('designer zones:',c)
for i in range(c):
    z=p+0xBC*i; out=[]
    for k in groups:
        n,q=block(z,k)
        idx=[struct.unpack('<h',h.rd(q+2*j,2))[0] for j in range(n)]
        out.append('%s: %s'%(gname[k],', '.join(nm(pals[k][x]) for x in idx if 0<=x<len(pals[k]))))
    print(' [%d]'%i, ' | '.join(o for o in out if not o.endswith(': ')))
c,p=block(scnr,0xAC)
cur=struct.unpack('<i',h.rd(BASE+0xAFBE30,4))[0]
print('zone sets:',c,'current',cur,'active designer mask %08X'%h.u32(BASE+0x4E2FB24))
for i in range(c):
    z=p+0x13C*i
    print(' [%d] %-28s loaded %08X unloaded %08X'%(i,h.rd(z+4,32).split(b'\0')[0].decode(),h.u32(z+0x11c),h.u32(z+0x120)))
