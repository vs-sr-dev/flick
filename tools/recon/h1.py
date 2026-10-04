import sys, re
def load(d):
    rows = []
    for l in open(f"runs/{d}/prof.txt"):
        if not l.startswith("T "): continue
        p = l.split()
        t, ins, low, idle = int(p[1]), int(p[3]), int(p[5]), int(p[7])
        marks = l.split(" mark")[1].split(" irq")[0].split()
        irq = dict(x.split(":") for x in l.split(" irq")[1].split(" api")[0].split())
        rows.append(dict(t=t, ins=ins, idle=idle, low=low, fps=int(marks[0]) if marks else 0, vbl=int(irq.get("53", 0))))
    return rows
for d in sys.argv[1:]:
    rows = load(d)
    print(f"== {d}  ({len(rows)} s)")
    line = []
    for r in rows:
        line.append(f"{r['t']}:{r['fps']}f/{100*r['idle']/r['ins']:.0f}%")
    for i in range(0, len(line), 12): print("  " + "  ".join(line[i:i+12]))
