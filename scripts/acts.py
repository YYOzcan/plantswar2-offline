import re
src=open('/home/yigit/pw2/libGame_res.c').read()
parts=re.split(r'(?m)^// ==== ',src)
out=[]
for p in parts:
    name=p.split(' @')[0]
    toks=re.findall(r"FUN_000ffca8\((\w+),'([^']*)'\);\s*\n(.*)",p)
    acts=[];keys=[]
    for var,key,nxt in toks:
        if key=='act':
            m=re.search(r'AddParam\([^,]+,\w+,(0x[0-9a-f]+|\d+)\)',nxt)
            acts.append(int(m.group(1),0) if m else '?')
        else: keys.append(key)
    if acts: out.append((acts,name,keys))
for a,n,k in sorted(out,key=lambda x:x[0][0] if isinstance(x[0][0],int) else 999):
    print(a,n,' '.join(k))
