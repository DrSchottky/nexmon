#!/bin/bash
# Revert the bcm43455c0 on the Pi test rig to stock 7.45.265 firmware.
set -e
PI=pi@192.168.1.131
SSH="sshpass -p raspberry ssh -o StrictHostKeyChecking=no $PI"

$SSH '
set -e
sudo update-alternatives --set cyfmac43455-sdio.bin /lib/firmware/cypress/cyfmac43455-sdio-standard.bin
sudo dmesg -C
sudo modprobe -r brcmfmac_cyw brcmfmac || true
sleep 2
sudo modprobe brcmfmac
sleep 8
dmesg | grep -iE "brcmfmac|Firmware:|TRAP|corrupted|not responding"
'
