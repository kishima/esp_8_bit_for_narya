#!/usr/bin/env python3
"""Build the Narya `roms` partition image.

Layout (little-endian throughout):

  0x0000 ... 0x0FFF   directory page (4 KB)
    0x00..0x07        ASCII magic "NRYAROMS"
    0x08..0x0B        u32 version (1)
    0x0C..0x0F        u32 entry count
    0x10..0xFFF       up to 127 entries of 32 bytes each:
                        0x00..0x17  name (24 bytes, NUL-padded)
                        0x18..0x1B  u32 offset from partition start
                        0x1C..0x1F  u32 size in bytes

  0x1000 ...           ROM data, each ROM offset 4 KB-aligned so that
                       esp_partition_mmap can target it directly.

Usage: build_roms.py <src_dir> <out_path>
       (only files ending in .nes, sorted alphabetically.)
"""

import os
import struct
import sys

DIR_BYTES   = 0x1000
ALIGN       = 0x1000
ENTRY_BYTES = 32
NAME_BYTES  = 24
MAX_ENTRIES = (DIR_BYTES - 0x10) // ENTRY_BYTES  # 127

def main(argv):
    if len(argv) != 3:
        sys.stderr.write("usage: build_roms.py <src_dir> <out_path>\n")
        return 1
    src_dir, out_path = argv[1], argv[2]

    files = sorted(
        f for f in os.listdir(src_dir)
        if f.lower().endswith(".nes") and not f.startswith(".")
    )
    if len(files) > MAX_ENTRIES:
        sys.stderr.write("too many ROMs: {} > {}\n".format(len(files), MAX_ENTRIES))
        return 1

    entries = []        # (name, offset, size, data)
    cursor = DIR_BYTES
    for name in files:
        path = os.path.join(src_dir, name)
        with open(path, "rb") as f:
            data = f.read()
        entries.append((name, cursor, len(data), data))
        cursor = (cursor + len(data) + ALIGN - 1) & ~(ALIGN - 1)

    total_size = cursor

    buf = bytearray(total_size)
    buf[0:8] = b"NRYAROMS"
    struct.pack_into("<I", buf, 0x08, 1)
    struct.pack_into("<I", buf, 0x0C, len(entries))

    for i, (name, offset, size, data) in enumerate(entries):
        ent = 0x10 + i * ENTRY_BYTES
        nb = name.encode("ascii", "replace")[:NAME_BYTES - 1]
        buf[ent:ent + len(nb)] = nb
        struct.pack_into("<I", buf, ent + NAME_BYTES,     offset)
        struct.pack_into("<I", buf, ent + NAME_BYTES + 4, size)
        buf[offset:offset + size] = data

    out_dir = os.path.dirname(out_path)
    if out_dir and not os.path.isdir(out_dir):
        os.makedirs(out_dir, exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(buf)

    print("rom image: {} ({} bytes, {} ROMs)".format(out_path, total_size, len(entries)))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
