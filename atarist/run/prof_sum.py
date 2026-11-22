import re,bisect,collections
syms=[]
for line in open('f3prof.map',errors='ignore'):
    m=re.match(r'\s+0x([0-9a-fA-F]{8,16})\s+([A-Za-z_][\w.$]*)\s*$',line)
    if m: syms.append((int(m.group(1),16),m.group(2)))
syms.sort(); addrs=[a for a,_ in syms]; d=dict((n,a) for a,n in syms)
rows=[]
for line in open('f3prof.txt',errors='ignore'):
    m=re.match(r'0x([0-9a-f]+)\s+([\d,]+)%\s+(\d+)\s+(.*?)\s+[\d,]+%\s+\((\d+), (\d+), \d+, \d+\)\s*$',line.rstrip('\n'))
    if m: rows.append((int(m.group(1),16),int(m.group(3)),m.group(4),int(m.group(5))))
OFF=0x1b018
ent=d['F3DripRows']+OFF
pics=[n for a,c,t,n in rows if a==ent]
P=pics[0] if pics else 1
per=collections.Counter(); tot=0
for a,c,t,n in rows:
    i=bisect.bisect_right(addrs,a-OFF)-1
    per[syms[i][1] if i>=0 else '?']+=c; tot+=c
print('pictures in 600 VBL:',P,' total cycles/VBL',tot//600,' cycles per picture (all, incl. idle)',tot//P)
for n,c in per.most_common(8): print('%-20s %5.1f%% %8d per picture'%(n,100*c/tot,c//P))
