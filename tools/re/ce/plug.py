# Flatten an Assembly plugin: prints offset, type, name with block nesting
import sys,re
import xml.etree.ElementTree as ET
def walk(e,base,depth,filt):
    for c in e:
        off=c.get('offset'); 
        if off is None: continue
        off=int(off,16)
        name=c.get('title') or c.get('name') or ''
        line=f"{'  '*depth}{hex(off)} {c.tag} {name}" + (f" [size {c.get('elementSize') or c.get('entrySize')}]" if c.tag in ('tagblock','reflexive') else '')
        if not filt or re.search(filt,name,re.I) or c.tag in('tagblock','reflexive'): print(line)
        if c.tag in ('tagblock','reflexive'): walk(c,0,depth+1,filt)
t=ET.parse(sys.argv[1]).getroot()
walk(t,0,0,sys.argv[2] if len(sys.argv)>2 else None)
