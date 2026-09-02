Reference firmware, not yet patch-ready.

- Source: `RPi-Distro/firmware-nonfree`, `debian/added-firmware/cypress/cyfmac43430-sdio.bin`
- Internal chip string: `43430a1-roml/...`
- Version: `7.45.98 (TOB) (56df937 CY)`, CRC `442f3e3`
- Date: Mon 2021-07-19 03:25:10 CDT
- Fetched: 2026-08-20

This is the official RPi firmware for this chip as currently distributed
(newer than the `7_45_41_46`/`7_45_41_26` versions already patched in this
repo). No `definitions.mk`/`structs.h`/patch code exists for this build yet
- that requires disassembling this exact binary and locating the relevant
hook points/offsets, same as was done for the existing supported versions.
Dropped in here purely as a starting point for that work.
