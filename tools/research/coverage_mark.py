"""Update docs/port/coverage-functions.csv from stdin lines  ADDR|status|subsystem|note  (empty field = keep).

Usage: python tools/research/coverage_mark.py < marks.txt
Status values: understood, partly, unknown, dead.
"""
import csv, sys
from common import COVERAGE as M
rows=list(csv.DictReader(open(M,encoding='utf-8'))); idx={int(r['address'],16):r for r in rows}
n=0
for line in sys.stdin:
    line=line.rstrip('\n')
    if not line.strip() or line.startswith('#'): continue
    a,st,sub,note=(line.split('|',3)+['','',''])[:4]
    r=idx[int(a,16)]
    if st: r['status']=st
    if sub: r['subsystem']=sub
    if note: r['note']=note
    n+=1
with open(M,'w',newline='',encoding='utf-8') as f:
    w=csv.DictWriter(f,fieldnames=['address','name','subsystem','status','note'],lineterminator='\n'); w.writeheader(); w.writerows(rows)
print('marked',n)
