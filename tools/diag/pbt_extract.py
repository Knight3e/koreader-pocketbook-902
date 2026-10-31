#!/usr/bin/env python3
"""pbt_extract.py - print the layout config (theme.cfg) inside a PocketBook theme file (*.pbt, format version 1).
Usage:  python3 pbt_extract.py modern.pbt            (prints the config)
        python3 pbt_extract.py modern.pbt theme.cfg  (writes it to a file)
Format (from the open-source 'pbtheme' tool): "PocketBookTheme", version byte, header size, ..., the config is zlib-compressed:
  uint32 at byte 20 = size when unpacked, uint32 at 24 = offset of the packed data, uint32 at 28 = size of the packed data.
"""
import struct
import sys
import zlib


def extract(path):
    d = open(path, "rb").read()
    if d[:15] != b"PocketBookTheme":
        sys.exit("%s is not a PocketBook theme file" % path)
    if d[15] != 1:
        sys.exit("unsupported theme version %d" % d[15])
    unpacked_len, offset, packed_len = struct.unpack_from("<III", d, 20)
    return zlib.decompress(d[offset:offset + packed_len])


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cfg = extract(sys.argv[1])
    if len(sys.argv) > 2:
        open(sys.argv[2], "wb").write(cfg)
        print("written %d bytes to %s" % (len(cfg), sys.argv[2]))
    else:
        sys.stdout.write(cfg.decode("utf-8", "replace"))
