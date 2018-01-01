obj-m := dysk.o
dysk-objs := dysk_utils.o dysk_worker.o dysk_bdd.o az.o

all:
	        make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules
clean:
	        make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
