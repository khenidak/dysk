# Build & Install #

## Shortcut ##

The easiest way to quickly run dysk is to source a shortcut script. The script defines alias for installing 
dysk via a container. Also a shortcut for running dyskctl (cli) via a container

```bash
# *ALWAYS READ SCRIPTS BEFORE USING THEM* #
cd /tmp # keep it clean
wget -q https://raw.githubusercontent.com/khenidak/dysk/master/tools/dysk_shortcuts.sh && source dysk_shortcuts.sh
cd -

#run the install
install_dysk

# run the cli via
dyskctl
```

> The below instructions detailed for manual build/install experience or via running containers.


## dysk Kernel Module ##
If you don't want to build the module + CLI, you can download the binaries from the [release](https://github.com/khenidak/dysk/releases) - COMING SOON - page.


> Module has been tested against 4.10.x++ kernel versions

### Using Docker ###

This docker image, downloads and build dysk kernel module based on your kernel version

1. It has to run using ```--privileged``` because it installs a kernel module
2. directories ```/usr/src``` and ```/lib/modules``` are needed for module install (and header downloads)
3. If you want to maintain a local version (to avoid clone then rebuild with every run) mount a host directory at this container ```/tmp```.
4. The container image by default uses the first stable tag dysk had, you can override this with setting ```DYSK_TAG``` environment variable.  


> The image now depends on Ubuntu image, a slimer alpine version is coming soon

```bash
docker run --rm \
	-it \
	--privileged \
	-v /usr/src:/usr/src \
	-v /lib/modules:/lib/modules \
	khenidak/dysk-installer:0.2
```

### Manual Build & Install ###
1. Download kernel headers

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

**Install**

```bash
#manual
cd module # or download location
sudo insmod dysk.ko

#or (on repo root)
make install-module

# to check that module loaded successfully 
lsmod | grep dysk
dmesg # Dysk leaves success init log line
```

## dysk cli  ##

### Using Docker ###

A docker image is the easiest way to use the command line without clone + build (manual steps).

```bash
# run: list all dysks mounted on local box
docker run --rm \
	-it --privileged \
	-v /etc/ssl/certs:/etc/ssl/certs:ro \
	khenidak/dysk-cli:0.1 \ 
	list

# run a mount using auto create
docker run --rm \
	-it --privileged \
	-v /etc/ssl/certs:/etc/ssl/certs:ro \
	khenidak/dysk-cli:0.1 \ 
	mount auto-create -a {account-name} -k {account-key}
```

### Manual Build ###
1. Dependencies

```bash
curl -O https://storage.googleapis.com/golang/go1.9.2.linux-amd64.tar.gz
sudo tar -C /usr/local -xzf go1.9.2.linux-amd64.tar.gz

# You should consider configuring your paths in bash your bash profile
export PATH="/usr/local/go/bin/:$PATH"
export GOPATH=$HOME/go
export PATH="$GOPATH/bin:$PATH" # add go bins to your path

go get github.com/golang/dep
cd $GOPATH/src/github.com/golang/dep
go install ./...
```

2. Build cli

```bash
cd ./dyskctl  # $GOPATH/src/github.com/khenidak/dysk/dyskctl
make deps && make build

#or on repo root
make build-cli
```

## Uninstall ##

```bash
sudo rmmod dysk
```

> Don't uninstall the module if you have dysk mounted (a limitation that should be soon removed)
