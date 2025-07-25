#	ardopcf Makefile
#		For Linux, the default build requires gcc, make, and development
#		libraries for ALSA.
#			sudo apt install build-essential libasound2-dev
#			make
#
#		For macOS, the default build requires Xcode Command Line Tools.
#		The build uses Core Audio framework (no additional packages needed).
#			xcode-select --install
#			make
#
#		Fow Windows, the default build requires installation of a MinGW build
#		environment.  The easily installable packages from https://winlibs.com
#		available for 32-bit or 64-bit builds are suggested.  These are used to
#		build the Windows releases.  Installing these in `C:\Program Files` is
#		not recommended since that may require admin privileges.  Other build
#		environments may also work but are not tested.
#			mingw32-make
#
#	`make test` which builds the executable and also runs some tests also
#	requires installation of cmocka, which is not required for the default build.
#		On Debian/Ubuntu this is easily installed with:
#			sudo apt install libcmocka-dev
#
#		Package managers for other Linux distributions are also likely to
#		provide easy installation of cmocka.
#
#		In the following description of how to install cmocka for Windows, a
#		winlibs MinGW installation is assumed to be located at `C:\winlibs`
#		If installed elsewhere, substitute the appropriate path.  Putting the
#		cmocka files into the winlibs install directory avoids the need for further
#		configuration.  This uses git (available from https://git-scm.com/downloads/win)
#		to download the cmocka source code.
#
#		git clone https://git.cryptomilk.org/projects/cmocka.git
#		cd cmocka
#		mkdir build
#		cd build
#		cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="C:\winlibs" ..
#		mingw32-make
#		mingw32-make install
#
#	To cross-compile for Windows on Linux,
#		sudo apt install mingw-w64
#		make CC_NATIVE=gcc CC=i686-w64-mingw32-gcc-posix WIN32=1

.PHONY: all buildtest test

# list all object files and their directories
# keep sorted by filename
OBJS = \
	lib/rawhid/rawhid.o \
	lib/rockliff/rrs.o \
	lib/ws_server/ws_server.o \
	src/common/ARDOPC.o \
	src/common/ARDOPCommon.o \
	src/common/ardopSampleArrays.o \
	src/common/ARQ.o \
	src/common/BusyDetect.o \
	src/common/FEC.o \
	src/common/FFT.o \
	src/common/HostInterface.o \
	src/common/Locator.o \
	src/common/log_file.o \
	src/common/log.o \
	src/common/Modulate.o \
	src/common/Packed6.o \
	src/common/RXO.o \
	src/common/sdft.o \
	src/common/SoundInput.o \
	src/common/StationId.o \
	src/common/TCPHostInterface.o \
	src/common/txframe.o \
	src/common/wav.o \
	src/common/gen-webgui.html.o \
	src/common/gen-webgui.js.o \
	src/common/Webgui.o \
	src/common/noise.o \

# Linux-only object files
OBJS_LIN = \
	src/linux/ALSASound.o \
	src/linux/LinSerial.o \

# Windows-only object files
OBJS_WIN = \
	src/windows/Waveout.o \
	lib/hid/hid.o \

# macOS-only object files
OBJS_MAC = \
	src/macos/CoreAudioSound.o \
	src/macos/MacSerial.o \

# user-facing executables, like ardopcf
OBJS_EXE = \
	src/common/ardopcf.o \

# Detect operating system for test configuration
UNAME_S := $(shell uname -s)

# unit test executables
ifeq ($(UNAME_S),Darwin)
# Exclude tests that require --wrap (not supported on macOS)
# Include macOS-specific platform test
TESTS = \
	test/ardop/test_ARDOPCommon \
	test/ardop/test_HostInterface \
	test/ardop/test_Locator \
	test/ardop/test_Packed6 \
	test/ardop/test_StationId \
	test/ardop/test_MacOSPlatform
else
TESTS = \
	test/ardop/test_ARDOPCommon \
	test/ardop/test_HostInterface \
	test/ardop/test_Locator \
	test/ardop/test_log \
	test/ardop/test_Packed6 \
	test/ardop/test_StationId
endif

# unit test common code
TEST_OBJS_COMMON = \
	test/ardop/setup.o \

# define newline for use with foreach to run tests
define newline


endef

# Configuration:
CPPFLAGS += -Isrc -Ilib
CFLAGS = -g -MMD
LDLIBS = -lm -lpthread
CC = gcc
CC_NATIVE ?= $(CC)

# How to wrap a symbol with ld
ifeq ($(UNAME_S),Darwin)
# macOS linker doesn't support --wrap, so disable it
LDWRAP := 
else
LDWRAP := -Wl,--wrap=
endif

# Path to txt2c executable; will be built if it does not already exist
TXT2C ?=

