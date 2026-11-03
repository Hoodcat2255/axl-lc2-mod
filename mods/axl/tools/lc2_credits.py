r"""Sprite-sheet credits at the end of X6's staff roll (Legacy Collection 2 PC, English roll: cledit_eng_reg_wor.arc).

The roll (stage 0x0F sub 1) draws a strip of map screens 1..28 left to right (layout in RXC2.exe):
  OCL  (0x6bc1bac7) X6\stage\cel\cledit_eng          "OCL\0", u32 1, u32 count, then count x (u16 attr, u16 tile)
  OMP  (0x11320e86) X6\stage\map\cledit_eng_reg_wor  "OMP\0", u32 1, u32 words, then 43 screens of 16 x 16 u16 words,
                                                     word = 0x4000 | cel index (0 = empty)
  TEX  (0x241f5deb) X6\stage\dds\cledit_eng(_chr256) 2048 x 2048 8bpp texels (4bpp values 0-15); tile t is the 16 x 16
                                                     block x = (p % 8) * 256 + (t & 15) * 16, y = (p // 8) * 256 +
                                                     (t >> 4 & 15) * 16, p = t >> 8. Screens 1-14 use cledit_eng,
                                                     15-28 cledit_eng_chr256.
Screens 27 and 28 held the PS1 theme-song credits; LC2 zeroed their map words, so they are blank roll space at the end.
build_credits() draws CREDIT_LINES there: text cells go into texture pages no cel uses (FREE_PAGES, written into both
textures), one new cel (attr 0) per distinct cell is appended to the OCL, and the two screens' map words point at them.

Glyphs: the roll's own lettering, cut out of the shipped strip (HARVEST: lines whose text is known; monospace, the
cut is checked against the letter count). The roll never uses p q x X V 2 3 9 / - ( ) . : p / q are b / d mirrored
about the x-height band, 9 is 6 turned 180 degrees, the rest are SYNTH bitmaps rendered once from DejaVu Sans Bold
(squeezed to the roll's size, coverage * 15, 1-px ring of value 1 like the roll's letters) and frozen here.
"""
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lc2_fmt as F  # noqa: E402

ARC = 'cledit_eng_reg_wor.arc'
H_OCL, H_OMP = 0x6bc1bac7, 0x11320e86
OCL_NAME = 'X6\\stage\\cel\\cledit_eng'
OMP_NAME = 'X6\\stage\\map\\cledit_eng_reg_wor'
TEX_NAMES = ('X6\\stage\\dds\\cledit_eng', 'X6\\stage\\dds\\cledit_eng_chr256')   # bank A (screens 1-14), B (15-28)
SHIPPED_CELS = 3629
SCREENS = (27, 28)          # blank in LC2; the last two screens of the strip
FREE_PAGES = (12, 13, 17)   # texture pages no shipped cel references (empty in both textures)
MAX_CELS = 0x1000           # map word = 0x4000 | 12-bit cel index

TITLE = 'SPRITE CREDITS (AXL)'
# the sheet's own list and order: JuggaloManX's "Ultimate Axl Sprite Sheet (2021)" on DeviantArt (CREDITS.md)
NAMES = ('Capcom', 'megafan97', 'JuggaloManX', 'DoriePiecrust', 'Yufforia', 'MasterChiefRamsay', 'Blackbeltdude',
         'brongaa12', 'Busteromegamon', 'Commandmissionfan', 'FXFreitas', 'hansungkee', 'HeatTeamPro', 'Kensuyjin33',
         'megaguy2222', 'NONE/Dragon', 'Rocklu', 'SlateDP', 'TheUltimateDark-Of', 'V-Nico')
HOSTS = 'The Spriters Resource / Sprite Database / Sprites Inc.'
TAIL = 'and the discord friends of the sheet'
MOD = 'MOD BY hoodcat'   # the mod's maker (user request 2026-10-27), right of the title
CREDIT_LINES = (TITLE, MOD) + NAMES + (HOSTS, TAIL)

# lettering: 'h' = section headings (SPECIAL THANKS), 'n' = names. Headings keep the roll's monospace advance
# (PRODUCER: 8 letters in 104 px); names are set proportionally (each letter's own width, the 1-px rings shared) so
# that 'l' / 'i' leave no holes ("Bl ackbel tdude" at a fixed pitch) and three columns of the sheet's names fit 512 px
PITCH = {'h': 13.1}
SPACE = 5                    # a word space in the names' lettering
CELL_H, CELL_BASE = 24, 17   # glyph cells: 24 rows, the baseline (last row of a capital) is row 17

