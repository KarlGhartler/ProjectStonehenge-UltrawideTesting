"""Log Ghidra renames: stdin lines  ADDR|NewName|evidence[|status]  (status confirmed (default) or hypothesis).

Usage: python tools/research/log_renames.py < renames.txt
Updates docs/symbols.csv (kind game for new rows; fix the kind by hand for SDK names), appends
docs/rename-log.csv and sets the coverage row to understood (confirmed) or partly (hypothesis).
The rename itself is done in Ghidra first (ghidra-mcp rename_function).
"""
import csv, sys
from common import SYMBOLS as S, RENAME_LOG as L, COVERAGE as M
syms=list(csv.DictReader(open(S,newline=''))); si={r['address'].upper():r for r in syms}
cov=list(csv.DictReader(open(M,encoding='utf-8'))); ci={int(r['address'],16):r for r in cov}
log=[]
for line in sys.stdin:
    line=line.rstrip('\n')
    if not line.strip() or line.startswith('#'): continue
    p=line.split('|'); a=f"0x{int(p[0],16):08X}"; n=p[1]; ev=p[2]; st=p[3] if len(p)>3 and p[3] else 'confirmed'
    old=si[a.upper()]['name'] if a.upper() in si else 'FUN_'+a[2:].lower()
    if a.upper() in si: si[a.upper()].update(name=n,status=st)
    else:
        r={'address':a,'name':n,'kind':'game','status':st}; syms.append(r); si[a.upper()]=r
    log.append([a, old, n if st=='confirmed' else 'plate: HYPOTHESIS '+n, ev])
    c=ci.get(int(a,16))
    if c:
        c['name']=n if st=='confirmed' else c['name']; c['status']='understood' if st=='confirmed' else 'partly'
        if not c['note']: c['note']=ev
syms.sort(key=lambda r:int(r['address'],16) if r['address'].startswith('0x') else 0)
with open(S,'w',newline='') as f:
    w=csv.DictWriter(f,fieldnames=['address','name','kind','status'],lineterminator='\n'); w.writeheader(); w.writerows(syms)
with open(L,'a',newline='') as f: csv.writer(f,lineterminator='\n').writerows(log)
with open(M,'w',newline='',encoding='utf-8') as f:
    w=csv.DictWriter(f,fieldnames=['address','name','subsystem','status','note'],lineterminator='\n'); w.writeheader(); w.writerows(cov)
print('logged',len(log))
