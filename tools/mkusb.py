#!/usr/bin/env python3
"""Make a USB stick image for Flick's --usb from a folder: an MBR with one FAT32 partition,
as the HyperScan SDK's FatFS expects ("FAT32 formatted as MBR").

    python tools/mkusb.py FOLDER OUT.img [SIZE_MB]

Sub-folders are copied too. Names must be 8.3 (DOOM1.WAD): the SDK builds FatFS without
long file names, so it could not open anything else. The default size is 64 MB, or what
the files need if that is more."""
import os
import struct
import sys
import time

SECTOR = 512
PART_START = 2048          # first sector of the partition (1 MiB, as every modern tool)
RESERVED = 32              # reserved sectors of a FAT32 volume
MIN_CLUSTERS = 65525       # fewer and the volume is FAT16 by definition


def name83(name):
    base, dot, ext = name.upper().rpartition(".")
    if not dot:
        base, ext = ext, ""
    ok = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!#$%&'()-@^_`{}~")
    if not (1 <= len(base) <= 8 and len(ext) <= 3 and set(base + ext) <= ok):
        sys.exit("%s: not an 8.3 name (the SDK's FatFS has no long file names)" % name)
    return (base.ljust(8) + ext.ljust(3)).encode("ascii")


def dos_time(t):
    tm = time.localtime(t)
    year = min(max(tm.tm_year, 1980), 2107)
    return (tm.tm_hour << 11 | tm.tm_min << 5 | tm.tm_sec // 2,
            (year - 1980) << 9 | tm.tm_mon << 5 | tm.tm_mday)


def entry(name11, attr, cluster, size, mtime):
    tm, dt = dos_time(mtime)
    return struct.pack("<11sBBBHHHHHHHI", name11, attr, 0, 0, tm, dt, dt,
                       cluster >> 16, tm, dt, cluster & 0xFFFF, size)


class Volume:
    def __init__(self, spc):
        self.spc = spc
        self.csize = spc * SECTOR
        self.fat = [0x0FFFFFF8, 0x0FFFFFFF]
        self.data = {}             # first cluster -> bytes or a file path

    def alloc(self, nbytes):
        """a contiguous chain for nbytes (at least one cluster); the first cluster"""
        n = max(1, -(-nbytes // self.csize))
        first = len(self.fat)
        self.fat += list(range(first + 1, first + n)) + [0x0FFFFFFF]
        return first

    def add_dir(self, path, parent):
        """the directory at path, recursively; its first cluster (parent: 0 for the root's
        children, as FAT32 wants in "..")"""
        names = sorted(os.listdir(path), key=str.upper)
        children = [n for n in names if os.path.isdir(os.path.join(path, n))]
        is_root = parent is None
        count = len(names) + (0 if is_root else 2)
        me = self.alloc(max(1, count) * 32)
        ents = []
        if not is_root:
            now = os.path.getmtime(path)
            ents.append(entry(b".          ", 0x10, me, 0, now))
            ents.append(entry(b"..         ", 0x10, parent, 0, now))
        seen = set()
        for n in names:
            p = os.path.join(path, n)
            n11 = name83(n)
            if n11 in seen:
                sys.exit("%s: two names that differ only in case" % p)
            seen.add(n11)
            if n in children:
                c = self.add_dir(p, 0 if is_root else me)
                ents.append(entry(n11, 0x10, c, 0, os.path.getmtime(p)))
            else:
                size = os.path.getsize(p)
                if size >= 1 << 32:
                    sys.exit("%s: larger than 4 GB" % p)
                c = self.alloc(size) if size else 0
                if size:
                    self.data[c] = p
                ents.append(entry(n11, 0x20, c, size, os.path.getmtime(p)))
        self.data[me] = b"".join(ents)
        return me


def main():
    if len(sys.argv) not in (3, 4) or not os.path.isdir(sys.argv[1]):
        sys.exit(__doc__)
    folder, out = sys.argv[1], sys.argv[2]

    need = sum(os.path.getsize(os.path.join(d, f)) + 4096
               for d, _, files in os.walk(folder) for f in files)
    size_mb = int(sys.argv[3]) if len(sys.argv) == 4 else max(64, need * 5 // 4 // (1 << 20) + 8)
    spc = 1 if size_mb <= 256 else 8
    total = size_mb * (1 << 20) // SECTOR
    part = total - PART_START
    # sectors per FAT: each cluster takes spc sectors of data and 4 bytes in both FATs
    fatsz = -(-(part - RESERVED) * 4 // (spc * SECTOR + 8)) + 1
    clusters = (part - RESERVED - 2 * fatsz) // spc
    assert (clusters + 2) * 4 <= fatsz * SECTOR
    if clusters < MIN_CLUSTERS:
        sys.exit("%d MB is too small for FAT32: at least 34" % size_mb)

    vol = Volume(spc)
    root = vol.add_dir(folder, None)
    used = len(vol.fat) - 2
    if used > clusters:
        sys.exit("the files need %d MB, the image is %d" % (used * vol.csize // (1 << 20) + 2, size_mb))

    boot = bytearray(SECTOR)
    struct.pack_into("<3s8sHBHBHHBHHHII", boot, 0, b"\xEB\x58\x90", b"MSDOS5.0", SECTOR, spc,
                     RESERVED, 2, 0, 0, 0xF8, 0, 63, 255, PART_START, part)
    struct.pack_into("<IHHIHH12xBBBI11s8s", boot, 36, fatsz, 0, 0, root, 1, 6,
                     0x80, 0, 0x29, int(time.time()) & 0xFFFFFFFF, b"FLICK USB  ", b"FAT32   ")
    boot[510:512] = b"\x55\xAA"
    info = bytearray(SECTOR)
    struct.pack_into("<I", info, 0, 0x41615252)
    struct.pack_into("<III", info, 484, 0x61417272, clusters - used, len(vol.fat))
    struct.pack_into("<I", info, 508, 0xAA550000)
    mbr = bytearray(SECTOR)
    struct.pack_into("<B3sB3sII", mbr, 446, 0, b"\xFE\xFF\xFF", 0x0C, b"\xFE\xFF\xFF", PART_START, part)
    mbr[510:512] = b"\x55\xAA"

    fat = struct.pack("<%dI" % len(vol.fat), *vol.fat)
    data0 = PART_START + RESERVED + 2 * fatsz          # sector of cluster 2
    with open(out, "wb") as f:
        f.truncate(total * SECTOR)
        f.write(mbr)
        for copy in (0, 6):                             # boot sector and FSInfo, and the backup
            f.seek((PART_START + copy) * SECTOR)
            f.write(boot)
            f.write(info)
        for copy in (0, 1):
            f.seek((PART_START + RESERVED + copy * fatsz) * SECTOR)
            f.write(fat)
        for first, what in vol.data.items():
            f.seek((data0 + (first - 2) * spc) * SECTOR)
            if isinstance(what, bytes):
                f.write(what)
            else:
                with open(what, "rb") as src:
                    while True:
                        block = src.read(1 << 20)
                        if not block:
                            break
                        f.write(block)
    print("%s: %d MB, FAT32, %d of %d clusters used" % (out, size_mb, used, clusters))


if __name__ == "__main__":
    main()
