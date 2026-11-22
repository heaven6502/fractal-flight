import zlib,struct,sys
def load(path):
    d=open(path,'rb').read(); assert d[:8]==b'\x89PNG\r\n\x1a\n'
    i=8; idat=b''; pal=None; 
    while i<len(d):
        n,=struct.unpack('>I',d[i:i+4]); t=d[i+4:i+8]; c=d[i+8:i+8+n]; i+=12+n
        if t==b'IHDR': w,h,bd,ct,_,_,il=struct.unpack('>IIBBBBB',c)
        elif t==b'PLTE': pal=[tuple(c[k:k+3]) for k in range(0,len(c),3)]
        elif t==b'IDAT': idat+=c
    raw=zlib.decompress(idat); assert il==0
    bpp={0:1,2:3,3:1,4:2,6:4}[ct]*(bd//8 if bd>=8 else 1); stride=(w*{0:1,2:3,3:1,4:2,6:4}[ct]*bd+7)//8
    rows=[];prev=bytearray(stride); p=0
    for y in range(h):
        f=raw[p]; p+=1; line=bytearray(raw[p:p+stride]); p+=stride
        for x in range(stride):
            a=line[x-bpp] if x>=bpp else 0; b=prev[x]; c=prev[x-bpp] if x>=bpp else 0
            if f==1: line[x]=(line[x]+a)&255
            elif f==2: line[x]=(line[x]+b)&255
            elif f==3: line[x]=(line[x]+((a+b)>>1))&255
            elif f==4:
                pa=abs(b-c); pb=abs(a-c); pc=abs(a+b-2*c)
                pr=a if pa<=pb and pa<=pc else (b if pb<=pc else c)
                line[x]=(line[x]+pr)&255
        rows.append(line); prev=line
    px=[]
    for y in range(h):
        r=rows[y]; row=[]
        for x in range(w):
            if ct==3:
                if bd==8: row.append(pal[r[x]])
                else:
                    per=8//bd; v=(r[x//per]>>(8-bd*(x%per+1)))&((1<<bd)-1); row.append(pal[v])
            elif ct==2: row.append(tuple(r[x*3:x*3+3]))
            elif ct==6: row.append(tuple(r[x*4:x*4+3]))
            elif ct==0: row.append((r[x],)*3)
        px.append(row)
    return w,h,px
