# bcm43436b0 / 9.88.4.77 port - status

Remote port session over Tailscale to a friend's Pi Zero 2 W (`testbuild`,
kernel 6.18.39). Plan: `~/.claude/plans/in-theory-i-don-t-federated-moth.md`.
Methodology: `REVERSE_ENGINEERING_NOTES.md` + memory `nexmon-bcm43455c0-265-port`.

## Hardware status (2026-08-29)

- **`rom_extraction` BOOTS and attaches.** `0x602`/`0x603` memory-read ioctls
  work. **ROM dumped** to `firmwares/bcm43436b0/9_88_4_77/rom.bin` (640 KB,
  also copied to `../9_88_4_65/`; gitignored).
- **Every `definitions.mk` value verified** by offline disassembly of the stock
  `.bin` and/or live read - see updated confidence table. FP_CONFIG_*_PTR
  confirmed: stock `.bin` holds `0x1800`/`0x1b10` at `0x407a0`/`0x408e4`.
- **`nexmon` (full monitor+injection) build links clean** after (a) fixing the
  duplicate-`wlc_ioctl` bug and (b) commenting stock `flash_patch_27`.
- Monitor-mode RX proven on this exact Pi/kernel/driver using Kali's
  `firmware-nexmon` 9.88.4.65 binary as a baseline (captures real beacons).
- **Bug fixed - duplicate `wlc_ioctl` wrapper:** `rom_extraction/src/
  local_wrapper.c` redefined `wlc_ioctl` at `0x8302D4`, which is ALSO in
  `patches/common/wrapper.c` (`FW_VER_ALL`, added in a later seemoo merge).
  The dup made the plugin emit a local `RETURN_DUMMY` stub in the reclaimed
  ucode region - the hook's `default: wlc_ioctl(...)` compiled to `b.w 0x60ca0`
  (freed RAM) instead of `b.w 0x8302d4`, hanging attach on the first ioctl.
  Fix: deleted the `wlc_ioctl` block from `local_wrapper.c` (match
  `bcm43455c0/7_45_154`). Verify: `objdump -d gen/patch.elf
  --disassemble=wlc_ioctl_hook` must show `b.w 8302d4`.
