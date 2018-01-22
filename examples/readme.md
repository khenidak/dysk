# Before you begin

## Pre-req
1. [Install dysk kernel module and dyskctl cli](../docs/build-install.docs)
2. dysk depends on Azure Storage (Page Blobs). Follow the below steps to create azure storage account via the cli

**Install and configure azure cli**
1. [Install azure cli] (https://docs.microsoft.com/en-us/cli/azure/install-azure-cli?view=azure-cli-latest).
2. Login with ```az login ```
3. Set your active subscription with ```az subscription set -s {AZURE-SUB-ID}```
4. Check your active subscription with ```az account listi -o table```

**Create storage account**

Names etc..:
```
export RG="mydyskrg"
export ACCOUNT="dysktestaccount01"
```

1. Create a resource group with ```az group create --name ${RG} --location westus```
2. Create a storage account with  ``` az storage account create -n ${ACCOUNT} -g ${RG} -l westus --sku Standard_LRS ```

> If you are running dysk locally then choose azure location closest to you. If you are running on Azure then choose the **same** azure region where your test VM are.

3. Get account key via ```export KEY="$(az storage account keys list -g ${RG} -n ${ACCOUNT} | jq -r '.[0].value')"


## Premium vs Standard Storage Accounts SKUs

Details here: https://docs.microsoft.com/en-us/azure/azure-subscription-service-limits
tl;dr:
Premium SKU will give you higher iops for larger page blobs (Highest iops achieved at 4TB page blobs). Standard accounts will give you flat iops for all page blobs sizes but but with lower cieling and capped at the storage account level.

**How can make the best selection**
1. If you want fastest iops you will have to go for largest page blob on premium sku. 
2. For any other iops go for standard sku and span them over large number of accounts 

> max # of accounts per subscription is 200 account (250 with a support request).

Enjoy the Examples!
