#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 VENDOR_ID DEVICE_ID"
    echo "Example: $0 10ee 903f"
    exit 2
fi

VID="$1"
DID="$2"
DRIVER="rk-pcie-sdk"
BDF=""

for f in /sys/bus/pci/devices/*; do
    [ -r "$f/vendor" ] || continue
    v=$(cat "$f/vendor" | sed 's/^0x//')
    d=$(cat "$f/device" | sed 's/^0x//')
    if [ "$v" = "$(echo "$VID" | tr 'A-F' 'a-f')" ] &&
       [ "$d" = "$(echo "$DID" | tr 'A-F' 'a-f')" ]; then
        BDF=${f##*/}
        break
    fi
done

if [ -z "$BDF" ]; then
    echo "PCI device ${VID}:${DID} not found"
    exit 1
fi

echo "Found $BDF; adding dynamic ID ${VID} ${DID}"
echo "$VID $DID" > "/sys/bus/pci/drivers/$DRIVER/new_id"
echo "$BDF" > "/sys/bus/pci/drivers/$DRIVER/bind"
echo "Bound $BDF to $DRIVER"
