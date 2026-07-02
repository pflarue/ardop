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

# Object files shared by the Unix-like platforms (Linux and macOS)
OBJS_UNIX = \
	$(BUILDDIR)/src/unix/os_util.o \

# Linux-only object files
OBJS_LIN = \
	$(BUILDDIR)/src/linux/ALSA.o \
	$(BUILDDIR)/src/linux/os_util.o \
	$(OBJS_UNIX) \

# Windows-only object files
OBJS_WIN = \
	$(BUILDDIR)/src/windows/Waveform.o \
	$(BUILDDIR)/src/windows/os_util.o \

# macOS-only object files
OBJS_MAC = \
	$(BUILDDIR)/src/macos/CoreAudio.o \
	$(BUILDDIR)/src/macos/os_util.o \
	$(OBJS_UNIX) \

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
# LDFLAGS (notably the linker-map flag) and CC are platform-specific and are
# set in the platform-selection block below.
CC = gcc
CC_NATIVE ?= $(CC)

# How to wrap a symbol with ld
LDWRAP := -Wl,--wrap=

# Path to txt2c executable; will be built if it does not already exist
TXT2C ?=

# Set WIN32 to non-empty to cross-compile on Linux.
# Leave empty for OS auto-detection
WIN32 ?= $(filter $(OS),Windows_NT)

# Used to auto-detect macOS (Darwin) for native builds.
UNAME_S = "NONPOSIX"
ifeq ($(WIN32),)
UNAME_S := $(shell uname -s)
endif

# Determine build directory based on target platform
ifneq ($(WIN32),)
PLATFORM := windows
OBJS += $(OBJS_WIN)
LDLIBS += -lwsock32 -lwinmm -lsetupapi -lws2_32 -lhid
LDFLAGS = -Xlinker -Map=$(BUILDDIR)/output.map
else ifeq ($(UNAME_S),Darwin)
PLATFORM := macos
OBJS += $(OBJS_MAC)
# Suppress the unhelpful gnu-folding-constant warning.
# See comments at https://ffmpeg.org/pipermail/ffmpeg-cvslog/2025-May/148334.html
#  about using -fno-common for compiler behavior on Apple similar to the default
#  used by gcc as used for Linux and Windows.
CFLAGS += -Wno-gnu-folding-constant -fno-common
# CoreAudio (audio I/O and device enumeration) and its supporting frameworks.
LDLIBS += -framework CoreAudio -framework AudioToolbox -framework CoreFoundation
# macOS ld64 spells the link-map option '-map <file>', unlike GNU ld's
# '-Map=<file>'.
LDFLAGS = -Xlinker -map -Xlinker $(BUILDDIR)/output.map
# Default to the system compiler (clang) on macOS unless overridden.
CC = cc
# Homebrew is not on the compiler/linker default search paths, so add its
# include and library directories.  This is where cmocka (required by
# `make test`) is found.  HOMEBREW_PREFIX is auto-detected via `brew` and
# works for both Apple Silicon (/opt/homebrew) and Intel (/usr/local)
# installs; it may be overridden on the command line if `brew` is not in PATH.
HOMEBREW_PREFIX ?= $(shell brew --prefix 2>/dev/null)
ifneq ($(HOMEBREW_PREFIX),)
CPPFLAGS += -I$(HOMEBREW_PREFIX)/include
LDFLAGS += -L$(HOMEBREW_PREFIX)/lib
endif
else
PLATFORM := linux
OBJS += $(OBJS_LIN)
LDLIBS += -lrt -lasound
LDFLAGS = -Xlinker -Map=$(BUILDDIR)/output.map
endif

# Build directory structure
BUILDDIR := build/$(PLATFORM)

# Platform-specific directory creation
ifeq ($(OS),Windows_NT)
MKDIR = if not exist "$(subst /,\,$1)" mkdir "$(subst /,\,$1)"
else
MKDIR = mkdir -p $1
endif

all: ardopcf

ardopcf: $(BUILDDIR)/ardopcf

$(BUILDDIR)/ardopcf: $(OBJS_EXE) $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LOADLIBES) $(LDLIBS)

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
		$(LDFLAGS) \
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
