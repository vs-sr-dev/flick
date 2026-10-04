import re, struct
img = open("runs/xm1/r_low.bin","rb").read(); base = 0xa0000000
lines = open("bios/low_dis.txt", encoding="latin1").read().splitlines()
idx = {}
for i,l in enumerate(lines):
    m = re.match(r"^\s+([0-9a-f]{8}):", l)
    if m: idx[int(m.group(1),16)] = i
def cstr(a):
    o = a - base
    if not (0 <= o < len(img)): return None
    e = img.find(b"\0", o)
    s = img[o:e]
    if 4 <= len(s) < 80 and all(32 <= c < 127 or c in (9,10,13) for c in s): return s.decode()
    return None
def scan(start, depth=0, seen=None, maxn=400):
    """follow straight-line code from start; collect ldis/ori constants, jl targets, IO bases"""
    seen = seen if seen is not None else set()
    strs, io, calls = [], set(), []
    i = idx.get(start); n = 0; hi = {}
    while i is not None and n < maxn and i < len(lines):
        l = lines[i]; n += 1
        p = l.split("\t")
        if len(p) >= 4:
            op = p[2].strip(); args = p[-1]
            m = re.search(r"(r\d+|gp), 0x([0-9a-f]+)", args)
            if op == "ldis" and m: hi[m.group(1)] = int(m.group(2),16) << 16
            elif op == "ori" and m and m.group(1) in hi:
                v = hi[m.group(1)] | int(m.group(2),16)
                if (v >> 24) == 0x88: io.add(v & 0xffff0000)
                s = cstr(v)
                if s: strs.append(s)
            elif op.startswith("jl"):
                t = re.search(r"0xffffffff([0-9a-f]{8})", args)
                if t: calls.append(int(t.group(1),16))
            if re.match(r"br!?$", op) and "r3" in args: break
        i += 1
    return strs, io, calls
tbl = {}
for a in range(0xa0000800, 0xa0000b24, 12):
    l = lines[idx[a+8]]
    t = re.search(r"0xffffffff([0-9a-f]{8})", l)
    tbl[a] = int(t.group(1),16) if t else None
used = [0xa000080c,0xa0000818,0xa0000824,0xa0000830,0xa000083c,0xa0000848,0xa0000854,0xa0000860,0xa000086c,0xa0000890,0xa000089c,0xa00008a8,0xa00008c0,0xa00008cc,0xa00008f0,0xa00008fc,0xa0000908,0xa0000914,0xa00009b0,0xa0000a1c,0xa0000a28,0xa0000a4c,0xa0000a58,0xa0000a88,0xa0000a94,0xa0000aa0,0xa0000ac4,0xa0000b00,0xa0000b18]
for a in sorted(tbl):
    t = tbl[a]
    if t is None: print(f"{a:08x} -> ?"); continue
    strs, io, calls = scan(t)
    # one level deeper for strings/io
    for c in calls[:12]:
        s2, io2, _ = scan(c, maxn=200); strs += s2; io |= io2
    mark = "*" if a in used else " "
    print(f"{mark}{a:08x} -> {t:08x} io={sorted(hex(x>>16) for x in io)} calls={len(calls)} str={strs[:4]}")
