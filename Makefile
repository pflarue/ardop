#	ardopcf Makefile
#		For Linux, the default build requires gcc, make, and development
#		libraries for ALSA.
#			sudo apt install build-essential libasound2-dev
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

.PHONY: all buildtest test clean cleanall

# list all object files and their directories
# keep sorted by filename
OBJS = \
	$(BUILDDIR)/lib/rockliff/rrs.o \
	$(BUILDDIR)/lib/ws_server/ws_server.o \
	$(BUILDDIR)/src/common/ARDOPC.o \
	$(BUILDDIR)/src/common/ARDOPCommon.o \
	$(BUILDDIR)/src/common/ardopSampleArrays.o \
	$(BUILDDIR)/src/common/ARQ.o \
	$(BUILDDIR)/src/common/BusyDetect.o \
	$(BUILDDIR)/src/common/FEC.o \
	$(BUILDDIR)/src/common/FFT.o \
	$(BUILDDIR)/src/common/HostInterface.o \
	$(BUILDDIR)/src/common/Locator.o \
	$(BUILDDIR)/src/common/log_file.o \
	$(BUILDDIR)/src/common/log.o \
	$(BUILDDIR)/src/common/Modulate.o \
	$(BUILDDIR)/src/common/Packed6.o \
	$(BUILDDIR)/src/common/RXO.o \
	$(BUILDDIR)/src/common/sdft.o \
	$(BUILDDIR)/src/common/SoundInput.o \
	$(BUILDDIR)/src/common/StationId.o \
	$(BUILDDIR)/src/common/TCPHostInterface.o \
	$(BUILDDIR)/src/common/txframe.o \
	$(BUILDDIR)/src/common/wav.o \
	$(BUILDDIR)/src/common/gen-webgui.html.o \
	$(BUILDDIR)/src/common/gen-webgui.js.o \
	$(BUILDDIR)/src/common/Webgui.o \
	$(BUILDDIR)/src/common/noise.o \
	$(BUILDDIR)/src/common/ptt.o \
	$(BUILDDIR)/src/common/eutf8.o \

# Linux-only object files
OBJS_LIN = \
	$(BUILDDIR)/src/linux/ALSA.o \
	$(BUILDDIR)/src/linux/os_util.o \

# macOS-only object files (stubs for initial scaffolding)
OBJS_MAC = \
	$(BUILDDIR)/src/macos/CoreAudioSound.o \
	$(BUILDDIR)/src/macos/MacSerial.o \
	$(BUILDDIR)/src/macos/os_util.o \

# Windows-only object files
OBJS_WIN = \
	$(BUILDDIR)/src/windows/Waveform.o \
	$(BUILDDIR)/src/windows/os_util.o \

# user-facing executables, like ardopcf
OBJS_EXE = \
	$(BUILDDIR)/src/common/ardopcf.o \

# unit test executables
TESTS = \
	$(BUILDDIR)/test/ardop/test_ARDOPCommon \
	$(BUILDDIR)/test/ardop/test_HostInterface \
	$(BUILDDIR)/test/ardop/test_Locator \
	$(BUILDDIR)/test/ardop/test_log \
	$(BUILDDIR)/test/ardop/test_Packed6 \
	$(BUILDDIR)/test/ardop/test_StationId \
	$(BUILDDIR)/test/ardop/test_ARDOPCommon_processargs \
	$(BUILDDIR)/test/ardop/test_eutf8 \
	$(BUILDDIR)/test/ardop/test_txframe \

# unit test common code
TEST_OBJS_COMMON = \
	$(BUILDDIR)/test/ardop/setup.o \

# define newline for use with foreach to run tests
define newline


endef

# Configuration:
CPPFLAGS += -Isrc -Ilib
CFLAGS = -g -MMD
LDLIBS = -lm -lpthread
LDFLAGS =
CC = gcc
CC_NATIVE ?= $(CC)

