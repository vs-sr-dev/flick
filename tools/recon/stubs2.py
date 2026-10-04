import re, struct, collections
def analyse(exe, dis, base):
    d = open(exe,"rb").read()
    slots = {base+o: struct.unpack_from("<I", d, o)[0] for o in range(0, len(d)-3, 4) if 0xa0000800 <= struct.unpack_from("<I", d, o)[0] < 0xa0000c00}
    lines = open(dis, encoding="latin1").read().splitlines()
    gp = None
    for i,l in enumerate(lines):
        if "\tldis\t\tgp," in l:
            gp = (int(re.search(r"0x([0-9a-f]+)", l.split("\t")[-1]).group(1),16)<<16)|int(re.search(r"0x([0-9a-f]+)", lines[i+1].split("\t")[-1]).group(1),16); break
    idx = {}; addrs = []
    for i,l in enumerate(lines):
        m = re.match(r"^\s+([0-9a-f]{8}):", l)
        if m: idx[int(m.group(1),16)] = i
    calls = collections.Counter(); cache = {}
    for l in lines:
        m = re.search(r"\tjl!?\t+0xffffffff([0-9a-f]{8})", l)
        if not m: continue
        t = int(m.group(1),16)
        if t not in cache:
            cache[t] = None
            i = idx.get(t)
            if i is not None:
                for k in range(i, min(i+9, len(lines))):
                    mm = re.search(r"\tlw\t\tr\d+, \[gp, (-?\d+)\]", lines[k])
                    if mm:
                        s = (gp + int(mm.group(1))) & 0xffffffff
                        if s in slots: cache[t] = slots[s]
                        break
                    if re.search(r"\tbr!?\t+r3", lines[k]): break
        if cache[t]: calls[cache[t]] += 1
    return calls
res = {}
for g, exe, dis, base in [("iwl","iwl/IWL.EXE","iwl/dis_game.txt",0xa00a4000),("xm","xm/HYPER.EXE","xm/dis.txt",0xa00901fc),("mh","mh/HYPER.EXE","mh/dis.txt",0xa00901fc)]:
    res[g] = analyse(exe, dis, base); print(g, "entries called:", len(res[g]), "call sites:", sum(res[g].values()))
allent = sorted(set().union(*[set(c) for c in res.values()]))
print("entry      " + " ".join(f"{g:>5}" for g in res))
for e in allent: print(f"{e:08x}  " + " ".join(f"{res[g].get(e,0):5d}" for g in res))
