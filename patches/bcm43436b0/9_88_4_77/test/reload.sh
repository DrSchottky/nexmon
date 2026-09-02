#!/bin/bash
# bcm43436b0 / 9.88.4.77 - reload the nexmon driver+firmware and bring up a
# monitor vif, in the order the earlier ports proved is mandatory (vif added
# BEFORE monitor mode is set, or brcmfmac refuses the interface).
#
# Usage: ./reload.sh            (radiotap monitor, -m2)
#        ./reload.sh 0          (plain, no monitor)
set -u

WLAN=${WLAN:-wlan0}          # the bcm43436b0 onboard interface
MON=${MON:-${WLAN}mon}
MODE=${1:-2}                 # nexutil -m argument (2 = radiotap)

echo "== unloading =="
sudo iw dev "$MON" del 2>/dev/null
sudo modprobe -r brcmfmac_wcc 2>/dev/null
sudo modprobe -r brcmfmac
sleep 1

echo "== loading =="
sudo modprobe brcmfmac
# wait for the interface to appear (SDIO probe + firmware download)
for i in $(seq 1 20); do
    [ -e "/sys/class/net/$WLAN" ] && break
    sleep 0.5
done
[ -e "/sys/class/net/$WLAN" ] || { echo "FAIL: $WLAN never appeared"; dmesg | tail -20; exit 1; }

sudo rfkill unblock all
sudo ip link set "$WLAN" up
sleep 1

echo "== firmware version =="
dmesg | grep -i "nexmon\|Firmware.*43436" | tail -3

if [ "$MODE" = "0" ]; then
    echo "== done (no monitor) =="
    exit 0
fi

echo "== monitor vif =="
# phy index increments on every reload - never hardcode phy0
PHY=$(cat "/sys/class/net/$WLAN/phy80211/name")
sudo iw phy "$PHY" interface add "$MON" type monitor
sudo ip link set "$MON" up
sudo ip link set "$WLAN" down
sudo nexutil -I"$MON" -m"$MODE"

echo "== state =="
iw dev | grep -A3 "$MON"
nexutil -I"$MON" -m
echo "reload OK: $MON on $PHY"
