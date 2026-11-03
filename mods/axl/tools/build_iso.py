"""Rebuild a PSX MODE2/2352 image with replaced files.

usage: build_iso.py orig.bin patchdir out.bin

Every file under patchdir replaces the ISO file with the same path (case-insensitive).
A file that still fits in its original extent is rewritten in place; a larger one is
moved into the ZNULL.DAT padding file at the end of the disc, which shrinks accordingly,
or appended past the end of the image when the padding is too small.
Directory records are updated and EDC/ECC is recomputed for every touched sector.
"""
import os
import shutil
import struct
import subprocess
import sys

SECTOR = 2352
DATA_OFF = 24
PAD_FILE = '/ZNULL.DAT'
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import native  # noqa: E402

FIXSEC = native.path('fixsec')


def bcd(n):
    return (n // 10) << 4 | n % 10


def sector_header(lba):
    a = lba + 150
    return bytes([bcd(a // 4500), bcd(a // 75 % 60), bcd(a % 75), 2])


def read_user(f, lba):
    f.seek(lba * SECTOR + DATA_OFF)
    return f.read(2048)


def records(f):
    """Yield (path, lba, size, dir_lba, offset_in_dir_data) for every file record."""
    pvd = read_user(f, 16)
    assert pvd[1:6] == b'CD001', 'no PVD'
    root = pvd[156:156 + 34]

    def walk(lba, size, path):
        data = b''.join(read_user(f, lba + i) for i in range((size + 2047) // 2048))
        pos = 0
        while pos < size:
            ln = data[pos]
            if ln == 0:
                pos = (pos // 2048 + 1) * 2048
                continue
            rec = data[pos:pos + ln]
            ext, sz = struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]
            name = rec[33:33 + rec[32]]
            if name not in (b'\x00', b'\x01'):
                full = path + '/' + name.decode('ascii').split(';')[0]
                if rec[25] & 2:
                    yield from walk(ext, sz, full)
                else:
                    yield full, ext, sz, lba + pos // 2048, pos % 2048
            pos += ln

    yield from walk(struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0], '')


def write_file_sectors(f, lba, data):
    n = max(1, (len(data) + 2047) // 2048)
    for i in range(n):
        chunk = data[i * 2048:(i + 1) * 2048].ljust(2048, b'\0')
        sub = bytes([0, 0, 0x89 if i == n - 1 else 0x08, 0]) * 2
        sec = b'\x00' + b'\xff' * 10 + b'\x00' + sector_header(lba + i) + sub + chunk + b'\0' * 280
        f.seek((lba + i) * SECTOR)
        f.write(sec)
    return n


def set_record(f, dir_lba, off, lba, size):
    f.seek(dir_lba * SECTOR + DATA_OFF + off + 2)
    f.write(struct.pack('<I', lba) + struct.pack('>I', lba) + struct.pack('<I', size) + struct.pack('>I', size))


def build(orig, patchdir, out):
    shutil.copyfile(orig, out)
    with open(out, 'r+b') as f:
        recs = {p.upper(): (p, lba, sz, dl, off) for p, lba, sz, dl, off in records(f)}
        pad = recs[PAD_FILE]
        pad_lba, pad_size = pad[1], pad[2]
        f.seek(0, 2)
        image_end = end_lba = f.tell() // SECTOR
        touched = []
        placed = {}     # iso path -> LBA (returned: test tools rebuild the game's sector tables from it)
        for root, _, files in os.walk(patchdir):
            for name in sorted(files):
                src = os.path.join(root, name)
                iso_path = '/' + os.path.relpath(src, patchdir).replace(os.sep, '/')
                key = iso_path.upper()
                if key not in recs or key == PAD_FILE:
                    sys.exit(f'{iso_path}: not a replaceable file in the image')
                path, lba, size, dl, off = recs[key]
                data = open(src, 'rb').read()
                need = (len(data) + 2047) // 2048
                if need > (size + 2047) // 2048:
                    if need * 2048 <= pad_size:
                        lba = pad_lba
                        pad_lba += need
                        pad_size -= need * 2048
                    else:
                        lba = end_lba
                        end_lba += need
                    print(f'{path}: relocated to LBA {lba} ({len(data)} bytes)')
                else:
                    print(f'{path}: in place at LBA {lba} ({len(data)} bytes)')
                placed[path] = lba
                n = write_file_sectors(f, lba, data)
                set_record(f, dl, off, lba, len(data))
                touched += [(lba, n), (dl, 1)]
        if pad_lba != pad[1]:
            set_record(f, pad[3], pad[4], pad_lba, pad_size)
            touched.append((pad[3], 1))
        if end_lba != image_end:
            # files appended past the old end: grow the volume space size in the PVD
            f.seek(16 * SECTOR + DATA_OFF + 80)
            f.write(struct.pack('<I', end_lba) + struct.pack('>I', end_lba))
            touched.append((16, 1))
    if touched:
        args = [str(x) for t in touched for x in t]
        subprocess.run([native.build('fixsec'), out] + args, check=True)
    cue = os.path.splitext(out)[0] + '.cue'
    with open(cue, 'w') as c:
        c.write(f'FILE "{os.path.basename(out)}" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n')
    return placed


if __name__ == '__main__':
    build(*sys.argv[1:4])
