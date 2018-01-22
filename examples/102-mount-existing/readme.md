# Mount an existing page blob

> The steps below create page blob then mount it. You can mount an existing **unattached** page blob data disk (not managed disk)

1. Create page blob - 1TB

```
sudo dyskctl create -a "${ACCOUNT}" -k "${KEY}" --size 1024 --auto-lease -o json > dysk.json

#file.json now contains dysk definition as json file
```

2. Use mount file command to mount it

```
sudo dyskctl mount-file -file ./dysk.json
```
Alternatively, you can directly use mount command to mount an existing page blob as dysk

```
sudo dyskctl mount -a ${ACCOUNT} -l {KEY} --pageblob-name {PAGEBLOBNAME} --container-name {CONTAINERNAME} --auto-lease
```

If the page blob was leased you can supply lease via ```--leaseid``` argument or use ```--break-lease``` to break the existing lease and create a new lease. Note: Breaking lease can be dangrous if applications are using it (and can not handle losing lease).

> follow [101-mount-create](../101-mount-create/readme.md) to format and use the disk
