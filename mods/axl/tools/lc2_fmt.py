"""Mega Man X Legacy Collection 2 (PC) X6 formats <-> PS1 DAT subfiles (spec: re/lc2/README.md).

ARC v7 archives (nativeDX10/X6/romPC/*.arc) hold zlib entries. For a player (plNN.arc):
  TEX  (0x241f5deb) plNN_kkk  type 2 section k: each frame's tiles as 8bpp texels (CLUT index 0-15), 16 tiles per
                              256-px row, frames stacked in order (same tile stream = same block), unused = 205
  COF  (0x2609378f) plNN_kkk  frame -> texel y of its block (u32[256]) + frame count
  PAT  (0x05c5ac17) plNN      type 3: all sections' frame headers, then all sections' parts (PS1 records)
  COL  (0x619cf7e7) plNN      type 9 as is (PS1 16-bit colors)
pl02.arc carries the Japanese data, pl02_eng.arc the US data.
"""
import os
import struct
import sys
import zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import axl_config as C  # noqa: E402
import datpak  # noqa: E402
import spritesheet as ss  # noqa: E402

H_TEX, H_COF, H_CTEX, H_PAT, H_COL = 0x241f5deb, 0x2609378f, 0x1ec24743, 0x05c5ac17, 0x619cf7e7
ROMPC = C.ROMPC   # (axl_config: the game folder of this machine)
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
DAT = {'us': os.path.join(C.X6_US_FS, 'ROCK_X6.DAT'),
       'jp': os.path.join(C.X6_JP_FS, 'ROCK_X6.DAT'),   # (optional: lc2_build.py's region check)
       'axl': os.path.join(ROOT, 'mods', 'axl', 'work', 'build', 'patch', 'ROCK_X6.DAT')}
BUILT = os.path.join(ROOT, 'mods', 'axl', 'work', 'lc2')   # lc2_build.py output
BACKUP = os.path.join(BUILT, 'backup')                     # pristine copies (lc2_install.py)
GEN = os.path.join(BUILT, 'gen')   # headers made from the game data (lc2_build / lc2_hook), packed into axl_data.bin
# normal X's archives: file -> (PS1 data it carries, PAT entry name)
STSEL = 'stsel_eng.arc'   # stage select: PLAYER SELECT shows "AXL" for the normal X (lc2_build.build_stsel)
KAO = 'kao.arc'   # dialogue portraits: k_plNN_000 (TEX, PS1 DAT entry 163 + 2N) + col k_plNN (COL, entry 164 + 2N)
KAO_TEX = 'X6\\chr\\kao\\k_pl%02d_000'
KAO_COL = 'X6\\col\\kao\\k_pl%02d'
PL02 = {'pl02_eng.arc': ('us', 'X6\\pat\\pl\\pl02_eng\\pl02_eng'), 'pl02.arc': ('jp', 'X6\\pat\\pl\\pl02\\pl02')}
COL_NAME = 'X6\\col\\pl\\pl02\\pl02'
TEX_NAME = 'X6\\chr\\pl\\pl02\\pl02_%03d'
TEX_STUB = {4}   # shipped type 2 section 4 is a 16-px stub with one frame (PS1: 16 frames of 32 tiles)


def orig_path(fn):
    """the shipped archive: the backup once lc2_install.py has replaced the game's copy"""
    b = os.path.join(BACKUP, fn)
    return b if os.path.exists(b) else os.path.join(ROMPC, fn)


TEX_PAD = 205          # texels outside every frame block
ARC_DATA_START = 0x8000  # the header area is padded to 32 KB in the shipped archives


# --- ARC v7 -----------------------------------------------------------------------------------------------------
class Arc:
    """entries: list of dict(name, hash, flags, raw (compressed bytes), data (lazy))"""

    def __init__(self, path):
        b = open(path, 'rb').read()
        assert b[:4] == b'ARC\0', path
        self.version, n = struct.unpack_from('<HH', b, 4)
        self.entries = []
        for i in range(n):
            o = 8 + 80 * i
            name = b[o:o + 64].split(b'\0')[0].decode('latin1')
            h, cs, sz, off = struct.unpack_from('<IIII', b, o + 64)
            self.entries.append({'name': name, 'hash': h, 'flags': sz >> 29, 'size': sz & 0x1fffffff,
                                 'raw': b[off:off + cs], 'data': None})

    def find(self, name, h):
        for e in self.entries:
            if e['name'] == name and e['hash'] == h:
                return e
        raise KeyError((name, hex(h)))

    def get(self, name, h):
        e = self.find(name, h)
        if e['data'] is None:
            e['data'] = zlib.decompress(e['raw'])
            assert len(e['data']) == e['size'], name
        return e['data']

    def put(self, name, h, data):
        e = self.find(name, h)
        e['data'], e['size'], e['raw'] = data, len(data), zlib.compress(data, 6)

    def add(self, name, h, data, after=None):
        """a new entry (flags as the shipped TEX / COF entries: 2), placed after the entry `after` (name, hash)"""
        e = {'name': name, 'hash': h, 'flags': 2, 'size': len(data), 'raw': zlib.compress(data, 6), 'data': data}
        k = len(self.entries) if after is None else self.entries.index(self.find(*after)) + 1
        self.entries.insert(k, e)

    def build(self):
        n = len(self.entries)
        head = bytearray(struct.pack('<4sHH', b'ARC\0', self.version, n))
        start = max(ARC_DATA_START, 8 + 80 * n)
        body, off = [], start
        for e in self.entries:
            head += e['name'].encode('latin1').ljust(64, b'\0')
            head += struct.pack('<IIII', e['hash'], len(e['raw']), e['flags'] << 29 | e['size'], off)
            body.append(e['raw'])
            off += len(e['raw'])
        return bytes(head.ljust(start, b'\0')) + b''.join(body)


