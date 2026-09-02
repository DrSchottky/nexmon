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

#include <firmware_version.h>
#include <wrapper.h>	// wrapper definitions for functions that already exist in the firmware
#include <structs.h>	// structures that are used by the code in the firmware
#include <patcher.h>
#include <helper.h>
#include "d11.h"
#include "brcm.h"

//#define RADIOTAP_MCS
#include <ieee80211_radiotap.h>

#define MONITOR_DISABLED  0
#define MONITOR_IEEE80211 1
#define MONITOR_RADIOTAP  2
#define MONITOR_LOG_ONLY  3
#define MONITOR_DROP_FRM  4
#define MONITOR_IPV4_UDP  5

void
wl_monitor_radiotap(struct wl_info *wl, struct wl_rxsts *sts, struct sk_buff *p) {
    struct sk_buff *p_new = pkt_buf_get_skb(wl->wlc->osh, p->len + sizeof(struct nexmon_radiotap_header));
    struct nexmon_radiotap_header *frame;
    struct tsf tsf;

    /* Out of packet buffers: drop this frame, exactly as the vendor's own
     * monitor routines do (RAM 0xa5e2 "cbz r0, 0xa62c" and ROM 0x819510
     * "cbz r0, 0x81955a" both return immediately on a NULL allocation).
     *
     * This matters far more than it looks on this chip: RAMSTART is 0x0, so a
     * NULL dereference does NOT fault - it silently writes the radiotap header
     * over the firmware's own low memory. The result is a chip that keeps
     * receiving but stops answering commands, with every ioctl returning -110
     * and no TRAP to point at the cause. Under sustained pwnagotchi load this
     * wedged the firmware after a few hours.
     */
    if (p_new == 0)
        return;

    frame = (struct nexmon_radiotap_header *) p_new->data;
    wlc_bmac_read_tsf(wl->wlc_hw, &tsf.tsf_l, &tsf.tsf_h);

    frame->header.it_version = 0;
    frame->header.it_pad = 0;
    frame->header.it_len = sizeof(struct nexmon_radiotap_header);
    frame->header.it_present =
          (1<<IEEE80211_RADIOTAP_TSFT)
        | (1<<IEEE80211_RADIOTAP_FLAGS)
        | (1<<IEEE80211_RADIOTAP_CHANNEL)
        | (1<<IEEE80211_RADIOTAP_DBM_ANTSIGNAL);
    frame->tsf.tsf_l = tsf.tsf_l;
    frame->tsf.tsf_h = tsf.tsf_h;
    frame->flags = IEEE80211_RADIOTAP_F_FCS;
    frame->chan_freq = wlc_phy_channel2freq(CHSPEC_CHANNEL(sts->chanspec));
    frame->chan_flags = 0;
    frame->dbm_antsignal = sts->signal;

    memcpy(p_new->data + sizeof(struct nexmon_radiotap_header), p->data + 6, p->len - 6);

    p_new->len -= 6;

    /* Deliver through the firmware's own send-up routine rather than nexmon's
     * usual wl->dev->chained->funcs->xmit() path. On 7.45.98 that path does
     * not return the skb to the pool: under sustained monitor RX the packet
     * buffers are exhausted within ~90s, after which the firmware keeps
     * receiving but stops answering commands (every ioctl -110, no TRAP, no
     * SDIO wedge, recoverable only by reloading the driver).
     *
     * Measured over 5.5 minutes of identical fixed-channel load: the
     * chained-xmit path produced 93 x -110 and a dead set_channel, while
     * routing the same frames through the vendor routine produced zero errors
     * and a still-responsive chip. Both vendor monitor routines end this way -
     * RAM 0xa5e2 "b.w 0xa46c" and ROM 0x819510 "b.w 0x880f10", each called as
     * (wl, NULL, p_new, 1).
     */
    if (wl->wlc->wlcif_list->next)
        wl_sendup_newdrv(wl, wl->wlc->wlcif_list->wlif, p_new, 1);
    else
        wl_sendup_newdrv(wl, 0, p_new, 1);
}

