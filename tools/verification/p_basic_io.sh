#!/bin/bash

set -eo pipefail
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

account="$1"
key="$2"
DYSKCTL="$3"

sudo mkdir -p /mnt/dysk01

echo "Adding an auto create disk 4 TB"
device_name=$(sudo ${DYSKCTL} mount auto-create -a "${account}" -k "${key}" --size 4096 -o json | jq -r '.Name' || echo -n "")

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


echo "formatting /dev/${device_name}"
sudo mkfs.ext4 /dev/${device_name}

echo "mounting /dev/${device_name} to /mnt/dysk01"
echo "running fio"

fio "${DIR}"/basic.fio

echo "fio done!"
echo "unmounting the mount point /mnt/dysk01"
sudo umount /mnt/dysk01

echo "Removing deviceName:$device_name"
sudo ${DYSKCTL} unmount -d "$device_name"

if [[ ! -z "$(lsblk | grep "$device_name" || echo -n "")" ]]; then
  echo "Test failed: $device_name STILL in blk devices"
  lsblk
  exit 1
fi



