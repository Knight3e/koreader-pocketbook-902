#!/usr/bin/env python3
"""make_logo.py - turn any picture into a logo for the PocketBook Pro 902 (power-off / boot logo).

The 2.x firmware wants:  *.bmp  in  /system/logo  on the device, 8-bit grayscale (not 1-bit, not 24-bit),
size 824x1200 (portrait). Then pick the file in the device settings (Appearance -> Power-off / Boot logo).

Usage:   python3 make_logo.py picture.jpg            -> picture_logo.bmp  (picture fitted, white borders)
         python3 make_logo.py picture.jpg --crop     -> fill the whole screen (cuts the edges)
         python3 make_logo.py picture.jpg --dither   -> reduce to the panel's 16 gray levels with dithering
                                                       (photos usually look better this way)
Needs:   pip install pillow
"""
import sys
from PIL import Image, ImageOps

W, H = 824, 1200


def dither16(img_l):
    """Floyd-Steinberg dithering of a grayscale image to 16 gray levels (0, 17, ... 255)."""
    pal = Image.new("P", (1, 1))
    pal.putpalette([v for i in range(16) for v in (i * 17,) * 3] + [0] * (768 - 48))
    q = img_l.convert("RGB").quantize(palette=pal, dither=Image.Dither.FLOYDSTEINBERG)
    return q.convert("L")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    src = sys.argv[1]
    opts = sys.argv[2:]
    img = Image.open(src).convert("L")
    if "--crop" in opts:
        out = ImageOps.fit(img, (W, H), Image.LANCZOS)
    else:
        img.thumbnail((W, H), Image.LANCZOS)
        out = Image.new("L", (W, H), 255)
        out.paste(img, ((W - img.width) // 2, (H - img.height) // 2))
    if "--dither" in opts:
        out = dither16(out)
    dst = src.rsplit(".", 1)[0] + "_logo.bmp"
    out.save(dst, format="BMP")            # mode "L" -> 8-bit BMP with a gray palette
    print("written:", dst, "(%dx%d, 8-bit gray%s)" % (out.size + (", dithered to 16 levels" if "--dither" in opts else "",)))


if __name__ == "__main__":
    main()
