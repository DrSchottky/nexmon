# fwmap - a call map for a firmware image

Navigation aid for reverse-engineering a chip's RAM+ROM images. It answers
"who calls this address" and "what does this function reach", which is the
question that comes up constantly when relocating an address between firmware
versions or tracing a path through unfamiliar vendor code.

## Use

    OBJDUMP=buildtools/gcc-arm-none-eabi-5_4-2016q2/bin/arm-none-eabi-objdump
    FW=firmwares/bcm43430a1/7_45_98
    mkdir -p /tmp/fwmap

    $OBJDUMP -D -b binary -m arm -M force-thumb --adjust-vma=0x0 \
        $FW/brcmfmac43430-sdio.bin > /tmp/fwmap/ram.asm
    $OBJDUMP -D -b binary -m arm -M force-thumb --adjust-vma=0x800000 \
        $FW/rom.bin > /tmp/fwmap/rom.asm

    python3 buildtools/fwmap/build.py /tmp/fwmap /tmp/fwmap/ram.asm /tmp/fwmap/rom.asm
    python3 buildtools/fwmap/up.py 0xd4e4 6      # walk callers upward

The RAM/ROM base addresses and sizes come from the firmware's
`definitions.mk` (`RAMSTART`/`RAMSIZE`, `ROMSTART`/`ROMSIZE`); `build.py` has
the 43430a1 values near the top and needs editing for another chip.

Whole thing takes about a second. Keep the generated files on disk and grep
them - `calls.txt` is keyed by target, `calls_by_caller.txt` by call site.

## What it is good for

Confirmed against known ground truth on `bcm43430a1`/`7_45_98`: it
independently reproduces that `wl_monitor` (RAM `0xa5e2`) has exactly one
caller, the RAM call site `0xd716`, and that the RAM RX routine `0xd4e4` is
entered from `0x81f410` by the stock flashpatch `b.w` - both of which were
originally established by hand-disassembly (see `REVERSE_ENGINEERING_NOTES.md`).

It also recovers the RX pipeline above the monitor hook, every edge
single-caller:

    0x9f24 -> 0x9e54 -> 0x25db0 -> 0x20ad4 -> 0x15ffc -> 0xd4e4 -> 0xa5e2

## relocate.py - porting addresses to a new firmware version

`fwmap` answers "who calls this"; `relocate.py` answers "where did this move
to" and "where is the slot holding this value". Together they cover both kinds
of entry in a `definitions.mk`: code addresses and data pointers/literals.

    R=buildtools/fwmap/relocate.py
    OLD=firmwares/bcm43430a1/7_45_41_46/brcmfmac43430-sdio.bin
    NEW=firmwares/bcm43430a1/7_45_98/brcmfmac43430-sdio.bin

    python3 $R sig --old $OLD --new $NEW --at 0x2390 --auto
    python3 $R sig --old $OLD --new $NEW --at 0x4f3b8 --auto --mask-literals
    python3 $R value --bin $NEW --value 0x1800 --context 1
    python3 $R ptr   --bin $NEW --lo 0x1000 --hi 0x2200

Validated by replaying relocations already established by hand for
7.45.41.46 -> 7.45.98, and it reproduces each one *and* the signature length
each was described with:

| entry | old | expected | result |
|---|---|---|---|
| `memcpy` | `0x2390` | `0x24ec` | unique at every length |
| `wlc_d11hdrs` | `0xa024` | `0xbf50` | unique to 32B, then none - hence "32B prologue" |
| `pkt_buf_free_skb` | `0x638c` | `0x6c74` | unique to 48B |
| `TEMPLATERAMSTART_PTR` | `0x4f3b8` | `0x55d98` | needs `--mask-literals` |
| `FP_DATA_END_PTR` | `0x39574` | `0x40edc` | needs `--mask-literals` |
| `WLC_UCODE_WRITE_BL_HOOK` | `0x45608` | `0x4cee4` | **not found** - see below |

`--mask-literals` exists because a pointer slot's neighbourhood is, as
`definitions.mk` puts it, "identical except for the relocated literal itself".
Comparing that literal guarantees a miss, so aligned words that look like
image addresses are excluded from the compare and reported as `masked:`.

The one failure is the honest kind: that region contains a *relative* `bl`,
whose encoded offset changes when the code moves, and no amount of literal
masking fixes that. `definitions.mk` records it as having been found by
disassembly rather than by signature. Treat "none at any length" as "use
disassembly", never as "absent".

## What it is not

Read the output with these in mind - the map is a skeleton, not an authority.

- **No symbols.** Bare addresses only.
- **Indirect calls are invisible.** ~1800 sites dispatch through a register or
  a pointer table (this firmware's ROM thunk at `0x488` among them). A static
  BL scan cannot follow any of them, so the holes sit exactly where the
  firmware is most dynamic. `indirect.txt` lists the sites so at least the
  gaps are visible.
- **Data decodes as instructions.** These are raw images with no section
  table, so constant pools and blobs produce edges that do not exist. Targets
  outside RAM/ROM are dropped and the flash-patch data region is tagged, but
  noise remains - a lone surprising edge deserves a look at the disassembly
  before it is believed.
- **Function boundaries are inferred**, by attributing each call site to the
  nearest BL target at or below it. Functions never reached by a BL (static,
  tail-called, or only ever called indirectly) do not exist as entries, and
  call sites inside them get attributed to whatever precedes them. A large
  offset in `up.py` output (`0xNNNN+0x8xx`) is the tell.
