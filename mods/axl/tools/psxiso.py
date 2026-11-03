"""Minimal ISO9660 reader for PSX MODE2/2352 images.

list:    python3 psxiso.py list  image.bin
extract: python3 psxiso.py extract image.bin outdir
"""
import os
import struct
import sys

SECTOR = 2352
DATA_OFF = 24  # sync(12) + header(4) + subheader(8)


def read_sector(f, lba):
    f.seek(lba * SECTOR)
    return f.read(SECTOR)


def read_data(f, lba, size):
    out = bytearray()
    n = (size + 2047) // 2048
    for i in range(n):
        out += read_sector(f, lba + i)[DATA_OFF:DATA_OFF + 2048]
    return bytes(out[:size])


def walk(f, lba, size, path=""):
    data = read_data(f, lba, size)
    pos = 0
    while pos < len(data):
        ln = data[pos]
        if ln == 0:
            pos = (pos // 2048 + 1) * 2048
            continue
        rec = data[pos:pos + ln]
        ext = struct.unpack_from("<I", rec, 2)[0]
        sz = struct.unpack_from("<I", rec, 10)[0]
        flags = rec[25]
        nlen = rec[32]
        name = rec[33:33 + nlen]
        pos += ln
        if name in (b"\x00", b"\x01"):
            continue
        name = name.decode("ascii", "replace").split(";")[0]
        full = f"{path}/{name}"
        if flags & 2:
            yield from walk(f, ext, sz, full)
        else:
            yield full, ext, sz


def entries(f):
    pvd = read_sector(f, 16)[DATA_OFF:DATA_OFF + 2048]
    assert pvd[1:6] == b"CD001", "no PVD"
    root = pvd[156:156 + 34]
    return list(walk(f, struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0]))


def main():
    cmd, img = sys.argv[1], sys.argv[2]
    with open(img, "rb") as f:
        ents = entries(f)
        if cmd == "list":
            for p, lba, sz in ents:
                print(f"{lba:8d} {sz:10d} {p}")
        elif cmd == "extract":
            outdir = sys.argv[3]
            for p, lba, sz in ents:
                dst = os.path.join(outdir, p.lstrip("/"))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                # XA/STR streams are form2; skip raw-extracting them as 2048-byte data
                if p.upper().endswith((".XA", ".STR")):
                    continue
                with open(dst, "wb") as o:
                    o.write(read_data(f, lba, sz))


if __name__ == "__main__":
    main()
