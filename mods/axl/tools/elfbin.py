"""Minimal ELF32 little-endian reader: flat binary of the allocated sections and the symbol table.

Replaces llvm-objcopy -O binary / llvm-nm so the build also runs where only Python and zig are available.
"""
import struct

SHT_SYMTAB, SHT_NOBITS = 2, 8
SHF_ALLOC = 2


def _sections(data):
    shoff, = struct.unpack_from('<I', data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 0x2e)
    secs = []
    for i in range(shnum):
        name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from(
            '<IIIIIIIIII', data, shoff + i * shentsize)
        secs.append(dict(name=name, type=typ, flags=flags, addr=addr, off=off, size=size, link=link, entsize=entsize))
    strtab = secs[shstrndx]
    for s in secs:
        end = data.index(b'\0', strtab['off'] + s['name'])
        s['name'] = data[strtab['off'] + s['name']:end].decode()
    return secs


def flat_binary(path, names=('.text', '.rodata', '.data')):
    """Bytes from the lowest to the highest address of the named sections (gaps zero-filled)."""
    data = open(path, 'rb').read()
    secs = [s for s in _sections(data) if s['name'] in names and s['flags'] & SHF_ALLOC and s['size']]
    lo = min(s['addr'] for s in secs)
    hi = max(s['addr'] + s['size'] for s in secs)
    out = bytearray(hi - lo)
    for s in secs:
        if s['type'] != SHT_NOBITS:
            out[s['addr'] - lo:s['addr'] - lo + s['size']] = data[s['off']:s['off'] + s['size']]
    return bytes(out), lo


def symbols(path):
    """{name: value} of defined symbols."""
    data = open(path, 'rb').read()
    secs = _sections(data)
    out = {}
    for s in secs:
        if s['type'] != SHT_SYMTAB:
            continue
        strs = secs[s['link']]
        for k in range(s['size'] // 16):
            name, value, size, info, other, shndx = struct.unpack_from('<IIIBBH', data, s['off'] + 16 * k)
            if name and shndx:
                end = data.index(b'\0', strs['off'] + name)
                out[data[strs['off'] + name:end].decode()] = value
    return out