# --- PS1 side ---------------------------------------------------------------------------------------------------
def dat_entry86(dat_path):
    """{type: bytes} of the RAM subfiles 2, 3, 9 of ROCK_X6.DAT entry 86"""
    dat = open(dat_path, 'rb').read()
    s, z = struct.unpack_from('<II', dat, 8 * 86)
    b = dat[s * 2048:s * 2048 + z]
    subs = datpak.parse(b)
    assert subs, dat_path
    return {t: b[o:o + zz] for t, o, zz in subs if t in (2, 3, 9)}


def t2_section(t2, k):
    secs = ss.sections(t2)
    return t2[secs[k]:secs[k + 1] if k + 1 < len(secs) else len(t2)]


def t2_frames(t2, k):
    """frame entries (u32) of type 2 section k"""
    sec = ss.sections(t2)[k]
    n = (struct.unpack_from('<I', t2, sec)[0] & 0xfffff) // 4
    return [struct.unpack_from('<I', t2, sec + 4 * f)[0] for f in range(n)]


def t3_section(t3, k):
    """[(cnt, [part records 4 B])] per frame of type 3 section k; frame count = frame 0's offset (headers first)"""
    s = ss.sections(t3)[k]
    nfr = struct.unpack_from('<HH', t3, s)[1]
    out = []
    for f in range(nfr):
        cnt, off = struct.unpack_from('<HH', t3, s + 4 * f)
        out.append([t3[s + 4 * (off + i):s + 4 * (off + i) + 4] for i in range(cnt)])
    return out


