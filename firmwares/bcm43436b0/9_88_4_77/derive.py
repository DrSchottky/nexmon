#!/usr/bin/env python3
"""Derive definitions.mk values for a bcm43436b0 firmware image.

Self-tests the portable heuristics against every known-good image in the
43430/43436 family that ships in this repo, then self-tests the full set
against bcm43436b0/9_88_4_65 (the only prior bcm43436b0 port), then prints
values for 9_88_4_77.

RAMSTART is 0x0 for this chip family, so file offset == RAM address here.

The flash-patch *config pointer* results (FP_CONFIG_BASE_PTR_1/2 and the
matching END pointers) are the least certain: the heuristic reproduces
9_88_4_65 exactly but there is only one reference point for it. Confirm them
on the device before trusting an injection/monitor build - dump the live
values with the rom_extraction memory-read ioctl (case 0x603) at the derived
addresses, or disassemble ROM to see which words ROM actually reads.
"""
import os
import struct
import sys

# ucode blob magic, common to the whole 43430/43436 family
UCODE_MAGIC = bytes.fromhex('01bc600300104e03bfde')

FP_DATA_BASE = 0x1000
FP_CONFIG_ORIGBASE = 0x1800


def u32(d, a):
    return struct.unpack('<I', d[a:a + 4])[0]


def slots(d, val, lo=0, hi=None):
    """All 4-byte-aligned offsets whose little-endian dword == val."""
    if hi is None:
        hi = len(d) - 4
    b = struct.pack('<I', val)
    return [a for a in range((lo + 3) & ~3, hi, 4) if d[a:a + 4] == b]


def derive(path, entry_stride=8, full=True):
    d = open(path, 'rb').read()
    r = {'_size': len(d)}

    # --- ucode blob, its compressed-length word, and the BL hook slot ---
    hits = [i for i in range(len(d)) if d.startswith(UCODE_MAGIC, i)]
    assert len(hits) == 1, ('ucode magic hits', [hex(x) for x in hits])
    us = hits[0]
    r['UCODESTART'] = us
    r['UCODESIZE'] = u32(d, us - 4)          # length stored immediately before the blob
    hooks = [a for a in range(4, len(d) - 4, 4)
             if u32(d, a) == us and u32(d, a - 4) == us - 4]
    assert len(hooks) == 1, ('hook slot', [hex(x) for x in hooks])
    r['WLC_UCODE_WRITE_BL_HOOK_ADDR'] = hooks[0] - 0x10

    # --- reclaim-region-0 end pointer: the <end>,0xFC,0x100,0,0 record ---
    rc = [a for a in range(0, len(d) - 20, 4)
          if u32(d, a + 4) == 0xFC and u32(d, a + 8) == 0x100
          and u32(d, a + 12) == 0 and u32(d, a + 16) == 0
          and 0x30000 < u32(d, a) < len(d)]
    assert len(rc) == 1, ('reclaim ptr', [hex(x) for x in rc])
    r['HNDRTE_RECLAIM_0_END_PTR'] = rc[0]
    r['HNDRTE_RECLAIM_0_END'] = u32(d, rc[0])

    # --- template RAM: [reclaim_end - size, reclaim_end); start is
    #     round_up_4(UCODESTART + UCODESIZE) and appears exactly once ---
    trs = (us + r['UCODESIZE'] + 3) & ~3
    tp = slots(d, trs)
    assert len(tp) == 1, ('templateram ptr', [hex(x) for x in tp])
    r['TEMPLATERAMSTART_PTR'] = tp[0]
    r['TEMPLATERAMSTART'] = trs
    r['TEMPLATERAMSIZE'] = r['HNDRTE_RECLAIM_0_END'] - trs

    if not full:
        return r, d

    # --- flash-patch config table: (target, dataptr) pairs from ORIGBASE ---
    a, n = FP_CONFIG_ORIGBASE, 0
    while 0x800000 <= u32(d, a) < 0x900000:
        n += 1
        a += entry_stride
    assert n > 20, ('fp entry count', n)
    r['_fp_entries'] = n
    r['FP_CONFIG_ORIGBASE'] = FP_CONFIG_ORIGBASE
    end = FP_CONFIG_ORIGBASE + n * entry_stride
    r['FP_CONFIG_ORIGEND'] = end

    # config base/end pointer pairs: an aligned 0x1800 with ORIGEND in the
    # adjacent word, and NOT buried in a run of table constants (the decoy
    # near FP_DATA_END_PTR is followed by more <0x2000 words).
    pairs = []
    for s in slots(d, FP_CONFIG_ORIGBASE):
        if u32(d, s + 4) == end and u32(d, s + 8) >= 0x2000:
            pairs.append((s, s + 4))
        elif s >= 4 and u32(d, s - 4) == end and u32(d, s + 4) >= 0x2000:
            pairs.append((s, s - 4))
    assert len(pairs) >= 2, ('fp config pointer pairs',
                             [(hex(b), hex(e)) for b, e in pairs])
    (r['FP_CONFIG_BASE_PTR_1'], r['FP_CONFIG_END_PTR_1']) = pairs[0]
    (r['FP_CONFIG_BASE_PTR_2'], r['FP_CONFIG_END_PTR_2']) = pairs[-1]

    # --- FP_DATA end pointer: unique literal == FP_DATA_BASE + n * 8 ---
    dv = FP_DATA_BASE + n * 8
    dp = slots(d, dv)
    assert len(dp) == 1, ('fp data end ptr', hex(dv), [hex(x) for x in dp])
    r['FP_DATA_END_PTR'] = dp[0]

    # --- FP_CONFIG_BASE: scratch area at the rounded-up end of the image ---
    r['FP_CONFIG_BASE'] = (len(d) + 0xF) & ~0xF

    # invariants
    assert r['TEMPLATERAMSTART'] + r['TEMPLATERAMSIZE'] == r['HNDRTE_RECLAIM_0_END']
    assert r['UCODESTART'] < r['TEMPLATERAMSTART'] < r['HNDRTE_RECLAIM_0_END']
    return r, d


