# res/flick.png -> res/flick.ico (the Windows icon built into flick.exe). Needs Pillow.
import os
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
src = Image.open(os.path.join(ROOT, "res", "flick.png")).convert("RGBA")
src = src.crop(src.getbbox())                       # drop the transparent border
side = max(src.size)
square = Image.new("RGBA", (side, side))
square.paste(src, ((side - src.width) // 2, (side - src.height) // 2))
big = square.resize((256, 256), Image.LANCZOS)
big.save(os.path.join(ROOT, "res", "flick.ico"),
         sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