# Optional overrides for cmocka include and library paths (default empty).
# Users on macOS with Homebrew can set, e.g.:
#   make CMOCKA_INC=/opt/homebrew/include CMOCKA_LIB=/opt/homebrew/lib test
CMOCKA_INC ?=
CMOCKA_LIB ?=
ifneq ($(strip $(CMOCKA_INC)),)
CPPFLAGS += -I$(CMOCKA_INC)
endif
ifneq ($(strip $(CMOCKA_LIB)),)
LDFLAGS += -L$(CMOCKA_LIB)
endif

# How to wrap a symbol with ld
LDWRAP := -Wl,--wrap=

# Path to txt2c executable; will be built if it does not already exist
TXT2C ?=

# Set WIN32 to non-empty to cross-compile on Linux.
# Leave empty for OS auto-detection
WIN32 ?= $(filter $(OS),Windows_NT)

# Determine build directory based on target platform
UNAME_S := $(shell uname -s)
ifneq ($(WIN32),)
PLATFORM := windows
OBJS += $(OBJS_WIN)
LDLIBS += -lwsock32 -lwinmm -lsetupapi -lws2_32 -lhid
else
	ifeq ($(UNAME_S),Darwin)
		PLATFORM := macos
		OBJS += $(OBJS_MAC)
		# Apple CoreAudio frameworks (no new external deps)
		LDLIBS += -framework AudioToolbox -framework AudioUnit -framework CoreAudio -framework CoreFoundation
		# Placeholder for future HID/PTT support:
		# LDLIBS += -framework IOKit
	else
		PLATFORM := linux
		OBJS += $(OBJS_LIN)
		LDLIBS += -lrt -lasound
	endif
endif

# Build directory structure
BUILDDIR := build/$(PLATFORM)

# Detect Homebrew-installed cmocka on macOS and add include/lib paths so tests build
ifeq ($(PLATFORM),macos)
CMOCKA_HEADER := $(firstword $(wildcard /opt/homebrew/include/cmocka.h /usr/local/include/cmocka.h))
ifneq ($(CMOCKA_HEADER),)
	CMOCKA_INCDIR := $(dir $(CMOCKA_HEADER))
	# Prefer matching lib directory to the header location
	ifneq ($(wildcard /opt/homebrew/lib/libcmocka.dylib),)
		CMOCKA_LIBDIR := /opt/homebrew/lib
	else ifneq ($(wildcard /usr/local/lib/libcmocka.dylib),)
		CMOCKA_LIBDIR := /usr/local/lib
	endif
	CPPFLAGS += -I$(CMOCKA_INCDIR)
	ifneq ($(CMOCKA_LIBDIR),)
		LDLIBS += -L$(CMOCKA_LIBDIR)
	endif
else
	# If cmocka isn't present, tests will fail to compile; provide a hint when invoking test targets
	ifneq (,$(filter test buildtest,$(MAKECMDGOALS)))
		$(info NOTE: cmocka not found under /opt/homebrew or /usr/local. Install with: brew install cmocka)
	endif
endif
endif

# macOS: exclude wrap-dependent test_log until cmocka & wrap semantics validated
ifeq ($(PLATFORM),macos)
TESTS := $(filter-out $(BUILDDIR)/test/ardop/test_log,$(TESTS))
# test_ARDOPCommon_processargs relies on GNU ld --wrap, unavailable on macOS ld64
TESTS := $(filter-out $(BUILDDIR)/test/ardop/test_ARDOPCommon_processargs,$(TESTS))
# Disable symbol wrapping on macOS (no ld --wrap flags)
LDWRAP :=
endif

# Platform-specific directory creation
ifeq ($(OS),Windows_NT)
MKDIR = if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
else
MKDIR = mkdir -p $1
endif

all: ardopcf

ardopcf: $(BUILDDIR)/ardopcf

$(BUILDDIR)/ardopcf: $(OBJS_EXE) $(OBJS)
	# macOS ld64 rejects -Map option; only use map file on non-macOS
	@if [ "$(PLATFORM)" = "macos" ]; then \
		$(CC) $(LDFLAGS) $^ -o $@ $(LOADLIBES) $(LDLIBS); \
	else \
		$(CC) $(LDFLAGS) -Xlinker -Map=$(BUILDDIR)/output.map $^ -o $@ $(LOADLIBES) $(LDLIBS); \
	fi

