#!/bin/bash

set -eo pipefail
account="$1"
key="$2"
DYSKCTL="$3"


echo "Adding an auto create disk 4 gb"
dysk_json="$(sudo ${DYSKCTL} mount auto-create -a "${account}" -k "${key}" --size 4 -o json)"

device_name="$(echo "$dysk_json" | jq -r '.Name //empty')"
dysk_path="$(echo "$dysk_json" | jq -r '.Path //empty')"
lease_id="$(echo "$dysk_json" | jq -r '.LeaseId //empty')"
mount_point="/mnt/dysk01"
message="Hello, dysk!"

echo "Created: $dysk_path with lease:$lease_id"
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

echo "creating mountpoint ${mount_point}"
sudo mkdir -p "${mount_point}"

echo "mounting device /dev/${device_name} to ${mount_point}"
sudo mount /dev/${device_name} ${mount_point}

echo "create test file ${mount_point}/hello.txt"
echo ${message} | sudo tee  ${mount_point}/hello.txt # mount point created at root owned directory


echo "unmounting the mount point ${mount_point}"
sudo umount ${mount_point}

echo "Removing deviceName:$device_name"
sudo ${DYSKCTL} unmount -d "$device_name"

if [[ ! -z "$(lsblk | grep "$device_name" || echo -n "")" ]]; then
  echo "Test failed: $device_name STILL in blk devices"
  lsblk
  exit 1
fi

container="$(dirname "$dysk_path")"
page_blob="$(basename "$dysk_path")"
#strip first / in container name
container="${container:1}"

echo "Remounting device ${device_name} as ro device"
sudo ${DYSKCTL} mount -a "${account}" \
                 -k "${key}" \
                 --device-name ${device_name} \
                 --lease-id ${lease_id} \
                 --container-name ${container} \
                 --pageblob-name ${page_blob} \
                 --read-only


if [[ -z "$(lsblk | grep "$device_name" || echo -n "")" ]]; then
  echo "Test failed: Unable to find $device_name in blk devices"
  lsblk
  exit 1
else
  echo "$device_name found in blk devices"
fi

echo "Mount /dev/${devicename} to ${mount_point} as readonly"
sudo mount /dev/${device_name} /${mount_point} --read-only

echo "testing file previously created"

new_message="$(sudo cat ${mount_point}/hello.txt)"
if [[ "${message}" != "${new_message}" ]];then
  echo "message is not the same, expected:${message} got:${new_message}"
  exit 1
fi

echo "unmounting the mount point ${mount_point}"
sudo umount ${mount_point}

echo "Removing deviceName:$device_name"
sudo ${DYSKCTL} unmount -d "$device_name"

if [[ ! -z "$(lsblk | grep "$device_name" || echo -n "")" ]]; then
  echo "Test failed: $device_name STILL in blk devices"
  lsblk
  exit 1
fi


