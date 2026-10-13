# Live: logs changes in the held target laser's state (weapon +0x1E0..+0x2F0,
# as offset:old->new bytes) and the airstrike state, for N seconds.
# Usage: python tlwatch.py 120
import sys,time,struct; from reachlive import *
inst=h.q(BASE+0xC1A600); n=names()
ti=[i for i,x in enumerate(n) if x.endswith('target_laser'+chr(92)+'target_laser')][0]
d=(struct.unpack('<H',h.rd(inst+8*ti+2,2))[0]<<16)|ti
end=time.time()+float(sys.argv[1]); t0=time.time(); lw=None; la=None; w=0
while time.time()<end:
    try:
        if not w or h.u32(w)!=d:
            w=0; objs=slot(2)
            for i in range(2048):
                o=h.q(entry(objs,i)+0x10)
                if o and h.u32(o)==d and h.u32(o+0x14)!=0xffffffff: w=o; break
        ws=h.rd(w+0x1E0,0x110) if w else b''
        a=slot(62); ab=h.rd(a,0x2C)
        ast=(struct.unpack_from('<h',ab,8)[0],ab[0xa],ab[0xb],struct.unpack_from('<i',ab,0x10)[0],struct.unpack_from('<i',ab,0x24)[0],struct.unpack_from('<i',ab,0x28)[0])
        t='%6.2f'%(time.time()-t0)
        if lw is not None and ws and len(ws)==len(lw) and ws!=lw:
            ch=[(k,lw[k],ws[k]) for k in range(len(ws)) if ws[k]!=lw[k]]
            print(t,'weap',' '.join('%X:%02x>%02x'%(0x1E0+k,x,y) for k,x,y in ch[:24]),flush=True)
        if ast!=la and (la is None or ast[:3]+ast[4:]!=la[:3]+la[4:] or ast[3]<=0):
            print(t,'strike state %d free %d nocd %d timer %d launches %d shots %d'%ast,flush=True)
        lw,la=ws,ast
    except Exception as ex: print('err',ex,flush=True); time.sleep(0.5); w=0
    time.sleep(0.016)
