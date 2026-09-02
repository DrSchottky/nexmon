#!/bin/bash
# Class B stress: fixed-channel, high-volume RX soak. No hopping at all - this
# isolates content-triggered failures (the bcm43455c0/7.45.265 A-MSDU crash
# looked like a hop bug but was really an A-MSDU frame smashing memory). Pick
# the busiest channel you can find first (iw scan on another adapter).
#
# Usage: ./rxsoak.sh [channel] [seconds]
set -u
cd "$(dirname "$0")"
. ./lib.sh

CH=${1:-6}
SECS=${2:-300}
CAP=/tmp/rxsoak_cap.pcap
SAMPLE=5      # liveness sample interval, seconds

echo "reloading + monitor up on channel $CH..."
./reload.sh 2 >/dev/null || { echo "reload failed"; exit 1; }
sudo iw dev "$MON" set channel "$CH"

health_valid || echo "WARN: case 612 did not return NEX1 - debug ioctls missing?"

sudo timeout $((SECS + 10)) tcpdump -i "$MON" -w "$CAP" -U 2>/dev/null &
TCPD=$!
sleep 1

fail=0
start=$(date +%s)
printf "  t   macintstatus  osh_held  alloc_head  rx\n"
while [ $(( $(date +%s) - start )) -lt "$SECS" ]; do
    sleep "$SAMPLE"
    r=$(d11regs);  mis=$(echo "$r" | awk '{print $3}')
    hv=$(health);  osh=$(echo "$hv" | awk '{print $2}'); head=$(echo "$hv" | awk '{print $3}'); mag=$(echo "$hv" | awk '{print $1}')
    rx=$(tcpdump -r "$CAP" 2>/dev/null | wc -l)
    printf "%4d  0x%-10s  0x%-6s  0x%-8s  %s\n" \
        $(( $(date +%s) - start )) "${mis:-????}" "${osh:-????}" "${head:-????}" "$rx"

    if [ "$mag" != "4e455831" ]; then
        echo "  !! ioctl noise (no NEX1) - firmware wedged/trapped"; fail=1; break
    fi
    if [ -n "$mis" ] && [ $(( 0x$mis & 0x100 )) -ne 0 ]; then
        echo "  !! MI_RXOV latched - RX FIFO overflow"; fail=1; break
    fi
    if dmesg | tail -5 | grep -qi "TRAP\|trap"; then
        echo "  !! TRAP in dmesg - likely a content-triggered crash (A-MSDU?)"; fail=1; break
    fi
done

sudo kill "$TCPD" 2>/dev/null; wait "$TCPD" 2>/dev/null
total=$(tcpdump -r "$CAP" 2>/dev/null | wc -l)
echo
echo "channel        : $CH"
echo "frames captured: $total  (~$(( total / (SECS>0?SECS:1) ))/s)"
if [ "$fail" -ne 0 ]; then
    echo "RESULT: FAIL - see class A/B in ../PORTING_STATUS.md"
    dmesg | tail -20
    exit 1
fi
echo "RESULT: PASS"