# layout in the 512 x 256 area of screens 27-28 (x, baseline y); the roll's text keeps to block rows 1-13 (y 16-223)
TITLE_AT = (4, 34)
# the names run on in list order, as many to a line as fit (three columns would need 516 px for the three longest)
NAME_Y0, LINE = 60, 22   # first names' baseline, line pitch
NAME_GAP = 18            # between two names on a line
NAME_X0, RIGHT = 8, 6    # left edge of the names (the title is at 4, the roll indents names under headings), margin

# strip x = (screen - 1) * 256 of screens 1..26 drawn side by side; (x0, y0, x1, y1, text, font)
HARVEST = (
    (5900, 203, 6118, 223, 'and CAPCOM all staff', 'n'),
    (5901, 53, 5958, 73, 'BUPPO', 'n'),
    (5902, 83, 6063, 103, 'JYUNKO FUJISAKI', 'n'),
    (5902, 113, 6079, 133, 'KOUJIROU OGIWARA', 'n'),
    (5901, 143, 6053, 163, 'Toshizi Hazumi', 'n'),
    (5902, 173, 6024, 193, 'Koichi Hase', 'n'),
    (5681, 203, 5835, 223, 'Akiko Watanabe', 'n'),
    (5681, 54, 5846, 74, 'TOMOKAZU KADOUE', 'n'),
    (5681, 83, 5847, 103, 'SINICHI SIOHARA', 'n'),
    (5681, 143, 5868, 163, 'JYUNICHIROU OGAWA', 'n'),
    (6203, 74, 6343, 93, 'TATUYA MINAMI', 'n'),
    (6202, 184, 6357, 204, 'YOSIKI OKAMOTO', 'n'),
    (1074, 115, 1259, 134, 'Watanabe Masaharu', 'n'),
    (1586, 74, 1758, 94, 'Kimio Yamazoe', 'n'),
    (1586, 104, 1784, 126, 'Tuyosi Fujisawa', 'n'),
    (2609, 73, 2753, 95, 'Yuji Yamamoto', 'n'),
    (2609, 103, 2753, 123, 'Yosihisa Ueda', 'n'),
    (2849, 73, 3014, 95, 'Tomomi Fujiwara', 'n'),
    (2850, 133, 2958, 156, 'Megumi Doi', 'n'),
    (4657, 54, 4854, 76, 'Syoutarou Morikubo', 'n'),
    (4657, 84, 4800, 104, 'Rumi Kasahara', 'n'),
    (4657, 114, 4798, 137, 'Wataru Takagi', 'n'),
    (614, 84, 725, 104, 'OHKOOMI 16', 'n'),
    (5254, 62, 5281, 83, '75', 'n'),          # the end of RoBAYAN'75
    (5670, 20, 5858, 44, 'SPECIAL THANKS', 'h'),
    (6185, 40, 6293, 64, 'PRODUCER', 'h'),
    (6184, 150, 6424, 174, 'EXECUTIVE PRODUCER', 'h'),
    (580, 51, 688, 74, 'DIRECTOR', 'h'),
)
CORE = 3   # texel value counted as letter body when cutting letters apart (1 = ring, 2 = faint edge)

