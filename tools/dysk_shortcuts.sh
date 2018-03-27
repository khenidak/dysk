#!/bin/bash
alias install_dysk="docker run --rm -it --privileged -v /usr/src:/usr/src  -v /lib/modules:/lib/modules khenidak/dysk-installer:0.6"
alias dyskctl="docker run --rm -it --privileged -v /etc/ssl/certs:/etc/ssl/certs:ro khenidak/dysk-cli:0.6"
