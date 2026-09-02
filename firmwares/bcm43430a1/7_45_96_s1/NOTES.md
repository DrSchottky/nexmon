Reference firmware, not yet patch-ready.

- Source: `RPi-Distro/firmware-nonfree`, `debian/added-firmware/brcm/brcmfmac43436s-sdio.bin`
- Internal chip string: `43430a1-roml/...` (same chip family as bcm43430a1, despite the
  "43436s" filename - this is the build RPi ships under the "model-zero-2-w"
  symlink chain)
- Version: `7.45.96.s1 (gf031a129)`, CRC `6670a1e`
- Date: Wed 2023-06-14 07:28:04 CST
- Fetched: 2026-08-20

Even newer than `7_45_98` in this same chip directory. No patch scaffolding
exists for this build yet - see the note in `../7_45_98/NOTES.md` for what's
still needed.
