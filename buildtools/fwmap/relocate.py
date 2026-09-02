#!/usr/bin/env python3
"""Relocate addresses and find data slots between two firmware images.

fwmap answers "who calls this"; this answers the other half of porting a
firmware version - "where did this move to", and "where is the slot holding
this value". Between them they cover the two kinds of entry in a
definitions.mk: code addresses (sig) and data pointers/literals (value, ptr).

Modes
  sig    take N bytes from OLD at ADDR, find them in NEW
  value  find every 4-byte-aligned dword equal to VALUE
  ptr    find every aligned dword pointing into [LO,HI)

Examples (bcm43430a1, 7.45.41.46 -> 7.45.98)

  # memcpy, documented as relocating 0x2390 -> 0x24ec
  relocate.py sig --old firmwares/bcm43430a1/7_45_41_46/brcmfmac43430-sdio.bin \
                  --new firmwares/bcm43430a1/7_45_98/brcmfmac43430-sdio.bin \
                  --at 0x2390 --auto

  # the flash-patch config slots, found by scanning for FP_CONFIG_ORIGBASE
  relocate.py value --bin firmwares/bcm43430a1/7_45_98/brcmfmac43430-sdio.bin \
                    --value 0x1800 --context 1

A caution the notes earned the hard way: a byte signature fails whenever the
region contains a *relative* branch, because the encoded offset changes when
the function moves. Zero hits therefore means "changed or position-dependent",
not "absent" - fall back to disassembly. --auto reports which signature
lengths worked, so a result that only matches at short lengths is visible as
the weaker evidence it is.
"""
import argparse
import struct
import sys


def load(path):
    with open(path, 'rb') as fh:
        return fh.read()


def _masked_find(new, sig, mask, base):
    """Find every offset where sig matches, comparing only masked-in bytes."""
    hits = []
    n = len(sig)
    keep = [i for i in range(n) if mask[i]]
    if not keep:
        return hits
    # Anchor on the first kept byte to avoid scanning every offset twice.
    first = keep[0]
    anchor = sig[first]
    i = new.find(anchor, 0)
    while i != -1 and i - first + n <= len(new):
        start = i - first
        if start >= 0 and all(new[start + k] == sig[k] for k in keep):
            hits.append(start + base)
        i = new.find(anchor, i + 1)
    return hits


def cmd_sig(a):
    old, new = load(a.old), load(a.new)
    off = a.at - a.base_old
    if not (0 <= off < len(old)):
        sys.exit(f'address 0x{a.at:x} outside old image (base 0x{a.base_old:x}, '
                 f'size 0x{len(old):x})')

    lengths = [8, 16, 24, 32, 48, 64, 96] if a.auto else [a.len]
    for n in lengths:
        sig = old[off:off + n]
        if len(sig) < n:
            continue
        mask = bytearray(b'\x01' * n)
        masked_words = []
        if a.mask_literals:
            # Any aligned word that looks like an address into the image is a
            # literal that will have moved; comparing it guarantees a miss.
            # This is what "identical except for the relocated literal itself"
            # in definitions.mk means in practice.
            for w in range(0, n - 3, 4):
                (v,) = struct.unpack('<I', sig[w:w + 4])
                if a.lit_lo <= v < a.lit_hi:
                    mask[w:w + 4] = b'\x00\x00\x00\x00'
                    masked_words.append((w, v))
        hits = _masked_find(new, sig, mask, a.base_new)
        status = 'UNIQUE' if len(hits) == 1 else ('none' if not hits else f'{len(hits)} hits')
        shown = ' '.join(f'0x{h:x}' for h in hits[:6])
        extra = ''
        if masked_words:
            extra = '  masked:' + ','.join(f'+0x{w:x}=0x{v:x}' for w, v in masked_words)
        print(f'  {n:3d}B  {status:9s} {shown}{extra}')
    if a.auto:
        print('\n  A good relocation is UNIQUE at the longer lengths. Unique only at')
        print('  8-16B is weak - short sequences repeat. None at any length usually')
        print('  means a relative branch or an embedded literal inside the region:')
        print('  retry with --mask-literals, and if that still fails, disassemble.')


def cmd_value(a):
    buf = load(a.bin)
    target = struct.pack('<I', a.value)
    n = 0
    for off in range(0, len(buf) - 3, 4):
        if buf[off:off + 4] == target:
            addr = off + a.base
            ctx = ''
            if a.context:
                lo = max(0, off - 4 * a.context)
                hi = min(len(buf), off + 4 * (a.context + 1))
                words = struct.unpack('<%dI' % ((hi - lo) // 4), buf[lo:hi])
                ctx = '  [' + ' '.join(f'{w:08x}' for w in words) + ']'
            print(f'  0x{addr:x}{ctx}')
            n += 1
    print(f'  {n} aligned occurrence(s) of 0x{a.value:x}')


def cmd_ptr(a):
    buf = load(a.bin)
    n = 0
    for off in range(0, len(buf) - 3, 4):
        (w,) = struct.unpack('<I', buf[off:off + 4])
        if a.lo <= w < a.hi:
            print(f'  0x{off + a.base:x} -> 0x{w:x}')
            n += 1
    print(f'  {n} pointer(s) into [0x{a.lo:x},0x{a.hi:x})')


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)

    s = sub.add_parser('sig', help='byte-signature relocation between two images')
    s.add_argument('--old', required=True)
    s.add_argument('--new', required=True)
    s.add_argument('--at', required=True, type=lambda x: int(x, 0))
    s.add_argument('--len', type=int, default=32)
    s.add_argument('--auto', action='store_true')
    s.add_argument('--base-old', dest='base_old', type=lambda x: int(x, 0), default=0)
    s.add_argument('--base-new', dest='base_new', type=lambda x: int(x, 0), default=0)
    s.add_argument('--mask-literals', action='store_true',
                   help='ignore aligned words that look like image addresses, '
                        'which relocate between versions and would force a miss')
    s.add_argument('--lit-lo', dest='lit_lo', type=lambda x: int(x, 0), default=0x400)
    s.add_argument('--lit-hi', dest='lit_hi', type=lambda x: int(x, 0), default=0x80000)
    s.set_defaults(fn=cmd_sig)

    v = sub.add_parser('value', help='find aligned dwords equal to a value')
    v.add_argument('--bin', required=True)
    v.add_argument('--value', required=True, type=lambda x: int(x, 0))
    v.add_argument('--context', type=int, default=0)
    v.add_argument('--base', type=lambda x: int(x, 0), default=0)
    v.set_defaults(fn=cmd_value)

    t = sub.add_parser('ptr', help='find aligned dwords pointing into a range')
    t.add_argument('--bin', required=True)
    t.add_argument('--lo', required=True, type=lambda x: int(x, 0))
    t.add_argument('--hi', required=True, type=lambda x: int(x, 0))
    t.add_argument('--base', type=lambda x: int(x, 0), default=0)
    t.set_defaults(fn=cmd_ptr)

    a = p.parse_args()
    a.fn(a)


if __name__ == '__main__':
    main()
