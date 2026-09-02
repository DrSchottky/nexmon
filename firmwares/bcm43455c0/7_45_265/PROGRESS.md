# bcm43455c0 / 7.45.265 port — progress

Rig: target chip is `wlan1` on the Pi (`pi`/`raspberry`, passwordless sudo). Management
link is the AWUS036AXM on `wlan0`, `192.168.1.131`, carrying the default route — verified
before every stage. `wlan1` is `nmcli`-unmanaged so NetworkManager never grabs it back.

- [DONE] Stage 0 — device prep: `werk` (NM profile bound to wlan1) autoconnect disabled,
  `wlan1` set unmanaged, stock firmware backed up to `/root/cyfmac43455-sdio.stock.bin`
  (md5 `64410bcb1364a794ce4946bc40c7998f`, matches live alternative), boot-time fallback
  service `nexmon-fallback.service` installed+enabled, packages installed
  (gawk, dkms, libnl-3-dev, libnl-genl-3-dev). `nexutil` not yet built (Stage 5b).
- [DONE] Stage 1 — firmware directory: `derive.py` self-tests pass (206/234_CY/241 all
  OK), `definitions.mk`/`Makefile`/`structs.h`/`NOTES.md` written,
  `patches/include/firmware_version.h` has `FW_VER_7_45_265 115`. Build verified:
  ucode.bin 53160B (0xCFA8) starts with ucode magic, templateram.bin 2272B (0x8E0),
  flashpatches.c has 255 entries all < 0xB0000.
- [DONE] Stage 2 — patch directory (`patches/bcm43455c0/7_45_265/`): sources copied from
  206 (injection/monitormode/sendframe/ioctl/patch/console) and 234_CY (version.c
  scaffolding + Makefile), all `at()` retargeted to 7.45.265 addresses,
  `wlc_monitor_amsdu_patch` commented out (Stage 6, not yet derived), 5 wrapper.c
  `FW_VER_7_45_265` lines added (memcpy, wl_send, wl_sendup, wl_monitor, chan2freq).
  Every address confirmed by disassembly against the live 265 image before building:
  - `0x1A7604` disassembles to exactly `bl 0x1a323c` (the wl_monitor call site) - exact.
  - `0x1A323C` (wl_monitor) internally calls `bl 0x19a0f8` and tail-calls `b.w 0x1a2f68`
    - i.e. it independently corroborates the memcpy (0x19A0F8) and wl_sendup (0x1A2F68)
    addresses at the same time, resolving what was the weakest-evidence address in the
    plan (only 16B signature match) to very high confidence.
  - `0x202AAC`/`0x202AC8` both currently hold `mov.w rX, #1024` (0x400) - exactly the
    "console buffer size" pattern 206 doubles to 0x800, at both sites.
  - `0x20B988` holds `0x203b9` (wlc_ioctl+1) and `0x200E20` holds `0x1a2831`
    (wl_send+1) - both exact.
  All five addresses check out; none needed correction from the planning-stage values.
- [DONE] Stage 3 — build. Hit one real (non-address) bug: the 234_CY Makefile we based
  ours on dropped `-Wno-address-of-packed-member` (234_CY never compiles radiotap.c,
  since it has no monitor mode; our port does). Fixed by adding the flag back. Clean
  build after that. Verified: image size exactly matches stock (609309B); no linker
  errors (only expected gc-sections notes); version string shows
  `7.45.265 (28bca26 CY nexmon.org: ...)`; full byte-diff against stock shows 36378
  changed bytes and **zero unexplained** - every one falls inside the ucode/patch/
  fp_config regions or a known GenericPatch4 slot. Added
  `patches/bcm43455c0/7_45_265/nexmon/.gitignore` (matching the `7_45_98` precedent) to
  keep the built binary and BUILD_NUMBER untracked.
