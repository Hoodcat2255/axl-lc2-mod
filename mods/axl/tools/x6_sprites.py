"""PS1 Mega Man X6 sprite container helpers, taken from mods/highmax/tools/build_hm_x6.py (the parts the Axl build
uses): the tile LZ encoder / decoder (FUN_80018be0 format), type 2 / 3 sections, section lists, the body palette
lists and the zig / MIPS I settings for the PS1 C code.

BODY_PALETTES / FLASH_PALETTES / DECO_PALETTES are indices of X's palettes in DAT 86 type 9 (which palette is body,
flash or decoration), not colours.
"""
import struct
import sys

import numpy as np

ZIG = [sys.executable, '-m', 'ziglang']
C_FLAGS = ['-target', 'mipsel-freestanding-none', '-mcpu=mips1', '-O2', '-fno-pic', '-mno-abicalls', '-G0',
           '-fomit-frame-pointer', '-ffreestanding', '-fno-builtin', '-ffunction-sections', '-fdata-sections',
           '-mno-check-zero-division']  # clang otherwise adds teq (MIPS II: reserved instruction on the R3000)

BODY_PALETTES = tuple(range(0, 33, 4)) + (73, 82)
FLASH_PALETTES = (35, 38, 41, 44, 47, 50)  # body tinted toward X's flash color (38/41 charge, 50 shot seen in traces)
DECO_PALETTES = tuple(range(3, 32, 4)) + (7, 10, 11, 14, 15, 18, 19, 22, 27, 30, 57, 63, 68, 69, 74)
FLASH_MIX = 0.4


# --- LZ (decoder: FUN_80018be0 / tools/spritesheet.lz) ---------------------------------------------
def lz_encode(words):
    """optimal parse for FUN_80018be0: literal / short ref (len 2-31) / short zero run (1-31, 1 word) /
    long ref or zero run (len 32-65535, 2 words). Same stream format as before."""
    n = len(words)
    INF = 10 ** 9
    C1, C2 = 17, 33                      # 1 word + 1/16 control, 2 words + 1/16
    dp, back = [INF] * (n + 1), [None] * (n + 1)
    dp[0] = 0
    zr = [0] * (n + 1)                   # zero-run length starting at i
    for i in range(n - 1, -1, -1):
        zr[i] = zr[i + 1] + 1 if words[i] == 0 else 0
    index = {}
    for i in range(n):
        d = dp[i]
        if d == INF:
            continue
        if d + C1 < dp[i + 1]:
            dp[i + 1], back[i + 1] = d + C1, ('lit', 1, 0)
        z = zr[i]
        for L in range(1, min(z, 31) + 1):
            if d + C1 < dp[i + L]:
                dp[i + L], back[i + L] = d + C1, ('zs', L, 0)
        for L in range(32, min(z, 0xffff) + 1):
            if d + C2 < dp[i + L]:
                dp[i + L], back[i + L] = d + C2, ('zl', L, 0)
        if i + 1 < n:
            key = (words[i], words[i + 1])
            best, bd = 0, 0
            lst = index.setdefault(key, [])
            for p in reversed(lst):          # every candidate in the 0x7ff window
                if i - p > 0x7ff:
                    break
                L = 2
                while i + L < n and words[p + L] == words[i + L] and L < 0xffff:
                    L += 1
                if L > best:
                    best, bd = L, i - p
                if best >= 96:
                    break
            lst.append(i)
            for L in range(2, min(best, 31) + 1):
                if d + C1 < dp[i + L]:
                    dp[i + L], back[i + L] = d + C1, ('rs', L, bd)
            for L in range(32, best + 1):
                if d + C2 < dp[i + L]:
                    dp[i + L], back[i + L] = d + C2, ('rl', L, bd)
    items, i = [], n
    while i > 0:
        k, L, dist = back[i]
        items.append(('l', [words[i - 1]]) if k == 'lit' else
                     ('r', [L << 11]) if k == 'zs' else
                     ('r', [0, L]) if k == 'zl' else
                     ('r', [(L << 11) | dist]) if k == 'rs' else
                     ('r', [dist, L]))
        i -= L
    items.reverse()
    items.append(('r', [0, 0]))
    out = []
    for g in range(0, len(items), 16):
        grp = items[g:g + 16]
        ctl = sum(1 << (15 - k) for k, it in enumerate(grp) if it[0] == 'r')
        out.append(ctl)
        for it in grp:
            out.extend(it[1])
    return struct.pack('<%dH' % len(out), *out)


