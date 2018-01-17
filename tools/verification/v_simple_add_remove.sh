#!/bin/bash

set -eo pipefail
account="$1"
key="$2"
DYSKCTL="$3"


echo "Adding an auto create disk 4 gb"
device_name=$(sudo ${DYSKCTL} mount auto-create -a "${account}" -k "${key}" --size 4 -o json | jq -r '.Name' || echo -n "")

if [[ -z "$device_name" ]]; then
  echo "Test failed"
  exit 1
fi

echo "Added deviceName:$device_name"

if [[ -z "$(lsblk | grep "$device_name" || echo -n "")" ]]; then
  echo "Test failed: Unable to find $device_name in blk devices"
  lsblk
  exit 1
else
  echo "$device_name found in blk devices"
fi

echo "Removing deviceName:$device_name"
sudo ${DYSKCTL} unmount -d "$device_name"

if [[ ! -z "$(lsblk | grep "$device_name" || echo -n "")" ]]; then
  echo "Test failed: $device_name STILL in blk devices"
  lsblk
  exit 1
fi



