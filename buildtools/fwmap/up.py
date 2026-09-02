#!/usr/bin/env python3
"""Walk the reverse call graph upward from an address.

Usage: up.py <hex addr> [depth]

The edges record the address of the BL *instruction*, not the function it sits
in, so a raw reverse lookup dead-ends immediately. Function entries are taken
to be the set of BL targets, and each call site is attributed to the nearest
entry at or below it - approximate, but good enough to recover a call chain,
and it degrades visibly (a call site far from its attributed entry is
suspicious) rather than silently.

Functions with many inbound callers are marked "(hub)" and not expanded:
those are shared utilities and expanding them buries the real chain.
"""
import bisect
import sys
from collections import defaultdict

callers = defaultdict(list)
inbound = defaultdict(int)
for line in open('/tmp/fwmap/calls.txt'):
    t, c, k = line.split()
    callers[int(t, 16)].append((int(c, 16), k))
for line in open('/tmp/fwmap/funcs.txt'):
    a, n = line.split()
    inbound[int(a, 16)] = int(n)

entries = sorted(inbound)


def owner(site):
    """Function entry containing this call site."""
    i = bisect.bisect_right(entries, site) - 1
    if i < 0:
        return None, None
    e = entries[i]
    return e, site - e


seen = set()


def walk(addr, depth, indent=0):
    pad = '  ' * indent
    cs = callers.get(addr, [])
    if not cs:
        print(f'{pad}0x{addr:x}  <- no static caller (entry point or indirect)')
        return
    for site, k in cs:
        e, off = owner(site)
        if e is None:
            print(f'{pad}0x{addr:x} <- site 0x{site:x} [{k}] (unattributed)')
            continue
        n = inbound.get(e, 0)
        hub = '  (hub)' if n > 8 else ''
        tag = '  [seen]' if e in seen else ''
        print(f'{pad}0x{addr:x} <- 0x{e:x}+0x{off:x} [{k}] in{n}{hub}{tag}')
        if depth > 1 and not hub and e not in seen:
            seen.add(e)
            walk(e, depth - 1, indent + 1)


if __name__ == '__main__':
    walk(int(sys.argv[1], 16), int(sys.argv[2]) if len(sys.argv) > 2 else 4)
