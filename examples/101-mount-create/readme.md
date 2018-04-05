# Mount (automatically created dysk) - 256GB


1. Mount (dyskctl will create the page blob for you)
```
sudo ${DYSKCTL} mount auto-create -a "${ACCOUNT}" -k "${KEY}" --size 256 -o json
```

2. dysk will be created at blob container named ```dysks``` with random page blob and device name.
3. to find the newly mounted disk

```
lsblk

#dysk randomly created names formatted as ```dyskxxxx```
```

4. Format the newly mounted disk

```
sudo mkfs.ext4 /dev/${DEVICE_NAME}
``` 

5. Mount the newly mounted disk

```
sudo mkdir /mnt/dysk
sudo mount -o '_netdev' /dev/${DEVICE_NAME} /mnt/dysk

```

> if systemd is your init system you will need `x-systemd.device-bound` [option](https://www.freedesktop.org/software/systemd/man/systemd.mount.html).

6. Use the disk
7. Unmounting the device

```
# unmount the mount point
sudo umount /mnt/dysk

# unmount dysk

sudo dyskctl unmount -d ${DEVICE_NAME}
``` 

> dysk - by default - creates dysks as vhds (with vhd footer written). These dysks can be attached as data disks (on unmanaged VMs) via azure. Follow [these steps](https://docs.microsoft.com/en-us/azure/virtual-machines/linux/attach-disk-portal) to attach dysk as data disk. If you want disks without vhd footer use ```-vhd false``` argument to dyskctl.


# deleting dysks

dysk provide an easy tool to delete page blobs (there is **no undo** for this operation) 

```
sudo ./dyskctl delete -a ${ACCOUNT} -k {KEY} --container-name {CONTAINERNAME} --pageblob-name {PAGEBLOBNAME} 
```

> TIP: use ```break-lease``` if the page blob is leased
