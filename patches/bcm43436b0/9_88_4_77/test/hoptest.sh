#!/bin/bash
# Class A stress: channel-hop loop with a per-hop liveness + leak sample.
# This is the test bcm43430a1/7.45.98 failed - it wedged on hop transitions
# with MI_RXOV latched. A clean run: chanspec tracks every hop, macintstatus
# bit 8 stays clear, case 612's alloc-headroom stays flat.
#
# Usage: ./hoptest.sh [hops] [dwell_seconds]
#   HAVE_FREE_WRAPPER=1 ./hoptest.sh   # once pkt_buf_free_skb is relocated,
#                                      # allows more samples without leaking
set -u
cd "$(dirname "$0")"
. ./lib.sh

HOPS=${1:-60}
DWELL=${2:-2}
CHANS=(1 6 11 3 9 1 6 11 2 7)     # 2.4GHz only - this is a 43430-family part
CAP=/tmp/hoptest_cap.pcap

echo "reloading + monitor up..."
./reload.sh 2 >/dev/null || { echo "reload failed"; exit 1; }

if ! health_valid; then
    echo "WARN: case 612 did not return NEX1 - debug ioctls missing from image?"
fi

sudo timeout $((HOPS * DWELL + 10)) tcpdump -i "$MON" -w "$CAP" -U 2>/dev/null &
TCPD=$!
sleep 1

fail=0
base_head=$(health | awk '{print "0x"$3}')
printf "hop  chan  chanspec   macintstatus  osh_held  alloc_head  rx\n"
for h in $(seq 1 "$HOPS"); do
    ch=${CHANS[$(( (h - 1) % ${#CHANS[@]} ))]}
    sudo iw dev "$MON" set channel "$ch" 2>/dev/null
    sleep "$DWELL"

    cs=$(cur_chanspec)
    r=$(d11regs);  mis=$(echo "$r" | awk '{print $3}')
    hv=$(health);  osh=$(echo "$hv" | awk '{print $2}'); head=$(echo "$hv" | awk '{print $3}'); mag=$(echo "$hv" | awk '{print $1}')
    rx=$(tcpdump -r "$CAP" 2>/dev/null | wc -l)

    printf "%3d  %4s  %-9s  %-12s  %-8s  %-10s  %s\n" \
        "$h" "$ch" "$cs" "0x${mis:-????}" "0x${osh:-????}" "0x${head:-????}" "$rx"

    if [ "$mag" != "4e455831" ]; then
        echo "  !! ioctl returned noise (no NEX1) - firmware may be wedged"; fail=1; break
    fi
    if [ -n "$mis" ] && [ $(( 0x$mis & 0x100 )) -ne 0 ]; then
        echo "  !! MI_RXOV latched (macintstatus & 0x100) - RX FIFO overflow wedge"; fail=1; break
    fi
done

sudo kill "$TCPD" 2>/dev/null; wait "$TCPD" 2>/dev/null
end_head=$(health | awk '{print "0x"$3}')
total=$(tcpdump -r "$CAP" 2>/dev/null | wc -l)

echo
echo "hops attempted : $HOPS"
echo "alloc-headroom : $base_head -> $end_head  (falling = buffer leak)"
echo "frames captured: $total"
if [ "$fail" -ne 0 ]; then
    echo "RESULT: FAIL - see class A in ../PORTING_STATUS.md"
    dmesg | tail -15
    exit 1
fi
echo "RESULT: PASS"
