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
#include <version.h>            // version information
//#include <bcmpcie.h>
#include <argprintf.h>          // allows to execute argprintf to print into the arg buffer

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

        case 0x603: // read from memory
            {
                memcpy(arg, *(char **) arg, len);
                ret = IOCTL_SUCCESS;
            }
            break;

        /* ---- stress-test diagnostics (see PORTING_STATUS.md, "Known issue
         * classes to prepare for"). bcm43436b0 is a 43430-family part, so the
         * bcm43430a1/7.45.98 RX-buffer-leak -> RXOV -> hop-wedge cascade is the
         * one to watch. These mirror that port's ioctl.c cases 604/612. Left
         * in for bring-up; strip before declaring the port done. ---- */

        case 604: // live D11 core regs: maccontrol, maccommand, macintstatus, macintmask
            /* macintstatus bit 8 (MI_RXOV, 0x100) latching = RX FIFO overflow.
             * Read-only; wlc->regs is the memory-mapped d11 core. */
            if (len >= 16) {
                volatile unsigned int *out = (volatile unsigned int *) arg;
                out[0] = wlc->regs->maccontrol;
                out[1] = wlc->regs->maccommand;
                out[2] = wlc->regs->macintstatus;
                out[3] = wlc->regs->macintmask;
                ret = IOCTL_SUCCESS;
            }
            break;

        case 612: // self-validating health sample: read-only osh counters
            /* out[0] is a magic word written FIRST - if the host does not read
             * "NEX1" back, the firmware never ran this and the whole sample is
             * noise (a timed-out ioctl returns the host's own zeroed buffer,
             * which is indistinguishable from a real zero). See the long note
             * in bcm43430a1/7_45_98/nexmon/src/ioctl.c case 612.
             *
             * Pure read of the osh header words. On this chip the packet-pool
             * "alloced" count lives at osh+0 and a pktalloced pointer at osh+4
             * (from disassembly of pkt_buf_get_skb 0x807c48 / the ROM free at
             * ~0x807bd8). A steady climb of the alloced count across a hop loop
             * == the RX buffer leak. Deliberately does NOT call
             * pkt_buf_get_skb/free_skb: pkt_buf_free_skb has no wrapper entry
             * for this chip yet, so calling it would jump into a reclaimed-RAM
             * stub and wedge. Add the wrapper (ROM free near 0x807bd8) before
             * reinstating an alloc/free probe. */
            if (len >= 32) {
                volatile unsigned int *out = (volatile unsigned int *) arg;
                volatile unsigned int *osh = (volatile unsigned int *) wlc->osh;
                out[0] = 0x4E455831; /* "NEX1" */
                out[1] = osh[0];
                out[2] = osh[1] ? ((volatile unsigned int *) osh[1])[0] : 0;
                out[3] = osh[2];
                ret = IOCTL_SUCCESS;
            }
            break;

        default:
            ret = wlc_ioctl(wlc, cmd, arg, len, wlc_if);
    }

    return ret;
}

// TODO(9_88_4_77): 0x4BA90 relocated from 9_88_4_65 0x4B274 - it is the unique
// aligned slot holding &wlc_ioctl|1 (0x8302D5) and also matches a 16-byte
// signature. Confirm on device (dump the dword at 0x4BA90).
__attribute__((at(0x4BA90, "", CHIP_VER_BCM43436b0, FW_VER_9_88_4_77)))
GenericPatch4(wlc_ioctl_hook, wlc_ioctl_hook + 1);
