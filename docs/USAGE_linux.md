# Using ardopcf on Linux

There are three main steps to using **ardopcf** on Linux.  You need to obtain the `ardopcf` binary executable file, determine what command line options you need to use, and choose a convenient way to start it.  The ways to start it discussed below include from a command line prompt using a bash script, using [PATMENU2](http://github.com/km4ack/patmenu2) as installed by [Build-a-Pi](https://github.com/km4ack/pi-build) or [73Linux](https://github.com/km4ack/73Linux), from the desktop menu, or from a desktop shortcut,

Unlike some programs such as [Pat](https://getpat.io) and [Hamlib rigctld](https://github.com/Hamlib/Hamlib) that may be suitable for automatically starting when you boot your Linux computer, and leaving running all the time, **ardopcf** is not currently well suited to this mode of use.

Throughout this page, I assume that you know how to use basic Linux commands like `cd`, `mkdir`, `cp`, `mv`, and `chmod`, and/or how to achieve similar results using a Linux desktop file manager.  I also assume that you know how to create and edit text files, and run a few command line programs like `dmesg`, even if you normally don't do much with the command line.  It is not necessary to have root access to build, install, and use **ardopcf**.  Where appropriate, I will try to indicate what you might need to do differently if you do not have root access.  If you do have root access, you may need to use `sudo` with some commands or an equivalent action if you are using a Linux desktop file manager.

## Getting `ardopcf`

If you want to try the latest changes that have been made to **ardopcf** since the last release, you can [build](BUILDING.md) it from source.  If you are using a recent version of Linux on a Intel/AMD machine or on a Raspberry Pi, then you can also download one of the pre-built binary executable files from the [releases](https://github.com/pflarue/ardop/releases/latest) page at GitHub.

If you downloaded a pre-built binary executable, you should rename it from something like `ardopcf_amd64_Linux_64` or `ardopcf_arm_Linux_32` to just `ardopcf`.

**ardopcf** requires only a single excutable file, `ardopcf`, and it requires no special installation procedure.  However, you probably don't want to run it from your Downloads directory (though you can if you want to).  I recommend moving it to either `/usr/local/bin` or (especially if you don't have root access) `$HOME/bin`.  This file must also be set as executable.

## Ardopcf PTT/CAT conrol

**ardopcf** has the ability to handle the PTT of your radio in a variety of ways, but other than activating PTT, it does not do other CAT control.  (ardopcf does allow a host program to provide hex strings to pass to a CAT port, but since these strings must be appropriate for the specific model of radio that you are using, this feature is not generally used.)  **ardopcf** can also allow a host program like [Pat](https://getpat.io) to handle PTT.  With Pat, this is convenient because it can also use CAT control to set your radio's frequency.  Other host programs, such as [ARIM](https://www.whitemesa.net/arim/arim.html) and [gARIM](https://www.whitemesa.net/arim/arim.html) do not have CAT or PTT capabilities, so they require **ardopcf** to handle PTT and require you to manually set the frequency on your radio.  A third option is to use the VOX capability of your radio to engage the PTT.  This may work OK for FEC mode operation with ARIM or gARIM, but can be unreliable for the ARQ mode operation used by Pat because it may not engage or disengage quickly enough.

If you want Pat to do PTT and CAT control then do not use any of the `-p`, `--ptt`, `-c`, `--cat`, `-g`, `-k`, `--keystring`, `-u`, `--unkeystring`, or `-g` options described in the remainder of this section.  Instead, set `"ptt_ctrl": true` in the ardop section of the Pat configuration and use the `"rig"` setting to point to a rig defined in the `"hamlib_rigs"` section.  The remainder of this section will assume that you want **ardopcf** to control PTT.  Note that you should **NOT** try to have **ardopcf** do PTT using the same USB or serial device that you will ask PAT (via Hamlib/rigctld) or any other program use for CAT control.

**ardopcf** has a few methods available to controlling PTT.  Serial Port RTS and CAT commands will be described here.  It may also be possible to use PTT via a CM108 device or using a GPIO pin on an ARM computer like a Raspberry Pi.  However, I haven't tried these myself, so I don't know for sure whether they work or know how to reliably configure them.  I may add instructions for these methods to future versions of these instructiosn.  It appears that a partial attempt was also made to allow ardopc to use Hamlib's rigctld to do PTT, but this is not currently usable (it might be usable in the future).

Both RTS and CAT PTT require the device name of the radio interface.  Usually this will be something line `/dev/ttyUSB1`.  While there may be other ways of determining this, I recommend the following.  If your radio interface is already connected to your computer, disconnect it and wait a few seconds.  Connect/reconnect your radio to your computer and run:
`dmesg | tail`

This will print the last 10 lines of the dmesg log.  You should see something like either a reference to `/dev/ttyUSB1` or `attached to ttyUSB1`.

If the interface between your computer and your radio supports PTT control via RTS, this is the simplest solution.  To use RTS PTT use the `-p /dev/ttyUSB1:BAUD` or `--ptt /dev/ttyUSB1:BAUD` option, but using the device name found above using dmesg and `BAUD` for the required baud rate.  If `:BAUD` is ommitted, the default speed of 19200 baud is used.  Often this default is acceptable.  You can find the baud rate required for your radio interface in the manual for your radio or on the internet.

If RTS PTT does not work, you may be able to use CAT PTT.  For this use the `-c /dev/ttyUSB1:BAUD` or `--cat /dev/ttyUSB1:BAUD` option, but using the device name found above using dmesg and `BAUD` for the required baud rate.  If `:BAUD` is ommitted, the default speed of 19200 baud is used.  Often this default is acceptable.  You can find the baud rate required for your radio interface in the manual for your radio or on the internet.  This only sets the CAT port and speed.  It is also necessary to provide the actual cat commands as hex strings to key (`-k` or `--keystring`) and unkey (`-u` or `--unkeystring`) your radio.  These will be specific to your radio model.  So you will have to find them in your radio manual or on the internet.

For example, my Xiegu G90 can do PTT via CAT control with `-c /dev/ttyUSB1:BAUD --keystring FEFE88E01C0001FD --unkeystring FEFE88E01C0000FD`.  For this radio `FEFE` is the fixed preamble to all CI-V commands, `88` is the transceiver address expected by Xiegu radios, and `E0` is the controller address.  Then command number `1C` is used for PTT/ATU control, sub-command `00` means set PTT status, data = `01` means press PTT while `00` means release PTT, and `FD` means end of message.  For Kenwood, Elecraft, and TX-500 `-c /dev/ttyUSB1:BAUD --keystring 54583B --unkeystring 52583B` should work, which is the HEX values for 'TX;' and 'RX;'.  These HEX strings are also reportedly suitable for the QDX and QMX radios, but those radios are not suitable for use with Ardop because they can only transmit a single tone at a time.  I don't know what baud rate is requried for these Kenwood or Elecraft, but the default works with my Xiegu.  Remember that `-c /dev/ttyUSB1` is equivalent to `-c /dev/ttyUSB1:19200` because **ardopcf** uses a default of 19200 baud if a value is not provided.  While CAT control of PTT works for my Xiegu, using `-p /dev/ttyUSB1` for RTS PTT is simpler and works just as well.  With the [Digirig](https://www.digirig.net) interface that I use, another advantage of using RTS PTT is that it requires only the audio cable, so I can leave the serial cable disconnected.

## Ardopcf audio devices and other options.

Your Linux computer may have multiple audio Capture (input) and Playback (output) devices.  **Ardopcf** must be told which of these devices to use.  **ardopcf** is designed to work with ALSA audio devices.  This low level audio interface system should be available on all Linux machines.

However, especially when running a Linux desktop interface, a higher level sound server like Pulse Audio, PipeWire, or Jack may also be running.  In a message to the users subgroup at ardop.groups.io an **ardopcf** user [wrote](https://ardop.groups.io/g/users/message/5411) that **ardopcf** will also work if `pulse` is specified for the audio devices.  I have confirmed that this works, but need to explore it further to understand it better.  For example, I do not know how to select a specific audio device with pulse if more than one is available.  I will update this documentation when I understand this better.

ALSA also allow the configuration of plugins that may allow multiple programs to use the same audio device.  John Wiseman provided a link to a [.asoundrc](https://www.cantab.net/users/john.wiseman/Downloads/alsa-shared-ardopcf-QtSM.txt) configuration file in a [discussion](https://ardop.groups.io/g/users/topic/108567735) of how to make this possible.  That discussion ended with a suggestion that some modification of **ardopcf** might be required for it to work well with specific other programs, and that the order in which those programs are started may also be relvant.  I don't fully understand this approach.  I will update this documention when I understand it better.

While the previous paragraphs describe possible alternatives, the remainder of this section will describe using simple ALSA devices directly.

Every time that **ardopcf** is started, it will print a list of all of the ALSA audio devices that it finds.  It does not detect and print info about devices such as `pulse` mentioned above.  This can be used to identify which device numbers should be used.  So, run `ardopcf` with no other options from a command line to see the list of audio devices.  If it does not exit after printing this list, press CTRL-C to kill it.  This should print something that looks similar to:
```
ardopcf Version 1.0.4.1.2 (https://www.github.com/pflarue/ardop)
Copyright (c) 2014-2024 Rick Muething, John Wiseman, Peter LaRue
See https://github.com/pflarue/ardop/blob/master/LICENSE for licence details including
  information about authors of external libraries used and their licenses.
ARDOPC listening on port 8515
Capture Devices

Card 0, ID 'vc4hdmi', name 'vc4-hdmi'

Card 1, ID 'Device', name 'USB Audio Device'
  Device hw:1,0 ID 'USB Audio', name 'USB Audio', 1 subdevices (1 available)
    1 channel,  sampling rate 44100..48000 Hz

Playback Devices

Card 0, ID 'vc4hdmi', name 'vc4-hdmi'
  Device hw:0,0 ID 'MAI PCM i2s-hifi-0', name 'MAI PCM i2s-hifi-0', 1 subdevices (1 available)
Error -524 opening output device

Card 1, ID 'Device', name 'USB Audio Device'
  Device hw:1,0 ID 'USB Audio', name 'USB Audio', 1 subdevices (1 available)
    2 channels,  sampling rate 44100..48000 Hz

Using Both Channels of soundcard for RX
Using Both Channels of soundcard for TX
Opening Playback Device ARDOP Rate 12000
ALSA lib pcm.c:2660:(snd_pcm_open_noupdate) Unknown PCM ARDOP
cannot open playback audio device ARDOP (No such file or directory)
Error in InitSound().  Stopping ardop.
```

In this case 'Card 1' with the name 'USB Audio Device' for both Capture and Playback devices is the [Digirig](https://www.digirig.net) interface that I use to connect to my Xiegu G90.  If you are unsure which device represents the interface to your radio, compare the results of running `ardopcf` with and without your interface connected to your computer.

This list shows 'hw:1,0' as the Device for both of these.  Your device is likely similar, but the numbers may be different.  Using a 'hw:' device directly with **ardopcf** doesn't usually work, because most sound cards do not natively support the 12 kHz sample rate that **ardopcf** uses.  However, ALSA provides a plugin that allows it to resample the audio to sample rates not directly supported by the hardware.  To use this, tell **ardopcf** to use 'plughw:1,0' instead of 'hw:1,0'.

To set the correct audio devices, you need to give **ardopcf** three command line options: port, capture device, and playback device.  The port should normally be 8515, but another value can be used if this causes a conflict with other software.  So, for my system, I would use:
`ardopcf 8515 plughw:1,0 plughw:1,0`

This starts **ardopcf** and may be sufficient if you decided after reading the earlier section on PTT/CAT control that you do not need **ardopcf** to handle PTT because a host program like [Pat](https://getpat.io) will handle this or because you will use VOX.  If you decided that you want **ardopcf** to handle PTT, then add those additional options.  For example:

`ardopcf -p /dev/ttyUSB1 8515 plughw:1,0 plughw:1,0`

or

`ardopcf -c /dev/ttyUSB1 --keystring FEFE88E01C0001FD --unkeystring FEFE88E01C0000FD 8515 plughw:1,0 plughw:1,0`

4. There are some additional command line options that you might want to use.  Other than port, capture device, playback device, the order in which the command line options are given does not matter.  See [Commandline_options.md](Commandline_options.md) for info on all possible options.

A.  By default, **ardopcf** writes log files in the directory where it is started.  You can change where these files are created with the `-l` or `--logdir` option.  For example, I created an `$HOME/ardop_logs` and use `--logdir ~/ardop_logs`.

B.  To enable the WebGui use `-G 8514` or `--webgui 8514`.  This sets the WebGui to be available by typing `localhost:8514` into the navgation bar of your web browser.  You may choose a different port number if 8514 causes a conflict with other software.  The WebGui is likely to be useful when you adjust the transmit and receive audio levels as described later.

C.  The `-H` or `--hostcommands` option can be used to automatically apply one or more semicolon separated commands that **ardopcf** accepts from host programs like [Pat](https://getpat.io).  See [Host_Interface_Commands.md](Host_Interface_Commands.md) for more information about these commands.  The commands are applied in the order that they are written, but usually this doesn't matter.  As an example, `--hostcommands "MYCALL AI7YN"` sets my callsign to `AI7YN`.  Pat will do this, so it isn't usually necessary, but it is a convenient example.  Because most commands will include a command, a space, and a value, you usually need to put quotation marks around the commands string.  After you adjust your sound audio levels, you may discover that you want the **ardopcf** transmit drive level to be less than the default of 100%.  `--hostcommands "MYCALL AI7YN;DRIVELEVEL 90"` would set my callsign and set the transmit drive level to 90%.

So, an example of the complete command you might want to use to start **ardopcf** is:
`ardopcf --logdif ~\ardopc_logs -p /dev/ttyUSB1 -G 8514 --hostcommands "DRIVELEVEL 90" 8515 plughw:1,0 plughw:1,0`

With this running, **ardopcf** is functional and ready to be used by a host program like [Pat](https://getpat.io).  However, you probably don't want to type all of this every time you want to start **ardopcf**.  So, the next sections describe some better options for starting **ardopcf**.  All of them will use the sequence of options that you identified in this section.


## Starting **ardopcf** from the command line.

Starting **ardopcf** from the command line is useful if you access your Linux machine through a text only interface such as ssh, or if you use a Linux desktop but prefer the use of a terminal window to GUI menus.  For this use, a bash script containing all of the necessary command line options make starting **ardopcf** easy.  So, use a text editor to create `$HOME/bin/ardop` with contents similar to the following.  See the [section](#ardopcf-audio-devices-and-other-options) on audio devices and other options for help understanding the ardopcf command line.
```
#! /bin/bash
ardopcf --logdif ~\ardopc_logs -p /dev/ttyUSB1 -G 8514 --hostcommands "DRIVELEVEL 90" 8515 plughw:1,0 plughw:1,0
```

Once you have created this file, use `chmod` or an equivalent feature in a file manager to make it executable.

**ardopcf** on Linux ignores SIGHUP.  This means that if the terminal (such as an ssh session or a Linux desktop terminal window) where **ardopcf** was started is closed, **ardopcf** will continue to run.  This may be useful in some use cases.  If this behavior is not desired, see the section below about starting **ardopcf** from a desktop menu.  The script used for that kills **ardopcf** when its terminal closes.

If you are connecting to your Linux computer via ssh and want to run several programs such as **ardopcf**, [Hamlib/rigctld](https://hamlib.github.io), and [Pat](https://getpat.io), then it may be useful to either run these programs in the background using a trailing ampersand (&), or to use a terminal multiplexer such as [screen](https://www.gnu.org/software/screen) or [tmux](https://github.com/tmux/tmux/wiki).  One advantage of using a terminal multiplexer is that any output printed by each program is kept separate which may make it easier to interpret.  If the programs are run in the background, it may be useful to redirect their output to a file (or even to /dev/null).  Details for such options are beyond the scope of this document.

## Starting **ardopcf** with patmenu2

KM4ACK's [PAT MENU 2](http://github.com/km4ack/patmenu2) as installed by [Build-a-Pi](https://github.com/km4ack/pi-build) or [73Linux](https://github.com/km4ack/73Linux) is a popular tool for using [Pat](https://getpat.io) on Linux systems.  The Pat Menu 2 Settings/Config screen has a space for an 'ARDOP Command'.  Put `ardopcf` with all of the appropriate options as described in the [section](#ardopcf-audio-devices-and-other-options) on audio devices and other options here.

That Settings screen also has a spot for an 'ARDOP GUI Comand'.  This was intended for use with John Wiseman's ARDOP GUI program, but you might choose to use it to open your web browser to **ardopcf**'s WebGui.  To do this, ensure that the 'ARDOP Command' includes the `-G 8514` or `--webgiu 8514` option (though you may choose a different port number).  Then set 'ARDOP GUI Command' to `xdg-open 'http://localhost:8514`.  You might also choose to create a bookmark in your web browser to this address for use if the browser is already open.

## Starting **ardopcf** from the desktop menu

The following is based on using the Linux desktop system included with the standard version of Raspberry Pi OS with desktop based on Debian version 12 (bookworm).  Other Linux desktop installations may have slightly different details, but I assume that a similar approach can be used.

1.  First create a bash script that starts **ardopcf** for use with a Linux desktop.  This is more complex than the simple script previously described for starting **ardopcf** from the command line.  First, this script checks to see whether **ardopcf** is already running.  If it is, then it notifies the user and exits.  Second, this script allows the user to stop **ardopcf** by either pressing CTRL-C (as would work if **ardopcf** were run normally) or by closing the window that it is running in.  So, use a text editor to create `$HOME/bin/desktop_ardopcf` with contents similar to the following.  See the [section](#ardopcf-audio-devices-and-other-options) on audio devices and other options for help understanding the ardopcf command line.  The comments (lines starting with #) are there for your benefit.  I recommend that you read and understand them.  By placing them in the script, rather than only in this help document, you (or someone else) who reads this script later, will have a better chance of understanding its contents.
```
#! /bin/bash
# Notice that /bin/bash is used rather than the /bin/sh.  On some systems,
# /bin/sh is linked to /bin/dash, which is not completely compatible with some
# features used in this script.

# To better accomodate some command line use cases, ardopcf on Linux ignores
# SIGHUP.  This means that if the terminal (such as an ssh session or a Linux
# desktop terminal window) used to start ardopcf is closed, ardopcf will
# continue to run.  However, when a desktop shortcut or menu item is used to
# start ardopcf, a user will expect that closing its terminal window will kill
# ardopcf.  So, this script provides that behavior.  If the user types CTRL-C,
# it will also kill ardopcf as it would if ardopcf were run normally without
# this script.

# First check whether ardopcf is already running.  If it is, don't try to run
# a second instance.  This script is NOT suitable to the case of running
# multiple simultaneous instances of ardopcf with different port numbers and
# audio devices.  In addition to the following test, killall is used below,
# which will kill all instances of ardopcf rather than just the one started
# here.
if pidof -q ardopcf; then
	read -n 1 -p "ardopcf is already running, so nothing to do.
Press any key to continue."
	exit
fi

# Start ardopcf in the background by using a trailing &.  This allows the rest
# of this script to run, while still showing anything that ardopcf prints.
# Edit the next line to include appropriate options for ardopcf including at
# least port, capturedevice, playbackdevice.  For help with this see:
# https://github.com/pflarue/ardop/blob/master/docs/USAGE_linux.md
ardopcf <PUT APPROPRIATE OPTIONS HERE> &

# If SIGINT (user types CTRL-C) or SIGHUP (terminal disconnecting) is
# detected, then kill ardopcf by sending it SIGNINT (as if someone typed CTRL-C),
# and then exit this script.
trap "killall -s SIGINT ardopcf; exit 1" INT HUP

# The following just keeps this script running so that the window stays open and
# the trap remains active until SIGINT or SIGHUP is detected.
while true
do
	sleep 1
done
```

Once you have created this file, use `chmod` or an equivalent feature in a file manager to make it executable.

2. Use a text editor to create `$HOME/.local/share/applications/ardopcf.desktop` containing the following.
```
[Desktop Entry]
Name=ardopcf
Comment=An implementation of the Amateur Radio Digital Open Protocol
Exec=desktop_ardopcf
Terminal=true
Type=Application
Categories=AudioVideo;Audio;HamRadio;
```


## Starting **ardopcf** from a desktop icon.

1. Create `$HOME/bin/desktop_ardopcf` as described in step 1 of [Starting ardopcf from the desktop menu](#starting-ardopcf-from-the-desktop-menu).
2. Use a text editor to create `$HOME/Desktop/ardopcf.desktop` with contents similar to the following.  This file is the same as `$HOME/.local/share/applications/ardopcf.desktop` created in step 2 of [Starting ardopcf from the desktop menu](#starting-ardopcf-from-the-desktop-menu).  So, if you already created that file, you can just copy it to `$HOME/Desktop/`
```
[Desktop Entry]
Name=ardopcf
Comment=An implementation of the Amateur Radio Digital Open Protocol
Exec=desktop_ardopcf
Terminal=true
Type=Application
Categories=AudioVideo;Audio;HamRadio;
```

Once you have created this file, use `chmod` or an equivalent feature in a file manager to make it executable.

3. By default (at least on my Raspberry Pi), when I double click this desktop icon, it asks me: "This text file 'ardopcf' seems to be an executable script.  What do you want to do with it?".  The options include 'Execute', 'Execute in Terminal', 'Open', and 'Cancel'.  Either of the first two options work.  This dialog can be bypassed by checking 'Don't ask options on launch of executable file' in the 'General' tab of the 'Preferences' dialog accessed throught the 'Edit' menu of the 'File Manager'.
