"""Axl's voice and sounds from Mega Man X8 (Legacy Collection 2, se/play_a/wav/*.xsew = MS-ADPCM WAV) in place of
X's samples in the player container (docs/sound.md).

Container 86: SPU blobs 0x20101 (bank 1, X's sounds) and 0x20303 (bank 3, X's voice), each with a type 6 table =
[u8 table length, ?, u16 bank, u32 blob size, 4-byte entries (bank | 0x80, program (0x80 = loop), tone << 4, ?)]
followed by a standard VAB header (pBAV v7: programs, tones (32 bytes: +4 center note, +5 fine, +6/+7 key range,
+22 sample), sample sizes / 8). A tone plays its sample at 44100 * 2^((key - center) / 12 - fine / 1536) Hz, so a new
sample is encoded at the rate its tones already play (or the tones are retuned) and the bank is repacked; each bank
stays within its original size (the SPU layout after it is the game's).
usage: sound.py preview   (decoded new banks -> work/sound/new/*.wav)"""
import os
import struct
import subprocess
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import axl_config as C  # noqa: E402
X8 = os.path.join(C.X8_SOUND, 'se', 'play_a', 'wav')
WORK = os.path.join(HERE, '..', 'work', 'sound')

# bank 1 sample -> X8 clips (played one after another). Sample numbers from the VAB (SE index -> tone -> sample):
#   SE 8 shot = sample 2, SE 5 dash = 3, SE 1 jump = 1, SE 7 (X's charge; the mod's DNA Change) = 7,
#   SE 11 (X's charge loop; the mod's hover start, made one-shot) = 10, SE 13 (X's charge loop 2; the mod's Giga Crush) = 9,
#   SE 10 (X's full charge shot, also SE 35; Axl never charges) = 6 = the mod's copy shot
BANK1 = {2: ['03_a_shot'], 3: ['00_a_dash_'], 1: ['01_a_jump_'], 7: ['41_a_copy_shot', '33_a_hensin'],
         10: [('09_a_burner_loop', 0.6)], 9: ['27_a_ikuzo'], 6: ['41_a_copy_shot']}
ONE_SHOT = {1: (11,)}   # bank -> SE entries whose loop flag (program | 0x80) is cleared
# bank 3 (voice) sample -> clip. X's use (src: 0x8003cfe0 callers, table 0x80073ce4): 0/1 = hurt (random),
# 2/3/4/6/7 = shouts on jumps / wall kicks (random); the others' use is not traced (estimates: 10, 11, 14 long cries)
BANK3 = {1: ['10_a_ite'], 2: ['11_a_u'], 3: ['13_a_ya'], 4: ['15_a_ha'], 5: ['14_a_sore'], 6: ['31_a_hehen2'],
         7: ['15_a_ha'], 8: ['14_a_sore'], 9: ['28_a_atare'], 10: ['32_a_ohhto'], 11: ['19_a_yabai'],
         12: ['20_a_uwaaaa'], 13: ['20_a_uwaaaa']}
BANKS = ((0x20101, 0, BANK1, 1), (0x20303, 1, BANK3, 3))   # (blob type, n-th type 6 table, samples, bank)

F = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))


def clip(name, rate):
    """X8 clip (name or (name, seconds)) as float samples at rate"""
    secs = None
    if isinstance(name, tuple):
        name, secs = name
    cmd = ['ffmpeg', '-v', 'error', '-i', os.path.join(X8, name + '.xsew'), '-ac', '1', '-ar', str(int(rate)),
           '-f', 's16le', '-']
    pcm = np.frombuffer(subprocess.run(cmd, check=True, capture_output=True).stdout, '<i2').astype(np.float64)
    if secs:
        n = int(secs * rate)
        pcm = pcm[:n] * np.minimum(1, np.linspace(4, 0, len(pcm[:n])))   # fade the cut end
    return pcm


