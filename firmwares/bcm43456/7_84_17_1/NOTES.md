Reference firmware, not yet patch-ready. No `patches/bcm43456/` directory exists yet -
`CHIP_VER_BCM43456` is not defined in `patches/include/firmware_version.h` either. This
chip has never been ported in this tree.

- Source: `RPi-Distro/firmware-nonfree`, `debian/added-firmware/brcm/brcmfmac43456-sdio.bin`
  (`trixie` branch; the module is registered under `brcm/`, not `cypress/` as
  `bcm43455c0`'s source was). Companion `.clm_blob` and `.txt` (board NVRAM/calibration)
  fetched alongside for reference - the live target device (Pi 400 / CM4, driven by
  `brcmfmac43456-sdio.raspberrypi,400.bin` -> this same base file) needs both to attach.
- Internal chip string: `43455c5-roml/43455_sdio-pno-aoe-pktfilter-pktctx-lpc-pwropt-
  43455_ftrs-mfp-noclminc-clm_min`
- Version: `7.84.17.1 (r871554)`, CRC `72494685`, FWID `01-3d9e1d87`
- Date: Thu 2020-05-14 17:41:11 KST
- Ucode Ver: 1043.20424
- sha256: `ddf83f2100885b166be52d21c8966db164fdd4e1d816aca2acc67ee9cc28d726`
- Confirmed **current**: this is the exact file (byte-identical, matching md5) currently
  live-installed on a Raspberry Pi 4B via the actively-updated `firmware-brcm80211
  1:20260519-1~bpo13+1+rpt1` package, and `RPi-Distro/firmware-nonfree`'s GitHub history
  shows exactly one commit for this file ever (`fc62b6da`, 2026-07-10, "Add Raspberry Pi
  43456 WLAN firmware (Pi 400, CM4)") - Broadcom/Cypress has not shipped a newer build
  since 2020; this is not a stale fetch.
- Fetched: 2026-08-28

### For whoever starts the actual port

The internal string identifies this as a **`43455c5` stepping**, not `43455c0` - a
*different* silicon stepping of the same die family this repo's `bcm43455c0` work
already covers, packaged by Cypress/Broadcom under the different module part number
"43456" (this is also why `brcmf_c_preinit_dcmds` in dmesg logs it as `BCM4345/6` on
*either* chip - same family ID at the SDIO probe level). The ucode magic bytes
(`4e 10 00 03 60 bc 01 00`) used to derive `bcm43455c0`'s `definitions.mk` are present in
this image too (`derive.py`'s technique should carry over), but **no addresses carry
over directly** - different stepping means a different ROM, so every relocated address
needs re-deriving from scratch, not copied from `bcm43455c0`. Use `bcm43455c0`'s
completed port (`patches/bcm43455c0/7_45_265/`, and the methodology writeups in
`REVERSE_ENGINEERING_NOTES.md`) as the template for *process*, not as a source of
addresses.

No same-chip earlier firmware version exists in this repo to relocate *from* either
(this is the first and only firmware file for this chip here) - the wrapper/patch
addresses will need to come from disassembly and the `bl`-to-ROM-constant technique
documented in `REVERSE_ENGINEERING_NOTES.md`, not from a same-chip byte-signature
relocation the way `bcm43455c0`'s `234_CY -> 265` port worked.
