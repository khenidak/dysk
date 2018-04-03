## 1. Build csi-dysk image

```
make dysk
docker build --no-cache -t andyzhangx/csi-dysk:1.0.0 -f ./app/dyskplugin/Dockerfile .
#docker login
docker push andyzhangx/csi-dysk:1.0.0
```

## 2. Test csi-flexvol-installer image
```
docker run -it --name csi-dysk andyzhangx/csi-dysk:1.0.0 --nodeid=abc bash
docker stop csi-dysk && docker rm csi-dysk
```
