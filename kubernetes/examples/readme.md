# Statefulset using dysk


## Prereq

[Install](../readme.md) dysk on you kubernetes cluster 

## Deployment

1. Deploy the storage class 

```
kubectl create -f storageclass.yaml
```

> Although flex volume does not support dynamic provisioning, the storage class helps in binding pvs to claims created by statefulset's claim template. 


2. Create secret 

```
kubectl create  secret generic xdysk --from-literal accountname={ACCOUNT NAME} --from-literal accountkey="{ACCOUNT KEY}" --type="azure/dysk"
```


3. Create dysks

Create 32 page blob (100GiB each)

> in this example we are using ```convert-pv``` dyskctl command to quickly create pvs. This commands creates page blob and convert dysk object to kubernetes PVs

```
for idx in {1..32} \
do \ 
dyskctl  -a {ACCOUNT NAME} -k {ACCOUNT KEY} -o json --size 100 | \
dyskctl convert-pv --secret-name xdysk --labels pvCollection=pvC01 --storageclass-name flexvoldysk01 | kubectl create -f -\
done
```  

> The label created on the PV and storage class name are used by claim template (on the statefulset) to bind PVCs<=>PVs.

4. Create the stateful set

```
kubectl create -f ./nginx-statefulset.yaml
```

> Statefulset pods are create sequentially. 

5. Watch pods light up

```
watch kubectl get po
```

6. Simulate loss of node to watch pods moving quickly between nodes (because of dysk mount speed)

```
kubectl drain {NODE NAME} --grace-period 0 --ignore-daemonsets
```
