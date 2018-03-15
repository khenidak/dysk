# Command List
1. Mount
2. Unmount
3. Get Dysk
3. List Dysks (Names &  Major/minors only) 

> All input commands are read at max 2048 bytes.Including a null terminator for the entire command and each entry. All responses are max 2048 bytes including a null terminator


## General Error Response
In case of error the following will be used

```
ERR\n
{Message}
```

# Mount

## Request

```
TYPE\n 	     	# Max 2 (R ReadOnly RW ReadWrite)
DeviceName\n 	# Max 32 desired device name *unique per all dysks and existing disks
SectorCount\n   # size_t sector count.
Account Name\n  # max 256
Account Key\n #max 128
Disk Path \n	# Max 1024 include the extention if any sample: /xxx/xxx/xx.xx
Host\n 		# Max 512 Full host name sample: dysk.core.blob.windows.core
IP\n		# max 32 ip host name.
Lease-Id\n	# max 64
0 or 1 \n 	# is vhd
```


## Response
Error Message or

```
OK\n
{REQUEST MESSAGE}\n
Major\n
Minor\n
```

# Unmount

## Request

```
DeviceName\n
```

## Response

Error Message or 

```
OK\n
```

# Get Dysk

## Request

```
GET\n
DeviceName\n
```

## Response

Error Message or

```
Ok\n
{Mount Response Message}\n'
```

# List

## Request

```
LIST
```

## Resonse

Error Message or

```
OK\n
devicename major:minor\n
...
```
