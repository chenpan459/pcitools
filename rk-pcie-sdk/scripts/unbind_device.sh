#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 DOMAIN:BUS:DEV.FUNC"
    echo "Example: $0 0000:03:00.0"
    exit 2
fi

BDF="$1"
echo "$BDF" > /sys/bus/pci/drivers/rk-pcie-sdk/unbind
