FROM ubuntu:17.04
RUN apt update && apt -y install kmod  build-essential bash git && rm -r /var/lib/apt/lists/*


COPY ./install.sh /etc/install_dysk.sh

# run it
CMD ["/etc/install_dysk.sh"]