def decode(b):
    out, s1, s2 = [], 0, 0
    for i in range(0, len(b), 16):
        sh, f, fl = b[i] & 15, b[i] >> 4, b[i + 1]
        for j in range(28):
            n = (b[i + 2 + j // 2] >> (4 * (j & 1))) & 15
            n = n - 16 if n >= 8 else n
            v = ((n << 12) >> sh) + ((s1 * F[f][0] + s2 * F[f][1] + 32) >> 6)
            v = max(-32768, min(32767, v))
            out.append(v)
            s2, s1 = s1, v
        if fl & 1 and i:
            break
    return np.array(out, np.int16)


def encode(pcm):
    """PS-ADPCM one-shot in the layout of X's samples: silent block, flag 4, ..., flag 1, a silent flag 7 block"""
    pcm = np.clip(np.round(pcm), -32768, 32767).astype(np.int64)
    pcm = np.concatenate([pcm, np.zeros((-len(pcm)) % 28, np.int64)])
    blocks = [bytes(16)]
    s1 = s2 = 0
    for b in range(0, len(pcm), 28):
        x = pcm[b:b + 28]
        best = None
        for f in range(5):
            for sh in range(13):
                t1, t2, err, nib = s1, s2, 0, []
                for v in x:
                    p = (t1 * F[f][0] + t2 * F[f][1] + 32) >> 6
                    n = int(np.clip(round(((v - p) << sh) / 4096), -8, 7))
                    d = max(-32768, min(32767, ((n << 12) >> sh) + p))
                    err += (int(v) - d) ** 2
                    nib.append(n & 15)
                    t2, t1 = t1, d
                if best is None or err < best[0]:
                    best = (err, f, sh, nib, t1, t2)
        _, f, sh, nib, s1, s2 = best
        blocks.append(bytes([f << 4 | sh, 0]) + bytes(nib[2 * k] | nib[2 * k + 1] << 4 for k in range(14)))
    blocks[1] = blocks[1][:1] + b'\x04' + blocks[1][2:]
    blocks[-1] = blocks[-1][:1] + b'\x01' + blocks[-1][2:]
    blocks.append(b'\x00\x07' + bytes(14))
    return b''.join(blocks)


class Bank:
    def __init__(self, table, blob):
        self.table, self.blob = bytearray(table), blob
        self.hl = table[0]
        self.vh = self.hl
        self.ps, self.ts, self.vs = struct.unpack_from('<HHH', table, self.vh + 0x12)
        self.tones0 = self.vh + 32 + 2048
        self.sizes0 = self.tones0 + self.ps * 512
        sizes = [struct.unpack_from('<H', table, self.sizes0 + 2 * i)[0] * 8 for i in range(self.vs + 1)]
        pos, self.samples = 0, {}
        for i in range(1, self.vs + 1):
            self.samples[i] = blob[pos:pos + sizes[i]]
            pos += sizes[i]
        assert pos == len(blob)

    def tones(self, vag):
        """table offsets of the tones playing sample vag"""
        out = []
        for p in range(self.ps):
            for k in range(16):
                a = self.tones0 + p * 512 + k * 32
                if self.table[a + 7] and struct.unpack_from('<H', self.table, a + 22)[0] == vag:
                    out.append(a)
        return out

    def rate(self, a):
        center, fine, key = self.table[a + 4], self.table[a + 5], self.table[a + 6]
        return 44100 * 2 ** ((key - center) / 12 - fine / 1536)

    def retune(self, a, rate):
        """center / fine so that the tone's key plays at rate"""
        semis = self.table[a + 6] - 12 * np.log2(rate / 44100)   # the effective center note (fine = 1/128 semitone)
        center = int(np.floor(semis))
        fine = int(round((semis - center) * 128))
        if fine >= 128:
            center, fine = center + 1, fine - 128
        self.table[a + 4], self.table[a + 5] = center, fine

    def replace(self, vag, names, budget):
        tones = self.tones(vag)
        assert tones, ('sample not played by any tone', vag)
        rate = self.rate(tones[0])
        pcm = np.concatenate([clip(n, rate) for n in names])
        # level like X's sample (peak)
        old = decode(self.samples[vag]).astype(np.float64)
        pcm *= min(4.0, (np.abs(old).max() + 1) / (np.abs(pcm).max() + 1))
        data = encode(pcm)
        while len(data) > budget:   # too long for the bank: a lower rate (the tones are retuned)
            rate *= 0.9
            pcm = np.concatenate([clip(n, rate) for n in names]) * (np.abs(old).max() + 1) / (np.abs(pcm).max() + 1)
            data = encode(pcm)
        for a in tones:
            self.retune(a, rate)
        self.samples[vag] = data
        return rate

    def build(self):
        blob = b''.join(self.samples[i] for i in range(1, self.vs + 1))
        for i in range(1, self.vs + 1):
            struct.pack_into('<H', self.table, self.sizes0 + 2 * i, len(self.samples[i]) // 8)
        struct.pack_into('<I', self.table, 4, len(blob))
        return bytes(self.table), blob


def rebuild(subs):
    """subs [(type, bytes)] of the container -> {(type, n-th of that type): new bytes}"""
    out = {}
    tables = [b for t, b in subs if t == 6]
    for blob_type, ti, plan, bank_no in BANKS:
        blob = [b for t, b in subs if t == blob_type][0]
        bank = Bank(tables[ti], blob)
        total = len(blob)
        for vag, names in plan.items():
            free = total - sum(len(s) for i, s in bank.samples.items() if i != vag)
            rate = bank.replace(vag, names, free)
            print('bank %d sample %d <- %s at %d Hz, %d bytes' % (bank_no, vag, '+'.join(
                n if isinstance(n, str) else n[0] for n in names), rate, len(bank.samples[vag])))
        for se in ONE_SHOT.get(bank_no, ()):
            bank.table[8 + 4 * se + 1] &= 0x7f
        table, new = bank.build()
        assert len(new) <= total, (bank_no, len(new), total)
        new += bytes(total - len(new))   # same subfile size: the SPU layout after it stays
        out[(6, ti)] = table
        out[(blob_type, 0)] = new
    return out


if __name__ == '__main__' and sys.argv[1:] == ['preview']:
    sys.path.insert(0, HERE)
    import datpak
    dat = open(os.path.join(C.X6_US_FS, 'ROCK_X6.DAT'), 'rb').read()
    sec, size = struct.unpack_from('<II', dat, 8 * 86)
    cont = dat[sec * 2048:sec * 2048 + size]
    subs = [(t, cont[o:o + z]) for t, o, z in datpak.parse(cont)]
    new = rebuild(subs)
    os.makedirs(os.path.join(WORK, 'new'), exist_ok=True)
    for blob_type, ti, plan, bank_no in BANKS:
        bank = Bank(new[(6, ti)], new[(blob_type, 0)][:struct.unpack_from('<I', new[(6, ti)], 4)[0]])
        for vag in plan:
            a = bank.tones(vag)[0]
            w = wave.open(os.path.join(WORK, 'new', 'b%d_%02d.wav' % (bank_no, vag)), 'wb')
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(int(bank.rate(a)))
            w.writeframes(decode(bank.samples[vag]).tobytes()); w.close()
    print('wrote', os.path.join(WORK, 'new'))


def rebuild_cached(subs):
    """rebuild() kept in work/sound/banks.bin (encoding takes minutes); redone when this file or the inputs change"""
    import hashlib
    key = hashlib.sha1(open(os.path.abspath(__file__), 'rb').read()
                       + b''.join(b for t, b in subs if t in (6, 0x20101, 0x20303))).hexdigest()
    path = os.path.join(WORK, 'banks.bin')
    if os.path.exists(path):
        raw = open(path, 'rb').read()
        if raw[:40].decode() == key:
            out, pos = {}, 40
            while pos < len(raw):
                t, n, size = struct.unpack_from('<III', raw, pos)
                out[(t, n)] = raw[pos + 12:pos + 12 + size]
                pos += 12 + size
            return out
    out = rebuild(subs)
    os.makedirs(WORK, exist_ok=True)
    with open(path, 'wb') as f:
        f.write(key.encode())
        for (t, n), b in out.items():
            f.write(struct.pack('<III', t, n, len(b)) + b)
    return out
