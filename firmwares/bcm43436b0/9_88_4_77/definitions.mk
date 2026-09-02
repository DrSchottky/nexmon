NEXMON_CHIP=CHIP_VER_BCM43436b0
NEXMON_CHIP_NUM=`$(NEXMON_ROOT)/buildtools/scripts/getdefine.sh $(NEXMON_CHIP)`
NEXMON_FW_VERSION=FW_VER_9_88_4_77
NEXMON_FW_VERSION_NUM=`$(NEXMON_ROOT)/buildtools/scripts/getdefine.sh $(NEXMON_FW_VERSION)`

NEXMON_ARCH=armv7-m

RAM_FILE=brcmfmac43436-sdio.bin
RAMSTART=0x0
RAMSIZE=0x80000

ROM_FILE=rom.bin
ROMSTART=0x800000
ROMSIZE=0xA0000

# All addresses below derived by derive.py in this directory. The portable
# heuristics (ucode / reclaim / templateram) self-test clean against
# bcm43430a1/7_45_41_46, bcm43430a1/7_45_98 and bcm43436b0/9_88_4_65; the
# flash-patch block self-tests against bcm43436b0/9_88_4_65 only.
#
# NOT yet hardware-verified. Before trusting an injection/monitor build,
# confirm at least FP_CONFIG_BASE_PTR_1/2 (and their END pointers) on the
# device - dump the live dwords at these addresses with the rom_extraction
# memory-read ioctl (case 0x603), or check ROM disassembly for which words
# ROM actually reads.

WLC_UCODE_WRITE_BL_HOOK_ADDR = 0x4dae0
HNDRTE_RECLAIM_0_END_PTR = 0x3080
HNDRTE_RECLAIM_0_END = 0x65804

PATCHSIZE=0x2000
PATCHSTART=$$(($(HNDRTE_RECLAIM_0_END) - $(PATCHSIZE)))

# original ucode start and size (length word sits at UCODESTART-4)
UCODESTART = 0x596CC
UCODESIZE = 0xbb08

# original template ram start and size
# TEMPLATERAMSTART = round_up_4(UCODESTART + UCODESIZE)
# TEMPLATERAMSIZE  = HNDRTE_RECLAIM_0_END - TEMPLATERAMSTART
TEMPLATERAMSTART_PTR = 0x59554
TEMPLATERAMSTART = 0x651D4
TEMPLATERAMSIZE = 0x630

# stock flash-patch table: 98 entries, 8-byte (target,dataptr) records
FP_DATA_END_PTR = 0x413A0
FP_CONFIG_BASE_PTR_1 = 0x407A0
FP_CONFIG_END_PTR_1 = 0x407A4
FP_CONFIG_BASE_PTR_2 = 0x408E4
FP_CONFIG_END_PTR_2 = 0x408E8
# scratch, at the 16-byte-rounded end of the image; overwritten after it is read
FP_CONFIG_BASE=0x65970
FP_DATA_BASE = 0x1000
FP_CONFIG_SIZE = 0xc00
FP_CONFIG_ORIGBASE = 0x1800
FP_CONFIG_ORIGEND = 0x1B10