# letters the roll never uses (DejaVu Sans Bold, see the module doc); 24 rows, baseline row 17
SYNTH = {
    ('n', 'X'): (
        '............',
        '............',
        '............',
        '............',
        '111111111111',
        '16ff7118ff61',
        '12efe22efe21',
        '117ff77ff711',
        '.12efeefe21.',
        '.117ffff811.',
        '..12effe21..',
        '..11cffd11..',
        '..15ffff51..',
        '.11cffffc11.',
        '.14ffaaff51.',
        '11cff33ffc11',
        '14ffb11aff41',
        '159931139951',
        '111111111111',
        '............',
        '............',
        '............',
        '............',
        '............',
    ),
    ('n', 'V'): (
        '............',
        '............',
        '............',
        '............',
        '111111111111',
        '1bff2112ffa1',
        '17ff4115ff71',
        '13ff8119ff31',
        '11efc11dfe11',
        '.1aff22ffa1.',
        '.16ff55ff61.',
        '.12ff99ff21.',
        '.11dfddfd11.',
        '..19ffff91..',
        '..15ffff51..',
        '..12ffff21..',
        '..11cffc11..',
        '...169951...',
        '...111111...',
        '............',
        '............',
        '............',
        '............',
        '............',
    ),
    ('n', 'x'): (
        '...........',
        '...........',
        '...........',
        '...........',
        '...........',
        '...........',
        '...........',
        '11111111111',
        '15ff818ff51',
        '11dfe3efd11',
        '.15ffeff51.',
        '.11cfffb11.',
        '..14fff41..',
        '.118fff811.',
        '.12fffff21.',
        '11aff8ffa11',
        '13ffb1bff31',
        '15993139951',
        '11111111111',
        '...........',
        '...........',
        '...........',
        '...........',
        '...........',
    ),
    ('n', '2'): (
        '.........',
        '.........',
        '.........',
        '..11111..',
        '11134211.',
        '18efff811',
        '1dfffff51',
        '1dfadffa1',
        '19212ffc1',
        '11111ffb1',
        '..116ff71',
        '.113efe21',
        '.12dff411',
        '11cff7111',
        '19ffd6641',
        '1efffffd1',
        '1efffffd1',
        '189999981',
        '111111111',
        '.........',
        '.........',
        '.........',
        '.........',
        '.........',
    ),
    ('n', '3'): (
        '..........',
        '..........',
        '..........',
        '..111111..',
        '.11233211.',
        '.18ffff911',
        '.1afffff61',
        '.1adacffa1',
        '.12112ffb1',
        '.11247ff81',
        '..19fffe21',
        '..19ffff41',
        '..148affb1',
        '.11111dfe1',
        '11a313efd1',
        '12ffffffa1',
        '12fffffe31',
        '116bdda311',
        '.11111111.',
        '..........',
        '..........',
        '..........',
        '..........',
        '..........',
    ),
    ('n', '/'): (
        '........',
        '........',
        '........',
        '........',
        '...11111',
        '...16f91',
        '...1af61',
        '..11ef21',
        '..12fd11',
        '..16f91.',
        '..1af61.',
        '.11df21.',
        '.12fd11.',
        '.16f91..',
        '.1af61..',
        '11df21..',
        '12fd11..',
        '16f91...',
        '1af61...',
        '12211...',
        '1111....',
        '........',
        '........',
        '........',
    ),
    ('n', '-'): (   # (6 px of body: the narrower DejaVu one read as a dot between proportional letters)
        '..........',
        '..........',
        '..........',
        '..........',
        '..........',
        '..........',
        '..........',
        '..........',
        '..........',
        '..........',
        '1111111111',
        '12ffffff21',
        '12ffffff21',
        '12ffffff21',
        '1111111111',
        '..........',
        '..........',
        '..........',
        '..........',
        '..........',
        '..........',
        '..........',
        '..........',
        '..........',
    ),
    ('n', '.'): (
        '......',
        '......',
        '......',
        '......',
        '......',
        '......',
        '......',
        '......',
        '......',
        '......',
        '......',
        '......',
        '......',
        '111111',
        '16ff61',
        '1ffff1',
        '1ffff1',
        '169961',
        '111111',
        '......',
        '......',
        '......',
        '......',
        '......',
    ),
    ('h', '('): (
        '........',
        '..111111',
        '..15dd71',
        '.11cff51',
        '.13ffe11',
        '.18ff91.',
        '11dff51.',
        '12fff21.',
        '15ffe11.',
        '17ffc1..',
        '18ffb1..',
        '18ffb1..',
        '17ffc1..',
        '16ffd1..',
        '13fff11.',
        '11fff31.',
        '.1bff71.',
        '.16ffc11',
        '.12eff21',
        '.119ff71',
        '..126631',
        '..111111',
        '........',
        '........',
    ),
    ('h', ')'): (
        '........',
        '111111..',
        '17dd51..',
        '15ffc11.',
        '11eff31.',
        '.1aff81.',
        '.15ffd11',
        '.12fff21',
        '.11eff41',
        '..1dff61',
        '..1cff81',
        '..1bff81',
        '..1cff71',
        '.11dff61',
        '.12fff31',
        '.14ffe11',
        '.17ffb1.',
        '11cff61.',
        '12ffe21.',
        '18ff811.',
        '136621..',
        '111111..',
        '........',
        '........',
    ),
}


