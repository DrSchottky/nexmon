NEXMON_CHIP=CHIP_VER_BCM43430a1
NEXMON_CHIP_NUM=`$(NEXMON_ROOT)/buildtools/scripts/getdefine.sh $(NEXMON_CHIP)`
NEXMON_FW_VERSION=FW_VER_7_45_98
NEXMON_FW_VERSION_NUM=`$(NEXMON_ROOT)/buildtools/scripts/getdefine.sh $(NEXMON_FW_VERSION)`

NEXMON_ARCH=armv7-m

RAM_FILE=brcmfmac43430-sdio.bin
RAMSTART=0x0
RAMSIZE=0x80000

ROM_FILE=rom.bin
ROMSTART=0x800000
ROMSIZE=0xA0000

# re-located from 7_45_41_46 (0x45608) by byte-signature match; same
# function bytes, still calls the same ROM target 0x844914
WLC_UCODE_WRITE_BL_HOOK_ADDR = 0x4cee4

# re-located from 7_45_41_46 (0x2684 / 0x5ad9c) by byte-signature match on
# the reclaim table + following function prologue
HNDRTE_RECLAIM_0_END_PTR = 0x27e0
HNDRTE_RECLAIM_0_END = 0x616c8

PATCHSIZE=0x2000
PATCHSTART=$$(($(HNDRTE_RECLAIM_0_END) - $(PATCHSIZE)))

# re-located from 7_45_41_46 (0x4f4b8 / 0xb2a3) by disassembly: the routine
# that calls WLC_UCODE_WRITE_BL_HOOK_ADDR (bl 0x4cee4-0x10) ends in the same
# "pop.w {r4,r5,r6,r7,r8,pc}" epilogue, immediately followed by the
# UCODESIZE literal, then the ucode blob; UCODESTART+UCODESIZE rounds up to
# TEMPLATERAMSTART (0x61088) on a 4-byte boundary, matching the same
# relationship in both 7_45_41_26 and 7_45_41_46
UCODESTART = 0x55e98
UCODESIZE = 0xb1ed

# re-located from 7_45_41_46 (0x4f3b8) by byte-signature match; the 32 bytes
# around the pointer slot are identical except for the relocated literal
# itself, and 0x61088 (TEMPLATERAMSTART, derived from
# HNDRTE_RECLAIM_0_END - TEMPLATERAMSIZE) occurs exactly once in the whole
# binary, at this offset
TEMPLATERAMSTART_PTR = 0x55d98
TEMPLATERAMSTART = 0x61088
TEMPLATERAMSIZE = 0x640

# re-located from 7_45_41_46 (0x39574) by byte-signature match: the
# register-only "append flash-patch data slot" accessor
# (ldr r0,[r3]; cmp r0,r2; itte ne; addne.w r2,r0,#8; strne r2,[r3];
# moveq r0,#0; bx lr) is byte-identical, just relocated; its literal pool
# resolves to this pointer, and dereferencing it gives a plausible runtime
# value (0x16a0, vs. 0x15b8 in the old firmware)
FP_DATA_END_PTR = 0x40edc

# re-located from 7_45_41_46 (BASE_PTR_1=0x3b0ec, BASE_PTR_2=0x3b364) by
# structural/value match: neither pointer is ever referenced by RAM code
# (they're only ever consumed by ROM), so instead scanned for every 4-byte
# aligned dword equal to the stock FP_CONFIG_ORIGBASE (0x1800) with a
# plausible "end" value in the preceding word. Both firmwares yield exactly
# 4 such candidates, clustered together with identical spacing between
# entries (0x64/0xec/0x128 apart in both), so the whole cluster relocated
# as one block; BASE_PTR_1/2 map to the first/last candidate respectively
FP_CONFIG_BASE_PTR_1 = 0x429e8
FP_CONFIG_END_PTR_1 = 0x429e4
FP_CONFIG_BASE_PTR_2 = 0x42c60
FP_CONFIG_END_PTR_2 = 0x42c5c
# can start at the end of the firmware (0x617f0), it will be overwritten after it is read
FP_CONFIG_BASE=0x617f0
FP_DATA_BASE = 0x1000
FP_CONFIG_SIZE = 0xc00
FP_CONFIG_ORIGBASE = 0x1800
# stock value read back from FP_CONFIG_END_PTR_1 in the 7_45_98 binary
FP_CONFIG_ORIGEND = 0x21f0
