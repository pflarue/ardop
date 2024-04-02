# ardopcf

**ardopcf** is a fork by Peter LaRue KG4JJA of [ardopc](https://github.com/g8bpq/ardop) by John Wiseman G8BPQ, which is a multi-platform port of **ARDOP_Win** (Windows only) by Rick Muething KN6KB.  ARDOP_Win is distributed as a part of [Winlink Express](https://winlink.org/WinlinkExpress).  John's [ARDOPC web page](https://www.cantab.net/users/john.wiseman/Documents/ARDOPC.html) provides documentation about use of [ardopc](https://github.com/g8bpq/ardop) and links to the binaries he produces.

See [releases](https://github.com/pflarue/ardop/releases) to download **ardopcf** binaries for Linux and Windows.  Each of these is a single executable file that is ready to run without explicit installation.  The filenames used for releases are intended to distinguish what platform they are for.  After download, they may be renamed to simply `ardopcf` as used in the examples below.

These are all implementations of ARDOP, the Amateur Radio Digital Open Protocol, developed by Rick Muething KN6KB.  The protocol is defined in the [ARDOP Specification](https://ardop.groups.io/g/users/files/ARDOP%20Specification.pdf).  Though it may also be used for other purposes, ARDOP was designed to be used by licensed amateur radio operators with [Winlink](www.winlink.org), a network of amateur radio stations that provide worldwide email over radio where internet connections are not available.

The **ardopcf** fork of [ardopc](https://github.com/g8bpq/ardop) includes bug fixes and features that should help **ardopcf** to more reliably communicate with other stations.  It also includes some additional user interface features.  Some of these features may useful to typical users.  However, many of the new features are primarily useful to the author and others interested in further development and debugging.  For a description of the list of differences between **ardopcf** and [ardopc](https://github.com/g8bpq/ardop), see the [changelog](changelog.md).

**ardopcf** should be considered perpetual beta software.  Limited real world use and testing show that it seems to work as intended.  However, no rigorous and comprehensive testing is currently being done.  Therefore, public releases are still likely to contain bugs that need to be fixed.  Source code in the `develop` branch will often contain additional unfinished features that need further work before they are ready for public release.  

If you find a problem with ardopcf, please create an Issue at the GitHub [repository](https://github.com/pflarue/ardop) and/or describe the problem in a message in the users subgroup of ardop.groups.io.  The users group is also a good place to ask questions about this software or provide other feedback.  If you are having trouble compiling or running ardopcf, don't hesitate to ask for help from the users group.  Since that group is not specific to the ardopcf implementation of ardop, be sure to mention that you are talking about ardopcf.

**ardopcf** is released under the MIT open source license.  See `LICENSE` for details.  Use, study, and experimentation with this software is not only permitted, it is strongly encouraged.  Such activity should be central to both amateur radio and open source software development.  However, the authors ask that if this source code is used to develop software that deviates from the [ARDOP Specification](https://ardop.groups.io/g/users/files/ARDOP%20Specification.pdf), or that is incompatible with existing implementations, that the `ardop` name not be used for such software.  Software claiming to be usable implementations of `ardop` should all be interoperable.  Otherwise, users may be confused and attempts to communicate with this software may fail.

##### EXAMPLE USAGE:

To get a full listing of the available arguments, use `ardopcf -h`.

###### On a Raspberry Pi or other Linux computer using a usb interface to the radio such as a [digirig](https://digirig.net):

```
ardopcf --logdir ~/ardop_logs -p /dev/ttyUSB0 8515 plughw:1,0 
    plughw:1,0 
```

`--logdir ~/ardop_logs` means put the log files into the specified subdirectory of my home directory.  New debug log and session log files are added to this directory every day that **ardopcf** is run.  A relative path may also be specified.  

`-p /dev/ttyUSB0` means use this USB tty device representing the control connection of my [digirig](https://digirig.net) to activate the PTT of my radio. 

`8515` is the TCP port that Winlink or another host program can use to connect to **ardopcf**.  This is the default, but must still be explicitly specified when the audio devices are specified.  

The first `plughw:1,0` is the audio capture device to use, while the second is the audio playback device.  Both of these represent the USB PnP Sound Devices of my [digirig](https://digirig.net).  Running `ardopcf` with no arguments can be used to get a list of the available audio devices.  This shows `hw:1,0` under both Capture and Playback.  Using `plughw:1,0` instead of `hw:1,0` provides the flexibility for **ardopcf** to use the required 12kHz audio sample rate that is not directly supported by the hardware.  

###### On a Windows PC using a usb interface to the radio such as a [digirig](https://digirig.net):

```
ardopcf --logdir C:\Users\Peter\Documents\ardop_logs -p COM5 8515 2 2  
```

`--logdir C:\Users\Peter\Documents\ardop_logs` means put the log files into the specified subdirectory of my Documents directory, where `Peter` is my user name on this computer.  New debug log and session log files are added to this directory every day that **ardopcf** is run.  A relative path may also be specified.

`-p COM5` means use this virtual COM port associated with the control connection of my [digirig](https://digirig.net) to activate the PTT of my radio. 

`8515` is the TCP port that Winlink or another host program can use to connect to **ardopcf**. This is the default, but must still be explicitly specified when the audio devices are specified. 

The first `2` is the audio capture device to use, while the second is the audio playback device. Both of these represent the USB PnP Sound Devices of my [digirig](https://digirig.net). Running `ardopcf` with no arguments can be used to get a list of the available audo devices.  

##### Other platforms (Teensy, Mac, etc.)

John's ardopc code that was forked to form ardopcf includes support for Teensy microcontrollers.  I have no experience with this hardware.  So, I can only guess whether my changes will help or hinder its use.  I welcome feedback from any Teensy users, especially if they identify changes that are required to the source code for it to continue to be useful with this hardware.  If I never receive any feedback from Teensy users, I may assume that nobody is trying to use this software on that hardware.

I mostly use Linux, both on a laptop and on Raspberry Pi computers.  I also occasionally use a Windows laptop.  So, I provide binaries for these systems, and do at least minimal testing of each binary.  I would like to know whether the ardopcf source code is also usable for Apple Mac computers.  So, I welcome feedback from any Mac users interested in building and using ardopcf.

I am also interested to hear from anyone interested in building and using ardopcf on any other platform or operating systems.

##### Why I'm working on ardopcf?

I am interested in supporting further development of systems to use [Winlink](https://winlink.org) for sending and receiving email over long range (HF) radio links using small energy efficient computers such as the Raspberry Pi Zero connected to low power portable radios.  This arrangement should allow licensed amateur radio operators to reliably send and receive email where neither phone nor internet service is available, amd using only limited electrical power.  This is important to me because I spend extended periods of time in places with no phone or internet service, and with limited electrical power available from small solar systems.  [Winlink](https://winlink.org) allows me to keep in contact with other licensed amateur radio operators as well as selected friends and family that are not radio operators when I am in such locations.

A variety of [Winlink](www.winlink.org) clients exist.  Of particular interest to users of small Linux based computers such as the Raspberry Pi are [Pat Winlink](https://getpat.io) and [WoAD](https://woad.sumusltd.com).  Both of these support the TCP Host Interface connection to ardopc, and thus also work with ardopcf.  [Pat Winlink](https://getpat.io) will run on most any computer, including various models of Raspberry Pi, and it provides an HTTP interface usable through a web browser on any connected device.  [WoAD](https://woad.sumusltd.com) runs on an Andoid phone or tablet.  It can access a radio by using a WiFi connection to a Raspberry Pi or other computer that is running **ardopcf**.

By using **ardopcf** along with either [Pat Winlink](https://getpat.io) and [WoAD](https://woad.sumusltd.com), an entire portable system can consist of the radio and antenna, a Raspberry Pi Zero, an audio interface such as a [digirig](https://digirig.net) with suitable cables to connect to the radio and the Pi, a small battery to power it all, and a cell phone or tablet (with no cellular or internet connection) to function as a screen and keyboard.  Some radios have a built-in digital interface so that a [digirig](https://digirig.net) or other audio interface device is not required.  The WiFi network required for the phone or tablet to connect to the Raspberry Pi can be generated either by the Pi or by the phone or tablet.

While **ardopcf** is particularly well suited to use with small Linux based computers, binaries for additional platforms are also provided.  Hopefully this will encourage more people to use ARDOP rather than exclusively using alternative protocols that are not Open Source, and thus do not allow amateur radio operators to view, modify, experiment with, and extend the software.  I understand that ardop does not provide the same performance as the commercial VARA HF software.  However, even if you use VARA, I encourage you to also sometimes use ardopcf or any other ardop implementation.  This not only develops and practices your ability to communicate with stations that are unable to run VARA, but it also encourages Winlink gateway operators to continue to support ardop.  Until a better open source multi-platform radio interface is available and widely adopted by Winlink host programs and gateway operators, I think it is important for ardop to continue to be supported.

I'm continuing to study DSP and other topics related to writing software for digital radio communication.  Hopefully this will allow me to participate in the future development of open source tools that improve the speed and reliability of such communication.  I believe that open source tools can and should provide performance at least as good as any commercial alternative.  I want to help make that happen.

While it may be more difficult for home built amateur radio hardware to compete with modern commercial hardware than it was in the past, I believe that today's amateur radio operators can and should be working to advance the state of the art of radio communication through software development.  With the free resources available online, it has never been easier for anyone to acquire the software development skills necessary to do so.  Also, unlike development of new hardware which may require specialized tools and materials, software development is possible using the computer that you probably already own.  

If you don't want to write software, you can still support open source projects by using the software and providing feedback to the developers, especially if you find bugs that the developers might not be aware of.  Your unique hardware or operating conditions might allow you to identify software bugs that the developers would never be aware of without your help.

Peter LaRue KG4JJA
