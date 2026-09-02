/***************************************************************************
 *                                                                         *
 *          ###########   ###########   ##########    ##########           *
 *         ############  ############  ############  ############          *
 *         ##            ##            ##   ##   ##  ##        ##          *
 *         ##            ##            ##   ##   ##  ##        ##          *
 *         ###########   ####  ######  ##   ##   ##  ##    ######          *
 *          ###########  ####  #       ##   ##   ##  ##    #    #          *
 *                   ##  ##    ######  ##   ##   ##  ##    #    #          *
 *                   ##  ##    #       ##   ##   ##  ##    #    #          *
 *         ############  ##### ######  ##   ##   ##  ##### ######          *
 *         ###########    ###########  ##   ##   ##   ##########           *
 *                                                                         *
 *            S E C U R E   M O B I L E   N E T W O R K I N G              *
 *                                                                         *
 * This file is part of NexMon.                                            *
 *                                                                         *
 * Copyright (c) 2016 NexMon Team                                          *
 *                                                                         *
 * NexMon is free software: you can redistribute it and/or modify          *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation, either version 3 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * NexMon is distributed in the hope that it will be useful,               *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License       *
 * along with NexMon. If not, see <http://www.gnu.org/licenses/>.          *
 *                                                                         *
 **************************************************************************/

#pragma NEXMON targetregion "patch"

#include <firmware_version.h>   // definition of firmware version macros
#include <debug.h>              // contains macros to access the debug hardware
#include <wrapper.h>            // wrapper definitions for functions that already exist in the firmware
#include <structs.h>            // structures that are used by the code in the firmware
#include <helper.h>             // useful helper functions
#include <patcher.h>            // macros used to craete patches such as BLPatch, BPatch, ...
#include <rates.h>              // rates used to build the ratespec for frame injection
#include <nexioctls.h>          // ioctls added in the nexmon patch
#include <capabilities.h>       // capabilities included in a nexmon patch
#include <sendframe.h>          // sendframe functionality
#include <argprintf.h>

/* Injection accounting counters, defined in injection.c. */
extern unsigned int nex_inject_calls;
extern unsigned int nex_inject_sent;

