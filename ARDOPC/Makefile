#	ardopcf Makefile

.PHONY: all

OBJS = ARDOPCommon.o BusyDetect.o \
ARDOPC.o ardopSampleArrays.o ARQ.o FFT.o FEC.o HostInterface.o Modulate.o rrs.o txframe.o \
SoundInput.o TCPHostInterface.o wav.o RXO.o sdft.o rawhid.o \
ws_server.o Webgui.o webgui_html.o webgui_js.o

# Configuration:
CFLAGS = -g -MMD
LDLIBS = -lm -lpthread
LDFLAGS = -Xlinker -Map=output.map
CC = gcc

ifeq ($(OS),Windows_NT)
OBJS += WinSerial.o Waveout.o hid.o
LDLIBS += -lwsock32 -lwinmm -lsetupapi -lws2_32
RM = del /Q /F
else
OBJS += LinSerial.o ALSASound.o
LDLIBS += -lrt -lasound
RM = rm -f --
endif

vpath %.c ../ARDOPCommonCode ../lib/rockliff ../lib/hid ../lib/rawhid ../lib/ws_server
vpath %.h ../ARDOPCommonCode ../lib/rockliff
vpath %.o ./

all: ardopcf

ardopcf: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LOADLIBES) $(LDLIBS)

-include *.d

clean :
	$(RM) ardopcf ardopcf $(OBJS) $(OBJS:.o=.d) output.map
