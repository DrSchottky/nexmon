# bcm43436b0 / 9.88.4.77 - Stage 7 stress tests

Run these on the friend's Pi over the Raspberry Pi Connect shell, with the
friend physically present (a `MI_RXOV` wedge needs a power cycle - see
`../PORTING_STATUS.md`, "Known issue classes to prepare for", class A).

These scripts are the device-side counterparts of the two failure classes the
earlier ports hit:

| script      | targets                                             |
|-------------|-----------------------------------------------------|
| `reload.sh` | robust driver reload + monitor vif bring-up          |
| `hoptest.sh`| class A - channel-hop loop, per-hop liveness sample  |
| `rxsoak.sh` | class B - fixed-channel high-volume soak             |

## Prerequisites on the device

- The 9.88.4.77 nexmon firmware built and installed (`install-firmware`).
- `nexutil` built (needs `libnexio` **and** `utilities/libargp` on Debian-13
  Pi OS).
- `tcpdump` installed.
- ioctl debug cases 604 and 612 present in the running image
  (`nexmon/src/ioctl.c` - they are in for bring-up, stripped before "done").
- `pkt_buf_free_skb` / `pkt_buf_get_skb` wrapper rows relocated for
  `FW_VER_9_88_4_77` (there are TODOs in `patches/common/wrapper.c`). Until
  then `case 612`'s free loop is a no-op and each call leaks 64 buffers -
  `hoptest.sh` will still show the decay, just don't run hundreds of samples.

Copy this whole dir to `~/nexmon-test/` on the device. `/tmp` is wiped on every
reboot; keep the canonical copy in `$HOME`.

## Interpreting the output

`case 604` -> `maccontrol maccommand macintstatus macintmask` (4x u32).
  **`macintstatus & 0x100` set and staying set = `MI_RXOV` latched = wedge.**

`case 612` -> `NEX1  osh_held  alloc_headroom` (3x u32).
  `NEX1` (0x4e455831) must read back or the sample is noise (timed-out ioctl).
  `alloc_headroom` steadily falling across hops, or `osh_held` steadily
  rising = the RX buffer leak. Stable = healthy.

A clean pass looks like: every hop's chanspec tracks the requested channel,
`macintstatus & 0x100 == 0` throughout, `alloc_headroom` flat (~64), tcpdump
frame count climbing steadily.