int
wlc_ioctl_hook(struct wlc_info *wlc, int cmd, char *arg, int len, void *wlc_if)
{
    argprintf_init(arg, len);
    int ret = IOCTL_ERROR;

    switch (cmd) {
        case NEX_GET_CAPABILITIES:
            if (len == 4) {
                memcpy(arg, &capabilities, 4);
                ret = IOCTL_SUCCESS;
            }
            break;

        case NEX_WRITE_TO_CONSOLE:
            if (len > 0) {
                arg[len-1] = 0;
                printf("ioctl: %s\n", arg);
                ret = IOCTL_SUCCESS;
            }
            break;

        case 600: // raw memory read via ROM memcpy: arg[0:4] holds source address, returns len bytes from *addr
            memcpy(arg, *(char **) arg, len);
            ret = IOCTL_SUCCESS;
            break;

        case 601: // sanity: unconditionally write a known constant into arg
            ((unsigned int *) arg)[0] = 0xDEADBEEF;
            ret = IOCTL_SUCCESS;
            break;

        case 602: // sanity: read-modify-write arg itself, no external memory touched
            ((unsigned int *) arg)[0] = ((unsigned int *) arg)[0] + 1;
            ret = IOCTL_SUCCESS;
            break;

        case 603: // raw memory read via manual byte loop, no ROM memcpy dependency
            {
                volatile unsigned char *src = *(unsigned char **) arg;
                volatile unsigned char *dst = (unsigned char *) arg;
                int i;
                for (i = 0; i < len; i++)
                    dst[i] = src[i];
                ret = IOCTL_SUCCESS;
            }
            break;

        case 604: // dump live D11 core registers, but only when the core is powered
            /* Reading wlc->regs reaches the d11 core across the backplane. That
             * is only safe while the core is out of reset and clocked: issue it
             * against an idle radio and the backplane access hangs, taking the
             * whole chip with it. Measured on hardware - a single 604 against a
             * freshly booted chip with wlan0 up but the radio never enabled:
             *
             *   [90.210547] calling brcmf_fil_cmd_data_get, cmd: 604
             *   [90.220244] brcmfmac: brcmf_sdio_isr: failed backplane access
             *
             * Ten milliseconds, no traffic, no injection, no hopping. From then
             * on every ioctl times out, F1 reads 0xffffffff instead of
             * 0x1541a9a6, brcmf_chip_recognition rejects the chip, and a
             * modprobe cycle cannot bring it back - only a power cycle can.
             * That is the same terminal signature this file has been attributing
             * to the hop/RX wedge, which makes an unguarded 604 in a sampling
             * loop indistinguishable from the bug it was added to measure.
             *
             * wlc->hw->up is the same predicate sendframe() already gates on.
             * Layout changed to carry the NEX3 magic word and that predicate, so
             * "the core was down so we did not look" is distinguishable from
             * "the registers really read zero" and from a timed-out reply.
             */
            if (len >= 24) {
                unsigned int *out = (unsigned int *) arg;
                out[0] = 0x4E455833; /* "NEX3" - sample is valid */
                out[1] = wlc->hw->up;
                out[2] = out[3] = out[4] = out[5] = 0;
                if (wlc->hw->up) {
                    out[2] = wlc->regs->maccontrol;
                    out[3] = wlc->regs->maccommand;
                    out[4] = wlc->regs->macintstatus;
                    out[5] = wlc->regs->macintmask;
                }
                ret = IOCTL_SUCCESS;
            }
            break;

        case 606: // probe how many packet buffers the firmware can still hand out
            /* Allocates until pkt_buf_get_skb() fails, reports the count, then
             * frees every one again. Used to test whether channel hopping leaks
             * packet buffers: sample this before, during and after hopping and
             * watch whether the available count decays. Bounded at 64 so this
             * can never itself exhaust the pool. */
            if (len >= 4) {
                void *bufs[64];
                int n = 0, got;
                for (n = 0; n < 64; n++) {
                    bufs[n] = pkt_buf_get_skb(wlc->osh, 128);
                    if (bufs[n] == 0)
                        break;
                }
                got = n;
                while (n-- > 0)
                    pkt_buf_free_skb(wlc->osh, bufs[n], 0);
                ((unsigned int *) arg)[0] = got;
                ret = IOCTL_SUCCESS;
            }
            break;

        case 607: // size-dependent buffer probe: how many buffers of each size are left
            /* Reports four counts - for 128, 512, 1024 and 1800 byte requests -
             * each measured by allocating until failure and then freeing every
             * one again. A fixed lbuf pool gives the same count for all four
             * sizes; a heap-backed allocator gives counts that fall as the size
             * rises, and a leak of large buffers then shows up as a decay in the
             * 1800-byte count long before the 128-byte count moves at all. That
             * distinction matters here: channel hopping leaves the 128-byte
             * count pinned at its healthy value for several hops and then drops
             * it straight to zero, which is what a large-buffer leak against a
             * shared heap looks like from a small-buffer probe. */
            if (len >= 16) {
                static const int sizes[4] = { 128, 512, 1024, 1800 };
                void *bufs[48];
                int s, n;
                for (s = 0; s < 4; s++) {
                    for (n = 0; n < 48; n++) {
                        bufs[n] = pkt_buf_get_skb(wlc->osh, sizes[s]);
                        if (bufs[n] == 0)
                            break;
                    }
                    ((unsigned int *) arg)[s] = n;
                    while (n-- > 0)
                        pkt_buf_free_skb(wlc->osh, bufs[n], 0);
                }
                ret = IOCTL_SUCCESS;
            }
            break;

        case 608: // read the firmware's own outstanding-packet counter
            /* The ROM allocator at 0x808744 increments osh[0] on every
             * successful packet allocation and pkt_buf_free_skb decrements it
             * once per non-cloned lbuf it releases, so osh[0] is the number of
             * packet buffers the firmware currently holds. Reading it directly
             * beats probing the pool: the probe only reports how many more
             * buffers of one size can still be handed out, which stays flat
             * until the underlying memory is gone and then collapses, whereas
             * this counter rises the moment something stops freeing.
             *
             * Also returns the two counters that pkt_buf_get_skb keeps for its
             * fallback reserve pool (0x41bf0 = served from reserve, 0x41bf4 =
             * reserve empty too), which tell us whether the main pool is
             * already failing before the probe notices. */
            if (len >= 16) {
                unsigned int *out = (unsigned int *) arg;
                /* Launder the two literal addresses through volatile ints:
                 * dereferencing an integer constant directly trips gcc's
                 * -Warray-bounds, which this tree builds with -Werror. */
                volatile unsigned int ctr_addr = 0x41bf0;
                volatile unsigned int rsv_addr = 0x774;
                volatile unsigned int *ctr = (volatile unsigned int *) ctr_addr;
                volatile unsigned int *rsv = (volatile unsigned int *) rsv_addr;
                out[0] = ((unsigned int *) wlc->osh)[0];
                out[1] = ctr[0];
                out[2] = ctr[1];
                out[3] = rsv[0];
                ret = IOCTL_SUCCESS;
            }
            break;

        case 609: // validate osh[0] as an outstanding-packet gauge
            /* Reads the counter before, during and after holding 16 packet
             * buffers, and returns the osh pointer itself. If osh[0] really is
             * the allocator's live count it must read 16 higher in the middle
             * sample; if it does not move, wlc->osh is not the structure the
             * ROM allocator increments and the gauge has to be found elsewhere. */
            if (len >= 16) {
                unsigned int *out = (unsigned int *) arg;
                volatile unsigned int *osh0 = (volatile unsigned int *) wlc->osh;
                void *bufs[16];
                int n;
                out[0] = osh0[0];
                for (n = 0; n < 16; n++)
                    bufs[n] = pkt_buf_get_skb(wlc->osh, 128);
                out[1] = osh0[0];
                while (n-- > 0)
                    if (bufs[n])
                        pkt_buf_free_skb(wlc->osh, bufs[n], 0);
                out[2] = osh0[0];
                out[3] = (unsigned int) wlc->osh;
                ret = IOCTL_SUCCESS;
            }
            break;

        case 610: // walk the firmware heap free list
            /* The packet allocator is heap-backed, not a fixed pool: RAM 0x2cf0
             * (reached from pkt_buf_get_skb via the ROM thunk at 0x880ba0,
             * which jumps through the RAM pointer table entry at 0x488) sizes
             * the request, rejects anything over 0x838 bytes, and then calls
             * malloc at 0x25e4. A NULL from that malloc is the only reason
             * pkt_buf_get_skb fails while osh[0] - the count of packets the
             * firmware currently holds - reads zero, which is exactly what
             * channel hopping produces. So the thing to watch is the heap, not
             * the packet count.
             *
             * malloc's free list is a singly linked list of {size, next} blocks
             * whose head node lives at 0x41a4c (the constant returned by the
             * accessor at 0x25b4); the head itself carries no block, so the
             * walk starts at head->next. Also returns malloc's own failure
             * counter (0x728) and lb_alloc's (0x41af0), which distinguish "heap
             * had nothing left" from "the request was rejected before malloc".
             */
            if (len >= 20) {
                unsigned int *out = (unsigned int *) arg;
                volatile unsigned int head_addr = 0x41a4c;
                volatile unsigned int mfail_addr = 0x728;
                volatile unsigned int lbfail_addr = 0x41af0;
                unsigned int *node = ((unsigned int **) head_addr)[1];
                unsigned int total = 0, count = 0, biggest = 0, guard = 0;

                while (node != 0 && guard++ < 4096) {
                    unsigned int sz = node[0];
                    total += sz;
                    count++;
                    if (sz > biggest)
                        biggest = sz;
                    node = (unsigned int *) node[1];
                }
                out[0] = total;
                out[1] = count;
                out[2] = biggest;
                out[3] = ((volatile unsigned int *) mfail_addr)[0];
                out[4] = ((volatile unsigned int *) lbfail_addr)[0];
                ret = IOCTL_SUCCESS;
            }
            break;

        case 611: // dump the raw heap free list
            /* Companion to 610, for telling heap exhaustion apart from free
             * list corruption. Channel hopping takes the list from ~74 kB in
             * 12 blocks straight to 0 bytes in 0 blocks in a single step, and
             * malloc's failure counter never moves - which is what losing the
             * head's next pointer looks like, not what running out of memory
             * looks like. Exhaustion would leave the head intact with a few
             * small blocks still on the list.
             *
             * Returns the head node's first four words followed by up to ten
             * {block address, block size} pairs, so the list can be compared
             * before and after: if the blocks still hold sane sizes but the
             * head no longer points at them, something overwrote the head. */
            if (len >= 96) {
                unsigned int *out = (unsigned int *) arg;
                volatile unsigned int head_addr = 0x41a4c;
                unsigned int *head = (unsigned int *) head_addr;
                unsigned int *node;
                int i;

                out[0] = head[0];
                out[1] = head[1];
                out[2] = head[2];
                out[3] = head[3];
                for (i = 0; i < 20; i++)
                    out[4 + i] = 0;
                node = (unsigned int *) head[1];
                for (i = 0; i < 10 && node != 0; i++) {
                    out[4 + 2 * i] = (unsigned int) node;
                    out[5 + 2 * i] = node[0];
                    node = (unsigned int *) node[1];
                }
                ret = IOCTL_SUCCESS;
            }
            break;

        case 612: // one consolidated, self-validating firmware health sample
            /* Every earlier probe here reported "0" for two very different
             * conditions: the firmware answered and the value really was zero,
             * or the firmware never answered at all and nexutil printed back
             * the buffer it had zeroed before the call. Once the chip stops
             * responding those look identical from the host, which is exactly
             * when the interesting measurements happen - so a run of zeros was
             * being read as "the pool is exhausted" or "the heap is gone" when
             * it only ever meant "no reply". out[0] is a magic word written
             * before anything else is measured: if the host does not read it
             * back, the whole sample must be thrown away rather than believed.
             *
             * Everything is gathered in one call so a single reply gives a
             * consistent picture:
             *   [1..3] heap free bytes, free block count, largest free block
             *   [4]    osh[0], packet buffers the firmware currently holds
             *   [5]    how many 128-byte buffers can still be allocated (<=64)
             *   [6]    malloc's own failure counter (0x728)
             *   [7]    lb_alloc's failure counter (0x41af0)
             */
            if (len >= 32) {
                unsigned int *out = (unsigned int *) arg;
                volatile unsigned int head_addr = 0x41a4c;
                volatile unsigned int mfail_addr = 0x728;
                volatile unsigned int lbfail_addr = 0x41af0;
                unsigned int *node = ((unsigned int **) head_addr)[1];
                unsigned int total = 0, count = 0, biggest = 0, guard = 0;
                void *bufs[64];
                int n;

                out[0] = 0x4E455831; /* "NEX1" - sample is valid */

                while (node != 0 && guard++ < 4096) {
                    unsigned int sz = node[0];
                    total += sz;
                    count++;
                    if (sz > biggest)
                        biggest = sz;
                    node = (unsigned int *) node[1];
                }
                out[1] = total;
                out[2] = count;
                out[3] = biggest;
                out[4] = ((volatile unsigned int *) wlc->osh)[0];

                for (n = 0; n < 64; n++) {
                    bufs[n] = pkt_buf_get_skb(wlc->osh, 128);
                    if (bufs[n] == 0)
                        break;
                }
                out[5] = n;
                while (n-- > 0)
                    pkt_buf_free_skb(wlc->osh, bufs[n], 0);

                out[6] = ((volatile unsigned int *) mfail_addr)[0];
                out[7] = ((volatile unsigned int *) lbfail_addr)[0];
                ret = IOCTL_SUCCESS;
            }
            break;

        case 620: // injection accounting: hook entries vs frames handed to sendframe
            /* calls=0 after injecting means the frames never reached
             * wl_send_hook, so the fault is between the BCDC/SDIO receive path
             * and the hooked pointer at 0x40fe0. calls climbing in step with
             * what was offered means they reach sendframe() and are lost at or
             * below wlc_txfifo. See the comment in injection.c for the hardware
             * measurement that makes this the open question.
             *
             * Writes the magic word first, per the rule this file earned the
             * hard way: an ioctl that can time out must be able to prove it
             * ran, or a wedged chip's zeroed reply gets read as data.
             */
            if (len >= 16) {
                unsigned int *out = (unsigned int *) arg;
                out[0] = 0x4E455832; /* "NEX2" - sample is valid */
                out[1] = nex_inject_calls;
                out[2] = nex_inject_sent;
                out[3] = 0;
                ret = IOCTL_SUCCESS;
            }
            break;

        case 500: // dump wlif list
            {
                struct wlc_if *wlcif = wlc->wlcif_list;

                for (wlcif = wlc->wlcif_list;  wlcif != 0; wlcif = wlcif->next) {
                    char ifname[32];

                    strncpy(ifname, wlcif->wlif == 0 ? wlc->wl->dev->name : wlcif->wlif->dev->name, sizeof(ifname));
                    ifname[sizeof(ifname) - 1] = '\0';

                    argprintf(" \"%s\" 0x%p type=%02x index=%02x flags=%02x\n", ifname, wlcif, wlcif->type, wlcif->index, wlcif->flags);
		}

                ret = IOCTL_SUCCESS;
            }
            break;

        default:
            ret = wlc_ioctl(wlc, cmd, arg, len, wlc_if);
    }

    return ret;
}

__attribute__((at(0x4a8bc, "", CHIP_VER_BCM43430a1, FW_VER_7_45_98)))
GenericPatch4(wlc_ioctl_hook, wlc_ioctl_hook + 1);