def lz_decode(data, off=0):
    out, p = [], off
    while True:
        ctl = struct.unpack_from('<H', data, p)[0]; p += 2
        for bit in range(15, -1, -1):
            w = struct.unpack_from('<H', data, p)[0]; p += 2
            if not ctl >> bit & 1:
                out.append(w)
                continue
            if w & 0xf800 == 0:
                dist, ln = w, struct.unpack_from('<H', data, p)[0]; p += 2
            else:
                ln, dist = w >> 11, w & 0x7ff
            if ln == 0 and dist == 0:
                return out
            if dist == 0:
                out += [0] * ln
            else:
                for _ in range(ln):
                    out.append(out[-dist])


# --- sprite data ---------------------------------------------------------------------------------------
def tile_block(tiles):
    """Decompressed upload buffer: full rows of 16 tiles as 256-px lines, then the remainder packed
    (rem*16 px per line), matching the rects FUN_80017b04 uploads (rows first, then the rest)."""
    n = len(tiles)
    blocks = []
    for r0 in range(0, n - n % 16, 16):
        blocks.append(np.hstack(tiles[r0:r0 + 16]))
    if n % 16:
        blocks.append(np.hstack(tiles[n - n % 16:]))
    raw = b''
    for b in blocks:
        b = b.astype(np.uint8)
        raw += (b[:, 0::2] | (b[:, 1::2] << 4)).astype(np.uint8).tobytes()
    return list(struct.unpack('<%dH' % (len(raw) // 2), raw))


def type2_section(frame_tiles):
    n = len(frame_tiles)
    streams, offs = {}, []
    body = bytearray()
    for tiles in frame_tiles:
        key = id(tiles)
        if key not in streams:
            words = tile_block(tiles)
            enc = lz_encode(words)
            assert lz_decode(enc) == words
            streams[key] = (4 * n + len(body), len(tiles))
            body += enc
            body += b'\0' * (-len(body) % 4)
        offs.append(streams[key])
    head = b''.join(struct.pack('<I', o | (t << 20)) for o, t in offs)
    return head + bytes(body)


def type3_section(frame_parts):
    n = len(frame_parts)
    body, offs, cache = bytearray(), [], {}
    for ps in frame_parts:
        key = tuple(ps)
        if key not in cache:
            cache[key] = (len(ps), (4 * n + len(body)) // 4)
            for p in ps:
                body += struct.pack('<BBbb', *p)
        offs.append(cache[key])
    return b''.join(struct.pack('<HH', c, o) for c, o in offs) + bytes(body)


def split_sections(blob):
    first = struct.unpack_from('<I', blob, 0)[0]
    offs = [struct.unpack_from('<I', blob, 4 * i)[0] for i in range(first // 4)]
    ends = offs[1:] + [len(blob)]
    return [blob[a:b] for a, b in zip(offs, ends)]


def join_sections(secs):
    head = 4 * len(secs)
    out, offs = bytearray(), []
    for s in secs:
        offs.append(head + len(out))
        out += s
        out += b'\0' * (-len(out) % 4)
    return b''.join(struct.pack('<I', o) for o in offs) + bytes(out)


def tint(body, flash):
    """Blend each body color toward the mean color of X's flash palette (index 0 stays transparent)."""
    rgb = lambda v: np.stack([v & 31, v >> 5 & 31, v >> 10 & 31], -1).astype(float)
    f = rgb(flash[1:]).mean(0)
    c = rgb(body) * (1 - FLASH_MIX) + f * FLASH_MIX
    c = np.clip(np.rint(c), 0, 31).astype(np.uint16)
    out = (c[:, 0] | c[:, 1] << 5 | c[:, 2] << 10 | (body & 0x8000)).astype('<u2')
    out[body == 0] = 0
    out[(out == 0) & (body != 0)] = 1 << 10  # keep opaque: 0x0000 is transparent on PS1
    return out


NOT_MIPS1 = ('teq', 'teqi', 'tne', 'tnei', 'tge', 'tgeu', 'tlt', 'tltu', 'beql', 'bnel', 'blezl', 'bgtzl', 'bltzl',
             'bgezl', 'll', 'sc', 'sync', 'movz', 'movn', 'mul', 'madd', 'maddu', 'msub', 'clz', 'ldc1', 'sdc1')
