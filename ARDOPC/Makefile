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
ws_server.o Webgui.o webgui/gen-webgui.html.o webgui/gen-webgui.js.o

# Configuration:
CFLAGS = -g -MMD
LDLIBS = -lm -lpthread
LDFLAGS = -Xlinker -Map=output.map
CC = gcc

# Path to txt2c executable; will be built if it does not already exist
TXT2C ?=

ifeq ($(OS),Windows_NT)
OBJS += Waveout.o hid.o
LDLIBS += -lwsock32 -lwinmm -lsetupapi -lws2_32
else
OBJS += LinSerial.o ALSASound.o
LDLIBS += -lrt -lasound
endif

vpath %.c ../lib/rockliff ../lib/hid ../lib/rawhid ../lib/ws_server
vpath %.h ../lib/rockliff
vpath %.o ./

all: ardopcf

ardopcf: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LOADLIBES) $(LDLIBS)

# if txt2c is not provided, build it
ifeq ($(TXT2C),)
TXT2C := ../lib/txt2c/txt2c

# build txt2c directly and without our link libraries (none are required)
$(TXT2C): $(TXT2C).c
	$(CC) $^ -o $@

# mark build products for cleaning
CLEAN += $(TXT2C) $(TXT2C).exe
endif

# Use txt2c to convert webgui/FOO.xyz → FOO.xyz.c
#   The C symbol name will be FOO_xyz.
#   This is used to convert HTML and JavaScript to C sources.
#   The implicit rule will then compile them to FOO.xyz.o.
webgui/gen-%.c:: webgui/% | $(TXT2C)
	$(TXT2C) $< $@ $(subst .,_,$(notdir $<))

-include *.d

# 'make clean' deletes files produced by the build process.
# After using git checkout change branches, it is sometimes neccessary to run
# 'make clean' before running 'make' to produce a successful build.  Failure
# to run 'make clean' before using git checkout may sometimes leave build
# related files that must then be manually deleted.
CLEAN += ardopcf ardopcf.exe $(OBJS) $(OBJS:.o=.d) output.map

ifeq ($(OS),Windows_NT)
# on Windows, del requires backslash paths
clean :
	del /Q /F $(subst /,\,$(CLEAN))
else
clean :
	rm -f -- $(CLEAN)
endif