- **Open - `pkt_buf_free_skb` has no wrapper entry** for this chip (silent
  stub). Only referenced by `ioctl.c` case 612, which has been rewritten as a
  read-only osh-counter probe so the stub is unreachable. ROM free function is
  near `0x807bd8` (structurally identical to bcm43430a1's `pkt_buf_free_skb`) -
  pin it down and add `AT(CHIP_VER_BCM43436b0, FW_VER_ALL, ...)` before any
  alloc/free probe or A-MSDU path needs it.
- **Driver `ndev_global`**: `patches/driver/brcmfmac_6.18.y-nexmon` is already
  the fixed version (byte-identical to the `brcmfmac-nexmon-dkms` fork). The
  `ndev_global is NULL` message only appears after a firmware crash - not a bug.

## Done offline (this scaffold)

- `FW_VER_9_88_4_77 = 711` registered in `patches/include/firmware_version.h`.
- `firmwares/bcm43436b0/9_88_4_77/`: `Makefile`, `structs.h` (copied from
  9_88_4_65), `derive.py`, `definitions.mk` (all 16 addresses derived).
- `derive.py` self-tests clean: portable subset (ucode / reclaim / templateram)
  against bcm43430a1/7_45_41_46, bcm43430a1/7_45_98, bcm43436b0/9_88_4_65;
  full set against bcm43436b0/9_88_4_65.
- Patch trees `nexmon/` and `rom_extraction/` copied from 9_88_4_65 and every
  version-specific `at()` / wrapper address relocated (table below).
- `patches/common/wrapper.c`: `FW_VER_9_88_4_77` rows added for the 5 RAM
  wrapper functions.

## definitions.mk - confidence

| value | how derived | confidence |
|---|---|---|
| UCODESTART / UCODESIZE | unique ucode-blob magic; size word at UCODESTART-4 | high |
| WLC_UCODE_WRITE_BL_HOOK_ADDR | unique slot (=UCODESTART, prev=UCODESTART-4) - 0x10 | high |
| HNDRTE_RECLAIM_0_END(_PTR) | unique `<end>,0xFC,0x100,0,0` record | high |
| TEMPLATERAMSTART(_PTR) / SIZE | round_up_4(UCODESTART+UCODESIZE), unique literal | high |
| FP_CONFIG_ORIGBASE/ORIGEND | walk (target,dataptr) table from 0x1800 (98 entries) | high |
| FP_DATA_END_PTR | unique literal == 0x1000 + 98*8 | high |
| FP_CONFIG_BASE | align16(image size) - scratch, overwritten after read | high |
| FP_CONFIG_BASE_PTR_1/2 + END_PTR_1/2 | heuristic reproduces 9_88_4_65 but n=1 reference | **VERIFY ON DEVICE** |

`FP_CONFIG_*_PTR` check: after flashing `rom_extraction`, `nexutil -g0x603`
the dwords at 0x407A0/0x407A4/0x408E4/0x408E8 - expect
`0x1800 0x1B10 0x1800 0x1B10`. If not, scan for the real pair(s).

## Relocated patch sites

| file | 9_88_4_65 | 9_88_4_77 | method | confidence |
|---|---|---|---|---|
| nexmon/src/ioctl.c (wlc_ioctl_hook) | 0x4B274 | 0x4BA90 | unique slot &wlc_ioctl\|1 + 16B sig | high |
| nexmon/src/injection.c (wl_send_hook) | 0x3CC10 | 0x3D37C | unique slot &wl_send\|1 | high |
| nexmon/src/sendframe.c (patch_null_pointer_scb) | 0x3489E | 0x353EA | 24B sig + disasm context | medium - mid-function code patch; consider dropping for first bring-up |
| nexmon/src/version.c version/date/time ptrs | 0xA8C0/0xA8CC/0xA8BC | 0xA830/0xA840/0xA82C | string-pointer slots | high |
| nexmon/src/monitormode.c (wl_monitor_hook) | ROM 0x82EA60 | ROM 0x82EA60 (unchanged) | stock flashpatch, index 25 -> 27 | see note below |
| rom_extraction fp_apply_patches | 0x400E8 | 0x40840 | 48B sig, prologue match | high |
| rom_extraction fp_apply_patches_hook (BPatch) | 0x4101A | 0x41772 | same init thunk, +0x758 delta (== fp_apply_patches delta) | high |
| rom_extraction/nexmon wlc_ioctl (ROM) | 0x8302D4 | 0x8302D4 (unchanged) | ROM constant | high |

### wrapper.c RAM functions (FW_VER_9_88_4_77 rows added)

| function | 65 | 77 | sig |
|---|---|---|---|
| wlc_bmac_read_tsf | 0x1B242 | 0x1B550 | 20B |
| wlc_sendctl | 0xBD4C | 0xBCC0 | 24B |
| wl_send | 0xA6D8 | 0xA644 | 32B |
| wl_monitor | 0xAE0A | 0xAD7E | 24B |
| wlc_phy_channel2freq | 0x8644 | 0x85B0 | 32B |

All five have a unique hit and a matching valid Thumb prologue. Deltas are
mutually consistent (wl_send/wl_monitor/wlc_phy_channel2freq/wlc_sendctl all
shift ~-0x90; wlc_bmac_read_tsf is in a different region, +0x30E).

## Must do on/with the device

1. **Generate `firmwares/bcm43436b0/9_88_4_77/flashpatches.c`** - `make` in that
   dir (needs `source setup_env.sh` + buildtools). Then **comment out stock
   `flash_patch_27`** (target 0x0082EA60), exactly as `flash_patch_25` is
   commented in the 9_88_4_65 file - `monitormode.c` overwrites that slot.
2. **Confirm the chip is genuinely bcm43436b0** (`strings` the running firmware
   for `43436b0-roml`, check SDIO device id).
3. **Verify FP_CONFIG_*_PTR** (see above) and the 3 `_PTR` values with a live
   `0x603` read.
4. **ROM dump** via `rom_extraction` + `make dump-rom` -> save to
   `firmwares/bcm43436b0/9_88_4_77/rom.bin` (and `../9_88_4_65/` - ROM is
   chip-constant). Needed for `structs.h` field-offset derivation (Stage 5) and
   to disassemble the monitor RX path / injection path.
5. **Wrapper-gap audit** after first build (`REVERSE_ENGINEERING_NOTES.md`,
   "audit method that finds all of these at once") - relocate anything the
   build compiled as a local stub. Note `pkt_buf_get_skb` is FW_VER_ALL ROM
   0x807C48 here; on bcm43430a1/7.45.98 that class of assumption was wrong and
   needed a RAM address - re-check it.
6. **`struct wl_rxsts`** (`structs.h`) - derive the 9_88_4_77 field layout from
   the stock RX routine's stores (unaligned `str.w [sp,#N]`), don't assume it
   matches 9_88_4_65 / structs.common.h. Guard any change with a `WL_RXSTS_*`
   macro.
7. **`patch_null_pointer_scb` (sendframe.c 0x353EA)** - re-verify by disasm or
   omit for the first injection bring-up.
8. **Relocate `pkt_buf_free_skb` + re-verify `pkt_buf_get_skb`** for
   `FW_VER_9_88_4_77` (TODOs in `patches/common/wrapper.c`) - prerequisite for
   the class-A leak test below.
9. **Stage-7 stress** - run `test/hoptest.sh` and `test/rxsoak.sh` (see
   `test/README.md`). Both must pass clean. If class A shows a leak, disassemble
   the RX caller for the "skip the free" branch and/or swap `monitormode.c` to
   the staged `wl_sendup_newdrv` delivery. See "Known issue classes to prepare
   for" below.

## Known issue classes to prepare for

Two earlier ports in this repo wedged under channel-hop / high-volume RX stress
after passing every quiet test. This chip is in the same family as the worse of
the two, so treat both as expected-until-disproven and test for them explicitly
in Stage 7 (`test/hoptest.sh` + `test/rxsoak.sh`, see `test/README.md`).

### Class A - RX FIFO overflow / `MI_RXOV` hop-wedge (bcm43430a1/7.45.98)

**This is the high-risk one - bcm43436b0 is a 43430-family part.**

- **Cascade:** a received frame is not returned to the packet pool -> pool
  drains in ~40 frames -> RX DMA can't refill -> RX FIFO overflows -> D11
  raises `MI_RXOV` (`macintstatus` **bit 8 = 0x100**, register offset `0x128`
  from the d11 base). `WLRXOV` is not built into this firmware family, so
  `MI_RXOV` is **not in `defmacintmask`** and the ISR spins on it forever:
  CPU pegs at 100%, `macintstatus` latches `0x100` permanently, every ioctl
  then returns `-110` (ETIMEDOUT), no TRAP. A soft `rmmod`/`modprobe` does
  **not** clear it; only a power cycle does (hence: friend present).
- **Root cause on 7.45.98:** the RX caller (RAM `0x1609e`) grew a branch that
  skips the `b.w pkt_buf_free_skb` tail (`0x160b0: beq.n` over the free) when
  `[r4+0x208] != 0 && [[r4]+0x3f] == 0`. 7.45.41.46 has no such branch.
- **Fix on 7.45.98:** NOP that one branch -
  `__attribute__((at(0x160b0, "", CHIP_VER_BCM43430a1, FW_VER_7_45_98)))
  GenericPatch2(rx_leak_no_early_exit, 0xbf00);`. Took rx from ~1 frame/5s to
  ~90-500 frames/s, 72k frames no wedge, 60/60 hops clean.
- **What to do for 9_88_4_77 (on device, needs ROM dump + disasm):**
  1. Disassemble the RX caller (the function that `bl`s into the routine
     ending in the `wl_monitor` call - find it via the `wl_monitor_hook`
     flashpatch at ROM `0x82EA60` and walk callers). Check for the analogous
     "skip the free" branch. If present, NOP it under `FW_VER_9_88_4_77` and
     record the address here.
  2. **Also suspect our own hook:** `wl_monitor_radiotap` allocates `p_new`
     every frame and delivers it via `chained->funcs->xmit` (see the big
     comment in `monitormode.c`). On 7.45.98 that hand-off itself leaked; the
     cure was `wl_sendup_newdrv(wl, wlif, p_new, 1)`. A commented-out
     alt-delivery block is staged in `monitormode.c` - swap to it if the stock
     RX path checks out clean but the leak still shows with our hook active.
  3. `pkt_buf_free_skb` has **no `CHIP_VER_BCM43436b0` wrapper row** (it will
     silently stub). Must be relocated from the stock `.bin` before any of the
     above - there's a TODO in `patches/common/wrapper.c`. Same for
     `pkt_buf_get_skb` (currently a `FW_VER_ALL` ROM guess `0x807C48` -
     re-verify; on 7.45.98 that class of assumption was wrong).
- **Instrumentation already staged:** `nexmon/src/ioctl.c`
  - `case 604` -> `maccontrol / maccommand / macintstatus / macintmask`.
    Watch `out[2] & 0x100`.
  - `case 612` -> `"NEX1"` magic word, `osh[0]` held-buffer count, and a
    64-deep alloc/free probe. A steady decay of `out[2]` across a hop loop is
    the leak. (Depends on the two pkt_buf wrappers above - the probe's own
    free loop is a no-op until `pkt_buf_free_skb` is relocated, so early runs
    leak 64 buffers per call: run sparingly until then.)

### Class B - crash that masquerades as a hop bug (bcm43455c0/7.45.265)

- **Symptom:** looked like a channel-hop failure; was actually an A-MSDU frame
  smashing memory. `wlc_monitor_amsdu_patch` (A-MSDU suppression in monitor
  mode) was left disabled because its address couldn't be relocated, so an
  A-MSDU subframe hit `memcpy` with a corrupt `p->len` -> heap smash -> TRAP
  several frames later, on whatever channel next carried A-MSDU traffic.
- **Lesson:** different channels carry different traffic mixes, so a
  content-triggered bug presents as "fails after N hops". Before blaming the
  hop path, run a **fixed-channel high-volume soak** (`test/rxsoak.sh`) on a
  busy channel - if that also dies, it's content, not hopping.
- **For 9_88_4_77:** `wlc_monitor_amsdu_patch` is not currently ported. If the
  soak TRAPs on a channel with 802.11n A-MSDU traffic, that's the likely
  cause - relocate it (or, cheaper for bring-up, filter A-MSDU frames in
  `wl_monitor_radiotap` by inspecting the QoS control A-MSDU-present bit).

### Class C - false alarms / diagnostic discipline

- **Timed-out ioctl returns the host's own zeroed input buffer** - a real zero
  and a dead firmware look identical. Every probe ioctl writes a magic word
  (`0x4E455831` "NEX1") into `out[0]` **first**; if the host doesn't read it
  back, discard the whole sample.
- **`struct d11regs` is `__attribute__((packed))`** -> GCC lowers a 32-bit
  register store into 4 byte stores, which kills the SDIO card. `case 604`
  only *reads* (reads come back coherent in practice), so it's fine as-is. If
  any future case needs to *write* a d11 register, use
  `#define D11REG(regs,off) (*(volatile unsigned int *)((unsigned int)(regs)+(off)))`
  - do not assign through the packed struct field.
- **`iw reg get` / regulatory-domain noise** and the driver's own
  auto-recovery can both look like a firmware fault - confirm with `case 604`
  (`macintstatus` latched at `0x100`) before concluding it's the wedge.
- **`make clean` after changing any `$(FW_PATH)` input** or the image is
  double-patched.
- **`nexutil -l` defaults to 4 bytes** and silently truncates - always pass
  `-l` explicitly and generously (`-l64`, `-l512`).
- **Console `printf()` is not available on every kernel** (no
  `dynamic_debug/control` on Debian-13 Pi OS). Bisect "did my code run" from
  "did RF work" with counters read via a spare ioctl case, not console reads.
- Strip `case 604` / `case 612` (and any counters) and re-verify a clean build
  before calling the port done - never ship debug-only instrumentation in a
  build that wasn't separately confirmed without it.

## 9_88_4_77 vs 9_88_4_65 stock differences found

- 98 stock flash-patch entries (vs 95). The 3 new ROM->RAM diversions:
  `0x805B98`, `0x80D8B8`, `0x87EE48`. None sits in the monitor/injection hook
  path as currently understood, but re-check when disassembling.
- Version string `9.88.4.77` @0x359E4, date `Mar 31 2022` @0x37658.
