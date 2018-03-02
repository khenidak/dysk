#! /bin/bash
set -eo pipefail

dysk_base_dir=${DYSK_BASE_DIR:-"/tmp"}
dysk_tag=${DYSK_TAG:-"e10e02f7e1644f3f4e2931feb7d9e0bf56ebcd53"}
dysk_src="${dysk_base_dir}/${dysk_tag}/dysk/"
install_mode="${INSTALL_MODE:-"STANDALONE"}"
function clone_dysk()
{
	if [[ ! -d  "${dysk_src}" ]]; then
		echo "INF: No dysk source found @ ${dysk_src} .. cloning"
		dysk_parent_dir="$(dirname "${dysk_src}")"
		
		mkdir -p "${dysk_parent_dir}"
		cd "${dysk_parent_dir}"
		
		git clone https://github.com/khenidak/dysk.git 
		cd "${dysk_src}"
		git checkout "${dysk_tag}"
	else
		cd "${dysk_src}"
		echo "INF: found dysk source @ ${dysk_src}. will pull to ensure latest udpates"
		git pull
	fi
}

function build_dysk()
{
	clone_dysk
	if [[ ! -d "/lib/modules/$(uname -r)" ]]; then
		echo "INF: headers for $(uname -r) were not found, downloading"
		apt update && apt install linux-headers-$(uname -r)
	fi

	cd "${dysk_src}/module/"
	if [[ ! -f "${dysk_src}/module/dysk.ko" ]]; then
		echo "INF: ${dysk_src}/module/dysk.ko is not there, building it"
		make clean && make
	fi
}

function dysk_bin_ok()
{
	local dysk_ver_m=""

	# Source there?
	if [[ ! -d  "${dysk_src}" ]]; then
		echo -n "0"
    return
	fi

	# built before?
	if [[ ! -f "${dysk_src}/module/dysk.ko" ]];then 
		echo -n "0"
    return
	fi

	cd "${dysk_src}/module/"

	# Match version magic?
	dysk_ver_m="$(modinfo dysk.ko | grep vermagic | awk '{print $2}')"
	if [[ "$dysk_ver_m" != "$(uname -r)" ]]; then
		echo -n "0"
	fi
}
function install_dysk_module()
{
	#if installed
	if [[ "" == "$(lsmod | grep dysk)" ]]; then
		# if bin ok
		if [[ "0" == "$(dysk_bin_ok)" ]];then
			build_dysk
		fi
		# install
		echo "INF: installing dysk kernel module tag:${dysk_tag}"
		insmod "${dysk_src}/module/dysk.ko"
	else
		echo "INF: dysk found on this machine"
	fi

	#modinfo dysk
}

function main()
{
	install_dysk_module	
  if [[ "STANDALONE" !=  "${install_mode}" ]]; then
    #https://github.com/kubernetes/kubernetes/issues/17182
    # if we are running on kubernetes cluster as a daemon set we should
    # not exit otherwise, container will restart and goes into crashloop (even if exit code is 0)
    while true; do echo "install done, daemonset sleeping" && sleep 300; done
  fi
}

#start here..
main

