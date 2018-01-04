#Design#

##Contrasting Dysk to Existing Data Disks##

Existing data disks follow the following path: 
1. Disks are attached to hosts which performs all network calls to Azure Storage on behalf of VMs.
2. Disk are then availed via hypervisor to VM SCSI devices.

Dysk are block devices running in your VMs are all network calls to Azure Storage are originated from your kernel.

##Dysk Components##

1. Dysk LKM
	1. Char Device: Because we don't have H/W involved, there are not IRQ to notify kernel when disks are pluggedi/unplugged. We relay on IOCTL performed against this char device. 
	2. Dysk bdd: Manages current list of mount dysks and integrates dysks with kernel block I/O interfaces.
	3. Dysk Block Device: Created dynamically in resonse to ``` mount ``` IOCTL.
	4. Worker: Performs asynchronous execution.
	5. AZ: manages Azure page blob REST API calls. All calls are nonblocking.
2. Dysk Client
	1. Go based client side package (executes IOCTL) that can be wrapped in any executable.
	2. CLI that wraps the above package.

> Due to the fact that Linux kernel does not support TLS all calls are executed against the HTTP endpoint its highly advisable that you use [Azure VNET service endpoints](https://docs.microsoft.com/en-us/azure/virtual-network/virtual-network-service-endpoints-overview). This will not expose your storage account (nor its traffic) outside your VNET. On-Prem VMs can VPN into this VNET to access the storage accounts.


##Handling Failed Disks##

Disks can fail for many reasons such as network(non transient failure), page blob deletion and breaking Azure Storage lease. Once any of these conditions is true, the following is executed:

1. The failed disk is marked as failed.
2. Any new requests will be canceled with [-EIO](http://www.virtsync.com/c-error-codes-include-errno) returned to userspace.
3. Any pending request will be canceled and -EIO is returned to userspace. 
4. Disk is removed gracefuly. 

> at which point processes attempting to read/write from disks will handle the error using standard error handling. 

##Handling Throttled Disks##

Once Azure Storage throttle a disk, dysk gracefully handles this event and pauses new I/O requests for 3 seconds before retrying the requests. 

##Handling Cluster Split Brains Scenarios##

Dysk is designed to work in high density orchesterated compute envrionment. Specifically, containers orchesterted by Kubernetes. In this scenario pods declare thier storage requirements via specs (PV/PVC)[https://kubernetes.io/docs/concepts/storage/persistent-volumes/]. At any point of time a node or more carrying a large number of containers and disk might be in a network split. Where containers keep on running but nodes fail to report healthy state to master. Because disks are not *attached* perse a volume driver can break the existing lease and create new one then mount dysks on healthy nodes. Existing dysks will gracefull fail as described above.

