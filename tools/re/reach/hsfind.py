import re,struct
xml=open('xml/ReachMCC_U13_Scripting.xml',encoding='ascii',errors='ignore').read()
vt={m.group(2):int(m.group(1),16) for m in re.finditer(r'<type opcode="(0x[0-9A-Fa-f]+)" name="([^"]+)"',xml.split('<valueTypes version="1">')[1].split('</valueTypes>')[0])}
funcs=[(int(m.group(1),16),m.group(2),vt.get(m.group(3),-1)) for m in re.finditer(r'<function opcode="(0x[0-9A-Fa-f]+)" name="([^"]+)" returnType="([^"]+)"',xml)]
gl=[(int(m.group(1),16),m.group(2),m.group(3)) for m in re.finditer(r'<global opcode="(0x[0-9A-Fa-f]+)" name="([^"]+)" type="([^"]+)"',xml)]
img=open('reach.img','rb').read(); B=0x180000000
def q(a): return struct.unpack_from('<Q',img,a)[0]
N=len(img)
best=None
for start in range(0x91e000, 0xafa000+0x400000, 8):
    ok=0
    for k in range(12):
        op,name,rt=funcs[k]
        p=q(start+8*k)-B
        if not (0<p<N-2): break
        if struct.unpack_from('<h',img,p)[0]!=rt: break
        ok+=1
    if ok==12:
        n=0
        while n<len(funcs):
            p=q(start+8*n)-B
            if not(0<p<N-2) or struct.unpack_from('<h',img,p)[0]!=funcs[n][2]: break
            n+=1
        print('table',hex(start),'matches',n,'of',len(funcs))