# if txt2c is not provided, build it
ifeq ($(TXT2C),)
TXT2C := $(BUILDDIR)/lib/txt2c/txt2c

# build txt2c directly and without our link libraries (none are required)
$(TXT2C): lib/txt2c/txt2c.c
	@$(call MKDIR,$(dir $@))
	$(CC_NATIVE) $^ -o $@

# mark build products for cleaning
CLEAN += $(TXT2C) $(TXT2C).exe
endif

# Use txt2c to convert webgui/FOO.xyz → FOO.xyz.c
#   The C symbol name will be FOO_xyz.
#   This is used to convert HTML and JavaScript to C sources.
#   The implicit rule will then compile them to FOO.xyz.o.
$(BUILDDIR)/src/common/gen-%.c:: webgui/% | $(TXT2C)
	@$(call MKDIR,$(dir $@))
	$(TXT2C) $< $@ $(subst .,_,$(notdir $<))

# Keep generated C files (don't delete them as intermediate files)
.PRECIOUS: $(BUILDDIR)/src/common/gen-%.c

# `make buildtest` builds the test-case executables but does not run them
buildtest: $(TESTS)

# `make test` prints the name of each test file and then runs that test.
# On macOS, install cmocka via: brew install cmocka.
# running the test should indicate the tests run and whether they passed
# or failed.
test: buildtest
	$(foreach test, $(TESTS), @echo $(test):$(newline)@$(test)$(newline))

# rule to make test-case executables from their sources
$(BUILDDIR)/test/ardop/test_%: test/ardop/test_%.c $(OBJS) $(TEST_OBJS_COMMON)
	@$(call MKDIR,$(dir $@))
	$(CC) \
		$(CPPFLAGS) \
		$(CFLAGS) \
		$(if $(LDWRAP),$(patsubst %,$(LDWRAP)%,$(WRAP))) \
		$< \
		$(OBJS) \
		$(TEST_OBJS_COMMON) \
		-o $@ \
		$(LDFLAGS) \
		$(LOADLIBES) \
		$(LDLIBS) \
		-lcmocka

# linkage overrides for unit tests
#   for tests that need only a subset of production code,
#   set OBJS to the .o files you want
#
#   for tests that need mock functions injected,
#   set WRAP to a space-separated list of functions to mock
$(BUILDDIR)/test/ardop/test_log: OBJS := \
	$(BUILDDIR)/src/common/log_file.o \
	$(BUILDDIR)/src/common/log.o
$(BUILDDIR)/test/ardop/test_log: WRAP := fopen fclose fwrite fflush freopen
$(BUILDDIR)/test/ardop/test_ARDOPCommon_processargs: WRAP := \
	printf puts ardop_log_start InitAudio \
	GetCM108Strlist GetSerialStrlist updateWebGuiNonAudioConfig \
	OpenCOMPort tcpconnect OpenCM108 OpenSoundCapture OpenSoundPlayback \

# Implicit rules to build object files in build directory
$(BUILDDIR)/%.o: %.c
	@$(call MKDIR,$(dir $@))
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Include dependency files
-include $(OBJS:.o=.d) $(OBJS_EXE:.o=.d) $(TEST_OBJS_COMMON:.o=.d)

# 'make clean' deletes files produced by the build process.
# After using git checkout change branches, it is sometimes neccessary to run
# 'make clean' before running 'make' to produce a successful build.  Failure
# to run 'make clean' before using git checkout may sometimes leave build
# related files that must then be manually deleted.

ifeq ($(OS),Windows_NT)
# on Windows, use rmdir for directories
clean :
	@if exist "$(subst /,\,$(BUILDDIR))" rmdir /S /Q "$(subst /,\,$(BUILDDIR))"
cleanall :
	@if exist build rmdir /S /Q build
else
clean :
	rm -rf $(BUILDDIR)
cleanall :
	rm -rf build
endif
