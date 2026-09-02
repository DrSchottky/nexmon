# Reverse engineering notes (BCM43430/1, Pi Zero 2 W, Debug branch)

Working notes from live debugging of `patches/bcm43430a1/7_45_98` against a
real chip (hostname `Jayofelony`, `10.12.194.1`). Kept here so the next
session (or the next chip) doesn't have to rediscover the method from
scratch. See also git history on the `Debug` branch for the actual fixes.

## The core bug pattern: silently-missing wrapper addresses

`patches/common/wrapper.c` declares vendor ROM/RAM functions via a stack of
`AT(CHIP_VER_x, FW_VER_y, ADDR)` attributes above one declaration, e.g.:

```c
AT(CHIP_VER_BCM43430a1, FW_VER_7_45_41_26_r640327, 0x9F38)
AT(CHIP_VER_BCM43430a1, FW_VER_7_45_41_46, 0xA024)
int
wlc_d11hdrs(void *wlc, void *p, void *scb, ...)
RETURN_DUMMY
```

The nexmon GCC plugin (`buildtools/gcc-nexmon-plugin/nexmon.c`) matches the
build's `chipver`/`fwver` against these lines. **If no line matches the
firmware version being built, this fails silently** — no compiler error, no
linker error. The plugin just compiles the `RETURN_DUMMY`/`VOID_DUMMY` body
(`{ ; return 0; }` / `{ ; }`) as an ordinary local function, and the linker
places it at whatever address function-section layout happens to give it.
The build looks completely clean. At runtime, calling that "wrapped"
function does *not* call the real vendor code — it either no-ops or, if the
linker placed it somewhere that gets overwritten by other RAM content before
it's called (heap, other data), it can jump into garbage and trap the
firmware.

This is exactly what happened porting `7_45_41_46` → `7_45_98` for
`bcm43430a1`: someone (an earlier session) relocated the flash-patch-table
bookkeeping addresses in `definitions.mk` (`WLC_UCODE_WRITE_BL_HOOK_ADDR`,
`HNDRTE_RECLAIM_0_END`, `UCODESTART`, `TEMPLATERAMSTART_PTR`,
`FP_DATA_END_PTR`, `FP_CONFIG_BASE_PTR_1/2`) but missed five functions used
by `sendframe.c` (injection) and `monitormode.c` (RX radiotap wrapping):
`wlc_d11hdrs`, `wlc_get_txh_info`, `wlc_txfifo`, `wlc_bmac_read_tsf`,
`wlc_phy_channel2freq`. Frame injection reliably firmware-trapped
(`TRAP 3`) because `sendframe()` called a fake `wlc_d11hdrs` stub sitting on
top of unrelated memory. Fixed on `Debug` (commit `d12879ab`) by relocating
all five from the known-good `7_45_41_46` addresses via byte-signature
matching (see below). **Confirmed fixed**: injection no longer traps the
firmware after the fix (tested live, two rounds, no `TRAP` in dmesg).

### How to detect this for a new chip/firmware port

After `make` in a `patches/<chip>/<fwver>/nexmon` dir, check
`gen/nexmon.pre` for every wrapped function actually used by the patch
sources (`grep -rn '\bwlc_[a-z_0-9]*(' src/*.c` to enumerate calls, then
check each name appears as a `DUMMY <addr> <name>` line, not just as a
`.text.<name>` section in `log/linker.log` with no matching `DUMMY` line).
If a function is called but never shows up in the `DUMMY` list, it silently
compiled as a local stub — that is the bug pattern above. Cross-check
against `wrapper.c` / `local_wrapper.c`: does an `AT()` line exist for this
exact `CHIP_VER_x, FW_VER_y` pair? If not, it needs relocating.

### Byte-signature relocation method (what worked)

Given a function's known-good address in an older/reference firmware
version of the *same chip*, and the two stock (unpatched)
`brcmfmac*.bin` RAM images on disk (`firmwares/<chip>/<fwver>/`):

```python
data_ref = open('firmwares/<chip>/<known_good_fwver>/brcmfmac*.bin','rb').read()
data_new = open('firmwares/<chip>/<target_fwver>/brcmfmac*.bin','rb').read()
sig = data_ref[known_addr:known_addr+32]   # 32-byte prologue signature
offs = [i for i in range(len(data_new)) if data_new.startswith(sig, i)]
# want exactly one hit; shrink siglen (24/16/12/8) if zero hits, since
# minor recompiles can shift bytes a few instructions in; a single unique
# hit at 32 bytes was enough for all 5 functions fixed this session
```

