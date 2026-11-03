"""Replace entries of ROCK_X?.DAT / .BIN (table of <sector,size> pairs at the file start, 2048-byte sectors).

usage: patch_dat.py orig.dat out.dat index=file [index=file ...]
An entry that fits in its old sector span is rewritten in place (rest of the span keeps old bytes);
a larger one is appended at the end of the file. The table entry is updated either way."""
import struct
import sys


def patch(orig, repl):
    b = bytearray(open(orig, 'rb').read())
    for idx, data in sorted(repl.items()):
        sec, size = struct.unpack_from('<II', b, 8 * idx)
        nxt = struct.unpack_from('<I', b, 8 * (idx + 1))[0] if struct.unpack_from('<I', b, 8 * (idx + 1))[0] else len(b) // 2048
        span = (nxt - sec) * 2048
        if len(data) <= span:
            b[sec * 2048:sec * 2048 + len(data)] = data
        else:
            sec = len(b) // 2048
            b += data + b'\0' * (-len(data) % 2048)
        struct.pack_into('<II', b, 8 * idx, sec, len(data))
    return bytes(b)


if __name__ == '__main__':
    repl = {}
    for a in sys.argv[3:]:
        i, f = a.split('=', 1)
        repl[int(i)] = open(f, 'rb').read()
    open(sys.argv[2], 'wb').write(patch(sys.argv[1], repl))
