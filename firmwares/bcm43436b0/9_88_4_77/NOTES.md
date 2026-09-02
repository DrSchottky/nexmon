Reference firmware for the in-progress 9.88.4.77 port.

- Source: `RPi-Distro/firmware-nonfree`, `debian/added-firmware/brcm/brcmfmac43436-sdio.bin`
- Internal chip string: `43436b0-roml/...`
- Version: `9.88.4.77`, CRC `143f9f15`
- Date: Thu 2022-03-31 17:25:16 CST
- Fetched: 2026-08-20

Offline scaffold in place (`definitions.mk` via `derive.py`, `structs.h`,
`Makefile`, and the `patches/bcm43436b0/9_88_4_77/` trees). NOT hardware
verified. See `patches/bcm43436b0/9_88_4_77/PORTING_STATUS.md` for exactly
what is derived, its confidence, and what still needs the device.

`rom.bin` is not committed - dump it from the chip with `rom_extraction`
(chip-constant, so a 9_88_4_65 dump works too).