# Set WIN32 to non-empty to cross-compile on Linux.
# Leave empty for OS auto-detection
WIN32 ?= $(filter $(OS),Windows_NT)

# Set platform-specific linker flags
ifeq ($(UNAME_S),Darwin)
LDFLAGS = 
# Add cmocka paths for macOS tests
CMOCKA_PREFIX = $(shell brew --prefix cmocka 2>/dev/null)
ifneq ($(CMOCKA_PREFIX),)
TEST_CPPFLAGS = -I$(CMOCKA_PREFIX)/include
TEST_LDFLAGS = -L$(CMOCKA_PREFIX)/lib
endif
else
LDFLAGS = -Xlinker -Map=output.map
endif

ifneq ($(WIN32),)
OBJS += $(OBJS_WIN)
LDLIBS += -lwsock32 -lwinmm -lsetupapi -lws2_32
else ifeq ($(UNAME_S),Darwin)
OBJS += $(OBJS_MAC)
LDLIBS += -framework CoreAudio -framework AudioUnit -framework CoreFoundation -framework IOKit
else
OBJS += $(OBJS_LIN)
LDLIBS += -lrt -lasound
endif

all: ardopcf

ardopcf: $(OBJS_EXE) $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LOADLIBES) $(LDLIBS)

# if txt2c is not provided, build it
ifeq ($(TXT2C),)
TXT2C := lib/txt2c/txt2c

# build txt2c directly and without our link libraries (none are required)
$(TXT2C): $(TXT2C).c
	$(CC_NATIVE) $^ -o $@

# mark build products for cleaning
CLEAN += $(TXT2C) $(TXT2C).exe
endif

# Use txt2c to convert webgui/FOO.xyz → FOO.xyz.c
#   The C symbol name will be FOO_xyz.
#   This is used to convert HTML and JavaScript to C sources.
#   The implicit rule will then compile them to FOO.xyz.o.
src/common/gen-%.c:: webgui/% | $(TXT2C)
	$(TXT2C) $< $@ $(subst .,_,$(notdir $<))

# `make buildtest` builds the test-case executables but does not run them
buildtest: $(TESTS)

# `make test` prints the name of each test file and then runs that test.
# running the test should indicate the tests run and whether they passed
# or failed.
test: buildtest
	$(foreach test, $(TESTS), @echo $(test):$(newline)@$(test)$(newline))

# rule to make test-case executables from their sources
test/ardop/test_%: test/ardop/test_%.c $(OBJS) $(TEST_OBJS_COMMON)
	$(CC) \
		$(CPPFLAGS) $(TEST_CPPFLAGS) \
		$(CFLAGS) \
		$(LDFLAGS) $(TEST_LDFLAGS) \
		$(patsubst %,$(LDWRAP)%,$(WRAP)) \
		$< \
		$(OBJS) \
		$(TEST_OBJS_COMMON) \
		-o $@ \
		$(LOADLIBES) \
		$(LDLIBS) \
		-lcmocka


# linkage overrides for unit tests
#   for tests that need only a subset of production code,
#   set OBJS to the .o files you want
#
#   for tests that need mock functions injected,
#   set WRAP to a space-separated list of functions to mock
test/ardop/test_log: OBJS := \
	src/common/log_file.o \
	src/common/log.o
test/ardop/test_log: WRAP := fopen fclose fwrite fflush freopen

# Skip building test_log on macOS (--wrap not supported)
ifeq ($(UNAME_S),Darwin)
test/ardop/test_log:
	@echo "Skipping test_log build on macOS: --wrap linker option not supported"
	@touch $@
endif

-include *.d

# 'make clean' deletes files produced by the build process.
# After using git checkout change branches, it is sometimes neccessary to run
# 'make clean' before running 'make' to produce a successful build.  Failure
# to run 'make clean' before using git checkout may sometimes leave build
# related files that must then be manually deleted.
CLEAN += \
	ardopcf \
	ardopcf.exe \
	$(OBJS) \
	$(OBJS:.o=.d) \
	$(OBJS_LIN) \
	$(OBJS_LIN:.o=.d) \
	$(OBJS_WIN) \
	$(OBJS_WIN:.o=.d) \
	$(OBJS_MAC) \
	$(OBJS_MAC:.o=.d) \
	$(OBJS_EXE) \
	$(OBJS_EXE:.o=.d) \
	$(TESTS) \
	$(TESTS:%=%.exe) \
	$(TESTS:%=%.d) \
	$(TEST_OBJS_COMMON) \
	$(TEST_OBJS_COMMON:.o=.d) \
	output.map \

ifeq ($(OS),Windows_NT)
# on Windows, del requires backslash paths
clean :
	del /Q /F $(subst /,\,$(CLEAN))
else
clean :
	rm -f -- $(CLEAN)
endif
