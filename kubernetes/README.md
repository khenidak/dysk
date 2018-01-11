# Kubernetes volume driver for Dysk
## 1. create a secret which stores dysk account name and password
```
kubectl create secret generic dyskcreds --from-literal accountname=USERNAME --from-literal accountkey="PASSWORD" --type="foo/dysk"
```

## 2. install flexvolume driver on every linux agent node
Please make sure dysk driver is already installed on every linux agent node
```
sudo mkdir -p /etc/kubernetes/volumeplugins/foo~dysk
cd /etc/kubernetes/volumeplugins/foo~dysk
sudo wget https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/dysk
sudo chmod a+x dysk

wget https://raw.githubusercontent.com/andyzhangx/Demo/master/linux/flexvolume/dysk/4.11.0-1016-azure/dyskctl
chmod a+x dyskctl
# test dysk driver is installed
sudo ./dyskctl list
```
#### Note:
Make sure `jq` package is installed on every node: 
```
sudo apt install jq -y
```

## 3. specify `volume-plugin-dir` in kubelet service config (skip this step from acs-engine v0.12.0)
```
sudo vi /etc/systemd/system/kubelet.service
  --volume=/etc/kubernetes/volumeplugins:/etc/kubernetes/volumeplugins:rw \
        --volume-plugin-dir=/etc/kubernetes/volumeplugins \
sudo systemctl daemon-reload
sudo systemctl restart kubelet
```
Note:
`/etc/kubernetes/volumeplugins` has already been the default flexvolume plugin directory in acs-engine (starting from v0.12.0)

## 4. create a pod with flexvolume-dysk mount on linux
kubectl create -f https://raw.githubusercontent.com/khenidak/dysk/master/kubernetes/nginx-flex-dysk.yaml

#### watch the status of pod until its Status changed from `Pending` to `Running`
```watch kubectl describe po nginx-flex-dysk```

## 5. enter the pod container to do validation
```kubectl exec -it nginx-flex-dysk -- bash```

```
azureuser@k8s-master-77890142-0:~$ kubectl exec -it nginx-flex-dysk -- bash
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


### Links
[Flexvolume doc](https://github.com/kubernetes/community/blob/master/contributors/devel/flexvolume.md)

More clear steps about flexvolume by Redhat doc: [Persistent Storage Using FlexVolume Plug-ins](https://docs.openshift.org/latest/install_config/persistent_storage/persistent_storage_flex_volume.html)
