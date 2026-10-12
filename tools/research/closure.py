"""Call closure over direct jal/j (tail calls included) from root functions, stopping at given addresses.

Usage: python tools/research/closure.py ROOT[,ROOT...] [STOP[,STOP...]]
Prints game/SDK counts and writes the list to out/research/closure.txt.
"""
import sys,bisect
from scan import W, starts, TXT_HI
from common import work
ALL=sorted(s for s in starts if 0x00100000<=s<TXT_HI)
def extent(f):
    i=bisect.bisect_right(ALL,f)
    return f, (ALL[i] if i<len(ALL) else TXT_HI)
def callees(f):
    a,e=extent(f); out=set()
    for x in range(a,e,4):
        w=W[x]
        if w>>26==3: out.add((w&0x3ffffff)<<2)
        if w>>26==2:
            t=(w&0x3ffffff)<<2
            if t in starts and not (a<=t<e): out.add(t)   # tail call
    return out
def closure(roots,stop=()):
    seen=set();q=list(roots)
    while q:
        f=q.pop()
        if f in seen or f in stop: continue
        seen.add(f); q.extend(callees(f))
    return seen
if __name__=="__main__":
    roots=[int(x,16) for x in sys.argv[1].split(",")]
    stop=set(int(x,16) for x in sys.argv[2].split(",")) if len(sys.argv)>2 and sys.argv[2] else set()
    s=closure(roots,stop)
    game=sorted(f for f in s if f<0x002616E8); sdk=sorted(f for f in s if f>=0x002616E8)
    print(len(s),"functions:",len(game),"game,",len(sdk),"SDK/lib")
    open(work('closure.txt'),'w').write("\n".join(f"{f:08X}" for f in sorted(s)))
