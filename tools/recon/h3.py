import re, collections
H = "D:/Homebrew2/hyperscan-emulator-master/PPCSDK/HyperScan-SPG29x-SDK-main/SPG290/scorelibs/include/SPG290_Registers.h"
names = {}
for l in open(H, encoding="latin1"):
    m = re.match(r"#define\s+(\w+)\s+\(UV32\s*\*\)\s*0x([0-9A-Fa-f]{8})", l)
    if m: names[int(m.group(2),16) & 0x1fffffff] = m.group(1)
def mame(a):
    b, o = a >> 16, a & 0xffff
    if b == 0x0801:
        if o < 0x100: return "reg" if (o <= 0x10 or 0x20 <= o <= 0x30 or o == 0x38 or 0x3c <= o <= 0x4c or o == 0x54 or 0x58 <= o <= 0x68 or 0x70 <= o <= 0x94 or 0xa0 <= o <= 0xd0) else "UNMAPPED"
        if 0x1000 <= o < 0x2000: return "palette"
        if 0x2000 <= o < 0x2800: return "hoffset-ram(stored,not rendered)"
        if 0x3000 <= o < 0x3800: return "voffset-ram(stored,not rendered)"
        if 0x4000 <= o < 0x5000: return "sprite-ram"
        return "UNMAPPED"
    if b == 0x0803: return "reg" if o in (0, 0xc) else "UNMAPPED"
    if b == 0x0807: return "reg" if o in (0,4,8,0x6c) else "UNMAPPED"
    if b == 0x0809: return "reg" if o == 0x20 else "UNMAPPED"
    if b in (0x0805, 0x0806, 0x0813) or (b == 0x0816): return "device"
    if b == 0x0821: return "reg" if (0x6c <= o <= 0x83 or o == 0xe4) else "UNMAPPED"
    if b == 0x0820: return "reg" if o in (0x24, 0x68, 0x70) else "UNMAPPED"
    if b == 0x0824: return "nop"
    return "UNMAPPED"
blocks = {0x0801:"PPU",0x0803:"TV",0x0805:"SPU",0x0806:"CDSERVO",0x0807:"MIU",0x0808:"APBDMA",0x0809:"BUFCTL",0x080a:"IRQCTL",0x080c:"LDMDMA",0x080d:"BLNDMA",0x0802:"JPG",0x0813:"I2C",0x0815:"UART",0x0816:"TIMER",0x0820:"SFTCFG",0x0821:"CKG",0x0824:"ECC",0x0818:"SD",0x081b:"USBDEV",0x081c:"USBHOST",0x0823:"MIU2",0x0822:"MP4",0x0810:"GPIO"}
games = {}
for g in ("iwl2","xm1","mh1"):
    regs = {}
    for l in open(f"runs/{g}/r_regs.txt"):
        p = l.split()
        a = int(p[0],16); w = int(p[1][2:]); r = int(p[2][2:]); first = float(p[3][6:].replace(",", "."))
        regs[a] = (w, r, first, l.strip())
    games[g] = regs
allr = sorted(set().union(*games.values()))
print("UNMAPPED / partially emulated accesses (w/r per game; first access time):")
for a in allr:
    st = mame(a)
    if st in ("reg","device","palette","sprite-ram","nop"): continue
    if (a >> 16) == 0x0801 and st.startswith("UNMAPPED") is False and "ram" in st:
        pass
    cells = []
    for g in games:
        v = games[g].get(a)
        cells.append(f"{v[0]}/{v[1]}@{v[2]:.0f}" if v else "-")
    print(f"{a:08x} {blocks.get(a>>16,'?'):8s} {names.get(a,''):28s} {st:32s} " + "  ".join(f"{c:>16s}" for c in cells))
