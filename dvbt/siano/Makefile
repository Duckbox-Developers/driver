smsmdtv-objs := smscoreapi.o sms-cards.o smsendian.o smsir.o

obj-m += smsmdtv.o smsdvb.o
obj-m += smsusb.o
#obj-m += smssdio.o
#EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
#EXTRA_CFLAGS += $(extra-cflags-y) $(extra-cflags-m)
#-DCONFIG_AS102_USB 

EXTRA_CFLAGS += -DLINUX -Idrivers/media/dvb/dvb-core
