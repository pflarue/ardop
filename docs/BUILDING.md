# Building ardopcf from source

## Why build from source?

Building **ardopcf** from source may be required if binaries compatible with your hardware and operating system are not available in the [latest release](https://github.com/pflarue/ardop/releases/latest).

Building from the source code found in the `develop` branch provides the opportunity to try recent updates to **ardopcf** that were made since the latest release.  **Warning:** contents of the `develop` branch may contain new bugs that will hopefully be identified and fixed before they are merged into the `master` branch for creation of a new release.

By making the source code for **ardopcf** available with only the minimal restrictions of the popular MIT Open Source [LICENSE](../LICENSE), the authors intend to encourage others to learn from, experiment with, and build upon it.  To do that, you need to build from the source code.  In addition to the restrictions found in the LICENSE, the authors request that you call it something other than ARDOP if you use this source code to develop something that is not over-the-air compatible with **ardopcf** and other related Ardop implementations.

## Do I need experience programming to build **ardopcf** from source?

No.  You do not need programming experience to build **ardopcf** from source.  One of the design goals of **ardopcf** is to make building from source as simple as possible.  If you have difficulty with the following instructions, please join the free users subgroup of ardop.groups.io and ask for help.

## Does **ardopcf** work with my hardware and operating system.

**ardopcf** is built and tested on computers with Intel/AMD processors having 32-bit and 64-bit Windows operating systems.  It is also built and tested both on computers having Linux operating systems including those with Intel/AMD and ARM processors.  Specifically, Raspberry Pi Zero W and Zero 2W computers are used to build and test **ardopcf** for ARM processors.  Users have reported success building and using **ardopcf** on some addional Linux machines.

I don't know whether anyone has tried building **ardopcf** for macOS or any other operating system.  Anyone who attempts to build for another OS is encouraged to send a message to the free users subgroup of ardop.groups.io to let us know whether it worked.  If it didn't work well, perhaps someone from that group will be able to help.

## Building ardopcf for Linux:

Building **ardopcf** from source mostly doesn't require root/admin privledges.  It does require `libasound2-dev` and several items installed via the `build-essential` meta-package.  `libasound2-dev` provides the development files for the ALSA sound system library.  `git` also provides a convenient way to download the **ardopcf** source code and choose the branch you want to build.  Since there are other ways to get the source code, `git` is not strictly required, but the instructions provided will assume that it is available.  Many Linux distributions install `build-essential` and `git`, but not `libasound2-dev` by default.  There may be a way to install these without root access, but if so, I am not familiar with it.  So, if you want to build **ardopcf** on a machine without root access and where these packages are not already installed, then I recommend that you ask your system administrator to install these them for you.  Then follow these instructions, skipping the first step.

Lines staring with # are comments.  Type (or copy and paste) the non-comment lines at the command line.  I recommend doing one line at a time so that you can identify which line causes any errors that may be produced.  The comments are there for your benefit.  Please read and understand them as you go along.  A bash script could have been produced to do this for you.  However, I recommend that you go through the process manually.  Among other things, the understanding of what each step in the process is doing may help you to ask more specific questions if you encounter a problem and need help.

```
# 1. Install other software that ardopcf requires to build.
# 'sudo' means do this with root privledges and may prompt you for a password.
# 'apt' is a package manager used by the very popular Debian/Ubuntu based Linux
# distributions and their derivatives.  If your system does not have 'apt', you will
# need to use whatever package manager is available.  In that case, 'build-essential'
# might also not be the target you need.
sudo apt install build-essential libasound2-dev git

# 2. Download the source code for ardopcf from GitHub.com.
# Before doing this you may want to create and/or move to a different directory.
# I use ~/repos as the location of all projects cloned from GitHub.
git clone https://github.com/pflarue/ardop.git
# That should have created a new ardop directory.  Change to it.
cd ardop

# OPTIONAL: If you prefer to use changes that have been made since the last ardopcf
# release, the following step selects the lastest version of the 'develop' branch.
# This may include improvements over the most recently released version, but it may
# also include changes that have not been tested as well as the released version.
# If you skip this step, you will be building from the 'master' branch, which is what
# was used to build the most recent release of ardopcf.
git checkout develop

# 3. Build ardopcf.
# If you are building from the master branch, you must change to the ARDOPC directory
# where the Makefile is located.  The develop branch has moved the Makefile to the
# repository's top level directory so that this is not required.  (It also eliminated
# the ARDOPC subdirectory)
cd ARDOPC
ls Makefile

# If the following produces one or more errors, then something went wrong that needs
# to be resolved.  If it produces only warnings, then the excutable file named ardopcf
# is produced.  Future updates to ardopcf will hopefully reduce the number of warnings.
make

# 4. Verify that it worked.
# If the build worked correctly, the following line should print the help screen.
./ardopcf -h

# 5. Move the executable to a directory in your $PATH.

# 5a. IF you have root access, and you want ardopcf to be available to all users, put it
# in /usr/local/bin
sudo cp ardopcf /usr/local/bin

# 5b. IF you do not have root access, then put it in your personal bin directory $HOME/bin.
# It is possible that $HOME/bin does not exist.  If not, you will get an error like:
# cp: cannot create regular file '/home/username/bin/ardopcf': No such file or directory
# If that occurs do 'mkdir $HOME/bin' first, then try again.  In this case you probably
# also need to logout and log back in before $HOME/bin will be added to your $PATH
cp ardopcf $HOME/bin/ardopcf

# Now typeing 'ardopcf -h' at the command line should work from any directory.
```
See [USAGE.md](USAGE.md) for guideance on use of **ardopcf**.


## Building ardopcf for Windows using MinGW:

### Downloading the ardopcf source code

Unlike the instructions for building **ardopcf** for Linux, these instuctions will not use `git` to clone (download) the source code.  However, if you are interested in studying or making changes to the source code, you probably want to install and learn to use `git`.  Using `git` may also be advantageous if you intend to build from the `develop` branch to try changes that have been made to **ardopcf** since that last release.  In this case, using `git` allows you easily continue to track and use the latest version of the `develop` branch as further changes are made to it.  See https://git-scm.com/docs to learn more about `git` and https://git-scm.com/downloads/win to get the version for Windows.

At https://github.com/pflarue/ardop click on the green `Code` button, and then on `Download ZIP`.  By default this will download the code from the master branch corresponding to the most recent release of **ardopcf**.  If you want to include changes made since the last release, then before clicking on `Code` click on `master` and select `develop` from the pulldown.  This may include improvements over the most recently released version, but it may also include changes that have not been tested as well as the released version.

When the download is complete, use Windows File Explorer to find the zip file in the Downloads directory.  Right click on it and choose `Extract All...`.  Choose a suitable directory.  A path that has any spaces in it may cause problems, so choose one without any.  Consider using `C:\Users\<USERNAME>` which will create `C:\Users\<USERNAME>\ardop-master`.

### Install MinGW

The **ardopcf** source code can be compiled into 32-bit or 64-bit Windows native binaries using Mingw32.  It is probably not compatible with the Microsoft Visual C++ compiler system.

MinGW stands for Minimalist GNU for Windows.  It provides a compiler very similar to the GNU C compiler that is used on Linux systems, but which produces native Windows binaries.  Compared to using Microsoft's C compiler system, using MinGW makes it simpler to create a program like **ardopcf** which can run on both Linux and Windows.

Several internet sites provide Pre-built toolchains and packages to simplify installing MinGW-64, the version of MinGW that produces 64-bit (or optionally 32-bit) windows native binaries.  I recommend using a Zip archive available from https://winlibs.com.  These are intended to be easy to install and use, and they are what are used to create the **ardopcf** release binaries.  There are a lot of different options available to download from that site.  **ardopcf** v1.0.4.1.3 for Windows is built with [Win64 GCC 14.2.0 (with POSIX threads) - without LLVM/Clang/LLD/LLDB](https://github.com/brechtsanders/winlibs_mingw/releases/download/14.2.0posix-19.1.1-12.0.0-ucrt-r2/winlibs-x86_64-posix-seh-gcc-14.2.0-mingw-w64ucrt-12.0.0-r2.zip) and [Win32 GCC 14.2.0 (with POSIX threads) - without LLVM/Clang/LLD/LLDB](https://github.com/brechtsanders/winlibs_mingw/releases/download/14.2.0posix-19.1.1-12.0.0-ucrt-r2/winlibs-i686-posix-dwarf-gcc-14.2.0-mingw-w64ucrt-12.0.0-r2.zip).  These are probably a good default choice to use.  All Windows 11 and most Windows 10 operating systems are 64-bit.  To find out if a Windows 10 operating system is 32 or 64 bit press the **Start** button and select **Settings**, click on **System** then on **About** and read the line starting with **System type**.  See the details below if you have a Windows system older than Windows 10.

<details>
	<summary>Details for choosing an specific version of MinGW-64 from https://winlibs.com. </summary>

	Using one of the MinGW-64 Zip archives linked above is an good default choice.  The following describes how to choose an alternative.  This may be appropriate if a newer version has been released since the last **ardopcf** release.

	The files available for download from https://winlibs.com are grouped by those based on the UCRT runtime followed by those based on the MSVCRT runtime.  Choose from the ones that use the UCRT runtime.  The UCRT runtime library that these require is a standard part of all Windows 10 and later operating systems.  If your operating system is Windows Vista SP2 up to Windows 8.1, you can get UCRT by manually installing the [Universal C Runtime update](https://support.microsoft.com/en-us/topic/update-for-universal-c-runtime-in-windows-c0514201-7fe6-95a3-b0a5-287930f3560c).  UCRT is a newer system that replaces MSVCRT.  It has better support for recent versions of Windows, and it conforms better to the standards that define how C compilers should work.  **ardopcf** has not been tested with MSVCRT.  Using MSVCRT **might** allow **ardopcf** to run on older Windows machines, or it might require modifications to the source code to work with that older system.

	Under UCRT runtime, the packages at the top of the list are the most recent, so choosing the first one on the list is usually a good choice.  Each version is available in Win32 and Win64 options.  As described above, all Windows 11 and most Windows 10 operating systems are 64-bit.

	You may notice that there are packages labeled '(with POSIX threads)' and '(with MCF threads)' having the same version and release numbers.  These differ in how they support multi-threaded programs.  Since **ardopcf** is a single threaded program, it does not matter which of these you choose.  The '(with POSIX threads)' versions also include archives with and without 'LLVM/Clang/LLD/LLDB'  Building **ardopcf** does not require any of these parts, so you may choose the package 'without LLVM/Clang/LLD/LLDB' since it will be a smaller download and take less space on your computer.  It appears that none of the '(with MCF threads)' packages include these parts, so they don't have separate options to exclude them from the download.

	`7-Zip` and `Zip` format archives are available for each download.  Since Windows can extract `Zip` files, choose the `Zip` archive unless you have installed the `7-Zip` program and know how to use it.
</details>

After downloading the Zip archive, it should be extracted into a path with no spaces in it using the same process as was used to unzip the source code.  For simplicty, I extract it to the root directory on my C drive, which creates something like `C:\mingw64` (or `C:\mingw32`)

To use this MinGW system, it is necessary to add the `bin` directory within the MinGW installation to the PATH used to find programs run from the command line.  I choose not to permanently add this to my system's PATH for two reasons.  First, running **ardopcf** without the mingw64\bin directory in the system PATH helps confirm that the binary will also work on other Windows systems where MinGW is not installed.  Also, I have both 64-bit and 32-bit versions of MinGW on my computer so that I can build the 64-bit and 32-bit releases of **ardopcf**.  I couldn't use both of them with a single system PATH setting.  So, rather than permemently add this to the system PATH, I add it temporarily as a part of the build process described below.

### Build ardopcf

1. Open a Windows Command Prompt.  On Windows 11, this can be done by pressing the Windows Start button and typing 'Command Prompt' into the search bar.  On older versions of Windows it is probably available in the Windows System folder after pressing the Windows Start button.
2. Use `cd` to navigate to the `ardop-master\ARDOPC` (or `ardop-develop`) directory that was created when you downloaded the source code.  Use `dir` to confirm that you are in the correct directory by verifying that `Makefile` is located there.  For example

```
cd /D C:\Users\<USERNAME>\ardop-master\ARDOPC
dir Makefile
```

3. Temporarily add the MinGW bin directory that was created when you installed MinGW to your PATH.  The part between the equal sign (=) and the semicolon (;) must be the full path to this bin directory.  The `;%PATH%` part at the end reuses everything else that was previously in your PATH.  For example:

`set PATH=C:\mingw64\bin;%PATH%`

4. Build ardopcf.  Note that both the 32-bit and 64-bit versions of MinGW use a command called `mingw32-make`.  If this command produces one or more errors, then something went wrong that needs to be resolved.  If it produces only warnings, then the excutable file named ardopcf.exe is produced.  Future updates to **ardopcf** will hopefully reduce the number of warnings.

`mingw32-make`

5. Verify that the build worked correctly.  The following line should print the help screen.

`ardopcf -h`

6. If you want to, you can copy ardopcf.exe to somewhere in your system PATH or modify your system PATH to point to the directory where it is found.  Unfortunately, Windows does not have a common location that is in the default PATH where users without admin privledges can put programs or scripts (like Linux systems' $HOME/bin).  It is also likely that most Windows users will create a desktop shortcut and/or a .bat or .cmd file to start **ardopcf** so that you don't have to type all of the command line options that you want to use each time you start ardopcf.  Any of these can include the full path to ardopcf.exe so that it does not need to be in your PATH.

See [USAGE.md](USAGE.md) for guideance on use of **ardopcf**.
