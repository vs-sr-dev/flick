import re, sys
d = sys.argv[1]
sec = {}
for l in open(f"{d}/r_sec.txt"):
    m = re.match(r"t=(\d+) host=(\d+)%.*w\[(.*?)\] r\[(.*?)\]", l)
    if m: sec[int(m.group(1))] = dict(host=int(m.group(2)), w=m.group(3), r=m.group(4))
for l in open(f"{d}/prof.txt"):
    if not l.startswith("T "): continue
    p = l.split()
    t = int(p[1]); ins = int(p[3]); low = int(p[5])
    api = l.split(" api")[1].split(" pages")[0].split()
    entries = [a for a in api if (int(a.split(":")[0],16)-0x800) % 12 == 0]
    irq = l.split(" irq")[1].split(" api")[0].strip()
    pages = l.split(" pages")[1].strip()
    s = sec.get(t, {})
    print(f"{t:4d} host={s.get('host','?'):>4}% low={100*low/ins:3.0f}% irq[{irq}] pages[{pages}] r[{s.get('r','')}] api[{' '.join(entries)}]")