Always sanity-check the resulting address by disassembling ~40 bytes there
(`arm-none-eabi-objdump -D -b binary -m arm -M force-thumb
--start-address=<addr> --stop-address=<addr+40> <file>`) — it should look
like a real function prologue (`push`/`stmdb` with a sensible register
list, followed by coherent instruction flow), not garbage ending in `udf`
(0xdebf, which is GNU as's Thumb padding/fill value — a strong tell that
you've landed on inter-function padding, not code).

RAM addresses (< `RAMSIZE`, i.e. below `0x80000` for this chip) are
firmware-build-specific and **must** be relocated per firmware version.
ROM addresses (>= `ROMSTART`, `0x800000`+ here) are chip-stepping-specific
and constant across firmware versions for the same physical chip revision
— but see the ROM-mismatch note below, they can still be wrong if the
*wrong reference chip revision* was used originally.

## Live ROM/RAM extraction from the actual chip

Added a raw-memory-read custom ioctl to confirm addresses against what's
*actually on this physical chip*, not just assumptions:

```c
// in patches/<chip>/<fwver>/nexmon/src/ioctl.c, inside wlc_ioctl_hook's switch:
case 603: // manual byte-loop memcpy, no dependency on the (possibly wrong) wrapped ROM memcpy
{
    volatile unsigned char *src = *(unsigned char **) arg;
    volatile unsigned char *dst = (unsigned char *) arg;
    int i;
    for (i = 0; i < len; i++) dst[i] = src[i];
    ret = IOCTL_SUCCESS;
}
break;
```

Build `utilities/nexutil` **natively on the target device** (aarch64 Pi:
`cd utilities/nexutil && make`, no cross-compile needed, no extra deps in
the default `USE_NETLINK` build path). Then:

```
nexutil -I wlan0 -g603 -i -v<hex addr> -l<len, up to 8192> -r > chunk.bin
```

Chunk in 8192-byte reads (matches `BRCMF_DCMD_MAXLEN`) and concatenate to
dump arbitrarily large ranges. Used this to pull this chip's full 640KB ROM
(`0x800000`-`0x8A0000`) into
`firmwares/bcm43430a1/7_45_98/rom.bin` for offline disassembly.

**Do NOT use a plain wrapped `memcpy()` for this** — on this specific chip,
`memcpy`'s assumed ROM address (`0x880B80`, `FW_VER_ALL` in `wrapper.c`) is
provably wrong (disassembles to an unrelated small thunk/trampoline, not a
copy routine; confirmed by testing writes/reads through it that silently
did nothing). `malloc` shares the exact same (wrong) address in
`wrapper.c` — also suspect, not yet independently verified. Always use the
manual byte-loop version above until/unless these are re-verified.

**Safety warning — do not scan low/unknown RAM addresses live.** Reading
ROM (`0x800000`+) this way is safe (full 640KB dump done cleanly, no
issues). Reading unverified RAM/peripheral-mapped low addresses is NOT
safe — one attempt to scan from `0x0` upward crashed the firmware hard
(`brcmf_sdio_bus_reset: giving up on SDIO card reset after 5 attempts, bus
stays down`) and required a **physical power cycle** to recover; `rmmod`/
`modprobe` alone hung in `D` state and could not recover it. If you need to
inspect RAM content, prefer reading the *static* `.bin` file on disk at the
same offset (RAM address == file offset, since `RAMSTART=0x0`) over a live
read, unless you specifically need to confirm runtime-only state (e.g. the
flash-patch config table, which is legitimately overwritten after boot —
see `FP_CONFIG_BASE`'s comment in `definitions.mk`, "will be overwritten
after it is read").

## Current unresolved issue on bcm43430a1/7_45_98: RX is completely silent

Frame injection is now fixed (crash resolved). **RX (monitor-mode capture)
still produces zero packets and zero SDIO interrupt activity**, even with:
- CLM blob, country/regulatory, channel-set all confirmed correct (`iw
  scan` in managed mode finds real APs with full details — RX genuinely
  works outside monitor mode).
- `SET_MONITOR`/`GET_MONITOR` round-trips correctly via the proper
  per-interface path (`brcmf_net_mon_open` in
  `patches/driver/brcmfmac_6.18.y-nexmon/core.c`), confirmed `monitor: 2`.
- `SET_PROMISC` added alongside `SET_MONITOR` in the same function (commit
  `ae2cb6c6`) — no change.
- The flashpatch table itself verified correct via static analysis of the
  built `.bin`: our `wl_monitor_hook` entry (`0x81F620`) is entry #213,
  correctly appended after the 212 stock entries, with both
  `FP_CONFIG_END_PTR` locations correctly extended. Not a build/install
  bug.
- `0x81F620` disassembles to a plausible RX-status-build-then-dispatch
  callsite (3-arg call matching `wl_monitor_hook(wl, sts, p)`'s shape).
- An unconditional `printf()` at the very top of `wl_monitor_hook()`
  (`patches/bcm43430a1/7_45_98/nexmon/src/monitormode.c`) — before any of
  the potentially-still-broken calls below — **never fires**, checked via
  `console_interval` debugfs polling and live beacon traffic on the tuned
  channel.

Not yet fixed/explained. `pkt_buf_get_skb` (used by `wl_monitor_radiotap`,
downstream of the hook) has **no `CHIP_VER_BCM43430a1` entry at all** in
`wrapper.c` (missing for every firmware version, not just `7_45_98`) — but
this can't explain the silence since the hook's `printf` runs before that
call is ever reached.

**Caller trace attempted, inconclusive.** Correctly identified the function
containing `0x81F620` starts at `0x81F414` (confirmed via a properly
instruction-aligned disassembly — a naive `--start-address=0x81f560` cut
mid-instruction the first time and produced misleading garbage; always
disassemble from a confirmed boundary, e.g. right after a `b.w`/`bx lr`, and
slice the region of interest out of one continuous pass, don't restart
objdump at an arbitrary address inside a Thumb function). Searched the
*entire* ROM dump and the stock RAM firmware image, both as disassembled
`bl`/`b.w`/`b.n` targets and as raw little-endian literal bytes (for
function-pointer/vtable-style indirect calls), for any reference to
`0x81F414` — zero hits, anywhere. Search method validated against a
known-good control (`wlc_bmac_read_tsf` at `0x20abc`, found referenced
twice, exactly as expected) — so the method works, the negative result is
real.

**But this doesn't prove `0x81F620` is wrong.** Ran the same zero-literal-
reference check against `wl_monitor` itself (`0x819510`, the ROM function
nexmon's own `wl_monitor_hook` calls for non-radiotap `MONITOR_IEEE80211`
mode) — also zero references anywhere in stock code. That's expected: this
whole class of function is vendor ROM code that stock firmware never calls
(consumer devices don't expose monitor mode) — nexmon's model is
specifically to hijack otherwise-dead ROM code via flashpatch redirection,
so "no static caller found" is the *normal* signature for every nexmon
monitor-hook target, not evidence specific to this address being wrong. A
static caller/xref search cannot distinguish "correct hook, naturally
unreferenced by design" from "wrong address" for this class of patch.

**What would actually be decisive** (not yet done, would need either more
tooling or a second data point):
- A real ROM dump from a *different, known-working* BCM43430/1 device
  running the same nexmon monitor-mode setup, to diff against this chip's
  `rom.bin` — if `0x81F620`'s neighborhood differs between the two, that's
  a smoking gun either way. Nobody had this available this session.
- Proper decompilation (Ghidra + the `armv7-m` processor module) instead of
  raw `objdump` text-grepping, so real cross-reference analysis (including
  through indirect/computed dispatch tables, which this ROM uses heavily —
  see the `blx r7` example near `0x81f3b8`) is possible. `objdump`/manual
  grepping cannot follow computed jump tables or vtable-style dispatch, and
  this ROM has plenty of both; that's the real blind spot in this session's
  method, not the direct-reference search itself.
- Alternatively, single-step/hardware-trace the actual RX interrupt handler
  on the live chip (out of scope for what's available here — no JTAG/SWD
  access set up, only the SDIO-side custom-ioctl memory read/write).

## A real reference firmware exists — and it disagrees with this repo's build

This device already runs monitor mode successfully in production via a
*different* source: Pwnagotchi here uses the `brcmfmac-nexmon-dkms` Debian
package (driver only) plus the `firmware-nexmon` Debian package (firmware +
CLM, version `0.2`) — **not** anything built from this repo. Both `.deb`s
are available locally for inspection/extraction:
`~/work-64bit/stage3/rootfs/home/pi/brcmfmac-nexmon-dkms_6.12.2+ndevfix1_all.deb`
and `.../firmware-nexmon_0.2_all.deb.1` (`dpkg-deb -x <deb> <destdir>`).
This is a **confirmed-working** reference, in production on this exact
physical chip — the single most valuable data point available, more useful
than a second physical device would have been.

Important wiring detail found while chasing this: on this board
(`raspberrypi,model-zero-2-w`), `brcmfmac` does **not** load
`/lib/firmware/brcm/brcmfmac43430-sdio.bin` directly. Firmware selection
follows board-specific symlinks:
`brcmfmac43430-sdio.raspberrypi,model-zero-2-w.bin` →
`brcmfmac43436s-sdio.bin` → `../cypress/cyfmac43430-sdio.bin`. This repo's
`install-firmware` target does write through this chain correctly (it `cp`s
onto `brcmfmac43436s-sdio.bin`, which is a symlink, so the real target
`/lib/firmware/cypress/cyfmac43430-sdio.bin` gets updated) — but if you're
ever unsure which file is *actually* loaded, don't trust the plain
`brcm/brcmfmac43430-sdio.bin` path on its own; `readlink -f` the
board-specific name and `strings | grep nexmon_ver`/version string on the
resolved file to confirm.

The confirmed-working firmware is `7.45.41.46 (r666254 CY)` —
**byte-identical stock vendor base** to
`firmwares/bcm43430a1/7_45_41_46/brcmfmac43430-sdio.bin` already in this
repo (same CRC `970a33e2`, same build date). So this is a clean, valid
comparison: same chip, same stock base firmware, different patch source.

**Test performed**: built this repo's `patches/bcm43430a1/7_45_41_46`
fresh (confirmed zero missing-wrapper gaps beforehand, see pattern above),
installed it, and it attaches perfectly cleanly with **no CLM blob**
present. Then installed the CLM blob extracted from the confirmed-working
`firmware-nexmon` package (`cyfmac43430-sdio.clm_blob`, md5
`9ff46519b8b8c2cab323322c9d983873` — this is the *same* blob already used
successfully with `7_45_98` throughout this whole session) alongside our
`7_45_41_46` build. Result: **100% reproducible firmware trap**, every
single reload, immediately during/right after `clmload`:
```
brcmf_c_process_clm_blob: clmload (4733 byte file) failed (-110)
brcmf_sdio_checkdied: firmware trap in dongle
dongle trap info: type 0xc @ epc 0x0001f106 ...
```
This is the exact same trap signature noted earlier this session (before
the missing-wrapper bug was even found) when `7_45_41_46` + this CLM was
first tried — now confirmed fully reproducible and isolated: not a
transient wedge, not the CLM blob's fault (proven good elsewhere), not the
missing-wrapper pattern (this build has none). It's specific to how this
repo's `7_45_41_46` nexmon source interacts with CLM loading.

**Corrected finding** (an earlier pass at this got the table location
wrong by exactly one entry — 12 bytes — and concluded the working binary
had no custom flash-patch entry at all; that was a parsing bug, not a real
finding, documented here so it isn't repeated). The real relocated table
starts at file offset `0x5aeb0`, not `0x5aebc` as first assumed
(`0x5aebc` is actually entry *1*, not entry 0). Entry 0 is:
```
0x5aeb0: orig=0x81f620 size=0x4 slot=0x1000
```
**This is exactly `wl_monitor_hook`'s flashpatch** — same original address,
same format, as this repo's `monitormode.c` produces. So the working
firmware *does* use the identical `0x81F620` flashpatch mechanism. The
"different patch mechanism" theory is wrong; retracted.

**What's actually different, confirmed by identical-source diff**: `git
diff b8c6999a..Debug -- patches/bcm43430a1/7_45_41_46/` (that commit is
this repo's own history — its short hash, `b8c6`, matches the working
firmware's `nexmon_ver: 2.2.2-552-gb8c6-2` exactly, and it's the *only*
commit in the whole repo with that prefix) shows **only two trivial
diffs**: the `rm -rf`-CLM-blob-on-install lines we already removed this
session, and a cosmetic `" (Nexmon)"` string suffix. `monitormode.c`,
`ioctl.c`, `patch.c`, `sendframe.c`, `injection.c`, `definitions.mk` are
all byte-identical. So the source this repo has for `7_45_41_46` is (and
was, at the point matching the working firmware) exactly what built the
working firmware — yet **the compiled output differs**: the working
firmware's table has `0x81F620` as entry 0, *prepended* before all 183
stock entries; this repo's own build (confirmed via `gen/nexmon.pre`,
captured earlier this session) appends it as entry 184, *last*, after
every stock entry — same as the `7_45_98` build's entry-213-of-213
placement pattern documented above.

**That's the real, narrowed-down lead**: identical source, different
build-tool output ordering for the same custom flash-patch entry. Since
attempting a local x86_64 build of this exact historical commit hit a
32-bit/64-bit plugin ABI mismatch (`buildtools/gcc-arm-none-eabi-5_4-2016q2`'s
`cc1` is 32-bit i386, the currently-built `gcc-nexmon-plugin/nexmon.so` is
x86-64 — would need a 32-bit rebuild of the plugin, or just build on the
Pi's aarch64 toolchain instead, which already works), this wasn't fully
run to ground this session.

**Not yet done, would be the decisive next step**: figure out why identical
source produces prepended-vs-appended table ordering — likely candidates:
(a) a different version of `buildtools/gcc-nexmon-plugin/nexmon.c` (the
compiler plugin itself, which decides `FLASHPATCH` placement/ordering) was
used for the working build vs. whatever's checked into this repo now, or
(b) a build-environment/toolchain-version difference (this repo's
`buildtools/` binaries are pinned; the Kali package may build with a
different, newer toolchain). Check `buildtools/gcc-nexmon-plugin/nexmon.c`'s
own git history for changes around ordering/placement logic, and consider
whether table order (prepended vs. appended) could plausibly matter for
CLM loading specifically (crash happens *during* `clmload`, so a plausible
mechanism: FP_DATA_BASE/heap layout shifts depending on where in the table
the custom entry lands, and something CLM-processing-related collides with
whatever ends up at the *end* of the table when appended vs. not). Worth
testing directly: if there's a way to force prepend-instead-of-append in
this repo's build tooling, try it and see if the crash goes away.

## Practical gotchas hit while testing on this device

- Software `reboot` (systemd) does **not** clear a genuine SDIO backplane
  wedge — the WiFi chip itself only loses power on a real physical power
  cycle. If a firmware trap wedges the bus, expect `dongle is not
  responding: err=-5` to persist across a soft reboot; you need the user to
  physically power-cycle.
- This device has a `brcmfmac-watchdog.service` (see
  `~/PycharmProjects/pwnagotchi/stage3/06-patches/files/brcmfmac-watchdog.*`)
  that detects `dongle is not responding` in dmesg and **automatically
  reboots the Pi**. Useful for unattended recovery, but it means a crash
  loop can eat several minutes on its own before you get a stable window to
  work in — don't fight it, just wait for `uptime` to show a fresh boot and
  move fast (`/tmp` gets wiped every time, re-`scp` anything you need).
- `/tmp/known_good.clm_blob`-style scratch files on the Pi do not survive
  any of the above reboots — re-transfer before every retest, don't assume
  a previous `scp` is still there.

## Decisive finding: `wl_monitor_hook` itself never fires — the RX failure is upstream in firmware, not in the driver or the wrapper addresses

This supersedes the "caller trace attempted, inconclusive" section above with
an actual answer, and rules out the flash-patch-table-ordering theory as the
proximate cause of the RX silence (it may still explain the *separate*
`7_45_41_46` CLM crash, but not this).

**Test setup**: added an unconditional `printf("wl_monitor_hook: called,
monitor=%d\n", ...)` at the top of `wl_monitor_hook` in
`patches/bcm43430a1/7_45_98/nexmon/src/monitormode.c` (commit `d6feca9b`),
rebuilt `7_45_98` firmware from this repo's current `Debug` branch source
(includes the byte-signature-relocated wrapper fixes from commit
`d12879ab`), rebuilt the driver via DKMS directly on the Pi (see gotcha
below about the driver `Makefile`'s `NEXMON_ROOT`-based include paths not
being portable to a DKMS build tree), installed the confirmed-known-good CLM
blob (md5 `9ff46519b8b8c2cab323322c9d983873`, extracted fresh from
`firmware-nexmon_0.2_all.deb` on the Pi — the earlier extraction in `/tmp`
does not survive a power cycle, `/tmp` is wiped every time), and enabled
firmware console log forwarding to `dmesg` via the `brcmfmac` driver's
`debug` module param (`modprobe brcmfmac debug=0x100000`, i.e. the
`BRCMF_FWCON_VAL` bit — this requires `CONFIG_DYNAMIC_DEBUG` NOT gating
`pr_debug`, which is the case on this Pi kernel; `/sys/kernel/debug/dynamic_debug/control`
doesn't even exist here, so `pr_debug` compiles as a plain `printk` given
this driver's `-DDEBUG` build flag).

Confirmed working end-to-end: booted cleanly (`nexmon_ver: 61c1-dirty-1`,
version string `7.45.98 (TOB) (56df937 CY) (Nexmon)`), no CLM crash, and
real firmware boot-time log lines (`Decompressing ucode...`,
`wlc_channels_commit`, `TCAM: 256 used: 212`, etc.) visibly forwarded to
`dmesg` as `brcmfmac: CONSOLE: ...` lines — proving the console-readback
path itself works and isn't a false negative.

Then: brought up `wlan0mon` (type monitor), set channel 6 (confirmed via
`iw dev wlan0 scan` to have multiple real, actively-beaconing APs in range —
`Jachtkamp18` etc., beacon interval 100ms, so ~150 beacons expected in a
15s window at minimum), ran `tcpdump -i wlan0mon` for 15 seconds. Result:
**0 packets captured, and zero new `CONSOLE:` lines of any kind** — not just
no `wl_monitor_hook: called` line, but no firmware console output *at all*
after the initial boot burst, in this or a subsequent 20s idle-window
control check. Since this firmware only prints what patch code explicitly
calls `printf()` for, this is *consistent* with — not contradictory to —
the hook simply never being reached; it's not proof of a broken console-poll
path (that was independently confirmed working via the boot-time messages).

**Conclusion**: RX frames are not reaching `wl_monitor_hook` at all in
monitor mode on this firmware/device, even with strong, active, real 802.11
traffic in range on the tuned channel. This means the bug is **upstream of
the flashpatch hook** — most likely the D11 MAC's hardware receive path
itself isn't delivering frames up into `wl`'s software RX callback chain
while in monitor state (interrupt mask, RX FIFO enable, or promiscuous/BSS
filter configuration at the hardware level), rather than anything about
*which* function the flashpatch redirects to or where wrapped addresses
point. The flash-patch-table-ordering divergence found earlier is probably
a red herring for this specific issue (it's a plausible lead for the
*separate* `7_45_41_46` CLM-load crash, which is a different failure mode
entirely — crash-on-load vs. silent-no-op).

**Also confirmed this session, ruling out driver/firmware-*version*
mismatch as the cause**: tested the confirmed-genuine reference firmware
binary (`cyfmac43430-sdio.bin`, md5 `59038e366d9389478e466e225c21bda1`,
extracted straight from the working `firmware-nexmon` package, i.e. not
rebuilt by this repo at all) together with this repo's own driver — also
0 packets on the correct channel. And separately, `wlan0` in plain managed
mode does a completely normal full scan and receives detailed beacon IEs
from all nearby APs without issue, proving the RF front end, SDIO bus, and
chip itself are all healthy — this is a monitor-mode-specific RX path
failure, not a hardware problem with this unit.

**Practical gotcha hit getting the debug console readback working**: the
driver's `Makefile` (`patches/driver/brcmfmac_6.18.y-nexmon/Makefile`) uses
`-I$(NEXMON_ROOT)/patches/driver/brcmfmac_6.18.y-nexmon[/include]` for its
`ccflags-y`, which only resolves inside this repo's own tree with
`NEXMON_ROOT` exported (e.g. via `source setup_env.sh`). A DKMS build tree
on the target device (`/usr/src/brcmfmac-nexmon-<ver>/`) is a flat copy with
no such env var and no matching directory structure, so building via
`dkms install` fails with `fatal error: defs.h: No such file or directory`
etc. Fix: use portable `-I$(src)` / `-I$(src)/include` instead — these are
the standard kbuild variables that always point at the module's own build
directory regardless of how/where it's invoked from.

## TODO — next session, start here

1. **New top priority**: figure out why frames never reach `wl_monitor_hook`
   in monitor mode despite real traffic in range and a healthy RF path (see
   decisive-finding section above). This is now a firmware D11/MAC RX-path
   investigation, not a wrapper-address or driver-delivery one. Promising
   next steps: (a) check D11 core interrupt-enable and RX-FIFO-enable
   registers live (via the `case 603` memory-read ioctl) before/after
   `SET_MONITOR`, comparing against what stock/managed mode sets, to see if
   monitor mode is actually opening the hardware receive path; (b) check
   whether `wlc_monitor` or a similar higher-level "enable monitor" routine
   in ROM is itself being reached — add another `printf` earlier in the
   call chain (e.g. in whatever sets `wl->wlc->monitor`) to narrow down how
   far the RX-enable path actually gets; (c) compare live D11 register state
   between this chip and — if ever obtainable — a chip confirmed to
   correctly RX in monitor mode, since a config register diff would be far
   more conclusive than more disassembly.
2. Still open, lower priority now: chase *why* identical
   `patches/bcm43430a1/7_45_41_46/` source (byte-identical to this repo's
   `Debug` branch, confirmed via `git diff` against historical commit
   `b8c6999a`) produces a different flash-patch table entry ordering
   (prepended vs. appended) between this repo's build and the known-working
   reference build — candidates are compiler plugin version or toolchain
   version. This is likely relevant to the *separate*, still-unexplained
   `7_45_41_46` + known-good-CLM crash-on-load bug, not the monitor-mode RX
   silence (see above). A local x86_64 build attempt of the historical
   commit hit a 32-bit/64-bit plugin mismatch; either fix that (rebuild
   `gcc-nexmon-plugin` as 32-bit to match the pinned 2016 cross-toolchain)
   or just build on the Pi's aarch64 toolchain, which already works fine
   for everything else this session.
3. Apply the same audit-and-fix treatment (wrapper-gap check +
   byte-signature relocation) to other chips/firmware versions:
- **bcm43436b0** — already has a `rom_extraction` reference implementation
  at `patches/bcm43436b0/9_88_4_65/rom_extraction/` (a full separate
  firmware build with a `wlc_ioctl_hook` at `0x4B274` implementing memory
  read codes `0x600-0x603` — same idea as the injected `case 603` above,
  worth comparing against). Also relevant: the user's original hypothesis
  this session was that their physical chip might actually be a
  COVID-shortage `bcm43436b0` substitute rather than genuine `43430/1` —
  tested and effectively disproven (stock 43436 firmware+CLM caused a hard
  SDIO attach failure on this chip: `clmload ... failed (-52)`,
  `dongle is not responding`). So this Pi's chip is genuine `43430/1`; the
  `43436b0` audit is for the user's *other* hardware, not this device.
- **bcm43455c0** — also has an existing `rom_extraction/` reference impl to
  compare against (`patches/bcm43455c0/7_45_154/rom_extraction/`).
- **Latest firmware versions** for whichever chips are relevant (check
  `firmware-brcm80211` package upstream for current stock firmware/CLM,
  same way this session confirmed `7.45.98` is the current stock version
  for `43430/1` — extract via `dpkg-deb -x` without installing, to avoid
  clobbering the working `firmware-nexmon` package).

Methodology to reuse for each: (1) build normally, (2) grep
`gen/nexmon.pre` for missing `DUMMY` entries against actual calls in the
patch `src/*.c` files, (3) byte-signature relocate any gaps from the
nearest known-good same-chip firmware version, (4) where no same-chip
reference address exists at all (like `pkt_buf_get_skb` here), extract the
live ROM via the `case 603` ioctl technique and either disassemble by hand
or trace via a caller that *is* correctly resolved.

## SOLVED: why 7.45.98 never receives RX in monitor mode but 7.45.41.46 does

Determined entirely offline from the stock firmware images — no device needed.

`monitormode.c` hooks **ROM** address `0x81F620` with `FW_VER_ALL`:

    __attribute__((at(0x81F620, "flashpatch", CHIP_VER_BCM43430a1, FW_VER_ALL)))
    BLPatch(flash_patch_179, wl_monitor_hook);

`FW_VER_ALL` is the bug. ROM is identical silicon across versions, but whether
that ROM code still *executes* is version-specific, because each stock firmware
ships its own flash-patch table that relocates ROM routines into RAM.

Comparing stock tables (`FP_CONFIG_ORIGBASE 0x1800`, 12-byte entries; sizes from
`FP_CONFIG_ORIGEND`: 41.46 = 183 entries, 7.45.98 = 212 — the extra 29 are extra
ROM→RAM relocations), in the window `0x81e000..0x81f620`:

- 7.45.41.46: one entry at `0x81ee08` (before the hook region)
- 7.45.98:   one entry at **`0x81f410`**

`0x81f410`'s replacement decodes to an unconditional **`B.W` to RAM `0xd4e4`**,
and `0xd4e4` contains a genuine prologue (`2de9f041` = `push.w {r4-r8,lr}`).
Cypress reimplemented that ROM routine in RAM for 7.45.98. Execution therefore
leaves ROM `0x210` bytes *before* `0x81F620`, so the nexmon hook is dead code —
never reached. 41.46 has no such diversion, so ROM runs through `0x81F620` and
the hook fires. This exactly matches the observed symptom (hook printf never
appears, no crash, zero frames).

Ruled out along the way, all verified rather than assumed:

- The built flash-patch table is byte-perfect: all 212 stock entries present
  with identical payloads, plus ours prepended at index 0 (`FP_CONFIG_BASE`
  `0x617f0`, 213 entries, both BASE/END pointer pairs updated). The earlier
  "table ordering differs from the working build" theory is therefore **not**
  the cause of the RX silence — retracted.
- Our `BL` payload at `0x81F620` is well-formed and targets `0x5FA98`.
- Wrapper addresses and the driver are not implicated (see prior section: the
  reference driver + reference 41.46 firmware also captured nothing, and plain
  managed-mode scanning works fine).

**Gotcha that hid this for so long**: `firmwares/bcm43430a1/7_45_98/rom.bin` is a
*live* dump taken through the TCAM on a running chip, so it already shows the
patched `B.W` at `0x81f410`. Comparing that "ROM" against the stock patch
payload makes the entry look like a meaningless identity patch. It is not — the
true ROM instruction is simply not recoverable from a live dump. Never diff a
live ROM dump against a flash-patch payload and conclude "no-op".

**Fix direction** (implemented — see next section): stop patching ROM
`0x81F620` for this version and hook the RAM reimplementation instead, under
`FW_VER_7_45_98` rather than `FW_VER_ALL`. More generally, any `FW_VER_ALL` ROM
flashpatch in this codebase is suspect: it must be re-validated per firmware
version by checking that no stock flash-patch entry diverts control flow ahead
of the hooked address.

## The fix for 7.45.98 (commit `310d96dd`) — built and verified in-image, NOT yet hardware-tested

### Finding the correct hook point

Note the RAM routine does **not** call the ROM `wl_monitor` at `0x819510` — a
scan of the whole RAM image finds no `BL` and no literal reference to it, and a
scan of ROM finds no caller either (because `0x81F620` *was* the sole caller and
nexmon had already overwritten it). 7.45.98 ships its own **RAM copy of
`wl_monitor` at `0xa5e2`**.

The RAM routine's tail is a register-renamed mirror of the ROM one, which is
what identifies the hook point unambiguously:

    ROM 0x81f61a  ldr r0,[r5,#8] / mov r1,sp / mov r2,r6 / bl wl_monitor  ; 0x81f620
                  add sp,#56  / ldmia.w sp!,{r4-r8,pc}
    RAM 0x00d710  ldr r0,[r6,#8] / mov r1,sp / mov r2,r8 / bl 0xa5e2      ; 0x00d716
                  add sp,#64  / ldmia.w sp!,{r4-r8,pc}

(The argument registers are permuted between the two builds: ROM has
`r5`=wlc/`r6`=p, RAM has `r6`=wlc/`r8`=p. `&sts` is `sp` in both.) The RAM
function has exactly one exit, at `0xd71a`, so `0xd716` is the only call site to
patch.

Three changes:

1. `patches/bcm43430a1/7_45_98/nexmon/src/monitormode.c` — replaced the dead
   `at(0x81F620, "flashpatch", …, FW_VER_ALL)` with
   `at(0xd716, "", CHIP_VER_BCM43430a1, FW_VER_7_45_98)`. RAM address ⇒ plain
   patch region `""`, not `"flashpatch"`.
2. `patches/common/wrapper.c` — added
   `AT(CHIP_VER_BCM43430a1, FW_VER_7_45_98, 0xa5e2)` for `wl_monitor`, ahead of
   the existing `FW_VER_ALL, 0x819510` line (needed by the `MONITOR_IEEE80211`
   case, which calls `wl_monitor` directly).
3. **`struct wl_rxsts` layout differs in this version** and had to be fixed too,
   or frames would flow but carry garbage radiotap metadata. The RAM routine
   omits the 2-byte hole after `htflags`; the struct is already
   `__attribute__((packed))`, so everything from `antenna` on sits 2 bytes
   lower. Confirmed by matching five independent field stores between the two
   routines (ROM offset → RAM offset): `16→14` (antenna), `20→18` (pktlength),
   `24→22` (mactime), `28→26` (sq), `32→30` (signal), `40→38` (preamble),
   `48→46` (nfrmtype); offsets `≤13` (chanspec/datarate/mcs/htflags) unchanged.
   The unaligned `str.w [sp,#22]`/`[sp,#14]`/`[sp,#30]` stores in the RAM
   disassembly are the tell. Implemented as a `WL_RXSTS_NO_PAD` guard around
   the `uint16 PAD` in `firmwares/bcm43430a1/structs.common.h`, defined by
   `firmwares/bcm43430a1/7_45_98/structs.h` before it includes the common file,
   so other chips/versions are untouched.

### Verified in the built image (not on hardware)

- `bl_wl_monitor_ram_call` placed at `0xd716`; the 4 bytes there decode to
  `BL 0x5fa98` = `wl_monitor_hook` (`nm` on `gen/patch.elf` agrees).
- Epilogue bytes at `0xd71a` (`10b0 bde8 f081`) intact — exactly the 4-byte `BL`
  was replaced, nothing else.
- `wl_monitor` now resolves to `0xa5e2`; no `0x81F620` entry remains in the
  flash-patch table.

**Still to do**: power-cycle the device and confirm the hook actually fires. The
debug `printf` in `wl_monitor_hook` (commit `d6feca9b`) is still in place, so a
single `tcpdump` run on a channel with real traffic will show it immediately in
`dmesg` as a `brcmfmac: CONSOLE:` line (needs `modprobe brcmfmac debug=0x100000`
— see the console-readback recipe above). If frames flow but radiotap fields
look wrong, re-check the `wl_rxsts` offsets before suspecting anything else.

## CONFIRMED ON HARDWARE: monitor-mode RX works on 7.45.98

The RAM-hook fix is verified on the device. `wl_monitor_hook: called, monitor=2`
now appears repeatedly in the firmware console during a capture, and `tcpdump`
on `wlan0mon` returns frames — the first frames this port has ever captured.
The trap stack even showed `sp+44 0000d71b`, i.e. the return address into the
new `BL` at `0xd716`, confirming the call path.

### Second bug, found immediately after the hook started firing

With the hook live, the first capture trapped:

    TRAP 3(6fdc8): pc 5d0f8, lr 5f9ab, sp 6fe1c

`lr 0x5f9ab` -> return address `0x5f9aa`, which disassembles to the instruction
right after `wl_monitor_radiotap`'s *first* call:

    5f9a6:  bl 5d0b8 <pkt_buf_get_skb>
    5f9aa:  mov r5, r0

`pkt_buf_get_skb` had `FW_VER_7_45_41_46` but no `FW_VER_7_45_98` entry, so it
compiled as a local stub at `0x5d0b8` and execution ran off into `0x5d0f8` -
the same silently-missing-wrapper pattern as before, one layer deeper. TRAP 3
is a prefetch abort, i.e. executing an invalid address, which is the signature
of jumping into a dummy stub.

Fixed: `pkt_buf_get_skb` -> `0x6c30`, `pkt_buf_free_skb` -> `0x6c74`.

### Audit method that finds all of these at once (do this instead of one crash at a time)

    grep -iE "DUMMY" gen/nexmon.pre | awk '{print $NF}' | sort -u > /tmp/resolved
    arm-none-eabi-nm gen/patch.elf | awk '$2=="W"{print $3, $1}' | sort |
      while read -r sym addr; do
        grep -qx "$sym" /tmp/resolved || echo "$sym -> stub at 0x$addr"
      done

Ignore the `b_flash_patch_*` hits - those are stock ROM flashpatch definitions,
not wrappers, and their addresses are genuine. On this port the audit reported
exactly three real gaps: `pkt_buf_get_skb`, `pkt_buf_free_skb`, `wl_send`.

### Important limitation of the byte-signature relocation method

Byte-signature matching **fails on functions containing relative branches**,
because `BL`/`B.W` encode a PC-relative offset that changes with the function's
address. `pkt_buf_get_skb` is identical between 41.46 and 7.45.98
instruction-for-instruction, including the same ROM callee `0x808744`, yet has
*zero* byte matches - the two `BL`s encode as `f002 d9f9` vs `f001 dd85`.

When a signature search returns no hits, do not conclude the function is gone.
Instead: locate a neighbouring function that *does* match, apply the same delta
as a candidate, and verify by disassembling both and comparing instructions.
`pkt_buf_free_skb` matched at 32 bytes (`0x638C` -> `0x6c74`, delta `0x8E8`);
applying that delta to `pkt_buf_get_skb` gave `0x6c30`, which disassembled
identically. The delta is **not** constant across the whole image - the same
delta applied to `wl_send` (`0x7fe4` -> `0x88cc`) lands mid-function.

### FIXED: radiotap payload and signal

Both remaining defects are resolved; captures now decode correctly (right
SSIDs, channel, rates, and realistic RSSI matching `iw scan`).

**Garbage frame body: `memcpy` was resolving to an address that is not memcpy.**
`wrapper.c` had, for this chip, `AT(CHIP_VER_BCM43430a1, FW_VER_ALL, 0x880B80)`
plus `AT(CHIP_VER_BCM43430a1, FW_VER_7_45_41_46, 0x2390)`. 7.45.98 matched only
the `FW_VER_ALL` line, and `0x880B80` is **not** memcpy on this chip - it
disassembles to a 5-instruction stub that loads one word from a literal pool,
stores it to `[sp,#4]` and returns. So every `memcpy()` in patch code silently
did nothing, and `wl_monitor_radiotap`'s frame body copy left uninitialised skb
memory - which is why frames arrived with all-zero MACs and nonsense types while
the radiotap fields written directly in C (TSF, channel) were correct.
Fixed: `memcpy` for `FW_VER_7_45_98` -> `0x24ec` (48-byte signature, unique hit;
corroborated because the 7.45.98 RAM RX routine itself calls `0x24ec`).
Note `malloc` carries the *same* bogus `0x880B80` for this chip and is still
unfixed - it is reached only from `helper.c`'s delayed-task path, not from RX.
A `FW_VER_ALL` entry being present is not evidence that it is correct.

**Wrong RSSI: a stale object file, not a bad address.** The `WL_RXSTS_NO_PAD`
layout fix was correct all along but had never actually been compiled in. This
Makefile does **not track header dependencies**, so editing
`firmwares/<chip>/structs.common.h` or `<fwver>/structs.h` and re-running `make`
relinks without recompiling - the build prints `LINKING OBJECTS` with no
`COMPILING` lines, which is the tell. `wl_monitor_radiotap` was still reading
`sts->signal` from offset 32 (the padded ROM layout) instead of 30.
**After any header change in this tree, run `make clean` first.** Verify the
result in the ELF rather than trusting the build:

    arm-none-eabi-objdump -d --start-address=0x5f990 --stop-address=0x5fa10 \
        gen/patch.elf | grep -E "r9, #"
    # r9 holds sts; expect [r9, #8] chanspec and [r9, #30] signal on 7.45.98

### Still open

1. ~~**Radiotap payload is wrong.**~~ FIXED - see above. Frames flow and no longer trap, but the
   decoded contents are garbage: all-zero BSSID/DA/SA, `-1dBm signal`, bogus
   frame types. The `WL_RXSTS_NO_PAD` guess (dropping the 2-byte hole after
   `htflags`) is evidently not the actual 7.45.98 layout. Re-derive it properly
   from the RAM routine's stores at `0xd4e4..0xd71a` rather than by inference,
   and cross-check against what `wl_monitor_radiotap` reads.
2. **`wl_send` is still an unresolved stub** (`0x5d0c0`). It was restructured in
   7.45.98 - neither its prologue nor its body matches anything in the image, and
   none of the 25 `stmdb sp!,{r0,r1,r2,r4-fp,lr}` prologue candidates disassemble
   like it. It is only reached from `injection.c`'s `wl_send_hook`, so it does not
   affect RX, but frame injection through that hook will trap until it is found.


## Session end state (2026-08-26): RX + injection working, channel hopping is the open issue

Working and verified on hardware:
- Monitor-mode RX decodes correctly (right SSIDs, channel, rates, realistic RSSI).
- pwnagotchi recon works end to end - real APs, real clients, vendor IDs.
- Injection no longer jams. After the `wl_send` fix (`6d691a9a`) bettercap logged
  **zero** "could not inject WiFi packet / Resource temporarily unavailable"
  errors, where before it produced them continuously.
- The wrapper audit is clean: no unresolved wrappers remain on this port.

**Open issue - `brcmf_cfg80211_nexmon_set_channel` times out (`-110`).**
Symptoms: repeated `Set Channel failed: chspec=NNNN, -110 (attempt 1..3/3)` in
dmesg, and bettercap `error while hopping to channel 6: iw: command failed:
Connection timed out (-110)`. Seen across many chanspecs (`0x1001` ch1,
`0x1002` ch2, `0x1006` ch6, `0x1009` ch9), so it is not channel-specific. When
it is failing, `tcpdump` on `wlan0mon` also returns 0 packets - the radio is
parked/unresponsive rather than hopping.

Important: this **predates** the `wl_send` fix (first observed while the TX
queue was still jammed) and persists after it, so it is a separate defect, not
a regression from it. Not yet established whether it is:
  (a) firmware-side - the nexmon `set_channel` ioctl path failing/blocking on
      7.45.98, possibly another wrong or missing address on that path;
  (b) driver-side - `brcmf_cfg80211_nexmon_set_channel` in
      `patches/driver/brcmfmac_6.18.y-nexmon/cfg80211.c`, its retry loop and
      timeout; or
  (c) a consequence of sustained RX load through the new hook.

**Next session, start here.** The isolation test was set up but not run: stop
`pwnagotchi` and `bettercap` so nothing else touches the radio, then check
whether the firmware answers simple ioctls at all (`nexutil -Iwlan0mon -m`) and
whether a manual `iw dev wlan0mon set channel N` succeeds. That distinguishes
"firmware wedged/unresponsive generally" from "set_channel specifically broken"
from "only breaks under bettercap's hop rate". Compare against 41.46, which is
known to hop correctly on this hardware - if 41.46 hops fine with the same
driver, the fault is firmware-side and the `set_channel` ioctl path on 7.45.98
should get the same wrapper-address scrutiny that fixed RX.


## SOLVED: the -110 "firmware stops answering" wedge was nexmon's monitor delivery path

Symptom: after ~90s of monitor-mode RX the chip kept receiving but stopped
answering every command - `chanspec`, `escan`, `SET_PROMISC`, `allmulti`,
pm-timeout all returning `-110`, with **no TRAP and no SDIO wedge**. `modprobe -r`
would hang; only a driver reload (or power cycle) recovered it. It first looked
like a `set_channel` bug because that is the command bettercap issues most often.

### How it was isolated (each step ruled out one thing)

1. **Not pwnagotchi orchestration.** Stopped and disabled pwnagotchi, bettercap,
   pwngrid and the brcmfmac watchdog, then drove the radio manually: continuous
   `tcpdump` plus a channel hop every 2s. It still wedged - first hop failure at
   iteration 46, i.e. ~92s in, 95 x `-110`. This mattered because the
   pwnagotchi launcher's own comments warn that repeated bettercap restarts
   force `reload_brcm()` driver reloads that can wedge the SDIO backplane, and
   the logs did show the firmware reloading three times in quick succession.
2. **Not channel hopping.** Same load on a *fixed* channel, no hopping at all:
   93 x `-110`, chip dead. So the trigger is RX volume through the hook, not the
   `chanspec` command path.
3. **It is nexmon's delivery path.** A diagnostic build routed
   `MONITOR_RADIOTAP` frames through the vendor's own RAM `wl_monitor`
   (`0xa5e2`) instead of `wl_monitor_radiotap`. Identical 5.5-minute load:
   **zero** `-110` and a still-responsive chip.

Note on measuring this: the site is rural with 3 APs (`Jachtkamp18`,
`_IoT`, `_Gast` plus a hidden BSSID on the same radio), so ~30-50 frames/s.
pcap *size* is therefore useless as a health signal - a small capture is normal
here, not evidence the firmware died. Only the `-110` count and whether
`set_channel` still answers are reliable.

### Root cause

`wl_monitor_radiotap` delivered the new skb with

    wl->dev->chained->funcs->xmit(wl->dev, wl->dev->chained, p_new);

On 7.45.98 that does not return the buffer to the pool. Every received frame
permanently consumed one packet buffer, so at ~30-50 frames/s the pool was gone
within seconds and the firmware could no longer allocate buffers to answer host
commands - hence `-110` everywhere, with no crash to point at.

Both vendor monitor routines instead end in a send-up call, RAM `0xa5e2`
`b.w 0xa46c` and ROM `0x819510` `b.w 0x880f10`, each invoked as
`(wl, NULL, p_new, 1)` - the existing `wl_sendup_newdrv` wrapper signature.
That routine does return the buffer.

### The fix, and the subtlety that made it two steps

Switching to `wl_sendup_newdrv(wl, 0, p_new, 1)` stopped the wedge but broke
capture entirely - 0 packets on `wlan0mon`. Passing `wlif = NULL` sends frames
up the normal netif, not the monitor vif. (This also explains the diagnostic
build's empty pcap, which was initially misattributed to missing radiotap
headers.) The working form passes the monitor interface:

    if (wl->wlc->wlcif_list->next)
        wl_sendup_newdrv(wl, wl->wlc->wlcif_list->wlif, p_new, 1);
    else
        wl_sendup_newdrv(wl, 0, p_new, 1);

plus `AT(CHIP_VER_BCM43430a1, FW_VER_7_45_98, 0xa46c)` for `wl_sendup_newdrv`.

Verified over 5.5 minutes of fixed-channel load: 0 x `-110`, `set_channel` still
answering afterwards, and captures decoding correctly with SSIDs and realistic
RSSI.

**Carry-over for other ports**: the `chained->funcs->xmit` delivery in
`wl_monitor_radiotap` is upstream nexmon code that works on 7.45.41.46, so this
is version-specific. When porting monitor mode to a new firmware, check what the
vendor's own `wl_monitor` tail-calls and prefer that routine - it is the one
that owns the buffer lifecycle for that build.

## Open: channel hopping wedges 7.45.98 (a second, independent trigger)

The `wl_sendup_newdrv` delivery fix above is real and validated, but it only
covers **fixed-channel** load. Channel hopping is a separate trigger and is
still unresolved.

Measured with everything else stopped (pwnagotchi, bettercap, pwngrid and the
brcmfmac watchdog all disabled), driving the radio by hand: continuous
`tcpdump` plus `iw dev wlan0mon set channel N` every 2s, 150 iterations.

| build | first hop failure | -110 | chip after |
|---|---|---|---|
| 7.45.98, before delivery fix | i=46 (~92s) | 95 | dead |
| 7.45.98, after delivery fix | i=52 (~104s) | 102 | dead |
| 7.45.98, monitor hook drops every frame (allocates nothing) | i=33 (~66s) | 135 | dead |
| 7.45.98, fixed channel (no hopping), after fix | n/a | **0** | **alive** |
| **7.45.41.46 reference, same test** | **none - all 150 hops OK** | **0** | **alive** |

### What this rules out

- **Not pwnagotchi/bettercap orchestration.** Reproduces with all services
  stopped, so it is not `reload_brcm()` churn.
- **Not our monitor RX path.** The drop-all build allocates nothing and still
  wedges - in fact slightly *sooner*. Whatever this is, `wl_monitor_radiotap`
  is not involved.
- **Not a bad patch site.** All three `GenericPatch4` targets hold correct stock
  values: ioctl hook `0x4a8bc` -> `0x81a2d5` (= `wlc_ioctl|thumb`, same as
  41.46's `0x4305c`), `wl_send` hook `0x40fe0` -> `0x9f41`, autostart `0x2c40`
  identical bytes to 41.46's `0x2a94`.
- **Not a bypassed `wlc_ioctl`.** Suspected `wlc_ioctl` (ROM `0x81a2d4`) might
  sit inside the function the stock flashpatch at `0x81a284` redirects to RAM
  (`b.w 0xeda8`) - the same trap as the monitor hook. Disproved: `0x81a2d2` is
  `pop {r4,r5,r6,pc}` ending that function, and `0x81a2d4` begins a clean
  prologue (`stmdb sp!,{r4-fp,lr}` / `sub sp,#364`) of its own.
- **It is 7.45.98-specific.** The 41.46 reference completes all 150 hops clean
  on the same hardware, driver and test.

### Caveat that could not be controlled

41.46 has CLM built in and **traps on an external blob** (`pc 0x1f106`, the
clmload crash), while 7.45.98 has no built-in CLM and **requires** the external
blob. So the two configurations necessarily differ by CLM, and that difference
cannot be factored out by testing 7.45.98 without a blob: with no CLM its
channels are not registered, and `iw set channel` is rejected by the *kernel*
(`-22`, "(extension) channel is disabled") before ever reaching the firmware -
62/62 failures were `-22` with **zero** `-110`. That run proves nothing about
the wedge and must not be read as a pass.

Note the CLM in use is **not** a mismatched or nexmon-specific blob, though
`dpkg -S` makes it look like one: it reports the file as owned by
`firmware-nexmon` (and `firmware-brcm80211` is held, not installed). That is
only because firmware-nexmon reinstalls the same bytes. Every 43430 CLM blob
available is byte-identical, md5 `9ff46519b8b8c2cab323322c9d983873`:

- `stage2/rootfs/.../brcm/brcmfmac43430-sdio.clm_blob`   (stock RPi OS, pre-pwnagotchi)
- `stage2/rootfs/.../cypress/cyfmac43430-sdio.clm_blob`  (stock RPi OS)
- `stage3/rootfs/.../cypress/cyfmac43430-sdio.clm_blob`  (after firmware-nexmon)
- upstream linux-firmware on the dev machine
- the blob in use on the device

So there is no 7.45.98-matched CLM to switch to, and "wrong CLM version" is
ruled out as a cause of the hopping wedge.

### RESOLVED as not-our-bug: stock unpatched 7.45.98 wedges identically

Ran the same test against the **stock, completely unpatched** 7.45.98 firmware
(`firmwares/bcm43430a1/7_45_98/brcmfmac43430-sdio.bin`, md5
`a89ad21eae027367eba3d1dcff52c0bd` - confirmed unpatched by `0` `nexmon_ver`
lines in dmesg). Stock firmware accepts the monitor vif and channel sets fine,
so the comparison is like-for-like.

| build | hop failures | first fail | -110 |
|---|---|---|---|
| nexmon, both GenericPatch4 hooks disabled | 33 | i=49 | 101 |
| **stock, zero nexmon patches** | **33** | **i=49** | **101** |

Identical to the iteration. Nexmon does not cause this. It is inherent to
7.45.98 plus this driver under repeated channel changes in monitor mode.

Note also how little the numbers moved across *every* variant tested
(first failure at i=46, 49, 49, 52, 33) regardless of which nexmon code was
active - that stability was itself the clue that the cause lay outside the
patches.

**Consequences**

- The `bcm43430a1/7_45_98` port itself is good: monitor RX decodes correctly,
  injection works, and fixed-channel operation is stable indefinitely.
- 7.45.41.46 does **not** have this problem (150/150 hops clean), which is why
  the existing pwnagotchi setup has never hit it.
- Any fix belongs driver-side or in hop policy, not in firmware patches.
  `brcmf_cfg80211_nexmon_set_channel` already retries 3x with a 20ms sleep;
  candidates are a longer backoff, rate-limiting chanspec changes, or bettercap
  hopping more slowly. **Tested: it is NOT hop-rate dependent - slowing down
  does not help.**

**Not thermal, and not a one-shot timer.** Tested directly on stock 7.45.98:
three channel changes over ~12s, then hopping *stopped* and the chip left in
monitor mode for 5 minutes while logging SoC temperature. Result: **0 x -110**,
`set_channel` still working afterwards, and temperature **flat at 44.0-45.1 C**
throughout (boot 46.2 C, post-wedge 45.1 C - it never climbs). `throttled=0x0`.
So the wedge needs *sustained* channel changing; a short burst is harmless, and
the chip is nowhere near a thermal limit. Note the BCM43430 has no exposed
temperature sensor - `thermal_zone0` is the SoC - but with the SoC flat at 44 C
and no throttling there is no plausible thermal mechanism here.

**Requires sustained hopping; the count/rate relationship is not simple.** Same
stock firmware, same test, only the interval changed:

| interval | first failure | elapsed | hops before failure |
|---|---|---|---|
| 2s | i=49 | ~98s | 49 |
| 5s | i=15 | ~75s | 15 |

The hop count to failure differs 3x (49 vs 15) while the wall-clock time is
similar, and 5s actually failed marginally *sooner* - so rate-limiting
bettercap's hopper is **not** a workaround. But this is not pure elapsed time
either, since 3 hops followed by 5 idle minutes is completely clean. The
requirement is *ongoing* channel changes; the threshold sits somewhere between
3 and ~15 continuous hops, and does not reduce to a simple count or rate.
With n=1 per condition the 75s vs 98s gap may well be noise; the robust facts
are: 0 hops = stable indefinitely, 3 hops = stable, continuous hopping = dead
within ~100s.

Also worth noting for contrast: fixed-channel monitor RX runs indefinitely
(5.5min clean, 0 errors) on the patched build - so some channel changing is
required to trigger it, but once triggered the clock seems to run on time rather
than on change count.

### Where to pick this up

The remaining difference between the two builds is the firmware itself plus the
CLM requirement. Worth trying next, cheapest first:
1. Is it hop-*rate* dependent? Repeat at 5s and 10s intervals. If slow hopping
   survives, that is both a diagnostic clue and a practical mitigation
   (bettercap's hop rate is configurable).
2. Does stock (unpatched) 7.45.98 survive the same hopping? That separates "our
   port" from "this firmware version" entirely. Monitor mode is needed to hold
   the vif, so this may not be directly testable - but if it is, it is decisive.
3. Instrument the chanspec path: a `printf` in `wlc_ioctl_hook` for the chanspec
   iovar, to see whether the firmware is still executing commands when the -110s
   begin, or has already stopped.


## ROOT CAUSE FOUND: channel hopping exhausts the firmware's packet-buffer pool

The `-110`-on-every-command wedge is **packet-buffer exhaustion**, and channel
changes are what consume the buffers. Measured directly, not inferred.

### How it was measured

Added a probe ioctl (`case 606` in `ioctl.c`) that allocates `pkt_buf_get_skb()`
until it fails, reports the count, then frees every one again - bounded at 64 so
it cannot itself exhaust the pool:

    case 606:
        void *bufs[64]; int n = 0, got;
        for (n = 0; n < 64; n++) { bufs[n] = pkt_buf_get_skb(wlc->osh, 128);
                                   if (bufs[n] == 0) break; }
        got = n;
        while (n-- > 0) pkt_buf_free_skb(wlc->osh, bufs[n], 0);
        ((unsigned int *) arg)[0] = got;

Then hopped channels every 2s while sampling the probe every 5s, logging both
with timestamps to the same file.

### Result

    t=1384  freebufs=64      <- healthy, saturating the probe's 64 cap
    t=1389  freebufs=64
    t=1394  freebufs=64
    t=1401  freebufs=0       <- pool exhausted, ~18s / ~9 hops in
    t=1409  freebufs=0
    HOPFAIL i=9   t=1411     <- first command failure, 10s AFTER exhaustion
    (every subsequent probe 0, every subsequent hop fails)

Buffer exhaustion **precedes** the command failures. Once the pool is empty the
firmware cannot allocate to answer host commands, so every ioctl times out at
-110 - with no TRAP and no SDIO wedge, because nothing has actually crashed.
This is the same signature as the `wl_monitor_radiotap` delivery leak fixed
earlier in `e0af3c3c`; that one leaked per received frame, this one leaks per
channel change.

Note the drop is abrupt (64 -> 0 within one 7s sampling gap, ~3 hops), not a
slow decay, so each channel change appears to consume many buffers rather than
one.

### Why this explains everything previously observed

- **Fixed channel is stable indefinitely** - no channel changes, nothing drains.
- **3 hops then idle is clean** - not enough changes to empty the pool, and
  nothing continues to consume once hopping stops.
- **Not rate dependent** - 5s intervals died in fewer hops but similar
  wall-clock time; the pool empties after a certain number of changes either way.
- **Not thermal** - temperature flat at 44-45 C throughout.
- **Stock unpatched 7.45.98 fails identically** - the leak is in the firmware's
  own channel-change path, not in any nexmon patch.
- **7.45.41.46 is unaffected** - 150/150 hops clean, so that firmware either
  does not leak or has enough headroom.

### Where to go next

The leak is inside 7.45.98's chanspec handling. Candidates worth disassembling:
the chanspec iovar handler and whatever it calls to re-tune the PHY, looking for
an allocation whose free path is missing or conditional. The probe ioctl makes
this cheap to iterate on - any candidate fix can be verified in ~30 seconds by
watching whether `freebufs` still collapses.

Also worth checking: whether the buffers are recoverable once hopping stops. The
log shows them still at 0 long after, which suggests they are genuinely leaked
rather than merely in flight.

## CORRECTION: the "packet-buffer exhaustion" root cause above is an artefact, not a finding

The section immediately above (commit `aa539e67`) concluded that channel hopping
drains the firmware's packet-buffer pool, on the strength of probe ioctl 606
reporting `freebufs=0` shortly before the first hop failure. **That conclusion
does not hold.** The measurement could not tell the two cases apart:

- the firmware answered and the count really was zero, or
- the firmware never answered, and `nexutil` printed back the buffer it had
  zeroed before the call.

Both print `00 00 00 00`. Every "pool exhausted" reading was taken in exactly
the window where the chip had stopped answering, so it is the second case.

### How this was proved

`nexutil -g600 -l<n> -v <addr> -i` turns the existing `case 600` into a general
memory dumper: the address goes into the buffer, the firmware `memcpy`s over it.
Dumping `0x41a4c` after the wedge returned

    0x000000: 4c 1a 04 00 00 00 00 00 00 00 00 00 ...   (rest all zero)

- word 0 is `0x00041a4c`, the address that was written *into* the buffer as
  input, and nothing else was touched. The firmware never ran the handler; the
host printed its own input back. The same shape appeared for every other
"collapsed" reading.

**Rule for all future probes on this chip: an ioctl that can time out must write
a magic word before it measures anything, and any sample without that magic must
be discarded, not interpreted.** Left in the tree as `case 612`, which writes
`0x4E455831` ("NEX1") into `out[0]` first and gathers everything else in one
reply. It is built and deployed but its first run has not been read yet.

## What the hopping wedge actually looks like, measured with valid samples only

Sampling the heap and the packet counters once per hop, and keeping only replies
that actually came back from the firmware:

    baseline   heapfree=74404  blocks=9   osh0=0
    hop=1      heapfree=74360  blocks=10  osh0=0
    hop=2..9   heapfree=74352  blocks=11  osh0=0
    hop=10     heapfree=74292  blocks=11  osh0=0
    hop=11     heapfree=74292  blocks=11  osh0=0
    hop=12     *** no reply *** -> wedged from here on

Across eleven channel changes the heap moved by **112 bytes** and the number of
packet buffers the firmware holds stayed at **zero**. There is no packet leak and
no heap leak. The firmware simply stops answering, from one hop to the next, with
its memory in a perfectly healthy state.

So the wedge is not resource exhaustion at all. Something in the channel-change
path hangs or blocks the wl thread. Consistent with this: after the wedge, our
own `case 606`/`case 612` handlers do not run either, so it is not "wl is busy
while the ioctl path still works" - the whole dongle-side command path is dead.

### The allocator chain, fully mapped (useful regardless)

    pkt_buf_get_skb   RAM 0x6c30
      -> ROM 0x808744        thin wrapper, increments osh[0] on success
        -> ROM 0x880ba0      indirect thunk: jumps through RAM pointer table
                             entry at 0x488  (41.46: same slot, 0x2b44)
          -> RAM 0x2cf0      lb_alloc: rejects requests over 0x838 bytes,
                             then calls malloc; bumps 0x41af0 on failure
            -> RAM 0x25e4    malloc, best-fit over a free list;
                             bumps 0x728 on failure

    free list head   0x41a4c   (constant returned by the accessor at 0x25b4)
    node layout      [0] = size, [4] = next; the head carries no block

`osh[0]` was verified to be the live outstanding-packet count with `case 609`:
it reads 0, then 16 while sixteen buffers are held, then 0 again. At idle in
monitor mode it sits at 0, i.e. nothing is retained per received frame.

Free blocks legitimately appear at very low addresses (e.g. a 28-byte block at
`0x214c`). That is not corruption: the boot log shows `reclaim section 0:
Returned 39344 bytes to the heap` and `reclaim section 1: Returned 80916 bytes`,
so init-only code below `0x2200` is handed to the allocator once boot completes.

## Iterating on this without power cycles

`rmmod brcmfmac; modprobe brcmfmac` fully recovers a wedged chip - the firmware
is re-downloaded. No physical power cycle is needed to run another test.

Two things that waste a lot of time if forgotten:

- **When the chip is wedged, do not touch the netdevs first.** `ip link set
  wlan0mon down` and `iw dev wlan0mon del` block on firmware ioctls that never
  answer, and `rmmod` then fails with `EBUSY`. Go straight to `rmmod`.
- **Create and open the monitor vif before setting monitor mode.** Calling
  `nexutil -m2` first makes the driver refuse the interface with
  `brcmf_net_mon_open: Monitor mode is already enabled` -> `RTNETLINK answers:
  File exists`, and the vif stays DOWN. Correct order: `ip link set wlan0 up`,
  `iw phy <phy> interface add wlan0mon type monitor`, `ip link set wlan0mon up`,
  `ip link set wlan0 down`, then `nexutil -Iwlan0mon -m2`. Note the phy index
  increments on every driver reload, so read it from
  `/sys/class/net/wlan0/phy80211/name` rather than hardcoding `phy0`.

Helper scripts left on the device: `/tmp/reload.sh` (robust reload + monitor
setup) and `/tmp/hoptest7.sh` (reload, then hop while sampling `case 612`).

## Where to go next on the hopping wedge

The question is no longer "what leaks" but "what blocks". Worth trying, roughly
in order of cost:

1. Read the first `case 612` run (it is deployed and was launched but never
   read) to confirm the picture above with the magic-word check in place.
2. Establish whether the wedge is the channel change itself or the RX that
   follows it: repeat the hop loop with monitor mode 0, so the firmware delivers
   nothing, and with the antenna quiet. If hopping alone never wedges, the
   trigger is the interaction with received frames, not the retune.
3. Find where the wl thread is stuck. The ARM here is a Cortex-M3 with the
   vector table at `0x0` (`0x30` = DebugMonitor, currently the default handler
   `0x21f1`), so a DWT data watchpoint plus a DebugMonitor handler installed
   over `0x30` would name the faulting instruction directly. Note nexmon's
   `debug.h` only defines register constants - there is no implemented API.
4. `msglevel` is a dead end: `WLC_SET_MSGLEVEL` reads back `0x00000000`
   whatever is written, and channel changes produce no console output, so the
   verbose logging is compiled out of this release firmware.

Also note: the *first* hop failure is not necessarily the first symptom. Because
failed ioctls used to be read as zeros, earlier claims about ordering ("the pool
empties ~10s before the first hop failure") were measuring the same event twice.

## The wedge needs RX, but not delivery, and not concurrency

`case 612` now has a first run that was actually read, and its baseline matches
the earlier table exactly (`heapfree=74404 blocks=9 biggest=74332 osh0=0
bufs=64 mfail=0 lbfail=0`, magic `0x4E455831` present). Everything below keeps
only samples that carried the magic.

Hopping was then repeated at three monitor settings. `MONITOR_DROP_FRM` (mode 4)
is the interesting one: the RX pipeline runs all the way to the `wl_monitor`
call site and our hook then discards the frame, so reception happens but nothing
is allocated and nothing is delivered to the host.

| monitor mode | promiscuous RX | frame delivered to host | first failure (per run) |
|---|---|---|---|
| 2 (radiotap) | yes | yes | 6, 6, 9 |
| 4 (drop at hook) | yes | **no** | 9, 6 |
| 0 (off) | no | no | **none, 40/40 clean** |

Two controls make this trustworthy:

- **Mode 4 really does keep RX on.** Its heap trajectory tracks mode 2's
  exactly (blocks 9 -> 10 -> 11 -> 12, `heapfree` 74404 -> 74352 -> 74284),
  while mode 0 sits frozen at 74404/9 for all 40 hops. If mode 4 had silently
  disabled reception it would look like mode 0, and it does not.
- **Mode 0 really does retune.** Read the firmware's own chanspec back after
  each set and it follows every one - `0x1001, 0x1006, 0x100b, 0x1003` - the
  same at mode 0 as at mode 2. The clean run is not an artefact of the firmware
  quietly ignoring the channel change.

### What this rules out

- **The retune alone is not sufficient.** 40 verified channel changes with RX
  off are harmless.
- **Delivery to the host is not the trigger.** Dropping the frame the moment
  the hook runs changes nothing (9, 6 vs 6, 6, 9). That exonerates
  `wl_sendup_newdrv`, the SDIO path, and all of `wl_monitor_radiotap` - which
  is exactly what has to be true, given stock unpatched 7.45.98 wedges
  identically (`2eacc0fa`).
- **The two do not need to overlap.** Muting RX across the retune - `-m0`,
  settle, retune, settle, `-m2` - still wedges: at hop 5 with a 50 ms settle,
  at hop 8 with a full 1000 ms on each side. A whole second of silence on both
  sides of the channel change does not move the failure point out of the normal
  6-9 range, so this is not a race between reception and retuning, and
  quiescing RX around the hop is **not** a viable workaround. (Worth stating
  plainly because it was the obvious fix to reach for: it does not work.)

So the wedge requires channel changes *and* frame reception, in any temporal
order, with the frame discarded before it ever leaves the chip. The trigger is
therefore somewhere in the RX processing *upstream* of the `wl_monitor` call
site, in code shared by stock and nexmon builds.

Note this also means no monitor-path firmware patch can fix it, and the
pwnagotchi use case only ever runs at mode 2 - modes 0 and 4 are diagnostic
instruments, not candidate configurations.

### The state at the moment of death

Unchanged from before, and worth restating now that it is measured with
validated samples: the last good sample before the wedge is completely healthy
(`heapfree=74292 blocks=11 osh0=0 bufs=64 mfail=0 lbfail=0`), no packet buffers
are held, both allocator failure counters are zero, and the chip dies from one
hop to the next. In runs A and B the channel set at hop 6 itself returned *ok*
and the very next ioctl got no reply, so the last thing the firmware
successfully does is a retune.

`dmesg` across a 45-second wedge shows **no TRAP and no console output at all** -
the last console line is `000001.048 wl0: wl_open` from boot, then nothing until
the reload banner. The firmware console ring is read by the host over SDIO as a
plain memory read, without the firmware servicing anything, so silence there is
suggestive but not yet conclusive: this firmware prints almost nothing during
normal operation, so it may simply have had nothing to say.

### Where to go next

The fork in the road is **whether the CPU is still executing at all**, because
it decides which instruments are even usable:

- If the CPU is alive and only the wl thread is stuck, a DWT watchpoint plus a
  DebugMonitor handler over vector `0x30` can name the blocking instruction.
- If the CPU is fully stalled - e.g. a backplane register access during the
  retune that never completes - nothing firmware-side will ever run again, and
  the only way in is host-side SDIO memory reads.

Cheapest decisive test: add a periodic heartbeat `printf` driven from a timer
or ISR context, wedge the chip, and watch `dmesg` for console lines. Because the
host reads the console ring by direct SDIO memory access rather than by asking
the firmware, a heartbeat that keeps ticking after the ioctl path dies proves
the CPU is alive; one that stops proves a hard stall. Either answer redirects
the search, and it costs one firmware rebuild.

Note the `debug.h` in `patches/include` is **not** usable for this as-is: it
describes the ARMv7-A CoreSight debug registers (`DBGBASE 0x18007000`,
`DBGDSCR`, `DBGBCR`/`DBGWCR`), not the Cortex-M3 DWT/FPB block this chip has.
For the M3 the relevant registers are `DWT_CTRL 0xE0001000`, `DWT_COMP0
0xE0001020` (comparators every 0x10), `DWT_FUNCTION0 0xE0001028`, `DEMCR
0xE000EDFC` (`MON_EN` = bit 16) and `DFSR 0xE000ED30`; vector `0x30` is entry 12,
DebugMonitor, consistent with the default handler seen there.

### WARNING: `case 612` is not a passive probe

`case 612` allocates **64 packet buffers and frees them on every call** - that
is what the `bufs=` field measures. It perturbs precisely the subsystem under
investigation, and it does so once per sample.

This surfaced when the chip wedged on a **fixed channel with no hopping at
all**, after 140 seconds of nothing but a `case 612` sample every 10 seconds:

    t=10s .. t=130s  ALIVE  heapfree=74300 blocks=10 osh0=0 bufs=64
    t=140s           DEAD

That contradicts the long-standing "fixed channel is stable indefinitely"
baseline, and the difference from those earlier runs is the probe.

What this does and does not undermine:

- **The mode 0/4/2 comparison stands.** The probe was identical in all three
  arms, so the difference between them is still attributable to RX. Mode 0 took
  the same 40 samples and never wedged.
- **Absolute hop counts do not stand.** "Wedges at hop 6" is partly the probe's
  doing; the unprobed numbers are much larger (stock wedged at i=49 with no
  nexmon probe available at all, `2eacc0fa`).
- **Any future fixed-channel stability claim must say whether it was probed.**

The fix is a passive variant that drops the allocation loop - everything else
in the sample (heap walk, `osh[0]`, the two failure counters) is a read-only
observation. Until that exists, treat `bufs=` as the cost of the measurement
rather than as free information.

Related: `case 600`/`case 603` appear broken when the chip is wedged, returning
the caller's own input. That is the same no-reply artefact, not a fault in the
handlers - both work correctly on a healthy chip, and `case 600` is what
produced the register reads below.

### The ARM core, measured rather than assumed

Read live via `case 600` on a healthy chip:

    CPUID   0xE000ED00 = 0x412fc230   Cortex-M3 r2p0
    ICSR    0xE000ED04 = 0x00400810   VECTACTIVE=0x10, i.e. sampled inside an ISR
    VTOR    0xE000ED08 = 0x00000000   vector table at 0
    AIRCR   0xE000ED0C = 0xfa050000   VECTKEY readback, decode confirmed
    SysTick 0xE000E010 = 0x00000004   ENABLE=0, RELOAD=0 - unused by the firmware
    DWT_CTRL 0xE0001000 = 0x40000000  NUMCOMP=4, four comparators, unused

So SysTick is free to take over, and there are four hardware watchpoints
available. Both are usable for instrumentation.

One unresolved oddity: with `VTOR=0` the table is at address 0, yet reading
`0x0..0x40` returns all zeros - while `ICSR` proves interrupts are actively
being dispatched. Note `case 600` reads a NULL source there
(`memcpy(arg, *(char**)arg, len)` with `arg[0]=0`), which a ROM `memcpy` may
short-circuit, so the zeros may be an artefact of the probe rather than the
memory. Re-read from a **nonzero** offset into the table before concluding
anything, and before writing to any vector.

## SETTLED: the CPU is alive during the wedge - the wl thread is what stops

The fork in the road above is resolved, and it is not the frightening branch.

A periodic heartbeat was added to the firmware (`case 614` arms it,
`nex_heartbeat` in `ioctl.c`) that `printf`s a sequence number and the count of
frames seen by the monitor hook. It reports through the **console ring**, not
through an ioctl reply, which is the entire point: the host reads that ring
over SDIO as a plain memory read, without the firmware servicing anything, so
it keeps working exactly when the command path does not.

Result, hopping at mode 2 until the wedge and then watching for 60 seconds
with no further hops:

    hop=9  ch=10  ok  NOREPLY            <- ioctl path dead from here
    ...
    000075.040 NEXHB 71 rx=39
    000076.040 NEXHB 72 rx=39
    ...
    000086.040 NEXHB 82 rx=39
    heartbeat lines: 23 at wedge -> 83 after 60s (delta 60)
    ioctl still dead? NOREPLY

**Exactly 60 heartbeats in 60 seconds**, the firmware's own timestamps
advancing a clean 1.000s per tick, while every ioctl timed out throughout.

| | state during the wedge |
|---|---|
| CPU core | **alive** - timer fires on schedule, `printf` works |
| RX | **stopped** - `rx=` frozen at 39 from the moment of the wedge |
| command path | **dead** - every ioctl times out |

### What this rules out, and what it opens up

- **No hard stall, no hung bus access.** The earlier worry - that a backplane
  register access during the retune never completes and freezes the core - is
  wrong. The core executes normally in RTE timer context for as long as you
  care to watch.
- **No trap, because nothing faulted.** Consistent with the absence of any
  TRAP message all along.
- **It is the wl thread specifically.** That thread services both RX
  processing and ioctls, and both stop together while an unrelated RTE timer
  keeps running. Whatever happens, happens to that thread - blocked on
  something that never completes, or spinning - not to the chip.
- **Firmware-side instrumentation is viable.** Since the CPU runs, a DWT
  watchpoint plus a DebugMonitor handler over vector `0x30` can execute and
  report, and any probe that reports via the console ring will be readable
  during a wedge. This was the open question that decided whether firmware
  instruments were usable at all; they are.

### D11 core state during the wedge: the MAC is fine

Second build, same experiment, with the heartbeat also printing the D11
registers `case 604` already knew how to read. Across 45 seconds of wedge the
values are **bit-for-bit identical on every beat**:

    NEXHB 58 rx=39 osh0=0 mc=c5520403 cmd=4 is=100 im=bae7a864
    ... (45 more, all identical except the sequence number)

`maccontrol = 0xc5520403` decodes, against the `MCTL_` definitions in
`firmwares/*/structs.common.h`, to:

    bit31 GMODE      bit30 DISCARD_PMQ  bit26 WAKE     bit24 PROMISC
    bit22 KEEPCONTROL bit20 BCNS_PROMISC bit17 INFRA   bit10 IHR_EN
    bit1  PSM_RUN    bit0  EN_MAC

The three that matter: **`EN_MAC` is set, `PSM_RUN` is set, and `PROMISC` is
set.** The MAC is enabled, the ucode PSM is running, and the core is still in
promiscuous mode - exactly the configuration it should be in for monitor mode -
and yet `rx=` never advances again. The receive hardware has not been disabled,
reset, or reconfigured by the wedge.

That the decode lands on such a coherent, expected bit pattern is also good
evidence the `wlc->regs` struct offsets are right. Treat `im=bae7a864` with
more caution - 17 bits set is an odd-looking interrupt mask, and there are no
`MI_` definitions anywhere in this repo to check it against, so the individual
interrupt bits are deliberately not named here. What is safe to say is that
`macintstatus` holds bit 8, that bit 8 is *clear* in `macintmask`, and that
neither register ever changes again - consistent with nobody servicing
interrupts any more, which is what a dead wl thread would look like.

### SETTLED: it is a spin, and macintstatus bit 8 latching is the trigger

The DWT cycle counter answers it outright. The boot banner gives the core clock
(`BCM43430 r1 @ 37.4/81.6/81.6MHz`), so 81.6M cycles in a one-second beat is
100% CPU with no idle at all.

    beat  1-6   dcyc 40.7M - 44.0M   is=0      ~50% duty, core sleeping half the time
    beat  7     dcyc 54.8M           is=0->100 transition
    beat  8     dcyc 81.62M          is=100
    beat  9-56  dcyc 81.59M-81.62M   is=100    never drops again

**The core never idles again after beat 8.** So the wl thread is *spinning* -
a loop whose exit condition never becomes true - not blocked on something that
never posts. That rules out the whole "waiting on a semaphore/event" family and
makes the DWT comparators the wrong next tool.

In the same beat the CPU pegs, `macintstatus` goes `0 -> 0x100` and **latches
there permanently** - it holds `0x100` for the rest of the run, as does
`macintmask` at `0xbae7a864`, in which bit 8 is *clear*.

### The failure is progressive, not instantaneous

Worth being precise about, because it constrains the mechanism:

    t~12s  (beat 8)   CPU pegs at 100%, is latches 0x100
    t~12-28s          RX still working - rx climbs 24 -> 39
    t~28s  (beat 24)  rx freezes at 39
    t~30s  (hop 11)   ioctl path dies

The CPU pegs roughly **18 seconds before** the command path dies, and RX keeps
running for ~16 of those seconds. So the spin does not immediately lock
everything out; it starves RX and the command path progressively. Any
explanation has to account for that gap rather than a clean simultaneous stop.

Note also that the spin begins right as RX traffic picks up (`rx` jumps 2 -> 24
in the same beat), which fits the mode-0/4 result: reception is required for
the wedge, and discarding the frame at the hook does not help because the
damage is already done upstream.

### Where to go next

The question is now specific and answerable statically: **what spins on
`macintstatus`, and why can bit 8 never be cleared?**

- `macintstatus` is at offset **`0x128`** from the D11 register base and
  `macintmask` at `0x12c`, with `maccontrol` at `0x120` (per `struct d11regs`
  in `firmwares/bcm43430a1/structs.common.h` - note it does *not* start with
  `maccontrol`). The base itself is `0x18001000`, read live from `wlc->regs`;
  it never appears as a literal in either image because it is resolved through
  the backplane at runtime.
- Bit 8 being set in `macintstatus` while clear in `macintmask` is the crux.
  A masked interrupt should simply sit pending and cost nothing, so a spin
  implies something *polls* the register rather than waiting for the interrupt.
  Find that poll.
- This repo has no `MI_` definitions, so bit 8 is deliberately not named here.
  In the stock Broadcom d11 layout bit 8 is one of the general-purpose
  ucode-to-driver signalling interrupts, which would fit a ucode-raised
  condition the driver never acknowledges - but that is an inference from
  outside this tree and should be confirmed before being relied on.
- Since stock 7.45.98 wedges identically and 7.45.41.46 does not, diffing how
  the two versions handle this register is likely to be short and decisive.

## FOUND: the spin is a MAC-suspend wait that never completes

Searching the disassembly for a `macintstatus` load followed by a backward
branch - the literal signature of a poll loop - gives four sites in RAM and
four in ROM. One of them is a tight 16-byte busy-wait, at RAM `0x210b0`,
inside a function entered at `0x2104c`:

    210aa:  movw r6, #8301        ; 0x206d  loop budget
    210ae:  b    0x210b6
    210b0:  movs r0, #10
    210b2:  bl   0x8081f0         ; busy delay, 10us
    210b6:  ldr  r3, [r5, #296]   ; macintstatus
    210ba:  lsls r3, r3, #31      ; test bit 0
    210bc:  bmi  0x210c2          ; set -> done
    210be:  subs r6, #1
    210c0:  bne  0x210b0          ; else keep polling
    210ca:  movs r3, #5           ; budget exhausted -> error 5
    210cc:  str  r3, [r4, #324]

This is suspend-MAC-and-wait: it asks for a suspend, then polls
`macintstatus` bit 0 - MAC-suspend-completed - for 8301 iterations of 10us,
about **83ms**, and stores error code 5 if it never arrives. The routine also
keeps a nesting count at `[r0,#0xf0]` and guards on `maccontrol + 1 == 0`
(the all-ones read that means the core is not answering), both of which fit a
suspend/resume refcount.

`0x8081f0` is a **pure busy-wait**: read a timer, then spin
`bl 0x87fec0 / subs / cmp / bcc` until the interval elapses. No sleep, no
yield. So every suspend attempt pins the core for up to 83ms - which is
exactly the 81.6M cycles/second the DWT counter measured.

### Everything lines up with the live measurements

- `macintstatus` during the wedge reads `0x100`, so **bit 0 is clear** - the
  MAC never reports that it suspended.
- `maccontrol` during the wedge still has `EN_MAC` and `PSM_RUN` set - the MAC
  genuinely was never suspended, this is not a stale status bit.
- Channel changes are what call this path, which is why hopping is required.
- The wait is a busy-wait, which is why the core sits at 100% and why the
  failure is progressive: each attempt burns 83ms, and the more of them pile
  up the less time anything else gets, until RX and then the command path
  starve out entirely.

### But the loop itself is NOT the difference between firmware versions

The same code exists in 7.45.41.46, which does **not** wedge, at `0x1c450`
(found with `relocate.py sig`, unique to 24B). It is instruction-for-
instruction identical: same `movw r6, #8301`, same `movs r0, #10`, same
`bl 0x8081f0`, same bit-0 test, same backward branch.

So the ARM-side suspend loop is the mechanism that burns the CPU, not the bug.
The real question is narrower: **why does bit 0 never get set on 7.45.98?**

### The remaining suspect: the PSM ucode

`MI_MACSSPNDD` is raised by the D11 PSM microcontroller, not by ARM code, and
the ucode is one of the things that genuinely differs between these two
firmware versions - `firmwares/bcm43430a1/7_45_98/ucode.bin` is 52056 bytes
against 52264 for `7_45_41_46`. A ucode that fails to acknowledge a suspend
request while it is busy receiving would produce precisely this: suspend
requested during a retune, never acknowledged while frames are arriving,
83ms burned per attempt, and no wedge at all when reception is off.

That also explains the two results that previously looked awkward: stock
unpatched 7.45.98 wedges identically (same ucode), and discarding the frame at
the monitor hook does not help (the ucode is already busy long before any
host-side delivery decision).

**This last step is an inference, not a measurement.** What is measured is:
the loop exists, it is a busy-wait, bit 0 never sets, the MAC never suspends,
and the ARM code is identical in the working version. Attributing it to the
ucode follows from those plus the version diff, but nothing here has yet read
or disassembled the ucode itself. The next concrete step is to compare the two
`ucode.bin` images around their suspend-acknowledge handling - or, far cheaper
as a first test, to try running 7.45.98 with 7.45.41.46's ucode and see
whether the wedge disappears.

### RULED OUT: the ucode is not the difference

Built 7.45.98 with 7.45.41.46's `ucode.bin` swapped in (compresses to 25430
bytes, well inside the 0x91ed budget) and flashed it. It boots cleanly -
`Decompressing ucode ... (len: 25430)`, `Decompression res:0`, chip comes up
and receives - so the two versions' ucode and ARM code are compatible enough
to run.

**It wedges anyway**, at hop 12, inside the normal 6-11 spread, and the
heartbeat signature is identical in every respect:

    NEXHB 29 rx=39 dcyc=81594974 osh0=0 mc=c5520403 is=100 im=bae7a864

Same 100% CPU, same `macintstatus` latched at `0x100`, same `maccontrol`, same
frozen `rx`. So the PSM ucode is **not** what differs in a way that matters,
and the inference recorded above is wrong. Worth keeping the negative: it was
cheap, it was the obvious suspect, and it is now eliminated.

What survives from that section is the measured part - the busy-wait loop, its
83ms budget, `macintstatus` bit 0 never setting, and the MAC never actually
suspending. Only the attribution to ucode is retracted.

### The strongest remaining lead: 7.45.98's RAM RX reimplementation

The one place these two firmwares are already *known* to diverge in the RX path
is documented earlier in this file: 7.45.98's stock flash-patch table contains
an entry at `0x81f410` that replaces the ROM RX routine's prologue with a
branch into a **RAM reimplementation at `0xd4e4`**, whereas 7.45.41.46 has no
such diversion and runs the ROM code. That is why the monitor hook had to move
to the RAM call site `0xd716` in the first place.

This lead fits everything the ucode hypothesis did, and unlike it, is not yet
contradicted:

- It is RX code, and RX is required for the wedge (mode 0 never wedges).
- It is upstream of the monitor hook, which is why discarding the frame at the
  hook does not help (mode 4 wedges like mode 2).
- It is present in stock unpatched 7.45.98, which wedges identically, and
  absent in 7.45.41.46, which does not.
- It is version-specific RAM code, so swapping the ucode would not change it -
  consistent with the negative result above.

The call map puts the whole chain in one region of RAM:

    0x9f24 -> 0x9e54 -> 0x25db0 -> 0x20ad4 -> 0x15ffc -> 0xd4e4 -> 0xa5e2

and note `0x20ad4` sits close to both `wlc_bmac_read_tsf` (`0x20abc`) and the
suspend-and-wait routine (`0x2104c`). Whether the RX path and the suspend path
actually share state there is the thing to check next.

### Next instrument, if hardware work resumes

Nothing so far proves the CPU is spinning *in that particular loop* - only that
it is spinning and that this loop is a plausible busy-wait on a bit that never
sets. A PC sampler would settle it: SysTick is confirmed free on this chip
(`CTRL=0x4`, `RELOAD=0`), so a SysTick handler that records the stacked PC from
the exception frame would name the spinning code directly, and the heartbeat
can print it.

That needs the vector table, and the table is still unresolved - `VTOR=0` says
it is at address 0, but every read of `0x0..0x40` has come back either zeroed
or as a failed-ioctl artefact. Reads of low memory also appear to wedge the
chip, though that is confounded by how readily this device now wedges on its
own. Resolve the table before writing any vector.

## ROOT CAUSE: macintstatus bit 8 is MI_RXOV, an RX FIFO overflow nothing can clear

### The missing header

This repo has no `MI_` definitions, which is why bit 8 went unnamed for so
long. They exist in a public Broadcom `wl` driver tree (found via
`seemoo-lab/nexmon` issue #595):

    https://github.com/FlUxIuS/rt-ac86u_et
      release/src-rt-5.02hnd/bcmdrivers/broadcom/net/wl/impl51/4365/src/
        include/d11.h      <- MI_* bits, d11regs, SHM layout
        wl/sys/wlc_bmac.c  <- the bmac layer, ISR and RXOV handling

That tree is worth keeping in mind for any future work here: it names
registers, struct fields and driver logic this repo only has as addresses.

From its `d11.h`:

    #define MI_NSPECGEN_1  (1 <<  8)   /* non-specific gen-stat bits set by PSM */
    #define MI_RXOV        MI_NSPECGEN_1   /* rxfifo overflow interrupt */

**Bit 8 is `MI_RXOV` - RX FIFO overflow.**

### Why it can never be cleared

The ISR at ROM `0x8563ec` decides what to acknowledge like this:

    8563f0:  ldr r5,[r0,#16]      ; r5 = wlc->hw
    8563f4:  ldr r7,[r5,#0x88]    ; r7 = hw->regs
    8563fc:  ldr r8,[r7,#0x128]   ; r8 = macintstatus
    85640e:  ldr r3,[r5,#0x5c]    ; defmacintmask
    856414:  ldr r6,[r5,#0x64]    ; macintmask
    856416:  orrs r6, r3
    856418:  ands r6, r6, r8      ; r6 = status & softmask
    85641c:  beq  0x856456        ; nothing for us -> return, ACKING NOTHING
    856432:  str  r6,[r7,#0x128]  ; ack only the bits in softmask

It only ever writes back bits that are in the software mask. A status bit
outside that mask is never acknowledged and latches permanently.

And `MI_RXOV` is outside it. `wlc_bmac.c` shows why:

    #ifdef WLRXOV
        if (WLRXOV_ENAB(wlc->pub))
            wlc_hw->defmacintmask |= MI_RXOV;
    #endif

`MI_RXOV` only enters the mask when `WLRXOV` is built in. It is not, in this
firmware - measured live on the chip, every beat, healthy and wedged alike:

    im=bae7a864  m5c=bae7a864  m60=bae7a864  m64=0

`m5c` is `defmacintmask`, `m64` is `macintmask`; the ISR's effective mask is
`m5c | m64` = `0xbae7a864`, and **bit 8 is clear in all of them**, including
the hardware `macintmask`. So the chip never even raises the interrupt, and
the ISR could not acknowledge it if it did.

### The transition, measured

    NEXHB 6  rx=16  dcyc=42414926  is=0    m5c=bae7a864 m64=0   healthy, ~50% duty
    NEXHB 7  rx=31  dcyc=53081217  is=100  m5c=bae7a864 m64=0   RXOV latches
    NEXHB 8  rx=31  dcyc=81570542  is=100  m5c=bae7a864 m64=0   100% CPU
    NEXHB 9+        dcyc=81.6M     is=100                       never recovers

`rx` jumps 16 -> 31 in the beat where `is` becomes `0x100`: a burst of frames
arrives, the FIFO overflows, `MI_RXOV` sets, and the core pegs and stays there.

### This explains every earlier result

- **RX required** (mode 0 clean, 40/40): no reception, no FIFO, no overflow.
- **Delivery irrelevant** (mode 4 wedges like mode 2): the overflow is in the
  D11 hardware FIFO, far upstream of any host-delivery decision.
- **Stock 7.45.98 fails identically**: the mask is the firmware's, not ours.
- **MAC still enabled and promiscuous, yet `rx` frozen**: the MAC is fine; its
  receive FIFO has overflowed and nothing recovers it.
- **No trap, no leak, healthy heap**: nothing crashed or leaked.
- **Concurrency not required**: RX and hopping both contribute traffic/bursts;
  they never had to overlap.

### Corrections this supersedes

- The MAC-suspend busy-wait at `0x210b0` is **not** where the CPU goes. Measured
  directly: `hw+0xf0` (suspend depth), `hw+0xf4` (pending) and `hw+0x144`
  (result) all read 0 throughout, including mid-wedge, with the `hw` pointer
  verified by `hw+0x88 == 0x18001000`. That loop was a plausible-looking red
  herring and is now excluded.
- "The wl thread is stuck" was too coarse. The heartbeat is an `hndrte` timer
  callback running off the RTE main loop, and it fires with 1.000s precision
  right through the wedge - so the main loop is alive and scheduling. What has
  stopped is RX progress and command servicing, while the loop spins without
  ever idling.

### Toward a fix

The vendor's own handling, `wlc_rxov_int()` in `wlc_bmac.c`, does not reset
anything dramatic - it throttles TX and backs off:

    wlc_set_txmaxpkts(wlc, wlc->rxov_txmaxpkts);
    wl_add_timer(wlc->wl, wlc->rxov_timer, wlc->rxov_delay, FALSE);
    wlc->rxov_delay = MIN(wlc->rxov_delay*2, RXOV_TIMEOUT_MAX);

so the intended recovery is throttle-and-retry, with `MI_RXOV` unmasked so the
condition is *noticed* in the first place. Two directions worth trying, in
order of cost:

1. Add `MI_RXOV` to the masks from a nexmon patch (`hw+0x5c`, and the hardware
   `macintmask`) so the bit is at least acknowledged and cleared each time,
   and see whether the wedge simply stops. This is cheap and directly tests
   whether the latched bit is what holds the RX path down.
2. If clearing alone is not enough, the FIFO itself needs recovering, which is
   what a fuller `wlc_rxov_int()` equivalent would do.

Note 7.45.41.46 is unaffected while presumably having the same masks, so
before building anything, check whether 41.46 ever raises `MI_RXOV` at all -
its RX path is the ROM routine, whereas 7.45.98 runs the RAM reimplementation
at `0xd4e4` reached via the stock flashpatch at `0x81f410`. If 41.46 simply
never overflows, the fix belongs in that RAM RX path rather than in the mask.

## TRAP: never touch the D11 registers through `struct d11regs`

`struct d11regs` in `structs.common.h` is declared `__attribute__((packed))`.
GCC therefore cannot assume its fields are aligned and **decomposes every
access into bytes**. This:

    nex_hb_wlc->regs->macintstatus = MI_RXOV;

compiles to four byte transactions against a memory-mapped, write-1-to-clear
hardware register:

    ldrb.w r6,[r0,#296] / strb.w r6,[r0,#296]   <- byte 0
    strb.w ip,[r0,#297]                         <- byte 1
    strb.w r6,[r0,#298] / strb.w r6,[r0,#299]   <- bytes 2,3

where the vendor's own ISR uses a single `str`. This cost five wasted runs: the
byte writes killed the SDIO card mid-test every single time
(`bad CIS tuple`, `error -22`, `Failed to initialize a non-removable card`),
which looked exactly like the device instability seen elsewhere in this file
and was initially attributed to it. With the access forced to a single word,
**SDIO deaths went from one per run to zero**.

Reads come back coherent in practice - every `maccontrol`/`macintstatus` value
recorded above was read this way and decoded sensibly - so existing read-only
probes such as `case 604` are not suspect. Writes are another matter. Use:

    #define D11REG(regs, off) \
        (*(volatile unsigned int *) ((unsigned int) (regs) + (off)))
    #define D11_MACCONTROL 0x120
    #define D11_MACCOMMAND 0x124
    #define D11_MACINTSTATUS 0x128
    #define D11_MACINTMASK 0x12c

and check the disassembly afterwards - `ldr.w`/`str.w` with `#296`, no `strb`.

## RULED OUT: clearing MI_RXOV does not fix the wedge

With the register access corrected, the two arms are finally comparable. Same
firmware image both times, arm selected at runtime by `case 615`, run from a
fresh boot with `setup.sh` (no `rmmod`/`modprobe`, which is itself a cause of
SDIO failures on this device):

| arm | first failure | RXOV seen | heartbeat | CPU | SDIO deaths |
|---|---|---|---|---|---|
| `clr=0` observe only | hop 10 | ov=51 | survives | 81.6M | 0 |
| `clr=1` clear the bit | hop 11 | **ov=52** | survives | 81.6M | 0 |

`ov=52` confirms the clear executed fifty-two times. `macintstatus` still read
`0x100` on every subsequent beat, and the wedge arrived on schedule.

**So the bit cannot be cleared by writing it back, and clearing it would not
help anyway.** Either bit 8 is not write-1-to-clear, or - far more likely - the
RX FIFO is still in overflow and the PSM re-raises it immediately. The latched
bit is a *symptom* of an overflow that has already happened; the receive path
is held down by the FIFO/DMA state behind it, not by the status bit.

### What that leaves

Recovery has to act on the FIFO, not the flag. The vendor's `wlc_rxov_int()`
never touches `macintstatus` directly either - it throttles TX and backs off,
letting the receive path drain:

    wlc_set_txmaxpkts(wlc, wlc->rxov_txmaxpkts);
    wl_add_timer(wlc->wl, wlc->rxov_timer, wlc->rxov_delay, FALSE);
    wlc->rxov_delay = MIN(wlc->rxov_delay*2, RXOV_TIMEOUT_MAX);

Worth trying next, in order of cost:

1. Find what drains or resets the RX FIFO after an overflow on this chip - the
   RX DMA reset path is the obvious candidate - and call it from the heartbeat
   when `MI_RXOV` is seen. Same cheap experimental shape as this one.
2. Establish why 7.45.41.46 never gets here at all. It has the same masks, so
   either it does not overflow or it recovers. Its RX path is the ROM routine,
   whereas 7.45.98 runs the RAM reimplementation at `0xd4e4` reached via the
   stock flashpatch at `0x81f410` - that difference remains the best lead, and
   diffing how each drains the receive FIFO is the concrete question.

## dma_rxfill makes the RXOV latch clearable - but does not end the wedge

`dma_rx` and `dma_rxfill` had **no `FW_VER_7_45_98` entry** in `wrapper.c`,
only `7_45_41_26` and `7_45_41_46`. Calling either would have silently compiled
to a no-op stub - the exact bug class this file documents. Relocated with
`buildtools/fwmap/relocate.py`, unique at every signature length up to 96B,
with prologues instruction-for-instruction identical to 41.46's:

    dma_rx      41.46 0x4F30  ->  7.45.98 0x56b0
    dma_rxfill  41.46 0x515C  ->  7.45.98 0x58ac

Neither has any static caller in either version - the DMA ops are dispatched
indirectly through the `di` vtable - so the call map cannot corroborate these,
and the disassembly comparison is the evidence. Both are now in `wrapper.c`.
Verified in the built image: `bl 58ac <dma_rxfill>`, not a stub.

`hw->di[]` is the per-FIFO `dma_info` array at `wlc_hw_info+0x14`, RX being
`di[0]`.

### Result: the flag becomes clearable, the wedge does not lift

Calling `dma_rxfill(hw->di[0])` before writing `MI_RXOV` back:

    NEXHB 50  is=100  ov=29 fill=29
    NEXHB 51  is=0    ov=29 fill=29   <- clears, and stays clear
    NEXHB 52  is=0    ov=29 fill=29
    NEXHB 53  is=0    ov=29 fill=29
    NEXHB 54  is=100  ov=30 fill=30   <- re-raises, cleared again
    NEXHB 55  is=0    ov=30 fill=30

Compare the clear-only arm, where `macintstatus` read `0x100` on **every** beat
through 52 clears and never once went to zero. So reposting receive descriptors
really does deassert the overflow: the condition behind the bit is being
addressed, and `ov` fell from 51 to 30 across a comparable run.

**But it does not fix anything that matters.** The wedge still arrived (hop 9),
`rx` still froze at 39, and `dcyc` stayed pinned at 81.6M - 100% CPU - across
every one of those beats, including the ones where `is` read 0.

### What that tells us

The RX FIFO overflow and the 100% CPU spin are **not the same problem**. The
overflow is real, is what `MI_RXOV` reports, and is treatable with
`dma_rxfill`. The spin persists with the flag clear, so whatever the core is
burning cycles on is a separate stuck state - most likely the receive DMA ring
left in a condition the dpc keeps retrying without progress. Refilling
descriptors does not unstick it.

This also retires the idea that unmasking `MI_RXOV` would have been a fix. The
bit is now observed going 1 -> 0 -> 1 while the chip stays wedged throughout.

### Where to go next

The remaining question is what the core actually executes during the spin, and
that is now worth answering directly rather than by inference - two previous
candidate loops (MAC-suspend, RXOV) have both been excluded by measurement
after looking convincing on paper.

A PC sampler is the tool. SysTick is free on this chip (`CTRL=0x4`,
`RELOAD=0`), and the vector table sits at 0 with real Thumb handlers visible
from `0x100` onward, the low entries (SysTick at `0x3C`) reading zero because
this firmware never uses them. Rather than writing into low RAM - reads there
have repeatedly destabilised the chip - build a relocated table in the patch
region, copy the existing one into it with a firmware-side `memcpy` from 0,
install a SysTick handler that records the stacked PC from the exception
frame, and point `VTOR` at the copy. The heartbeat can then print the sampled
PC, and `buildtools/fwmap` turns that straight into a function.

Everything so far says the wl thread stops while the chip around it stays
healthy. The next thing worth knowing is whether that thread is **spinning or
blocked**, because it splits the search cleanly: a spin means a loop with an
exit condition that never becomes true, a block means a wait on something that
never posts.

The DWT cycle counter answers it cheaply and is already available
(`DWT_CTRL` reads `0x40000000`, so the block is present with 4 comparators and
currently unused). Enable `CYCCNT` (`DWT_CTRL` bit 0, counter at
`0xE0001004`) and print the per-beat delta:

- delta at roughly the full clock rate -> the core never idles -> something is
  spinning
- delta well below it -> the core is sleeping between interrupts -> the wl
  thread is blocked, waiting

Take a healthy-chip baseline in the same run for comparison, since the
absolute clock rate here is not documented anywhere in this repo.

If it turns out to be blocked, the four DWT comparators become the tool of
choice: watch the semaphore/event word the thread waits on and catch who
should have posted it.

The heartbeat is now a general carrier for anything readable during a wedge,
at zero risk, because it only reads. The obvious next payload is the D11 core
state that `case 604` already knows how to read - `maccontrol`, `maccommand`,
`macintstatus`, `macintmask` - printed once per beat. That answers directly
whether the MAC is still raising interrupts and whether the firmware has left
them masked, which distinguishes "the wl thread is blocked waiting for an
interrupt that will never come" from "the wl thread is spinning".

Worth adding at the same time: `osh[0]` and the heap summary, so the passive
`case 613` picture keeps being available after the ioctls die.

### Always `make clean` after changing a firmware input

Rebuilding after swapping `ucode.bin` (or any `$(FW_PATH)` input) without a
`make clean` silently produces a **corrupt image**. The patcher writes into
`./brcmfmac43430-sdio.bin` in the patch directory, and if that file already
exists from a previous build it gets patched *again* rather than being recopied
from the pristine firmware, so patches land on top of patches.

Nothing in the build output says so - it reports `APPLYING PATCHES` and exits
0. The failure only shows up at load time, and helpfully the driver does catch
it:

    brcmfmac: brcmf_sdio_verifymemory: Downloaded RAM image is corrupted,
              block offset is 401408, len is 480
    brcmfmac: brcmf_sdio_download_firmware: dongle image file download failed

(401408 + 480 = 401888 = exactly the image size, i.e. the final block.) The
chip then never enumerates, there are no `wlan` netdevs and no firmware console
output at all, which looks alarmingly like dead hardware but is only a bad
image. `make clean && make` fixes it; compare md5s to confirm, since a
double-patched image is the same *size* as a good one.

### Rebuilding the test rig

A reboot wipes `/tmp`, which cost this session the previously deployed
`nexutil`, both helper scripts, and the unread `case 612` run. Everything now
lives in persistent locations instead: `nexutil` at `/usr/local/bin/nexutil`
(built natively on the Pi - it has gcc, make and libnl3; `utilities/libnexio`
then `utilities/nexutil`), and `/home/pi/reload.sh`, `/home/pi/hoptest.sh`,
`/home/pi/quiesce.sh` with logs beside them as `/home/pi/hoplog-*.txt`.

Two additions to `reload.sh` worth keeping: `rfkill unblock all` before bringing
interfaces up (the Pi boots soft-blocked, and without it `ip link set` fails
with "Operation not possible due to RF-kill"), and reading the phy index from
`/sys/class/net/wlan0/phy80211/name` since it increments on every reload.

The Pi has no RTC and its clock runs hours behind the host, so correlate by
relative time in the logs rather than by wall clock.

## DEAD END: the ARM debug hardware is unusable on this chip

Both routes to a PC sampler were tried and both kill the firmware. They fail
for the same underlying reason, so neither is worth retrying.

### Low memory cannot be read, which rules out the vector table

`VTOR` reads `0`, so the vector table is nominally at address 0. It cannot be
copied, because that memory cannot be read:

    0x0    ->  00000000 00000000 00000000 00000000   (reads, all zero)
    0x10   ->  *** chip dead from here on ***

Reading `0x10` through `0xFF` **kills the firmware outright** - the following
`case 613` returns the no-reply artefact and never recovers. Reads at `0x100`
and above are perfectly fine and return a dense table of odd (Thumb) pointers:

    0x100  =  00019a0d 00024b8b 00024be7 000245f5
    0x140  =  0002414d 000249bd 00002239 00002235
    0x1c0  =  00017a19 00017a15 00017a1f 0001a709
    0x300  =  00034247 0003423b 00034263 0003422d

So whatever backs `0x00..0xFF` is not ordinary RAM, and the exception vectors
proper are exactly the part that cannot be touched. Note nexmon patches at
`0x2c40` (`b_hndrte_idle`) work fine, so this is a narrow window near zero, not
low RAM generally.

Attempting the relocation anyway - copy 256 entries from 0 into an aligned
table in the patch region, patch entry 15, set `VTOR` - **bricks the chip the
instant it is armed**, exactly as that unreadable source implies. The code is
left in `ioctl.c` `case 616` as a disabled record, since "relocate the vector
table" is the obvious thing to reach for.

### DWT watchpoints kill it too

The fallback was to avoid exceptions entirely: arm a DWT comparator in PC-match
mode (`FUNCTION=4`) and *poll* its `MATCHED` bit (bit 24, clears on read) from
the heartbeat, giving a hardware "did this range execute" probe with no vector
involved. Four comparators are present and unused.

Configuration works - `NEXDWT c0 addr=5ff94 mask=8 func=4` - but the chip dies
on the first match. Isolated cleanly by arming a comparator on `nex_heartbeat`
itself, which runs every second:

    heartbeats logged before arming:    6
    heartbeats logged 6s after arming:  6
    ... NEXHB 5 ...
    ... NEXDWT c0 addr=5ff94 mask=8 func=4
    (nothing further)

With `MON_EN` clear in `DEMCR` and no halting debugger attached, a watchpoint
debug event has nowhere to go and locks the core. Enabling `MON_EN` would
vector to `0x30` - back inside the unreadable window.

**Conclusion: no DWT watchpoint, no DebugMonitor, no SysTick handler, no PC
sampling.** The DWT *counters* remain fine, because they are read-only and
generate no debug events - `CYCCNT` produced the 81.6M measurement and can keep
being used.

### What is left for finding the spin

Software instrumentation only, guided by the call map:

1. Nexmon already hooks `hndrte_idle` (`b_hndrte_idle` at `0x2c40`, see
   `autostart.c`). Counting calls to it through a wedge is cheap and directly
   informative: the core never sleeps during the wedge, so if idle is still
   being reached the loop is cycling and finding work each pass, and if it is
   not, the loop is stuck inside a single call.
2. Beyond that, bisect with `BLPatch` counters at candidate sites from
   `buildtools/fwmap` - one build per candidate, but it depends on no debug
   hardware and cannot brick the chip the way the above does.

## MECHANISM: the wedge is an interrupt storm on IRQ 0

Two measurements settle what the core is doing, neither needing debug hardware.

### The "main loop" is `wfi`, and during the wedge it stops running

`hndrte_idle` at `0x2c40` is not called per iteration - it *is* the loop, and is
entered exactly once:

    2c58:  mov r0, r4
    2c5a:  bl  0x807de4      <- per-iteration dispatch
    2c5e:  b.n 0x2c58        <- unconditional

Following `0x807de4` through two thunks (`0x808338`, then `0x80700c`) lands on:

    80700c:  wfi
    80700e:  bx lr

So the idle loop is literally "sleep until an interrupt, repeat". A `BLPatch`
at `0x2c5a` counting iterations (`nex_loop_count`, `autostart.c`) gives:

    healthy      dloop=195, 9, 7, 5, 7, 9    dcyc ~42-44M   (sleeps between wakes)
    wedge onset  dloop=2, 8, 2               dcyc 81.6M
    wedged       dloop=0                     dcyc 81.6M

`dloop=0` with `CYCCNT` at the full core clock is the important combination:
the core is **neither sleeping nor returning to idle**. The only place left is
interrupt context.

### IRQ 0 is active and pending at the same time

Reading the NVIC from the heartbeat - all read-only - names it:

    healthy   iabr=1  ispr=18e  icsr=400810   VECTPENDING=0
    wedged    iabr=1  ispr=18f  icsr=410810   VECTPENDING=16

`iabr` bit 0 is set in both: IRQ 0 is the interrupt the firmware runs in
normally. The difference is `ispr` bit 0, and `VECTPENDING=16` (exception 16 =
IRQ 0) in `ICSR`. So while the IRQ 0 handler is *still active*, IRQ 0 is
*already pending again* - it re-enters the moment it returns, forever.

That is an interrupt storm, and it explains the whole symptom set at once:

- 100% CPU with no idle - the core never reaches the `wfi`.
- `dloop=0` - the idle loop never gets another iteration.
- The heartbeat keeps perfect 1.000s time - `hndrte` timers are dispatched from
  interrupt context, which is the one thing still running.
- RX and ioctl processing die - both are thread-level work driven from the idle
  loop, and it never runs again.
- No trap, no leak, healthy heap, MAC still enabled - nothing crashed.

Note the heartbeat's own `ICSR` reads `VECTACTIVE=16`, i.e. it is itself
dispatched under IRQ 0, which is why `VECTACTIVE` alone was never going to be
informative and `IABR` was the register worth reading.

### Where this points

IRQ 0's vector lives at `0x40`, inside the unreadable window, so the handler
cannot be identified that way. But it does not need to be: the question is what
keeps *asserting* IRQ 0.

`MI_RXOV` is latched in `macintstatus` throughout, and `dma_rxfill` was already
shown to clear that flag without ending the wedge - so the assertion survives
the flag being cleared. The likely source is therefore a DMA-side receive error
or overflow status that is separate from `macintstatus` and is never
acknowledged.

Next concrete step: dump the RX DMA registers for `hw->di[0]` (rcv control and
status - `dma_rx`/`dma_rxfill` are now relocated for this firmware, `0x56b0`
and `0x58ac`) and look for a status bit that stays asserted through the wedge.
That is the thing to clear.

## THE REAL INVARIANT: ~40 received frames, not hops, not channels, not time

Everything in this file framed as a "channel hopping wedge" is mis-framed.
Hopping was never the trigger; it only ever mattered because it generates
received frames.

### Hop count is not the invariant - the frame count is

Across ten valid runs the failure lands anywhere from hop 7 to hop 11, but the
frame counter freezes at the same place every time:

    run         fail at hop   rx before   frozen at
    bg80shdyr        7            37          39
    bn1tfwpjv        8            38          39
    bwz4c6g5m        9            38          39
    bei78horv       10            38          39
    bv87axk07       10            37          39
    b4ytaxk8w       10            38          39
    bads0xjta       10            38          39
    bj2ly6c8e       11            38          39
    blsixw07n       11            37          38

"Hop 10" was a coincidence of this RF environment: about two frames arrive per
hop here, so ~40 frames happens to be ~10 hops.

### Hop *rate* changes nothing, because it does not change frames per hop

Re-measured with the passive probe (`case 613`), since the original
rate-independence finding used `case 612` and was contaminated:

    interval=2s   first failure hop 10   elapsed 32s
    interval=5s   first failure hop 10   elapsed 62s

Same hop count, double the wall clock. Slowing the hop rate is **not** a
workaround, and the frames-per-hop stayed at ~2 regardless of dwell time.

### Hopping is not required at all

The decisive control - fixed channel 6, no channel change whatsoever:

    t=5s  rx=28   t=35s rx=34   t=56s rx=38
    t=10s rx=29   t=40s rx=35   t=63s NOREPLY
    t=25s rx=32   t=51s rx=37   WEDGED at t=71s with NO hops

Frozen at `rx=39`, `dloop=0`, `is=100`, `ispr=18f` - the identical wedge,
identical signature, zero hops. **This also retires "fixed channel is stable
indefinitely"**, which appears earlier in this file: it is only stable for as
long as it takes a quiet channel to deliver forty frames.

### The failing channel is not a channel

Across all runs the first failure occurred on channels 1, 2, 4, 5, 8, 9, 10 and
11 - eight different channels, no clustering. Not a regulatory-domain effect.
Note the hop sequence used here is `(i % 11) + 1`, so channels **12 and 13 were
never exercised**; an ETSI-domain device hopping to 12/13 is untested and is a
separate question.

### Per-frame dma_rxfill does not help

If ~40 were the RX descriptor ring and frames consumed descriptors that were
never reposted, calling `dma_rxfill(hw->di[0])` on every frame should have
fixed it. It does not:

    perframe=1   first failure hop 11   rx frozen at 39   fill=39

`fill=39` confirms it really was called once per frame. So either the ring is
not the exhausted resource, or `dma_rxfill` had nothing to post - it needs
buffers and free ring slots, and its return value was not checked here, which
is the first thing to fix if this line is picked up again.

### What this means

The chip dies on approximately the fortieth frame delivered to the monitor
hook, under every condition tested. That is an extremely tight bound on a fixed
resource of size ~40 that is consumed per frame and never returned. Finding
what has forty of something in the receive path is now the whole problem, and
`dma_attach`'s `nrxd`/`nrxpost` arguments are the obvious place to start
looking - they are the ring sizing parameters, and `dma_attach` already has a
wrapper entry (though not yet for this firmware version).

## FIXED: 7.45.98 skips pkt_buf_free_skb on received frames

### The bug

The caller of the RX routine (RAM `0x15ffc`) ends one of two ways:

    1619e:  mov r0,r7 / mov r1,r5 / movs r2,#0
    161a8:  b.w 0x6c74              <- pkt_buf_free_skb, returns the buffer
    161ac:  ldmia.w sp!, {...,pc}   <- plain return, buffer NOT freed

and 7.45.98 adds a branch, immediately after the RX routine returns, that takes
the second one:

    1609e:  bl 0xd4e4              <- RX routine, ends in the wl_monitor call
    160a2:  ldr.w r3,[r4,#0x208]
    160a6:  cbz r3, 0x160b2
    160a8:  ldr r3,[r4,#0]
    160aa:  ldrb.w r3,[r3,#0x3f]
    160ae:  cmp r3,#0
    160b0:  beq.n 0x161ac          <- skip the free
    160b2:  ldrh r3,[r6,#16]       <- normal path, reaches the free

7.45.41.46's equivalent function (`0x12c20`, located with `relocate.py`,
unique to 32B) has no such branch and always reaches the free. That is the
version difference this file has been hunting since the beginning.

### The fix

    __attribute__((at(0x160b0, "", CHIP_VER_BCM43430a1, FW_VER_7_45_98)))
    GenericPatch2(rx_leak_no_early_exit, 0xbf00);   // nop

NOPping the branch makes the early exit unreachable, so every received frame
reaches `pkt_buf_free_skb`, as on 41.46. One instruction.

### Result

Before, under every condition tested, the chip died at ~40 received frames.
After:

    fixed channel, 240s:   rx = 20,949    no wedge
    fixed channel, longer: rx = 72,074    no wedge
    60 channel hops @2s:   60/60 hops ok, 0 failed, chanspec tracks every hop

with `is=0` (no RXOV latch), `dloop` back to 85-345 per beat (was 0), and the
SDIO card healthy throughout. Receive throughput went from a trickle of ~1
frame per 5s - the pool was already starving from the first frame - to ~90-500
frames/second.

### Caveats worth carrying

- **The branch presumably existed for a reason.** The condition is
  `[r4+0x208] != 0 && [[r4]+0x3f] == 0`; if that marks frames handed off
  elsewhere and freed by someone else, NOPping it converts a leak into a
  double free. Nothing bad was observed across 70,000+ frames, but the
  condition has not been decoded and that is the risk to check before shipping
  this. Decoding what `+0x208` and `+0x3f` are is the obvious follow-up.
- **`osh[0]` never showed the leak.** It read 0-1 throughout, where a leak of
  forty buffers should have made it climb. Either it does not track this pool
  or it is not the counter this file has assumed since `case 609`. Unresolved,
  and it is why the leak was not spotted from the probe data alone.
- The `~40` figure was never confirmed as a specific pool size; it is simply
  where the chip died, repeatably, under every condition.

### Harness bug found alongside

`setup.sh` only deleted `wlan0mon` when the *netdev* existed. When an add
failed, it still went on to run `nexutil -m2`, leaving the driver tracking a
monitor vif with no netdev - after which every later add failed with

    brcmf_vif_add_validate: ... there is already a monitor interface, returning EOPNOTSUPP

and `iw dev wlan0mon set channel` returned `No such device (-19)` while
`nexutil` kept working, because nexutil's netlink path uses the driver's global
netdev rather than the named interface. That disagreement between the two tools
is the tell. Always verify the add succeeded before setting monitor mode, and
treat a missing netdev as needing cleanup rather than as nothing to clean.

## bcm43455c0 / 7.45.265 port — full port done, monitor mode + injection working on hardware

New chip/rig session, done end-to-end in one sitting (2026-08-28). Target device is a
Raspberry Pi 4B (`pi`/`raspberry`, passwordless sudo, kernel `6.18.39+rpt-rpi-v8`), and
the rig has an out-of-band management link that the 43430a1 session never had: an
AWUS036AXM USB adapter (`wlan0`, `mt7921u`) carries SSH on `192.168.1.131` while `wlan1`
(the BCM43455 being patched) is completely free to reload, wedge, or reboot without
losing the session. That single change eliminated most of the operational overhead the
43430a1 notes above describe (no watchdog-reboot races, no waiting for `/tmp` to survive
a crash loop, no power-cycle dependency).

### Choosing the right reference firmware — do not go by version number alone

This chip's `firmwares/bcm43455c0/` had five existing ports (154, 189, 206, 241,
234_4ca95bb_CY) before 265. **241 is a trap**: it is numerically closest to 265 but is a
different, older build lineage - its feature string (`lpc-pwropt-43455_ftrs`, no
`gtkoe`/`txbf`/`dpp`) matches 154/206, not 265. `234_4ca95bb_CY`'s feature string
(`...wfds-mfp-dfsradar-wowlpf-...-gtkoe-roamprof-txbf-ve-*sae-dpp-sr-okc-bpd`) is nearly
identical to 265's. Byte-signature relocation run 234_CY -> 265 landed cleanly with a
uniform local delta (-0x5380 in the 0x1A2xxx band); the same relocations attempted from
241 would have been relocating from the wrong lineage entirely. **Always diff the
firmware's feature string (`strings *.bin | grep roml`) between candidate references
before picking one to relocate from - the version number is not a reliable proxy for
which build shares layout.**

234_CY itself only ships a console+ioctl patch (no monitor mode), while 206 has the full
monitor+injection patch but is on the wrong lineage for 265. The fix: take the *build
scaffolding* (Makefile, definitions.mk shape, version.c's `VERSION_PTR_1..4` macro form)
from 234_CY since it matches 265's structure, and take the *patch sources*
(monitormode.c/injection.c/sendframe.c/ioctl.c/console.c) from 206 since that is the most
recent full-featured reference. Retarget every address via 234_CY->265 relocation, not
206->265.

### A self-testing derivation script beat hand-relocating every address

Rather than relocate each `definitions.mk` entry individually and hope, wrote
`firmwares/bcm43455c0/7_45_265/derive.py`: it locates each value by *structural*
signature (the ucode magic bytes, the fixed tail pattern after
`HNDRTE_RECLAIM_0_END`, the six aligned slots equal to `FP_CONFIG_ORIGBASE`, the version/
date/time string slots) rather than by copying a byte offset from a similar-looking
build. Before ever computing 265's values, it re-derives the *already-known* values for
206/234_CY/241 and asserts they match - so a bug in the derivation logic fails loudly on
firmwares whose answer is already known, instead of silently producing a wrong answer
for the new one. All three self-tests passed on the first correct version of the script,
and every one of 265's derived values matched what independent manual analysis (done
during planning, before any code was written) had already found. This is worth doing for
any future port in this chip family - the method generalizes, only the structural
signatures need adapting per-chip.

### Wrapper relocation confirmed itself as a side effect, for the weakest-evidence address

`wl_monitor` only matched a 16-byte signature going from 234_CY to 265 (`0x1A85BC` ->
`0x1A323C`), the weakest evidence of any address in this port. Rather than trusting it on
signature length alone, disassembled it - and it turned out to internally call `bl
0x19a0f8` (the just-independently-verified `memcpy` relocation) and tail-call `b.w
0x1a2f68` (the just-independently-verified `wl_sendup` relocation). Three separately
"weak" pieces of evidence turned out to corroborate each other through the actual code
structure. **When a relocated address's own body calls other addresses you've also just
relocated, disassemble it - internal consistency between unrelated relocations is much
stronger evidence than any single signature match, and it's free once you're already
looking at the disassembly.**

### Compiler-flag gap from blending two Makefiles - not an address bug

First build failed with `-Werror=address-of-packed-member` in
`patches/common/radiotap.c`. Not a relocation problem: 234_CY's Makefile (the base for
this port's scaffolding) dropped `-Wno-address-of-packed-member` because 234_CY has no
monitor mode and never compiles `radiotap.c`; this port's `monitormode.c`/`injection.c`
pull it in via `ieee80211_radiotap.h`, so the flag is needed again. Restored it from
206's Makefile. **When blending a Makefile from one reference port with patch sources
from another, diff the two Makefiles' `CFLAGS` explicitly - a flag that looks unrelated
to addresses can still break the build for reasons specific to which source files each
reference actually compiles.**

### Wrapper audit clean on the first build

Ran the same audit as the 43430a1 session
(`grep DUMMY gen/nexmon.pre` vs `nm -D gen/patch.elf | awk '$2=="W"'`, ignoring
`b_flash_patch_*`) after the first successful build: zero real findings. All 33 actually-
called wrapped functions resolved; none of the 43430a1-style silently-missing-wrapper
bugs occurred in this port. Whether that is because 234_CY/206 already carry more
complete `wrapper.c` coverage for this chip than 43430a1 did at the time, or because this
session's `derive.py`-first approach caught the gaps before they could compile silently,
is not established - but the audit is cheap and mechanical, and running it before every
deploy (not just once) cost nothing and confirmed nothing regressed across three rebuilds.

### First deploy attempt booted clean - no wrapper-address surprises on real hardware

Unlike the 43430a1 port (which needed several rounds of crash -> byte-signature-fix ->
retest before RX worked), this port's first hardware deploy showed:

    Firmware: BCM4345/6 wl0: ... version 7.45.265 (28bca26 CY nexmon.org: ...)

immediately - no `TRAP`, no `dongle is not responding`, no corrupted-image message. The
nexmon suffix in the version string is itself a correctness proof for `VERSION_PTR_1..4`/
`DATE_PTR`/`TIME_PTR` and the patch region placement, since it can only appear if those
four pointer slots actually got overwritten to point at the patch's own version string
and the patch/ucode/flash-patch regions didn't corrupt each other.

### Live ioctl validation without needing a raw memory-read ioctl this time

The `wlc_ioctl_hook`'s own existing debug cases (`510`: `argprintf("%s\n",
__FUNCTION__)`, `511`: walk `wlc->dumpcb_head` and dump each registered debug callback)
turned out to be a complete, zero-extra-code way to validate the ioctl hook live:
`nexutil -Iwlan1 -g510 -l64 -r` returning exactly `"wlc_ioctl_hook"` proves the
`GenericPatch4` slot, `argprintf`, and the ioctl dispatch path all work; `-g511` returning
real structured VHT capability info proves `bcm_binit`/`bcm_bprintf` and the `struct
wlc_info` layout are correct against the live chip, not just the static image. Didn't
need the 43430a1 session's `case 603` manual-memcpy ROM-dump technique at all for this
port - useful to remember that a full raw-memory-read ioctl is not always necessary if
the existing patch source already has a debug ioctl case that touches the addresses in
question.

**Gotcha carried over from the 43430a1 notes and reconfirmed here**: `nexutil`'s default
`-l` (buffer length) is 4 bytes and silently truncates output with no error. Always pass
`-l` explicitly and generously (e.g. `-l64`, `-l512`) - the first `-g510` attempt without
it returned `"wlc "`, which looks like a working-but-wrong-string false negative rather
than the truncation it actually was.

Also needed for `nexutil` to build at all on this Debian-13-based Pi OS: not just
`libnexio` (as the 43430a1 notes say) but `utilities/libargp` too -
`utilities/nexutil/nexutil.c` includes `argp-extern.h` from there even on the native
(non-Android) Linux build path, and that header doesn't exist anywhere else in the tree.

### Monitor mode and injection both worked, following the 43430a1 vif-ordering lesson

`/home/pi/mon.sh` on the device does, in order: `rfkill unblock all` -> `wlan1` up ->
read the phy name from `/sys/class/net/wlan1/phy80211/name` (it increments every reload,
same as the 43430a1 notes warn) -> `iw phy $PHY interface add wlan1mon type monitor` ->
`wlan1mon` up -> `wlan1` down -> `nexutil -Iwlan1mon -m2`. Same order the 43430a1 session
established as mandatory (vif before mode, or the driver refuses the interface). Worked
first try: a `tcpdump` capture decoded correct band/rate/frequency, plausible signal
levels, sane 802.11 frame types, and - on the AP's channel - beacons with the real SSID
in the clear. Cross-checked against an independent `iw dev wlan0 scan` on the mt7921:
same BSSID, same frequency. None of the 43430a1 port's monitor-mode defects (all-zero
MACs from a bad `memcpy` relocation, wrong RSSI from a `struct wl_rxsts` layout mismatch)
appeared here.

### A misleading "injection is broken" false start - caused by RF setup, not the port

First injection test (craft a probe request with scapy, `sendp()` it out `wlan1mon`,
listen on a second monitor vif `wlan0mon` on the mt7921) showed **zero** received frames,
which looked exactly like the kind of firmware-glue bug the 43430a1 port had for
`wl_send`. Root-caused with a *counter*, not a console read: this kernel doesn't have
`/sys/kernel/debug/dynamic_debug/control` available, so the driver's own
`pr_debug("CONSOLE: %s\n", ...)` firmware-console poll (`brcmf_sdio_readconsole` in
`sdio.c`) can't be turned on to see `printf()` output from patch code - the 43430a1
session's live-printf method doesn't transfer to every kernel build. Instead, added two
global counters (`wl_send_hook` calls, `inject_frame` calls) plus a spare debug ioctl
case (`512`) to read them, reusing the *already-proven* `case 510`/`511` ioctl mechanism
instead of chasing console access. Result: **both counters incremented 1:1 with every
injected frame**, before any RF was even involved - meaning the entire software path
(`wl_send_hook` -> `inject_frame` -> `sendframe`) was firing correctly on every attempt.
The actual cause was mundane: a fresh `wlan1mon` after a firmware reload defaults to
channel 36, while `wlan0mon` (inheriting from the already-associated `wlan0`) was on
channel 40 - the two monitor vifs were simply not listening on the same channel. Aligning
both to channel 40 made all 20/20 injected frames arrive at the independent capture
immediately.

**Takeaway for the next session**: when a live capture-based test shows nothing, check
both endpoints' channel *before* suspecting the port. A counter-based liveness check
(increment a global in the suspect hook, read it back via a spare debug ioctl case) is a
fast, low-risk way to bisect "did my code run" from "did the RF actually work" without
needing console access, and it reuses infrastructure (`argprintf`/custom ioctl cases)
that's already proven to work rather than adding a new untested mechanism. The debug
counters and ioctl case were removed and the clean image was rebuilt, redeployed, and
fully re-verified (ioctl/monitor/injection all retested) before this was called done -
never leave debug-only instrumentation in a build that hasn't been separately confirmed
without it.

### Session end state (2026-08-28): full port working, hardware-verified

Working and verified live on hardware, on the final committed (debug-instrumentation-
free) build: firmware boots with the correct patched version string; `wlc_ioctl_hook`
confirmed via two independent debug ioctl cases; monitor-mode RX with correct radiotap
decode, cross-checked against an independent capture device; frame injection with 20/20
delivery, also cross-checked independently. `wlc_monitor_amsdu_patch` (A-MSDU frame
suppression) is the one remaining gap - its address could not be relocated from either
206 or 189 (relative-branch body, zero signature hits at any length) and is shipped
disabled rather than guessed; see the `TODO(Stage 6)` comment in `monitormode.c`. It only
affects A-MSDU frame handling in monitor mode, not RX/injection generally.

## bcm43455c0 / 7.45.265 — DKMS-free driver, and the A-MSDU crash that looked like a channel-hopping bug

Follow-up session (2026-08-28) checking four things explicitly asked for: the nexmon
firmware buildable without DKMS, and that channel hopping and RX-FIFO-overflow - both
real problems on the `bcm43430a1` port - are not present here.

### The loaded driver was a third-party DKMS package, not this repo's - always check build-id

Monitor mode and injection had worked perfectly in the previous session, but that was
riding entirely on a pre-installed `brcmfmac-nexmon-dkms` package (`dkms status` showed
`brcmfmac-nexmon/6.18.39+ndevfix1 ... installed`) - nothing from `patches/driver/` in
this repo had actually been built or loaded yet. Easy to miss, since the symptom (monitor
mode/injection working) looks identical either way.

**How to tell them apart**: `sudo cat /sys/module/brcmfmac/notes/.note.gnu.build-id | xxd`
against `readelf -n <your-built.ko>` - the GNU build-id is a content hash, so an exact
match is a hard guarantee the loaded module is byte-for-byte your build, not just
"probably the same source." `modinfo` wasn't installed on this Pi OS image, so this
xxd-vs-readelf comparison is the fallback.

**Building it (no DKMS)**: the driver tree at `patches/driver/brcmfmac_<KERNEL>.y-nexmon`
is a complete, ordinary out-of-tree kernel module - it builds directly against the
running kernel's headers:

    ARCH=arm64 make -C /lib/modules/$(uname -r)/build M=$(pwd) -j4

on the Pi itself (native aarch64, needs `linux-headers-<version>` installed - already
present here). No cross-compiler, no nexmon gcc plugin, no DKMS wrapper - that machinery
is only for the *firmware* half (`patches/<chip>/<fwver>/nexmon`'s `RAM_FILE` target),
never the driver. It built clean on the first attempt against this repo's existing
`patches/driver/brcmfmac_6.18.y-nexmon` source.

**Making it load automatically (persistence without DKMS)**: `depmod`'s module search
order prefers `/lib/modules/<kver>/updates/dkms/` over `/lib/modules/<kver>/kernel/...`,
so `modprobe brcmfmac` was resolving to the DKMS copy specifically, not just "a"
brcmfmac. Backed up that file, replaced it with an `xz`-compressed copy of the freshly
built `.ko` at the exact same path, ran `depmod -a`, then proved persistence isn't just
theoretical by `rmmod`ing and re-`modprobe`-ing (the same command a boot-time
`systemd-udevd` triggers) and re-checking the build-id - it resolved to the new file
correctly. A real reboot wasn't done (that's the one thing this doesn't prove), but the
mechanism (`depmod`-registered path + module search order) is exactly what a reboot
relies on too. **Caveat worth remembering**: if `dkms`/`apt` ever rebuild the DKMS
package again (e.g. on a kernel upgrade), its postinst hook will overwrite this file back
to the original - this is a one-time proof of DKMS-independence, not a permanent
uninstall of the DKMS package.

### A firmware crash that looked exactly like a channel-hopping bug, and wasn't one

First channel-hop test (cycling 2.4GHz/5GHz channels while capturing) crashed the
firmware within two hops:

    ieee80211 phy: brcmf_fw_crashed: Firmware has halted or crashed
    ...
    brcmfmac: dongle trap info: type 0x1 @ epc 0x002280b8
    lr 0x00228f65 ...

**The trap PC is not where the bug is - it's downstream data corruption.** Disassembling
around `epc 0x2280b8` showed a mechanical, repeating `ldrh/movs/movs/movs` pattern with
no coherent control flow - the classic tell (see the byte-signature section earlier in
this file) that execution landed in data, not code. Trap type `0x1` is exactly what an
illegal-instruction/PC-into-garbage fault looks like on this core.

**`lr` is what actually matters here.** `0x228f65 & ~1 = 0x228f64` (the low bit is the
Thumb-mode marker on a `bl` return address, not part of the address) is the instruction
immediately after a `bl 0x19a0f8` - the *already independently-confirmed* `memcpy`
relocation from the previous session. Disassembling the function containing that call
showed it was `hnd_pkt_dup_hook` from `monitormode.c`, matched instruction-for-instruction
against the C source (`lb_alloc(p->len + sizeof(radiotap_hdr), 0)`, then
`memcpy(pnew->data, p->data, p->len)`, then `osh->pktalloced++`) - i.e. a real,
correctly-compiled function, not a stub. The crash is consistent with `memcpy` being
handed a corrupt/huge `p->len` for some A-MSDU sub-frame and smashing adjacent memory
(most likely the stack, given the eventual PC-into-garbage symptom on an unrelated later
return).

**This is exactly the bug `wlc_monitor_amsdu_patch` exists to route around** - the
"Temporary fix to ignore A-MSDU frames" comment inherited from 206/189's source, whose
address for 7.45.265 hadn't been relocated (byte-signature search returns nothing, same
as for 206→265, because the block contains relative branches - see the earlier session's
note on this in the same file). It had been left disabled and commented out as
seemingly-low-risk ("only affects A-MSDU handling"). **It is not low-risk** - it is what
stands between ordinary A-MSDU-aggregated traffic (common on any modern 5GHz/VHT
network) and a firmware crash, and it just happens to look like a channel-hopping bug
because different channels expose different traffic mixes, not because hopping itself is
implicated.

**Found by tracing forward from the ROM side, not by re-trying byte signatures.** The
flashpatch redirect target for `hnd_pkt_dup_hook` (`0x25AF2`) is a ROM address and
therefore chip-constant - identical across every firmware version of this stepping. Its
*caller* (inside `wlc_monitor_amsdu`, in RAM) actually calls a small ROM thunk near it
(`0x25AE8` in this chip, also chip-constant), so grepping the *265* RAM disassembly for
`bl 0x25ae8` finds the call site directly, no signature matching needed:

    206:  0x1b2a46: bl 0x25ae8   (patch site)   ->  0x1b2a62 (skip target), delta 0x1C
    265:  0x1b3e6a: bl 0x25ae8   (patch site)   ->  0x1b3e86 (skip target), delta 0x1C

The surrounding ~40 bytes of code are byte-identical in relative layout between the two
versions, and the landing instruction at the skip target (`8a33  ldrh r3,[r6,#16]`) is
*literally* the same encoding in both - about as strong a confirmation as this kind of
relocation gets. Fixed:
`__attribute__((at(0x1B3E6A, ...))) BPatch(wlc_monitor_amsdu_patch, 0x1B3E86);`

**General technique worth keeping**: when byte-signature relocation fails because a
block contains relative branches, and the block's job is "call some chip-constant ROM
address," search the *target* firmware's disassembly for calls to that ROM address
directly - it sidesteps the relative-branch problem entirely, since the ROM address
being called doesn't move.

### Channel hopping and RX-FIFO-overflow: neither reproduced, once the actual bug was fixed

After the A-MSDU fix: 56 channel hops (8 full cycles across `1 6 11 36 40 44 48`,
`iw ... set channel` + a live capture + an ioctl-liveness check after every single hop)
completed with **zero failures and zero crashes** - `TOTAL_FRAMES=3667 FAILED_HOPS=0`. A
separate sustained 45-second capture on the busiest channel (40) with no hopping
delivered **14,222 frames, 0 dropped by kernel, 0 dmesg errors** - a volume more than two
orders of magnitude past the "~40 frames" invariant that reliably wedged `bcm43430a1`
(see the extensive investigation earlier in this file). Neither the driver-level
`macintstatus`-spin wedge nor any RX-FIFO-overflow symptom appeared under either test.
Whether that's because this chip's D11 core doesn't share `bcm43430a1`'s buffer-pool
exhaustion bug, or because this port's RX/monitor path just doesn't hit whatever
triggered it there, wasn't determined - not needed, since nothing wedged.

**One more false alarm worth flagging for next time**: attempting to hop to channels
149/153/157/161 during testing failed with `kernel reports: (extension) channel is
disabled` / `command failed: Invalid argument (-22)`. This looked like it could be
another hopping bug, but `iw reg get` showed the regulatory domain is `NL` (DFS-ETSI),
which doesn't clear those channels for this device without a DFS CAC scan first - the
rejection happens in the kernel's `cfg80211` layer, before the command ever reaches the
firmware. **Always check `iw reg get` before treating an "unavailable channel" as a
driver or firmware bug** - regulatory-domain restrictions and real hopping wedges produce
superficially similar "can't get onto this channel" symptoms but are completely
different layers.

### Auto-recovery is real and worth trusting

Before the A-MSDU fix was found, the very first crash auto-recovered on its own: the
driver detected the halted firmware, ran its own SDIO `mmc_hw_reset` sequence (5 attempts,
all individually reported "failed" in dmesg, which reads alarmingly but is normal - the
resets are what re-triggers card re-enumeration), and the card came back
(`mmc1: card 0001 removed` -> `new high speed SDIO card` -> firmware re-attached with the
correct patched version string) with **no `rmmod`/`modprobe` needed, and no physical power
cycle** - a meaningfully better failure mode than `bcm43430a1`'s SDIO wedges, which
sometimes needed exactly that. Don't assume a crash means the session is over; check
`ip -br link` and retry a liveness ioctl before reaching for recovery escalation.
