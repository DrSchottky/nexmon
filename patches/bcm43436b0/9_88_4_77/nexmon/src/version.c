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
 * Copyright (c) 2020 NexMon Team                                          *
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
#include <patcher.h>            // macros used to craete patches such as BLPatch, BPatch, ...

char version[] = "9.88.4.77 (nexmon.org: " GIT_VERSION "-" BUILD_NUMBER ")";
char date[] = __DATE__;
char time[] = __TIME__;

// TODO(9_88_4_77): pointer-table slots relocated from 9_88_4_65
// (0xA8C0/0xA8CC/0xA8BC) by matching which slot holds the pointer to the
// version / date / time C-string in stock firmware. In 9_88_4_77 the version
// string is at 0x359E4, date "Mar 31 2022" at 0x37658, time at 0x37664.
__attribute__((at(0xA830, "", CHIP_VER_BCM43436b0, FW_VER_9_88_4_77)))
GenericPatch4(version_patch, version);

__attribute__((at(0xA840, "", CHIP_VER_BCM43436b0, FW_VER_9_88_4_77)))
GenericPatch4(date_patch, date);

__attribute__((at(0xA82C, "", CHIP_VER_BCM43436b0, FW_VER_9_88_4_77)))
GenericPatch4(time_patch, time);
