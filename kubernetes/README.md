# dysk FlexVolume driver for Kubernetes (Preview)
 - supported Kubernetes version: available from v1.7
 - supported agent OS: Linux 

# About
This driver allows Kubernetes to use [fast kernel-mode mount/unmount AzureDisk](https://github.com/khenidak/dysk)

# Prerequisite
 - A storage account should be created in the same region as the kubernetes cluster
 - An azure disk should be created in the specified storage account, below example will create a vhd(`dysk01.vhd`) in default container `dysks`
```
docker run --rm \
	-it --privileged \
	-v /etc/ssl/certs:/etc/ssl/certs:ro \
khenidak/dysk-cli:0.4 create --account ACCOUNT-NAME --key ACCOUNT-KEY --device-name dysk01 --size 1
```

# Install dysk driver on a kubernetes cluster
## 1. config kubelet service (skip this step in [AKS](https://azure.microsoft.com/en-us/services/container-service/) or from [acs-engine](https://github.com/Azure/acs-engine) v0.12.0)
 - specify `volume-plugin-dir` in kubelet service config

append following two lines **seperately** into `/etc/systemd/system/kubelet.service` file
```
  --volume=/etc/kubernetes/volumeplugins:/etc/kubernetes/volumeplugins:rw \
        --volume-plugin-dir=/etc/kubernetes/volumeplugins \
```

```
sudo vi /etc/systemd/system/kubelet.service
...
ExecStart=/usr/bin/docker run \
  --net=host \
  ...
  --volume=/etc/kubernetes/volumeplugins:/etc/kubernetes/volumeplugins:rw \
    ${KUBELET_IMAGE} \
      /hyperkube kubelet \
        --require-kubeconfig \
        --v=2 \
	...
      --volume-plugin-dir=/etc/kubernetes/volumeplugins \
        $KUBELET_CONFIG $KUBELET_OPTS \
        ${KUBELET_REGISTER_NODE} ${KUBELET_REGISTER_WITH_TAINTS}
...

sudo systemctl daemon-reload
sudo systemctl restart kubelet
```

Note:
 - `/etc/kubernetes/volumeplugins` has already been the default flexvolume plugin directory in acs-engine (starting from v0.12.0)
 - Flexvolume is GA from Kubernetes **1.8** release, v1.7 is depreciated since it does not support [Dynamic Plugin Discovery](https://github.com/kubernetes/community/blob/master/contributors/devel/flexvolume.md#dynamic-plugin-discovery).
 
## 2. install dysk FlexVolume driver on every agent node
 - create daemonset to install dysk driver
```
kubectl create -f https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/deployment/dysk-flexvol-installer.yaml
```

 - check daemonset status:
```
kubectl describe daemonset dysk-flexvol-installer --namespace=flex
kubectl get po --namespace=flex
```

> Note: for deployment on v1.7, it requires restarting kubelet on every node(`sudo systemctl restart kubelet`) after daemonset running complete due to [Dynamic Plugin Discovery](https://github.com/kubernetes/community/blob/master/contributors/devel/flexvolume.md#dynamic-plugin-discovery) not supported on k8s v1.7

# Basic Usage
## 1. create a secret which stores dysk account name and password
```
kubectl create secret generic dyskcreds --from-literal username=USERNAME --from-literal password="PASSWORD" --type="azure/dysk"
```

## 2. create a pod with dysk flexvolume mount on linux
#### Example#1: Tie a flexvolume explicitly to a pod (ReadWriteOnce)
- download `nginx-flex-dysk.yaml` file and modify `container`, `blob` fields
```
wget -O nginx-flex-dysk.yaml https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/nginx-flex-dysk.yaml
vi nginx-flex-dysk.yaml
```
 - create a pod with dysk flexvolume driver mount
```
kubectl create -f nginx-flex-dysk.yaml
```

#### Example#2: Create dysk flexvolume PV & PVC and then create a pod based on PVC (ReadOnlyMany)
> Note:
>  - access modes of blobfuse PV supports ReadWriteOnce(RWO), ReadOnlyMany(ROX)
>  - `Pod.Spec.Volumes.PersistentVolumeClaim.readOnly` field should be set as `true` when `accessModes` of PV is set as `ReadOnlyMany`
 - download `pv-dysk-flexvol.yaml` file, modify `container`, `blob`, `storage` fields and create a dysk flexvolume persistent volume(PV)
```
wget -O pv-dysk-flexvol.yaml https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/pv-dysk-flexvol.yaml
vi pv-dysk-flexvol.yaml
kubectl create -f pv-dysk-flexvol.yaml
```

 - create a dysk flexvolume persistent volume claim(PVC)
```
kubectl create -f https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/pvc-dysk-flexvol.yaml
```

 - check status of PV & PVC until its Status changed to `Bound`
 ```
kubectl get pv
kubectl get pvc
 ```
 
 - create a pod with dysk flexvolume PVC
```
kubectl create -f https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/nginx-flex-dysk-readonly.yaml
 ```

## 3. enter the pod container to do validation
 - watch the status of pod until its Status changed from `Pending` to `Running`
```
watch kubectl describe po nginx-flex-dysk
```
 - enter the pod container
kubectl exec -it nginx-flex-dysk -- bash

```
root@nginx-flex-dysk:/# df -h
Filesystem         Size  Used Avail Use% Mounted on
overlay            291G  6.3G  285G   3% /
tmpfs              3.4G     0  3.4G   0% /dev
tmpfs              3.4G     0  3.4G   0% /sys/fs/cgroup
/dev/dyskpoosf9g0  992M  1.3M  924M   1% /data
/dev/sda1          291G  6.3G  285G   3% /etc/hosts
shm                 64M     0   64M   0% /dev/shm
tmpfs              3.4G   12K  3.4G   1% /run/secrets/kubernetes.io/serviceaccount
tmpfs              3.4G     0  3.4G   0% /sys/firmware
```
In the above example, there is a `/data` directory mounted as dysk filesystem.

### Tips
#### How to use flexvolume driver in Helm
Since flexvolume does not support dynamic provisioning, storageClass should be set as empty in Helm chart, take [wordpress](https://github.com/kubernetes/charts/tree/master/stable/wordpress) as an example:
 - Set up a dysk flexvolume PV and also `dyskcreds` first
```
kubectl create secret generic dyskcreds --from-literal username=USERNAME --from-literal password="PASSWORD" --type="azure/dysk"
kubectl create -f pv-dysk-flexvol.yaml
```
 - Specify `persistence.accessMode=ReadWriteOnce,persistence.storageClass="-"` in [wordpress](https://github.com/kubernetes/charts/tree/master/stable/wordpress) chart
```
helm install --set persistence.accessMode=ReadWriteOnce,persistence.storageClass="-" stable/wordpress
```

### Links
[Flexvolume doc](https://github.com/kubernetes/community/blob/master/contributors/devel/flexvolume.md)

[Persistent Storage Using FlexVolume Plug-ins](https://docs.openshift.org/latest/install_config/persistent_storage/persistent_storage_flex_volume.html)

[dysk - Fast kernel-mode mount/unmount of AzureDisk](https://github.com/khenidak/dysk)
