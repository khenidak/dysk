#Build & Install #

##Build##

###Dysk Kernel Module###
If you don't want to build the module + CLI, you can download the binaries from the [release](https://github.com/khenidak/dysk/releases) page.


1. Download kernel header

```
# Debian
sudo apt-get install linux-headers-$(uname -r)
```

2. Build

```
cd ./module
make 
```

> You can use ``` make clean ``` to cleanup the build artifacts 

###Install###

```
cd module # or download location
sudo insmod dysk.ko

# to check that module loaded successfully 
lsmod | grep dysk
dmesg # Dysk leaves success init log line
```

###Dysk CLI###

> Make sure that you install and configure [Go](https://golang.org/doc/install) 

```
cd ./dyskctl 
go build .
```

##uninstall##

```
sudo rmmod dysk
```

> Don't uninstall the module if you have dysk mounted (a limitation that should be soon removed)