- [DONE] Stage 4 — wrapper audit. 266 weak (`W`) symbols in `gen/patch.elf`; 233 are
  `b_flash_patch_N` (expected stock ROM flashpatch entries, ignored per the audit method),
  the remaining 33 all appear as `DUMMY` in `gen/nexmon.pre`
  (bcm_binit/bcm_bprintf/free/hndrte_free_timer/hndrte_init_timer/hndrte_schedule_work/
  hndrte_time_ms/lb_alloc/memcpy/memset/pkt_buf_free_skb/pkt_buf_get_skb/printf/
  si_corereg/sprintf/strlen/strncmp/udelay/vsnprintf/wlc_bmac_write_template_ram/
  wlc_enable_mac/wlc_ioctl/wlc_iovar_op/wlc_pcb_fn_register/wlc_phy_chan2freq_acphy/
  wlc_queue_80211_frag/wlc_recv/wlc_scan_ioctl/wlc_sendctl/wlc_suspend_mac_and_wait/
  wl_monitor/wl_send/wl_sendup). **Zero real findings.** No silently-missing wrapper
  in this build - safe to deploy.
- [IN PROGRESS] Stage 5 — deploy. `scripts/deploy265.sh` / `scripts/revert265.sh` written
  and both tested (revert-while-stock-was-already-loaded confirmed the mechanism works
  and re-attaches cleanly, before ever loading the patched image).
  - [DONE] 5a boot: first deploy attempt succeeded outright -
    `Firmware: BCM4345/6 wl0: ... version 7.45.265 (28bca26 CY nexmon.org: 1065-dirty-1)`,
    no TRAP/corrupted/not-responding. wlan1 attached, wlan0 management link undisturbed.
  - [DONE] 5b ioctl via nexutil. Built natively on the Pi in ~/nexmon-build
    (libnexio -> nexutil; also needed utilities/libargp, not mentioned in the old
    43430a1 notes - nexutil.c includes argp-extern.h from there even on the native
    Linux build path). Installed to /usr/local/bin/nexutil (persistent, survives
    reboot unlike /tmp). `nexutil -Iwlan1 -g510 -l64 -r` returns exactly
    "wlc_ioctl_hook" (case 510's argprintf of __FUNCTION__) - confirms the
    wlc_ioctl_hook slot (0x20B988) is live. `-g511 -l512 -r` walks
    wlc->dumpcb_head and dumps real structured firmware state (VHT capability
    info) - confirms argprintf/bcm_binit/bcm_bprintf and the struct wlc_info
    layout are all correct on real hardware. Note: nexutil's default -l (buffer
    length) is 4 bytes and silently truncates output - always pass -l explicitly.
    console.c's two mov-immediate patches remain statically verified only (no
    runtime console-dump path in this port's ioctl.c to probe them live); already
    confirmed pre-build by disassembly (both sites held the expected
    `mov.w rX, #1024` pattern - see stage 2 commit).
  - [DONE] 5c monitor mode. `/home/pi/mon.sh` (persistent) does rfkill unblock -> wlan1
    up -> read phy from /sys/class/net/wlan1/phy80211/name -> iw phy $PHY interface add
    wlan1mon type monitor -> wlan1mon up -> wlan1 down -> nexutil -Iwlan1mon -m2, in
    that order (vif before mode, per the 43430a1 lesson). Worked on the first attempt:
    40-frame tcpdump capture decoded cleanly (correct band/rate/frequency, plausible
    -40 to -69dBm signal, sane frame types - BA/RTS/CTS - and MAC addresses, including
    the local wlan0 adapter's own MAC as a peer). A second capture on the AP's channel
    (40, 5200MHz) decoded beacons with the correct SSID "Jachtkamp18" in the clear.
    Cross-checked against `iw dev wlan0 scan`: BSSID 94:2a:6f:7a:b9:75 at 5200MHz
    matches exactly. No TRAP, no garbage/all-zero-MAC frames, no wrong-RSSI issue -
    none of the defects the 43430a1 port hit. wl_monitor (0x1A323C, the port's
    weakest pre-build address) is now fully hardware-confirmed.
  - [DONE] 5d injection. First attempt (wrong-channel wlan1mon vs wlan0mon after a
    fresh reload reset wlan1mon to channel 36) showed zero received frames -
    misdiagnosed as a possible firmware bug. Root-caused with a temporary counter+
    ioctl diagnostic (case 512, dbg_wl_send_hook_calls/dbg_inject_frame_calls) added
    to injection.c/ioctl.c: it proved wl_send_hook and inject_frame both fired 1:1 for
    every injected frame, `hdr0` matched, before either interface's channel was even
    checked - meaning the software path was correct and the failure was purely RF
    (channel mismatch), not a wrong address or a firmware bug. After aligning both
    monitor vifs to channel 40, 20/20 injected marker frames were received
    independently by the mt7921. **Debug instrumentation removed and clean image
    rebuilt+redeployed+re-verified end-to-end** (ioctl/monitor/injection all retested
    on the final committed source, all still pass: ioctl returns "wlc_ioctl_hook",
    monitor mode captured 321 beacons, injection delivered 20/20 marker frames).
    wl_send (0x1A2830) and the wl_send_hook slot (0x200E20) are now fully
    hardware-confirmed.

All four Stage 5 sub-steps (5a/5b/5c/5d) pass on the final clean build. Monitor mode
and frame injection both work.

- [DONE] Stage 6 — `wlc_monitor_amsdu_patch`. **This turned out not to be optional.**
  A channel-hopping stress test crashed the firmware (`brcmf_fw_crashed`, trap type 0x1,
  `lr` traced into `hnd_pkt_dup_hook` via the confirmed `memcpy` relocation) - the exact
  bug this patch exists to route around, on ordinary A-MSDU-aggregated traffic. Found the
  address by tracing forward from the chip-constant ROM call target (`0x25AE8`, the
  flashpatch redirect thunk) rather than by byte signature: `0x1B3E6A` (patch site,
  `bl 0x25ae8`) → `0x1B3E86` (skip target), byte-identical surrounding code and landing
  instruction to 206's `0x1B2A46`→`0x1B2A62`. Rebuilt, wrapper audit re-run clean,
  redeployed, and the crash never recurred across 56 subsequent channel hops + a
  14,222-frame sustained capture. See `REVERSE_ENGINEERING_NOTES.md`.
- [DONE] DKMS-free build verified. The loaded driver was actually a pre-installed
  third-party `brcmfmac-nexmon-dkms` package, not this repo's - confirmed via build-id
  mismatch. Built `patches/driver/brcmfmac_6.18.y-nexmon` on the Pi via plain kbuild
  (`ARCH=arm64 make -C /lib/modules/$(uname -r)/build M=<dir>`, no DKMS), confirmed
  build-id match after loading it, then re-ran the full ioctl/monitor/injection suite
  against *this repo's own* driver - all pass. Made it the `modprobe`-resolved module by
  replacing the file at `/lib/modules/<kver>/updates/dkms/brcmfmac.ko.xz` (the path
  `depmod`'s search order actually prefers) and running `depmod -a`; verified via
  `rmmod`+`modprobe` (not just `insmod`) that the boot-time load path picks it up.
- [DONE] Channel hopping. 56 hops (8 cycles × 7 channels, 1/6/11/36/40/44/48) with a live
  capture and an ioctl-liveness check after every hop: 0 failures, 0 crashes,
  3667 frames total, after the A-MSDU fix above. (149/153/157/161 correctly rejected by
  the kernel's regulatory domain - NL/DFS-ETSI doesn't clear them without a DFS CAC scan;
  confirmed via `iw reg get`, not a driver/firmware issue.)
- [DONE] RX FIFO overflow. Sustained 45s single-channel capture, no hopping: 14,222
  frames, 0 dropped by kernel, 0 dmesg errors. No `bcm43430a1`-style `macintstatus`/MI_RXOV
  spin wedge at any point in this session, including before the A-MSDU fix (that crash
  was a different, unrelated bug - see above).
- [DONE] Flash patching. 255 entries applied with zero errors in `flashpatches.log`
  (all individual `dd` copies clean); implicitly proven live by the whole port working.
- [DONE] Ucode compression. Build produces `gen/ucode_compressed.bin` (53160 → 26711
  bytes, real ~50% reduction) and a 2231-line generated `ucode_compressed.c`; the PHY
  visibly working (correct rates/channels/RSSI across thousands of captured frames) is
  the on-chip decompression proof.

**Full checklist (monitor mode, radiotap, injection, flash patching, ucode compression,
DKMS-free build, channel hopping, RX-FIFO-overflow) is now green.**
- [TODO] Stage 6 — `wlc_monitor_amsdu_patch` address (optional, only after 5c/5d pass).

Port complete: full checklist green, hardware-verified, DKMS-free, channel hopping and
RX-FIFO-overflow both explicitly stress-tested clean.

Plan file: `/home/jayofelony/.claude/plans/i-need-you-to-bubbly-bonbon.md` (all address
tables, confidence levels, and verification commands live there — this file only tracks
stage status).