# --- encoders ---------------------------------------------------------------------------------------------------
def tex_cof(t2, k, cof_template):
    """(TEX bytes, COF bytes) for type 2 section k; cof_template = an original COF of this slot (header kept)"""
    ents = t2_frames(t2, k)
    blocks, ys, seen, y = [], [], {}, 0
    for f, ent in enumerate(ents):
        n = ent >> 20
        if ent in seen:
            ys.append(seen[ent])
            continue
        if n == 0:
            seen[ent] = 0
            ys.append(0)
            continue
        img, _ = ss.tiles_image(t2, ss.sections(t2)[k], f)
        assert img is not None
        blk = np.zeros((16 * ((n + 15) // 16), 256), np.uint8)
        blk[:img.shape[0], :img.shape[1]] = img
        seen[ent] = y
        ys.append(y)
        blocks.append(blk)
        y += blk.shape[0]
    assert len(ys) <= 256, 'COF holds 256 frames (section %d has %d)' % (k, len(ys))
    h = 16
    while h < y:
        h *= 2
    assert h < 1 << 13, 'TEX height %d does not fit 13 bits (section %d)' % (h, k)
    tex = np.full((h, 256), TEX_PAD, np.uint8)
    if blocks:
        tex[:y] = np.vstack(blocks)
    head = struct.pack('<4sIIII', b'TEX\0', 0x200080a1, 1 | 256 << 6 | h << 19, 0x00011201, 0x14)
    cof = bytearray(cof_template)
    struct.pack_into('<I', cof, 0x29, len(ys))
    struct.pack_into('<256I', cof, 0x2d, *(ys + [0] * (256 - len(ys))))
    return head + tex.tobytes(), bytes(cof)


def pat(sections):
    """PAT bytes from a list of sections, each [[part record]...] per frame (t3_section output)"""
    out = bytearray(struct.pack('<4sII', b'PAT\0', 3, len(sections)))
    for sec in sections:
        out += struct.pack('<I', len(sec)) + struct.pack('<%dI' % len(sec), *(len(p) for p in sec))
    for sec in sections:
        run = len(sec)
        for parts in sec:
            out += struct.pack('<HH', len(parts), run)
            run += len(parts)
    for sec in sections:
        for parts in sec:
            out += b''.join(parts)
    return bytes(out)


def col(t9):
    return struct.pack('<4sII', b'COL\0', 4, len(t9) // 2) + t9


# --- decoders (for checks) --------------------------------------------------------------------------------------
def tex_image(tex):
    dims = struct.unpack_from('<I', tex, 8)[0]
    w, h = dims >> 6 & 0x1fff, dims >> 19
    return np.frombuffer(tex[20:20 + w * h], np.uint8).reshape(h, w)


def name_hash(name):
    """MT Framework resource name hash (CTEX / COF references): ~crc32"""
    return ~zlib.crc32(name.encode('latin1')) & 0xffffffff


def add_texture(arc, ctex_name, name, tex, cof_template, nframes, ys):
    """append TEX + COF `name` to the arc and register it at the end of the CTEX list `ctex_name`; returns its index"""
    cof = bytearray(cof_template)
    ln = cof[0x10]
    old = cof[0x11:0x11 + ln].decode('latin1')   # with its NUL
    assert len(name) + 1 == len(old), (name, old)
    cof[0x0c:0x10] = struct.pack('<I', name_hash(name))
    cof[0x11:0x11 + ln] = name.encode('latin1') + b'\0'
    struct.pack_into('<I', cof, 0x29, nframes)
    struct.pack_into('<256I', cof, 0x2d, *(list(ys) + [0] * (256 - len(ys))))
    last = (TEX_NAME % 18, H_COF)
    arc.add(name, H_TEX, tex, after=last)
    arc.add(name, H_COF, bytes(cof), after=(name, H_TEX))
    return register_texture(arc, ctex_name, name)


def register_texture(arc, ctex_name, name):
    """list the texture `name` (already in the arc) at the end of the CTEX list `ctex_name` too; returns its index"""
    ct = bytearray(arc.get(ctex_name, H_CTEX))
    n = struct.unpack_from('<I', ct, 8)[0]
    ct += struct.pack('<II', name_hash(name), H_COF)
    struct.pack_into('<I', ct, 8, n + 1)
    arc.put(ctex_name, H_CTEX, bytes(ct))
    return n


def cof_ys(cof):
    n = struct.unpack_from('<I', cof, 0x29)[0]
    return list(struct.unpack_from('<%dI' % n, cof, 0x2d))


def pat_sections(p):
    """PAT bytes -> [[part records] per frame] per section"""
    nsec = struct.unpack_from('<I', p, 8)[0]
    o, counts = 12, []
    for _ in range(nsec):
        n = struct.unpack_from('<I', p, o)[0]
        counts.append(struct.unpack_from('<%dI' % n, p, o + 4))
        o += 4 + 4 * n
    hdr = o
    parts0 = hdr + 4 * sum(len(c) for c in counts)
    out, hpos, ppos = [], hdr, parts0
    for c in counts:
        sec = []
        for f in range(len(c)):
            cnt, off = struct.unpack_from('<HH', p, hpos + 4 * f)
            base = ppos + 4 * (off - len(c))
            sec.append([p[base + 4 * i:base + 4 * i + 4] for i in range(cnt)])
        hpos += 4 * len(c)
        ppos += 4 * sum(c)
        out.append(sec)
    return out


def render(parts, tile_img, pal_words, size=320, pbase=0):
    """RGBA array of one frame: parts = records, tile_img = 16-tile-wide texel grid of the frame's tiles"""
    canvas = np.zeros((size, size, 4), np.uint8)
    pal = np.array(pal_words, np.uint32)
    for rec in parts:
        attr, lo, x, y = struct.unpack('<BBbb', rec)
        t = lo | (attr & 3) << 8
        tile = tile_img[(t // 16) * 16:(t // 16) * 16 + 16, (t % 16) * 16:(t % 16) * 16 + 16]
        if tile.shape != (16, 16):
            continue
        tile = np.where(tile == TEX_PAD, 0, tile)
        if attr & 0x40:
            tile = tile[:, ::-1]
        p = pbase + ((attr & 0xc) >> 2)
        c = pal[16 * p + tile.astype(np.uint32)] if 16 * p + 16 <= len(pal) else np.zeros((16, 16), np.uint32)
        rgba = np.stack([(c & 31) << 3, (c >> 5 & 31) << 3, (c >> 10 & 31) << 3, np.full_like(c, 255)], -1).astype(np.uint8)
        x0, y0 = size // 2 + x, size // 2 + y
        dst = canvas[y0:y0 + 16, x0:x0 + 16]
        m = c != 0
        dst[m] = rgba[m]
    return canvas