# --- resources --------------------------------------------------------------------------------------------------
def tile_xy(t):
    p = t >> 8
    return (p % 8) * 256 + (t & 15) * 16, (p // 8) * 256 + (t >> 4 & 15) * 16


def read_ocl(b):
    assert b[:4] == b'OCL\0', b[:4]
    n = struct.unpack_from('<I', b, 8)[0]
    return np.frombuffer(b, np.uint16, 2 * n, 12).reshape(n, 2).copy()


def read_omp(b):
    assert b[:4] == b'OMP\0', b[:4]
    n = struct.unpack_from('<I', b, 8)[0]
    return np.frombuffer(b, np.uint16, n, 12).reshape(-1, 16, 16).copy()


def tex_pixels(tex):
    return F.tex_image(tex).copy()


def load(path):
    """(arc, cels, map, [texture A, texture B]) of a cledit_eng_reg_wor.arc"""
    arc = F.Arc(path)
    cels = read_ocl(arc.get(OCL_NAME, H_OCL))
    mp = read_omp(arc.get(OMP_NAME, H_OMP))
    texs = [tex_pixels(arc.get(n, F.H_TEX)) for n in TEX_NAMES]
    return arc, cels, mp, texs


def bank(screen):
    return 0 if screen < 15 else 1


def draw_screen(cels, mp, texs, s):
    """screen s as texel values (256 x 256)"""
    out = np.zeros((256, 256), np.uint8)
    im = texs[bank(s)]
    for r in range(16):
        for c in range(16):
            w = int(mp[s, r, c])
            if w:
                x, y = tile_xy(int(cels[w & 0xfff, 1]))
                out[r * 16:r * 16 + 16, c * 16:c * 16 + 16] = im[y:y + 16, x:x + 16]
    return out


# --- glyphs -----------------------------------------------------------------------------------------------------
def _runs(mask):
    r, s = [], None
    for i, v in enumerate(list(mask) + [False]):
        if v and s is None:
            s = i
        elif not v and s is not None:
            r.append((s, i))
            s = None
    return r


def harvest(strip):
    """{font: {letter: cell}} cut from the shipped roll; a letter cut from a lone run beats one split off a run of
    touching letters"""
    glyphs, quality = {'h': {}, 'n': {}}, {'h': {}, 'n': {}}
    for x0, y0, x1, y1, text, font in HARVEST:
        reg = strip[y0:y1, x0:x1]
        core = reg >= CORE
        parts = []
        for s, e in _runs(core.any(0)):
            k = max(1, int(round((e - s + 1.5) / 10.67 if font == 'n' else (e - s + 1.5) / 13.1)))
            if k == 1:
                parts.append((s, e, 0))
                continue
            cuts = [s]
            for j in range(1, k):   # touching letters: cut at the faintest column near the even split
                c = int(round(s + (e - s) * j / k))
                cuts.append(c - 2 + int(reg[:, c - 2:c + 3].astype(int).sum(0).argmin()))
            cuts.append(e)
            parts += [(cuts[j] + (1 if j else 0), cuts[j + 1], 1) for j in range(k)]
        letters = text.replace(' ', '')
        assert len(parts) == len(letters), (text, len(parts))
        bottoms = [int(np.nonzero(core[:, s:e].any(1))[0].max()) for s, e, _ in parts]
        base = int(np.bincount(bottoms).argmax())
        for (s, e, q), ch in zip(parts, letters):
            if ch in glyphs[font] and quality[font][ch] <= q:
                continue
            g = reg[:, max(s - 1, 0):e + 1]
            cell = np.zeros((CELL_H, g.shape[1]), np.uint8)
            for r in range(CELL_H):
                y = base - CELL_BASE + r
                if 0 <= y < g.shape[0]:
                    cell[r] = g[y]
            # the edge columns hold the ring; anything brighter there is the neighbour letter's edge
            cell[:, [0, -1]] = np.minimum(cell[:, [0, -1]], 1)
            glyphs[font][ch], quality[font][ch] = cell, q
    return glyphs


def all_glyphs(strip):
    g = harvest(strip)
    n = g['n']
    xh_top = CELL_BASE - 9   # first row of the x-height band (a, b's bowl), measured on the roll's 'b'
    n['p'] = _flip_band(n['b'], xh_top)
    n['q'] = _flip_band(n['d'], xh_top)
    n['9'] = _rot180(n['6'])
    for (font, ch), rows in SYNTH.items():
        g[font][ch] = np.array([[int(c, 16) if c != '.' else 0 for c in r] for r in rows], np.uint8)
    return g


def _flip_band(cell, top):
    """mirror vertically about the middle of rows top..CELL_BASE (b -> p, d -> q)"""
    out = np.zeros_like(cell)
    for r in range(CELL_H):
        src = top + CELL_BASE - r
        if 0 <= src < CELL_H:
            out[r] = cell[src]
    return out


def _rot180(cell):
    """6 -> 9: turned about the middle of the capital height (rows CELL_BASE-12..CELL_BASE)"""
    return _flip_band(cell, CELL_BASE - 12)[:, ::-1].copy()


def _advance(glyphs, ch, font):
    if font in PITCH:
        return PITCH[font]
    return SPACE if ch == ' ' else glyphs[font][ch].shape[1] - 1   # neighbours share the 1-px ring column


def text_width(glyphs, text, font):
    """left edge of the first letter to the right edge of the last"""
    if font in PITCH:
        return int(round((len(text) - 0.5) * PITCH[font])) + 6   # letters about 11 px wide with their ring
    return sum(_advance(glyphs, ch, font) for ch in text) + 1


def put_text(canvas, glyphs, text, font, x, base):
    """headings: each letter centred in its PITCH slot; names: letters side by side. max() keeps touching rings"""
    pos = float(x)
    for ch in text:
        adv = _advance(glyphs, ch, font)
        if ch != ' ':
            g = glyphs[font][ch]
            gx = int(round(pos + adv / 2 - g.shape[1] / 2)) if font in PITCH else int(round(pos))
            gy = base - CELL_BASE
            h, w = g.shape
            y0, y1, x0, x1 = max(gy, 0), min(gy + h, canvas.shape[0]), max(gx, 0), min(gx + w, canvas.shape[1])
            assert (y0, y1, x0, x1) == (gy, gy + h, gx, gx + w), ('off the canvas', text, ch)
            canvas[y0:y1, x0:x1] = np.maximum(canvas[y0:y1, x0:x1], g)
        pos += adv


def compose(glyphs):
    """the 256 x 512 texel area of screens 27-28"""
    cv = np.zeros((256, 512), np.uint8)
    put_text(cv, glyphs, TITLE, 'h', *TITLE_AT)
    # (the heading font has no M: the names' capitals), right-aligned on the title's line
    put_text(cv, glyphs, MOD, 'n', 512 - RIGHT - text_width(glyphs, MOD, 'n'), TITLE_AT[1])
    lines, cur = [], []
    for t in NAMES:
        w = sum(text_width(glyphs, u, 'n') + NAME_GAP for u in cur + [t]) - NAME_GAP
        if cur and NAME_X0 + w > 512 - RIGHT:
            lines.append(cur)
            cur = []
        cur.append(t)
    lines.append(cur)
    y = NAME_Y0
    for ln in lines:
        x = NAME_X0
        for t in ln:
            put_text(cv, glyphs, t, 'n', x, y)
            x += text_width(glyphs, t, 'n') + NAME_GAP
        y += LINE
    y += 4   # the hosts and the tail a little apart from the names
    for t in (HOSTS, TAIL):
        assert NAME_X0 + text_width(glyphs, t, 'n') <= 512 - RIGHT, ('line too wide', t)
        put_text(cv, glyphs, t, 'n', NAME_X0, y)
        y += LINE
    assert y - LINE <= 223, ('below the roll text area', y - LINE)
    return cv


# --- build ------------------------------------------------------------------------------------------------------
def build_credits(out_dir, src=None):
    """write <out_dir>/cledit_eng_reg_wor.arc: the shipped roll + CREDIT_LINES on screens 27-28; returns its path"""
    src = src or F.orig_path(ARC)
    arc, cels, mp, texs = load(src)
    assert len(cels) == SHIPPED_CELS and not mp[list(SCREENS)].any(), 'not the shipped roll: %s' % src
    used = {int(t) >> 8 for t in cels[:, 1]}
    strip = np.hstack([draw_screen(cels, mp, texs, s) for s in range(1, 27)])
    cv = compose(all_glyphs(strip))

    free = []
    for p in FREE_PAGES:
        assert p not in used, p
        for im in texs:
            x, y = (p % 8) * 256, (p // 8) * 256
            assert not im[y:y + 256, x:x + 256].any(), ('page not empty', p)
        free += [(p << 8) | i for i in range(256)]
    new_cels, seen = [], {}
    for k, s in enumerate(SCREENS):
        for r in range(16):
            for c in range(16):
                blk = cv[r * 16:r * 16 + 16, k * 256 + c * 16:k * 256 + c * 16 + 16]
                if not blk.any():
                    continue
                key = blk.tobytes()
                if key not in seen:
                    t = free.pop(0)
                    x, y = tile_xy(t)
                    for im in texs:
                        im[y:y + 16, x:x + 16] = blk
                    seen[key] = len(cels) + len(new_cels)
                    new_cels.append((0, t))
                mp[s, r, c] = 0x4000 | seen[key]
    cels = np.vstack([cels, np.array(new_cels, np.uint16)])
    assert len(cels) <= MAX_CELS, len(cels)

    ocl = struct.pack('<4sII', b'OCL\0', 1, len(cels)) + cels.astype('<u2').tobytes()
    omp_old = arc.get(OMP_NAME, H_OMP)
    omp = omp_old[:12] + mp.astype('<u2').tobytes()
    assert len(omp) == len(omp_old)
    arc.put(OCL_NAME, H_OCL, ocl)
    arc.put(OMP_NAME, H_OMP, omp)
    for name, im in zip(TEX_NAMES, texs):
        old = arc.get(name, F.H_TEX)
        arc.put(name, F.H_TEX, old[:20] + im.tobytes() + old[20 + im.size:])
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, ARC)
    tmp = out + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(arc.build())
    os.replace(tmp, out)
    print('%s: %d new cels (%d -> %d), pages %s' % (out, len(new_cels), SHIPPED_CELS, len(cels),
                                                    sorted({t >> 8 for _, t in new_cels})))
    return out


def is_shipped(path):
    """the roll as shipped: the OCL count unchanged and screens 27-28 blank (for lc2_install.is_shipped)"""
    _, cels, mp, _ = load(path)
    return len(cels) == SHIPPED_CELS and not mp[list(SCREENS)].any()


def check(path, src=None):
    """re-read a built arc: screens 1-26 and every shipped cel / texel the same as the shipped file's"""
    _, c0, m0, t0 = load(src or F.orig_path(ARC))
    _, c1, m1, t1 = load(path)
    assert (c1[:len(c0)] == c0).all() and (m1[:27] == m0[:27]).all() and (m1[29:] == m0[29:]).all()
    for a, b in zip(t0, t1):
        diff = np.argwhere(a != b)
        pages = {(y // 256) * 8 + x // 256 for y, x in diff}
        assert pages <= set(FREE_PAGES), pages
    return c1, m1, t1


def preview(path, png, first=23):
    """screens first..28 decoded from the built arc (texel value * 17), plus 27-28 at 3x"""
    from PIL import Image
    cels, mp, texs = check(path)
    row = np.hstack([draw_screen(cels, mp, texs, s) for s in range(first, 29)])
    big = np.kron(row[:, (27 - first) * 256:], np.ones((3, 3), np.uint8))
    w = max(row.shape[1], big.shape[1])
    img = np.zeros((row.shape[0] + 8 + big.shape[0], w), np.uint8)
    img[:row.shape[0], :row.shape[1]] = row
    img[row.shape[0] + 8:, :big.shape[1]] = big
    img = (img.astype(np.uint16) * 17).clip(0, 255).astype(np.uint8)
    for k in range(first, 29):   # screen borders on the 1x row
        img[:4, (k - first) * 256] = 128
    Image.fromarray(img).save(png)
    return png


if __name__ == '__main__':
    out = build_credits(F.BUILT)
    print('preview', preview(out, '/tmp/credits_preview.png'))
