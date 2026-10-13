import re, struct, pefile, sys
xml = open('../h3/H3MCC_U14_Scripting.xml', encoding='ascii', errors='ignore').read()
vt = {m.group(2): int(m.group(1), 16) for m in re.finditer(r'<type opcode="(0x[0-9A-Fa-f]+)" name="([^"]+)"', xml.split('<valueTypes>')[1].split('</valueTypes>')[0])}
funcs = []
for m in re.finditer(r'<function opcode="(0x[0-9A-Fa-f]+)" name="([^"]+)" returnType="([^"]+)"([^>]*)>', xml):
    funcs.append((int(m.group(1),16), m.group(2), vt.get(m.group(3), -1), 'null="true"' in m.group(4)))
print('functions', len(funcs), 'max opcode', hex(max(f[0] for f in funcs)))
pe = pefile.PE(r'F:\SteamLibrary\steamapps\common\Halo The Master Chief Collection\halo3\halo3.dll', fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
for s in pe.sections: print(s.Name.rstrip(b'\0'), hex(s.VirtualAddress), hex(s.Misc_VirtualSize))
open('halo3.img','wb').write(img)
print('imagebase', hex(base), 'size', hex(len(img)))
