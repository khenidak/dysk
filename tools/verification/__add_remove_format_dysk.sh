#!/bin/bash

set -eo pipefail
account="$1"
key="$2"
DYSKCTL="$3"
COUNT="$4"
FORMAT="$5"
echo "COUNT:$COUNT $FORMAT"

for (( idx=1; idx<=${COUNT}; idx++)); 
do
  echo "at ${idx}/${COUNT}"
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
  
  if [[ "FORMAT" == "${FORMAT}" ]];then
    echo "Formatting: ${device_name}"
    sudo mkfs.ext4 /dev/${device_name}
  fi

  echo "Removing deviceName:$device_name"
  sudo ${DYSKCTL} unmount -d "$device_name"

  if [[ ! -z "$(lsblk | grep "$device_name" || echo -n "")" ]]; then
    echo "Test failed: $device_name STILL in blk devices"
    lsblk
    exit 1
  fi

done
