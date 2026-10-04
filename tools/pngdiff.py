"""pngdiff.py a.png b.png - count differing pixels (RGB), exit 1 if any."""
import sys
from PIL import Image

a = Image.open(sys.argv[1]).convert("RGB")
b = Image.open(sys.argv[2]).convert("RGB")
if a.size != b.size:
    print(f"size differs: {a.size} vs {b.size}")
    sys.exit(1)
pa, pb = a.load(), b.load()
diff = [(x, y) for y in range(a.size[1]) for x in range(a.size[0]) if pa[x, y] != pb[x, y]]
print(f"{len(diff)} differing pixels of {a.size[0] * a.size[1]}")
for x, y in diff[:10]:
    print(f"  ({x},{y}) {pa[x, y]} vs {pb[x, y]}")
sys.exit(1 if diff else 0)
