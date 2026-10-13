import pickle, sys, bisect
L = pickle.load(open('text.pkl', 'rb')); A = [l[0] for l in L]
def ar(r, b, a):
    i = bisect.bisect_left(A, r)
    for l in L[max(0, i - b):i + a]: print(('>>' if l[0] == r else '  ') + f'{l[0]:08x}: {l[1]} {l[2]}')
def callers(t):
    s = hex(0x180000000 + t)
    return [l[0] for l in L if l[1] in ('call', 'jmp') and l[2] == s]
if __name__ == '__main__':
    if sys.argv[1] == 'callers':
        for t in sys.argv[2:]: print(t, [hex(c) for c in callers(int(t, 16))])
    else:
        ar(int(sys.argv[1], 16), int(sys.argv[2]), int(sys.argv[3]))
