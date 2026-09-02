#!/usr/bin/env python3
"""Build a call map for bcm43430a1 / 7.45.98 from raw objdump output.

Emits, into the same directory:
  funcs.txt  - every address that is the target of at least one BL, i.e. a
               plausible function entry, with its inbound call count
  calls.txt  - "target caller kind" one per line, sorted by target, so
               "who calls X" is a grep and "what does X call" is a grep on
               calls_by_caller.txt
  calls_by_caller.txt - same edges keyed the other way
  indirect.txt - sites that dispatch through a register or a pointer, which
               a static scan cannot resolve and which this firmware uses
               heavily (the ROM thunk table at 0x488 being the known case)

Caveat worth remembering when reading the output: this is a flat disassembly
of a raw image, so data regions decode as instructions and produce edges that
do not exist. Anything landing outside RAM (0..0x80000) or ROM
(0x800000..0x8A0000) is noise and is dropped; the flash-patch data at
0x1000..0x1c00 is tagged rather than trusted.
"""
import re
import sys
from collections import defaultdict

RAM = (0x0, 0x80000)
ROM = (0x800000, 0x8A0000)
FP_DATA = (0x1000, 0x1C00)

line_re = re.compile(
    r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4}\s+)+\s*(bl|blx|b\.w|b|bx)\s+(?:0x)?([0-9a-f]+)\s*$')
indirect_re = re.compile(
    r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4}\s+)+\s*(blx|bx|mov\s+pc,|ldr\s+pc)\s*(.*)$')


def in_image(a):
    return RAM[0] <= a < RAM[1] or ROM[0] <= a < ROM[1]


def main(paths, outdir):
    edges = []          # (target, caller, kind)
    indirect = []
    for p in paths:
        with open(p, errors='replace') as fh:
            for line in fh:
                m = line_re.match(line)
                if m:
                    caller = int(m.group(1), 16)
                    kind = m.group(2)
                    target = int(m.group(3), 16)
                    if not in_image(target):
                        continue
                    if FP_DATA[0] <= caller < FP_DATA[1]:
                        kind += ":fpdata"
                    edges.append((target, caller, kind))
                    continue
                m = indirect_re.match(line)
                if m and not line_re.match(line):
                    indirect.append((int(m.group(1), 16),
                                     m.group(2).strip(), m.group(3).strip()))

    inbound = defaultdict(int)
    for t, c, k in edges:
        if k == 'bl':
            inbound[t] += 1

    with open(f'{outdir}/calls.txt', 'w') as fh:
        for t, c, k in sorted(edges):
            fh.write(f'0x{t:x} 0x{c:x} {k}\n')
    with open(f'{outdir}/calls_by_caller.txt', 'w') as fh:
        for t, c, k in sorted(edges, key=lambda e: (e[1], e[0])):
            fh.write(f'0x{c:x} 0x{t:x} {k}\n')
    with open(f'{outdir}/funcs.txt', 'w') as fh:
        for a, n in sorted(inbound.items(), key=lambda kv: -kv[1]):
            fh.write(f'0x{a:x} {n}\n')
    with open(f'{outdir}/indirect.txt', 'w') as fh:
        for a, op, rest in sorted(indirect):
            fh.write(f'0x{a:x} {op} {rest}\n')

    print(f'edges       {len(edges)}')
    print(f'bl-targets  {len(inbound)}  (plausible functions)')
    print(f'indirect    {len(indirect)}')


if __name__ == '__main__':
    main(sys.argv[2:], sys.argv[1])
