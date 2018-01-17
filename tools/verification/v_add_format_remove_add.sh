#!/bin/bash
# Adds a disk
# formats the disk
# removes that disk
# adds another disk

set -eo pipefail
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
account="$1"
key="$2"
DYSKCTL="$3"


echo "Adding an auto create disk 1 gb"
device_name=$(sudo ${DYSKCTL} mount auto-create -a "${account}" -k "${key}" --size 1 -o json | jq -r '.Name' || echo -n "")

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

echo "formatting.."
sudo mkfs.ext4 /dev/${device_name}

echo "Removing *formatted* deviceName:$device_name"
sudo ${DYSKCTL} unmount -d "$device_name"

if [[ ! -z "$(lsblk | grep "$device_name" || echo -n "")" ]]; then
  echo "Test failed: $device_name STILL in blk devices"
  lsblk
  exit 1
fi

## Calling simle add remove
echo "calling simple add remove script"
"${DIR}"/v_simple_add_remove.sh "${account}" "${key}" "${DYSKCTL}"

