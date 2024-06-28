#	ardopcf Makefile
#		For Linux, the default build requires only gcc and libraries typically
#		installed by default.
#			cd ardop/ARDOPC
#			make
#
#		Fow Windows, the default build requires installation of a MinGW build
#		environment.  The easily installable packages from https://winlibs.com
#		available for 32-bit or 64-bit builds are suggested.  These are used to
#		build the Windows releases.  Installing these in `C:\Program Files` is
#		not recommended since that may require admin privileges.  Other build
#		environments may also work but are not tested.
#			cd ardop\ARDOPC
#			mingw32-make

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
OBJS += Waveout.o hid.o
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

# 'make clean' deletes files produced by the build process.
# After using git checkout change branches, it is sometimes neccessary to run
# 'make clean' before running 'make' to produce a successful build.  Failure
# to run 'make clean' before using git checkout may sometimes leave build
# related files that must then be manually deleted.
clean :
	$(RM) ardopcf ardopcf $(OBJS) $(OBJS:.o=.d) output.map
