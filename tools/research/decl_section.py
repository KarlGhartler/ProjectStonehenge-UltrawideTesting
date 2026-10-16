"""Research helper: append a game.h section declaring every function defined in a src/game file
(lines following '// EE:' markers) and drop the same declarations from the 'still to translate'
section. Usage: python tools/research/decl_section.py src/game/x.cpp "Section title (x.cpp)"."""
import re
import sys

src, title = sys.argv[1], sys.argv[2]
code = open(src, encoding='utf-8').read().splitlines()
decls = []
for i, line in enumerate(code):
    if line.startswith('// EE: 0x'):
        j = i + 1
        while code[j].startswith('//'):
            j += 1
        sig = code[j].rstrip(' {').rstrip()
        if not sig.endswith(')'):
            sig = sig[:sig.rfind(')') + 1]
        decls.append(sig + ';')
p = 'src/game/game.h'
h = open(p, encoding='utf-8').read()
start = h.find('// ---- Flight systems still to translate')
if start < 0:
    start = h.rstrip().rfind('}  // namespace ac04::game')
names = {re.search(r'(\w+)\(', d).group(1) for d in decls}
lines = h[start:].split('\n')
keep = [l for l in lines if not (re.match(r'^\w[\w\s\*]*\s(\w+)\(', l) and re.match(r'^\w[\w\s\*]*\s(\w+)\(', l).group(1) in names)]
rest = '\n'.join(keep)
head = h[:start]
existing = {m.group(1) for m in re.finditer(r'^\w[\w\s\*]*\s(\w+)\(', head, re.M)}
new = [d for d in decls if re.search(r'(\w+)\(', d).group(1) not in existing]
sec = '// ---- ' + title + ' ' + '-' * max(3, 95 - len(title) - 8) + '\n' + '\n'.join(new) + '\n\n'
open(p, 'w', encoding='utf-8', newline='\n').write(head + sec + rest)
print(len(new), 'declared;', len(lines) - len(keep), 'removed from pending')
