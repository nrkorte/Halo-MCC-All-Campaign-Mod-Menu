# Writes odst.img (halo2.dll mapped at its section RVAs) and text.pkl (.text disassembly).
import pefile, pickle, sys
from capstone import *
DLL = r'F:\SteamLibrary\steamapps\common\Halo The Master Chief Collection\halo3odst\halo3odst.dll'
pe = pefile.PE(DLL)
img = bytearray(pe.OPTIONAL_HEADER.SizeOfImage)
img[:len(pe.header)] = pe.header
for s in pe.sections:
    d = s.get_data()
    img[s.VirtualAddress:s.VirtualAddress + len(d)] = d
open('odst.img', 'wb').write(img)
if 'img' in sys.argv: sys.exit()
md = Cs(CS_ARCH_X86, CS_MODE_64); md.skipdata = True
t = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
B = 0x180000000
lines = [(i.address - B, i.mnemonic, i.op_str) for i in md.disasm(bytes(img[t.VirtualAddress:t.VirtualAddress + t.Misc_VirtualSize]), B + t.VirtualAddress)]
pickle.dump(lines, open('text.pkl', 'wb'))
print(len(lines))
