#!/usr/bin/env python3
"""List / extract Coded Arms "KCEO ARCDT 1.0" archives (USRDIR/Arc/*.ARC).

Format (verified against CASTEST.ARC, DG.ARC and MAIN.ARC of ULES-00124; see
TECHNICAL_NOTES.md):
  0x00  char[16]  "KCEO ARCDT 1.0\\0\\0"
  0x10  u32       entry count N
  0x14  u32       sector size (2048)
  0x18  u32       table size in bytes (= N * 36)
  ....  '-' padding up to 0x800
  0x800 entry[N], 36 bytes each:
          char[24] name (NUL padded)   u32 start sector   u32 size in bytes   u32 always 0
  After the table: u16 array (purpose unknown, not needed to extract).
Each file starts on a sector boundary. The original archive is never modified.

Usage:
  arc_tool.py list ARCFILE
  arc_tool.py extract ARCFILE OUTDIR
"""
import os
import struct
import sys

MAGIC = b"KCEO ARCDT 1.0"


def read_entries(data):
    if not data.startswith(MAGIC):
        raise ValueError("not a KCEO ARCDT archive")
    count, sector, table = struct.unpack_from("<III", data, 0x10)
    if table != count * 36:
        raise ValueError("unexpected table size %d for %d entries" % (table, count))
    entries = []
    for i in range(count):
        name, start, size, extra = struct.unpack_from("<24sIII", data, 0x800 + i * 36)
        name = name.split(b"\0", 1)[0].decode("ascii")
        offset = start * sector
        if offset + size > len(data):
            raise ValueError("entry %s exceeds archive size" % name)
        entries.append((name, offset, size, extra))
    return entries


def main(argv):
    if len(argv) < 3 or argv[1] not in ("list", "extract"):
        print(__doc__)
        return 2
    with open(argv[2], "rb") as f:
        data = f.read()
    entries = read_entries(data)
    if argv[1] == "list":
        for name, offset, size, _ in entries:
            print("%-24s %10d @0x%08x  %s" % (name, size, offset, data[offset:offset + 4]))
        return 0
    out = argv[3]
    os.makedirs(out, exist_ok=True)
    for name, offset, size, _ in entries:
        if "/" in name or "\\" in name or name.startswith("."):
            raise ValueError("refusing unsafe name %r" % name)
        with open(os.path.join(out, name), "wb") as f:
            f.write(data[offset:offset + size])
    print("extracted %d files to %s" % (len(entries), out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
