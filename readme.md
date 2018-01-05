# Dysk #

Dysk mounts Azure disks as Linux block devices directly on VMs without dependency on the host. Dysks can be used within Azure VMs or on-prem machines.

**Project Status**: Alpha

## Motivation ##

1. Pack more data disks per VM. Dysk has no restriction on max # of disks. Dysk can mount many disks per node (depending on CPU/Memory/Network).
2. Quickly *attach* and *detach* disks to node (or node's workload). Dysk attaches/detaches disks in 1 second or less.
3. Treat Azure disks (in essence they are storage client) as workload (similar to pods in kubernetes). They start fast on nodes and move fast between nodes when needed. 

## How it looks like ##

[![asciicast](https://asciinema.org/a/kajaK23xjBUCyQsnwl1eSOcAI.png)](https://asciinema.org/a/kajaK23xjBUCyQsnwl1eSOcAI)

> Check [Design](docs/design.md) for details on how dysk works.

## How to use ##

> Please [install](docs/build-install.md) dysk kernel module before running the below commands.

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


Dysks are block devices, so it can be used using common Linux commands

```
#List block devices
lsblk

# dysks can be formatted as regular disks using mkfs command example:
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