void
wl_monitor_hook(struct wl_info *wl, struct wl_rxsts *sts, struct sk_buff *p) {
    switch(wl->wlc->monitor & 0xFF) {
        case MONITOR_RADIOTAP:
                wl_monitor_radiotap(wl, sts, p);
            break;

        case MONITOR_IEEE80211:
                wl_monitor(wl, sts, p);
            break;

        case MONITOR_LOG_ONLY:
                printf("frame received\n");
            break;

        case MONITOR_DROP_FRM:
            break;

        case MONITOR_IPV4_UDP:
                printf("%s: udp tunneling not implemented\n");
                // not implemented yet
            break;
    }
}

/* 7.45.98 does NOT execute the ROM RX routine that older firmware versions do.
 * The stock flash-patch table for this version contains an entry at 0x81f410 -
 * the entry point of the ROM function spanning 0x81f410..0x81f626 - replacing
 * its prologue with an unconditional "b.w 0xd4e4" into a RAM reimplementation.
 * That ROM function's only exit is the tail call to wl_monitor at 0x81F620, so
 * flashpatching 0x81F620 (as FW_VER_ALL does, and as works fine on 7.45.41.46,
 * whose stock table has no such diversion) patches code that never runs: the
 * hook is silently dead and monitor mode captures nothing.
 *
 * The RAM reimplementation at 0xd4e4 mirrors the ROM function exactly and ends
 * in the same tail call, against a RAM copy of wl_monitor at 0xa5e2:
 *
 *   ROM 0x81f61a  ldr r0,[r5,#8] / mov r1,sp / mov r2,r6 / bl wl_monitor
 *   RAM 0x00d710  ldr r0,[r6,#8] / mov r1,sp / mov r2,r8 / bl 0xa5e2
 *
 * so the correct hook point for this firmware is the RAM call site at 0xd716.
 * This is an ordinary RAM patch, not a flashpatch.
 */
__attribute__((at(0xd716, "", CHIP_VER_BCM43430a1, FW_VER_7_45_98)))
BLPatch(wl_monitor_ram_call, wl_monitor_hook);

/* 7.45.98 skips pkt_buf_free_skb on received frames; NOP the branch that does.
 *
 * The caller of the RX routine ends one of two ways:
 *
 *   1619e:  mov r0,r7 / mov r1,r5 / movs r2,#0
 *   161a8:  b.w 0x6c74              <- pkt_buf_free_skb, returns the buffer
 *   161ac:  ldmia.w sp!, {...,pc}   <- plain return, buffer NOT freed
 *
 * and 7.45.98 adds a branch, immediately after the RX routine returns, that
 * takes the second one:
 *
 *   1609e:  bl 0xd4e4              <- RX routine, ends in the wl_monitor call
 *   160a2:  ldr.w r3,[r4,#0x208]
 *   160a6:  cbz r3, 0x160b2
 *   160a8:  ldr r3,[r4,#0]
 *   160aa:  ldrb.w r3,[r3,#0x3f]
 *   160ae:  cmp r3,#0
 *   160b0:  beq.n 0x161ac          <- skip the free
 *   160b2:  ldrh r3,[r6,#16]       <- normal path, reaches the free
 *
 * 7.45.41.46's equivalent function (0x12c20) has no such branch and always
 * reaches the free, which is why it never wedges.
 *
 * Without this patch the chip dies after ~40 received frames under every
 * condition: fixed channel or hopping, any hop rate, any channel, and in
 * monitor mode 4 where the frame is discarded at the hook - because the leak is
 * in the caller, upstream of anything nexmon does. Once the packet buffers are
 * gone the receive FIFO overflows, MI_RXOV latches in macintstatus, and because
 * MI_RXOV is not in this firmware's interrupt mask the ISR can never
 * acknowledge it, so the overflow storms IRQ 0 and the core never reaches its
 * idle loop again. With the patch: 20,000+ frames on a fixed channel and 60/60
 * channel hops, with no wedge.
 *
 * 0xbf00 is "nop". See REVERSE_ENGINEERING_NOTES.md for the full derivation,
 * including two caveats: the branch condition ([r4+0x208] != 0 &&
 * [[r4]+0x3f] == 0) has not been decoded, so if it marks frames freed
 * elsewhere this would convert a leak into a double free; and osh[0] never
 * reflected the leak, so it does not track this pool.
 */
__attribute__((at(0x160b0, "", CHIP_VER_BCM43430a1, FW_VER_7_45_98)))
GenericPatch2(rx_leak_no_early_exit, 0xbf00);
