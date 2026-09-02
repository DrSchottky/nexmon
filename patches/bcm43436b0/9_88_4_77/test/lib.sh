# shared helpers for the bcm43436b0/9.88.4.77 stress tests - source this
# nexutil -gN -lL -r  writes L raw bytes to stdout (see REVERSE_ENGINEERING_NOTES
# line 113). We format the u32 words ourselves. If your nexutil build needs
# different flags, this is the one place to fix it.

MON=${MON:-wlan0mon}

# read N u32 little-endian words from ioctl case $1, length $2 bytes
_ioctl_u32() {
    local cmd=$1 len=$2
    nexutil -I"$MON" -g"$cmd" -l"$len" -r 2>/dev/null | od -An -tx4 --endian=little
}

# case 604 -> "maccontrol maccommand macintstatus macintmask"
d11regs() { _ioctl_u32 604 16; }

# case 612 -> "NEX1 osh_held alloc_headroom"
health() { _ioctl_u32 612 32; }

# true if macintstatus (word 3 of case 604) has MI_RXOV (bit 8) set
rxov_latched() {
    local w
    w=$(d11regs)
    local mis
    mis=$(echo "$w" | awk '{print $3}')
    [ -n "$mis" ] && [ $(( 0x$mis & 0x100 )) -ne 0 ]
}

# true if case 612 returned the NEX1 magic (else the sample is noise)
health_valid() {
    local m
    m=$(health | awk '{print $1}')
    [ "$m" = "4e455831" ]
}

# current chanspec as the firmware sees it (sanity vs the requested channel)
cur_chanspec() { nexutil -I"$MON" -k 2>/dev/null; }
