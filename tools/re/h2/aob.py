import re,sys
img=open('h2.img','rb').read()
def find(p):
    rx=b''.join(b'.' if t=='??' else re.escape(bytes([int(t,16)])) for t in p.split())
    return [m.start() for m in re.finditer(rx,img[0x1000:0x1000+0xb04893],re.S)]
if __name__=='__main__':
    for p in sys.argv[1:]:
        h=[x+0x1000 for x in find(p)]; print(p,'->',[hex(x) for x in h])
