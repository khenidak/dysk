# dysk (dīsk) #

Attach Azure disks in < 1 second. Attach as many as you want. Attach them where ever you want. dysk mounts Azure disks as Linux block devices directly on VMs without dependency on the host.

**Project Status**: Alpha

## Motivation ##

1. Pack more data disks per VM. Dysk has no restriction on max # of disks. Dysk can mount many disks per node (depending on CPU/Memory/Network).
2. Quickly *attach* and *detach* disks to node (or node's workload). dysk attaches/detaches disks in 1 second or less.
3. Treat Azure disks (in essence they are storage client) as workload (similar to pods in kubernetes). They start fast on nodes and move fast between nodes when needed.

## How it looks like ##

[![asciicast](https://asciinema.org/a/kajaK23xjBUCyQsnwl1eSOcAI.png)](https://asciinema.org/a/kajaK23xjBUCyQsnwl1eSOcAI)

> Check [Design](docs/design.md) for details on how dysk works.

## How to use ##

> Dysk works with storage accounts V1 or V2[storage api version:2017-04-17]. Premium SKU is subject to [size based throttling](https://docs.microsoft.com/en-us/azure/virtual-machines/windows/premium-storage). Please [install](docs/build-install.md) dysk kernel module before running the below commands.

Auto-Create and mount 2 GB Azure Page Blob as a block device
```
sudo dyskctl mount auto-create -a {STORAGE ACCOUNT NAME} -k {STORAGE ACCOUNT KEY}

## output
Created PageBlob in account:xdysk dysks/dysk6Hjr5R52.vhd(2GiB)
Wrote VHD header for PageBlob in account:xdysk dysks/dysk6Hjr5R52.vhd
Type                            Name                            VHD                             SizeGB                          AccountName                     Path
RW                              dysk6Hjr5R52                    Yes                             2                               xdysk                           /dysks/dysk6Hjr5R52.vhd
```
> When using the auto-create command the client library by default writes the vhd footer for you. This enables you to mount the disk using ARM if needed. You can disable this using the ``` -vhd ``` flag
> Make sure the storage account supports http (not https)

Mount existing Azure Page Blob (with or without a lease id) 
```
sudo dyskctl mount -a {STORAGE ACCOUNT NAME} -k {STORAGE ACCOUNT KEY} -c {CONTAINER NAME} -d {DISK NAME} -i {LEASE ID}

## output
Type                            Name                            VHD                             SizeGB                          AccountName                     Path
RW                              dysk1cmwC5uU                    Yes                             2                               dyskdemo                        /dysks/dysk1cmwC5uU.vhd
```

If you are seeing error `storage: service returned error: StatusCode=409, ErrorCode=LeaseAlreadyPresent, ErrorMessage=There is already a lease present.`, you can set the `--break-lease` flag to `true` to break the existing lease.
```
sudo dyskctl mount -a {STORAGE ACCOUNT NAME} -k {STORAGE ACCOUNT KEY} -c {CONTAINER NAME} -d {DISK NAME} -i {LEASE ID} -b true

## output
lease not valid
acquiring new lease
break lease
Type                            Name                            VHD                             SizeGB                          AccountName                     Path
RW                              dysk1cmwC5uU                    Yes                             2                               dyskdemo                        /dysks/dysk1cmwC5uU.vhd
```

> If you do not provide a `--lease-id`, you will most likely need to set the `--break-lease` flag to `true` to break any existing lease on it. Unless it has been previously released.


Dysks are block devices, so it can be used using common Linux commands

```
#List block devices
lsblk

# dysks can be formatted as regular disks using mkfs command, example:
sudo mkfs.ext4 /dev/dysk6Hjr5R52 #Device name from the output above.
```

> Dysks can be mounted as read-only (on many nodes) devices using the --read-only flag

You can list dysks using the following command

```
# List currently attached dysks
sudo dyskctl list -o json
[
    {
        "Type": "RW",
        "Name": "dyskxOQO4esH",
        "AccountName": "xdysk",
        "AccountKey": "{KEY}",
        "Path": "/dysks/dyskxOQO4esH.vhd",
        "LeaseId": "{LEASE}",
        "Major": 252,
        "Minor": 32,
        "Vhd": true,
        "SizeGB": 2
    },
    {
        "Type": "RW",
        "Name": "dysk6Hjr5R52",
        "AccountName": "xdysk",
        "AccountKey": "{KEY}",
        "Path": "/dysks/dysk6Hjr5R52.vhd",
        "LeaseId": "{LEASE}",
        "Major": 252,
        "Minor": 16,
        "Vhd": true,
        "SizeGB": 2
    }
]
```

> Keys are never stored, they are kept in module's kernel memory.

Unmounting using the following command

```
sudo dyskctl unmount -d dysk6Hjr5R52 
```

> for further CLI commands execute ```dyskctl --help ```

