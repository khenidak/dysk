# Kubernetes volume driver for dysk

> The flex vol currently supports ReadWriteOnce access mode. Coming Soon: ReadOnlyMany

## Install

### For cluters created with acs-engine 0.12 or later 

> These cluster configured for volume plugins at /etc/kubernetes/volumeplugins on every node

Just run:
```
kubectl create -f https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/deployment/dysk-system-ns.yaml # creates namespace
kubectl create -f https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/deployment/dysk-kubernetes-installer.yaml # creates kernel module installer + flex vol install
```
### For clusters that has been previously configured for flex vol plugin

1. Modify Volumes/Volumes Mounts ```flexvol-driver-installer``` container in ```dysk-kubernetes-installer.yaml``` to point to the directory that has your current flex volume plugin.
2. Modify TARGET_DIR env var on ```flexvol-driver-installer``` container in ```dysk-kubernetes-installer.yaml``` to point to the directory that has your current flex volume plugins.
3. Run the commands above.

### For clusters that has not been configured  flex volume plugins

> kubelet on these cluster is **not** running with ```--volume-plugin-dir``` argument.

1. On each node, perform the following
```
# find the unit file for  kubelet
kubelet_uf_path="$(udo systemctl show -p FragmentPath kubelet)"
echo "kubelet unit file is at ${kubelet_uf_path}"

#edit the file
sudo vi ${kubelet_uf_path}

# Edit kubelet arguments by adding --volume-plugin-dir
#example:  <kubelet executable> <args..>    --volume-plugin-dir=/etc/kubernetes/volumeplugins

# if you are running containerized kubelet add the above path as volume
#example <docker run> <-v args >  --volume=/etc/kubernetes/volumeplugins:/etc/kubernetes/volumeplugins:rw <..other args ..>

# reload systemd and restart kuebelet
sudo systemctl daemon-reload
sudo systemctl restart kubelet
```

2. Run the commands above

## Basic Usage

1. create a secret which stores storage  account name and key (dysk uses Azure storage page blobs)

```
kubectl create secret generic dyskcreds --from-literal accountname=USERNAME --from-literal accountkey="PASSWORD" --type="dysk/dysk"
```

2. create a pod with flexvolume-dysk mount on linux
kubectl create -f https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/nginx-flex-dysk.yaml

3. watch the status of pod until its Status changed from `Pending` to `Running`
```watch kubectl describe po nginx-flex-dysk```

4. enter the pod container to do validation
```kubectl exec -it nginx-flex-dysk -- bash```

```
kubectl exec -it nginx-flex-dysk -- bash
root@nginx-flex-dysk:/# df -h
Filesystem         Size  Used Avail Use% Mounted on
overlay            291G  3.9G  287G   2% /
tmpfs              3.4G     0  3.4G   0% /dev
tmpfs              3.4G     0  3.4G   0% /sys/fs/cgroup
/dev/dyskI7cFTURv  5.8G   12M  5.5G   1% /data
/dev/sda1          291G  3.9G  287G   2% /etc/hosts
shm                 64M     0   64M   0% /dev/shm
tmpfs              3.4G   12K  3.4G   1% /run/secrets/kubernetes.io/serviceaccount
```


## More info on FlexVol drivers
[Flexvolume doc](https://github.com/kubernetes/community/blob/master/contributors/devel/flexvolume.md)

More clear steps about flexvolume by Redhat doc: [Persistent Storage Using FlexVolume Plug-ins](https://docs.openshift.org/latest/install_config/persistent_storage/persistent_storage_flex_volume.html)
