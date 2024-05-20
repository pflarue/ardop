#	ardopcf Makefile

OBJS = ARDOPCommon.o LinSerial.o KISSModule.o pktARDOP.o pktSession.o BusyDetect.o i2cDisplay.o ALSASound.o \
ARDOPC.o ardopSampleArrays.o ARQ.o FFT.o FEC.o HostInterface.o Modulate.o rrs.o txframe.o \
SoundInput.o TCPHostInterface.o SCSHostInterface.o wav.o RXO.o sdft.o rawhid.o \
ws_server.o Webgui.o webgui_html.o webgui_js.o

# Configuration:
CFLAGS = -DLINBPQ -MMD -g 
CC = gcc

vpath %.c ../ARDOPCommonCode ../lib/rockliff ../lib/bpq32 ../lib/rawhid ../lib/ws_server
vpath %.h ../ARDOPCommonCode ../lib/rockliff
vpath %.o ./
			                       
all: ardopcf
			
ardopcf: $(OBJS)
	gcc $(OBJS) -Xlinker -Map=output.map -lrt -lm -lpthread -lasound -o ardopcf


-include *.d

clean :
	rm ardopcf $(OBJS) $(OBJS:.o=.d) output.map

