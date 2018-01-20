FROM alpine:3.5

WORKDIR /bin

RUN apk add --no-cache bash jq
ADD ./dyskctl /bin/dyskctl
ADD ./dysk /bin/dysk
ADD ./install.sh /bin/install_dysk_flexvol.sh

ENTRYPOINT ["/bin/install_dysk_flexvol.sh"] 
