# Build & Install #

## Build ##

### Dysk Kernel Module ###
If you don't want to build the module + CLI, you can download the binaries from the [release](https://github.com/khenidak/dysk/releases) - COMING SOON - page.


> Module has been tested against 4.10.x and 4.11+ kernel versions

1. Download kernel header

```bash
# Debian
sudo apt-get install linux-headers-$(uname -r)
```
2. Dependencies

```bash
sudo apt-get install -y update
sudo apt-get install -y build-essential
```

3. Build module

```bash
cd ./module
make 
```

> You can use ``` make clean ``` to cleanup the build artifacts 

### Install ###

```bash
cd module # or download location
sudo insmod dysk.ko

# to check that module loaded successfully 
lsmod | grep dysk
dmesg # Dysk leaves success init log line
```

### Dysk CLI ###

1. Dependencies

```bash
curl -O https://storage.googleapis.com/golang/go1.9.2.linux-amd64.tar.gz
sudo tar -C /usr/local -xzf go1.9.2.linux-amd64.tar.gz
export PATH="/usr/local/go/bin/:$PATH"
export GOPATH=$HOME/go
go get github.com/golang/dep
cd $GOPATH/src/github.com/golang/dep
go install ./...
```

2. Build cli

```bash
cd ./dyskctl 
make deps && make build
```

## Uninstall ##

```bash
sudo rmmod dysk
```

> Don't uninstall the module if you have dysk mounted (a limitation that should be soon removed)
