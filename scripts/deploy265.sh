#!/bin/bash
# Deploy the built bcm43455c0/7_45_265 nexmon firmware to the Pi test rig.
set -e
PI=pi@192.168.1.131
SSH="sshpass -p raspberry ssh -o StrictHostKeyChecking=no $PI"
IMG=patches/bcm43455c0/7_45_265/nexmon/cyfmac43455-sdio-standard.bin

sshpass -p raspberry scp -o StrictHostKeyChecking=no "$IMG" $PI:/tmp/nexmon.bin

$SSH '
set -e
sudo mkdir -p /lib/firmware/nexmon
sudo cp /tmp/nexmon.bin /lib/firmware/nexmon/cyfmac43455-sdio-standard.bin
sudo update-alternatives --install /lib/firmware/cypress/cyfmac43455-sdio.bin cyfmac43455-sdio.bin /lib/firmware/nexmon/cyfmac43455-sdio-standard.bin 30
sudo update-alternatives --set cyfmac43455-sdio.bin /lib/firmware/nexmon/cyfmac43455-sdio-standard.bin
sudo dmesg -C
sudo modprobe -r brcmfmac_cyw brcmfmac || true
sleep 2
sudo modprobe brcmfmac
sleep 8
dmesg | grep -iE "brcmfmac|Firmware:|TRAP|corrupted|not responding"
'
