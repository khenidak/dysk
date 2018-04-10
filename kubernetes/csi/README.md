# dysk CSI driver for Kubernetes (Preview)
 - supported Kubernetes version: available from v1.10.0
 - supported agent OS: Linux 

# About
This driver allows Kubernetes to use [fast kernel-mode mount/unmount AzureDisk](https://github.com/khenidak/dysk)

# Prerequisite
 - A storage account should be created in the same region as the kubernetes cluster

# Install dysk CSI driver on a kubernetes cluster 
## 1. install dysk CSI driver on every agent node
 - create daemonset to install dysk driver
```
kubectl create -f https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/flexvolume/deployment/dysk-flexvol-installer.yaml
```

 - check daemonset status:
```
watch kubectl describe daemonset dysk-flexvol-installer --namespace=flex
watch kubectl get po --namespace=flex
```

 - install dysk CSI components
```
kubectl create -f https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/csi/deployment/csi-provisioner.yaml
kubectl create -f https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/csi/deployment/csi-attacher.yaml
kubectl create -f https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/csi/deployment/csi-dysk-driver.yaml
```

 - check pods status:
```
watch kubectl get po
```
example output:
```
NAME                READY     STATUS    RESTARTS   AGE
csi-attacher-0      1/1       Running   1          1m
csi-dysk-m8lqp      2/2       Running   0          1m
csi-provisioner-0   1/1       Running   0          2m
```

# Basic Usage
## 1. create a secret which stores dysk account name and password
```
kubectl create secret generic dyskcreds --from-literal username=USERNAME --from-literal password="PASSWORD" --type="azure/dysk"
```

## 2. create a pod with csi dysk driver mount on linux
#### Example#1: Dynamic Provisioning (ReadWriteOnce)
 - Create a dysk CSI storage class
```
kubectl create -f https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/csi/storageclass-csi-dysk.yaml
```

 - Create a dysk CSI PVC
```
kubectl create -f https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/csi/pvc-csi-dysk.yaml
```
make sure pvc is created successfully
```
watch kubectl describe pvc pvc-csi-dysk
```

 - create a pod with dysk CSI PVC
```
kubectl create -f https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/csi/nginx-pod-csi-dysk.yaml
```

#### Example#2: Static Provisioning (ReadOnlyMany)
> Note:
>  - access modes of blobfuse PV supports ReadWriteOnce(RWO), ReadOnlyMany(ROX)
>  - `Pod.Spec.Volumes.PersistentVolumeClaim.readOnly` field should be set as `true` when `accessModes` of PV is set as `ReadOnlyMany`
 - Prerequisite

An azure disk should be created and formatted in the specified storage account, disk in exapmle#1 could be used.

 - download `pv-csi-dysk-readonly.yaml` file, modify `container`, `blob`, `volumeHandle` fields and create a dysk csi persistent volume(PV)
```
wget https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/csi/pv-csi-dysk-readonly.yaml
vi pv-csi-dysk-readonly.yaml
kubectl create -f pv-csi-dysk-readonly.yaml
```

 - create a dysk csi persistent volume claim(PVC)
```
kubectl create -f https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/csi/pvc-csi-dysk-readonly.yaml
```

 - check status of PV & PVC until its Status changed to `Bound`
```
kubectl get pv
kubectl get pvc
```
 
 - create a pod with dysk csi PVC
```
kubectl create -f https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/csi/nginx-pod-csi-dysk-readonly.yaml
```

## 3. enter the pod container to do validation
 - watch the status of pod until its Status changed from `Pending` to `Running`
```
watch kubectl describe po nginx-csi-dysk
```
 - enter the pod container

```
kubectl exec -it nginx-csi-dysk -- bash
root@nginx-csi-dysk:/# df -h
Filesystem         Size  Used Avail Use% Mounted on
overlay            291G  3.6G  288G   2% /
tmpfs              3.4G     0  3.4G   0% /dev
tmpfs              3.4G     0  3.4G   0% /sys/fs/cgroup
/dev/sda1          291G  3.6G  288G   2% /etc/hosts
/dev/dyskPKFDLeec  4.8G   10M  4.6G   1% /mnt/disk
shm                 64M     0   64M   0% /dev/shm
tmpfs              3.4G   12K  3.4G   1% /run/secrets/kubernetes.io/serviceaccount
tmpfs              3.4G     0  3.4G   0% /sys/firmware
```
In the above example, there is a `/mnt/disk` directory mounted as dysk filesystem.

 - check pod with readOnly disk mount
 ```
 root@nginx-csi-dysk:/mnt/disk# touch /mnt/disk/a
touch: cannot touch '/mnt/disk/a': Read-only file system
 ```

### Links
 - [dysk - Fast kernel-mode mount/unmount of AzureDisk](https://github.com/khenidak/dysk)
 - [Analysis of the CSI Spec](https://blog.thecodeteam.com/2017/11/03/analysis-csi-spec/)
 - [CSI Drivers](https://github.com/kubernetes-csi/drivers)
 - [Container Storage Interface (CSI) Specification](https://github.com/container-storage-interface/spec)
