Reference firmware, not yet patch-ready.

- Source: `RPi-Distro/firmware-nonfree`, `debian/added-firmware/cypress/cyfmac43439-sdio.bin`
- Internal chip string: `43439a0-roml/...`
- Version: `7.95.75 (0a0104c CY)`, CRC `16c9d604`
- Date: Wed 2023-11-22 18:33:30 PST
- Fetched: 2026-08-20

Newer than the `7_95_49_2271bb6` version already patched in this repo. Note
the existing version's firmware is stored as a combined header
(`w43439A0_7_95_49_00_combined.h`) rather than a raw `.bin` - this drop is
the raw `.bin` as distributed by RPi, so it will need converting to match
whatever format `firmwares/bcm43439a0/Makefile` expects before it's usable.
No `definitions.mk`/`structs.h`/patch code exists for this build yet either.