# known-good values from the committed definitions.mk files
PORTABLE = {
    'bcm43430a1/7_45_41_46/brcmfmac43430-sdio.bin': dict(
        WLC_UCODE_WRITE_BL_HOOK_ADDR=0x45608, HNDRTE_RECLAIM_0_END_PTR=0x2684,
        HNDRTE_RECLAIM_0_END=0x5ad9c, UCODESTART=0x4f4b8, UCODESIZE=0xb2a3,
        TEMPLATERAMSTART_PTR=0x4f3b8, TEMPLATERAMSTART=0x5a75c, TEMPLATERAMSIZE=0x640),
    'bcm43430a1/7_45_98/brcmfmac43430-sdio.bin': dict(
        WLC_UCODE_WRITE_BL_HOOK_ADDR=0x4cee4, HNDRTE_RECLAIM_0_END_PTR=0x27e0,
        HNDRTE_RECLAIM_0_END=0x616c8, UCODESTART=0x55e98, UCODESIZE=0xb1ed,
        TEMPLATERAMSTART_PTR=0x55d98, TEMPLATERAMSTART=0x61088, TEMPLATERAMSIZE=0x640),
}

FULL_65 = dict(
    WLC_UCODE_WRITE_BL_HOOK_ADDR=0x4d220, HNDRTE_RECLAIM_0_END_PTR=0x3038,
    HNDRTE_RECLAIM_0_END=0x64f78, UCODESTART=0x58CA0, UCODESIZE=0xbca5,
    TEMPLATERAMSTART_PTR=0x58B28, TEMPLATERAMSTART=0x64948, TEMPLATERAMSIZE=0x630,
    FP_DATA_END_PTR=0x40C48, FP_CONFIG_BASE_PTR_1=0x40048, FP_CONFIG_END_PTR_1=0x4004C,
    FP_CONFIG_BASE_PTR_2=0x4018C, FP_CONFIG_END_PTR_2=0x40190,
    FP_CONFIG_ORIGBASE=0x1800, FP_CONFIG_ORIGEND=0x1AF8, FP_CONFIG_BASE=0x650F0,
)

ORDER = ['WLC_UCODE_WRITE_BL_HOOK_ADDR', 'HNDRTE_RECLAIM_0_END_PTR',
         'HNDRTE_RECLAIM_0_END', 'UCODESTART', 'UCODESIZE',
         'TEMPLATERAMSTART_PTR', 'TEMPLATERAMSTART', 'TEMPLATERAMSIZE',
         'FP_DATA_END_PTR', 'FP_CONFIG_BASE_PTR_1', 'FP_CONFIG_END_PTR_1',
         'FP_CONFIG_BASE_PTR_2', 'FP_CONFIG_END_PTR_2', 'FP_CONFIG_BASE',
         'FP_CONFIG_ORIGBASE', 'FP_CONFIG_ORIGEND']


def check(rel, got, exp):
    bad = {k: (hex(v), hex(got.get(k, -1))) for k, v in exp.items() if got.get(k) != v}
    print(('  OK   ' if not bad else '  FAIL ') + rel + (' ' + repr(bad) if bad else ''))
    return not bad


def main():
    base = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..')
    ok = True
    print('portable-subset self-test (43430/43436 family):')
    for rel, exp in PORTABLE.items():
        got, _ = derive(os.path.join(base, 'firmwares', rel), full=False)
        ok &= check(rel, got, exp)
    print('full self-test (bcm43436b0/9_88_4_65):')
    got, _ = derive(os.path.join(base, 'firmwares/bcm43436b0/9_88_4_65/brcmfmac43436-sdio.bin'))
    ok &= check('bcm43436b0/9_88_4_65', got, FULL_65)
    if not ok:
        sys.exit('self-test failed')

    tgt = os.path.join(base, 'firmwares/bcm43436b0/9_88_4_77/brcmfmac43436-sdio.bin')
    r, _ = derive(tgt)
    print('\n# --- 9_88_4_77 (derived; verify FP_CONFIG_*_PTR on device) ---')
    print('# entries in stock flash-patch table: %d' % r['_fp_entries'])
    for k in ORDER:
        print('%-28s = 0x%X' % (k, r[k]))


if __name__ == '__main__':
    main()
