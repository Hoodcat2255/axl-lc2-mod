"""MT Framework sound banks of X6 in Legacy Collection 2 (read only):
SBKR (hash 0x15d782fb): 'SBKR', u32 version 4, u32 n_tables, u32 n_sounds, n_tables x 16 bytes, then n_sounds records,
  each holding the path of its wave (0x724df879 resource) as a NUL-terminated string followed by that type hash.
SRQR (hash 0x1bcc4966): 'SRQR', u32 version 5, u32 n_requests, ..., records of 0x84 bytes from 0x34: byte 0 = the
  request id the game plays (PS1 FUN_80016c48's id), byte 2 = the SBKR sound number (from 1); the bank's name at the end.
sounds(arc, bank) -> {request id: wave path}"""
import re
import struct

H_SBKR, H_SRQR, H_WAV = 0x15d782fb, 0x1bcc4966, 0x724df879


def sbkr_paths(b):
    """wave path of every sound record, in order"""
    return [m.group(1).decode('latin1') for m in re.finditer(rb'(X6\\sound\\[\x20-\x7e]+?)\x00\x79\xf8\x4d\x72', b)]


def srqr_requests(q):
    n = struct.unpack_from('<I', q, 8)[0]
    return [(q[0x34 + 0x84 * i], q[0x34 + 0x84 * i + 2]) for i in range(n)]


def sounds(arc, bank):
    b, q = arc.get(bank, H_SBKR), arc.get(bank, H_SRQR)
    paths = sbkr_paths(b)
    # (the sound number counts from 1, 0 = none: request 27 played record 26's wave - user report 2026-10-27, the
    # change sounded like Scaravich's ball, the charge said "henshin")
    return ({rid: (paths[s - 1] if 0 < s <= len(paths) else None) for rid, s in srqr_requests(q)}, len(paths),
            struct.unpack_from('<I', b, 12)[0])
