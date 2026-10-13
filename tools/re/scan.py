import sys, re, pickle
from capstone import *
img=open('halo3.img','rb').read(); BASE=0x180000000
md=Cs(CS_ARCH_X86,CS_MODE_64); md.skipdata=True
lines=[]
start,end=0x1000,0x1000+0x6f811c
for ins in md.disasm(img[start:end], BASE+start):
    lines.append((ins.address-BASE, ins.mnemonic, ins.op_str))
pickle.dump(lines, open('text.pkl','wb'))
print(len(lines))
