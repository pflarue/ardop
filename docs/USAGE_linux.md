# Using ardopcf on Linux

There are three main steps to using **ardopcf** on Linux.  You need to obtain the `ardopcf` binary executable file, determine what command line options you need to use, and choose a convenient way to start it.  The ways to start it discussed below include from a command line prompt using a bash script, using [PATMENU2](http://github.com/km4ack/patmenu2) as installed by [Build-a-Pi](https://github.com/km4ack/pi-build) or [73Linux](https://github.com/km4ack/73Linux), from the desktop menu, or from a desktop shortcut,

Unlike some programs such as [Pat](https://getpat.io) and [Hamlib rigctld](https://github.com/Hamlib/Hamlib) that may be suitable for automatically starting when you boot your Linux computer, and leaving running all the time, **ardopcf** is not currently well suited to this mode of use.

Throughout this page, I assume that you know how to use basic Linux commands like `cd`, `mkdir`, `cp`, `mv`, and `chmod`, and/or how to achieve similar results using a Linux desktop file manager.  I also assume that you know how to create and edit text files, and run a few command line programs like `dmesg`, even if you normally don't do much with the command line.  It is not necessary to have root access to build, install, and use **ardopcf**.  Where appropriate, I will try to indicate what you might need to do differently if you do not have root access.  If you do have root access, you may need to use `sudo` with some commands or an equivalent action if you are using a Linux desktop file manager.

## Getting `ardopcf`

If you want to try the latest changes that have been made to **ardopcf** since the last release, you can [build](BUILDING.md) it from source.  If you are using a recent version of Linux on a Intel/AMD machine or on a Raspberry Pi, then you can also download one of the pre-built binary executable files from the [releases](https://github.com/pflarue/ardop/releases/latest) page at GitHub.

If you downloaded a pre-built binary executable, you should rename it from something like `ardopcf_amd64_Linux_64` or `ardopcf_arm_Linux_32` to just `ardopcf`.

**ardopcf** requires only a single excutable file, `ardopcf`, and it requires no special installation procedure.  However, you probably don't want to run it from your Downloads directory (though you can if you want to).  I recommend moving it to either `/usr/local/bin` or (especially if you don't have root access) `$HOME/bin`.  This file must also be set as executable.

## Ardopcf PTT/CAT conrol

**ardopcf** has the ability to handle the PTT of your radio in a variety of ways, but other than activating PTT, it does not do other CAT control.  (ardopcf does allow a host program to provide hex strings to pass to a CAT serial device or TCP port, but since these strings must be appropriate for the specific model of radio or TCP middleware such as Hamlib/rigctld that you are using, this feature is not generally used.)  **ardopcf** can also allow a host program like [Pat](https://getpat.io) to handle PTT.  With Pat, this is convenient because it can also use CAT control to set your radio's frequency.  Other host programs, such as [ARIM](https://www.whitemesa.net/arim/arim.html) and [gARIM](https://www.whitemesa.net/garim/garim.html) do not have CAT or PTT capabilities, so they require **ardopcf** to handle PTT and require you to manually set the frequency on your radio.  A third option is to use the VOX capability of your radio to engage the PTT.  This may work OK for FEC mode operation with ARIM or gARIM, but can be unreliable for the ARQ mode operation used by Pat because it may not engage or disengage quickly enough.

If you want Pat to do PTT and CAT control then do not use any of the `-p`, `--ptt`, `-c`, `--cat`, `-g`, `-k`, `--keystring`, `-u`, `--unkeystring`, or `-g` options described in the remainder of this section.  Instead, set `"ptt_ctrl": true` in the ardop section of the Pat configuration and use the `"rig"` setting to point to a rig defined in the `"hamlib_rigs"` section.  The remainder of this section will assume that you want **ardopcf** to control PTT.  Note that you should **NOT** try to have **ardopcf** do PTT using the same USB or serial device that you will ask PAT (via Hamlib/rigctld) or any other program use for CAT control.  However, you may set Pat to provide CAT control via Hamlib/rigctld to set the frequency, while setting **ardopcf** to handle PTT via the same Hamlib/rigctld middleware.

**ardopcf** has a few methods available to controlling PTT.  Serial device RTS/DTR, CAT commands sent to a serial device or TCP port (as used with Hamlib/rigctld), Raspberry Pi GPIO pin, and use of CM108 sound devices using IO pin 3 for PTT will be described here.

RTS/DTR, CAT (except TCP CAT), and CM108 PTT all require the device name of the radio interface.  Usually this will be something line `/dev/ttyUSB1` for a serial device for RTS/DTR or `/dev/hidraw0` for a CM108 device.  While there may be other ways of determining this, I recommend the following.  If your radio interface is already connected to your computer, disconnect it and wait a few seconds.  Connect/reconnect your radio to your computer and run:

`dmesg | tail`

This will print the last 10 lines of the dmesg log.  You should see something like either a reference to `/dev/ttyUSB1` or `attached to ttyUSB1` for a serial device or `hidraw0: USB HID` for a CM108 device.

For the RTS/DTR serial devices, you can also specify the serial device by its id.  (This cannot be used for a CM108 device.)  This can be useful if, for example, sometimes your device shows up as /dev/ttyUSB0, and other times it is /dev/ttyUSB1.  `ls /dev/serial/by-id` will list the available serial device id values.  The serial device of my DigiRig appears as `/dev/serial/by-id/usb-Silicon_Labs_CP2102N_USB_to_UART_Bridge_Controller_3cc92e511387ed11b72131d7a603910e-if00-port0` while the serial device of my QRP-Labs QMX appears as `/dev/serial/by-id/usb-QRP_Labs_QMX_Transceiver-if00`.  These are a lot to type if you were typing the ardopcf command manually every time.  However, if you put this into a Bash script or desktop shortcut, the length of this id is not a problem.

If the interface between your computer and your radio supports PTT control via RTS, this is often the simplest solution.  To use RTS PTT use the `-p /dev/ttyUSB1:BAUD` or `--ptt /dev/ttyUSB1:BAUD` option, but using the device name found above using dmesg and `BAUD` for the required baud rate.  If `:BAUD` is ommitted, the default speed of 19200 baud is used.  Often this default is acceptable.  You can find the baud rate required for your radio interface in the manual for your radio or on the internet.

If the interface between your computer and your radio supports PTT control via DTR, this is very similar to the solution for RTS, but with a `DTR:` prefix applied to the device name.  To use DTR PTT use `-p DTR:/dev/ttyUSB1:BAUD` or `--ptt DTR:/dev/ttyUSB1:BAUD` option, but using the device name found above using dmesg and `BAUD` for the required baud rate.  If `:BAUD` is ommitted, the default speed of 19200 baud is used.  Often this default is acceptable.  You can find the baud rate required for your radio interface in the manual for your radio or on the internet.

If RTS/DTR PTT does not work, you may be able to use CAT PTT.  For this use the `-c /dev/ttyUSB1:BAUD` or `--cat /dev/ttyUSB1:BAUD` option, but using the device name found above using dmesg and `BAUD` for the required baud rate.  If `:BAUD` is ommitted, the default speed of 19200 baud is used.  Often this default is acceptable.  You can find the baud rate required for your radio interface in the manual for your radio or on the internet.

You can also direct **ardopcf** to pass CAT commands via a TCP port rather than a serial device.  This has been tested with Hamlib/rigctld.  Given the correct PTT ON and PTT OFF command strings, it should work with any radio or other middleware that accepts CAT commands via a TCP port.  By default, rigctld uses TCP port 4532.  So, with rigctld running on your local machine, `-c TCP:4532` will allow **ardopcf** to connect to it.  If rigctld is running on a different networked computer, you can use an option such as `-c TCP:192.168.100.5:4532` to connect to that remote rigctld process.  Any IPv4 address and port expressed as ddd.ddd.ddd.ddd:port should work.

The `--cat` or `-c` option only sets the CAT serial device or TCP port to be used.  To use CAT for PTT control, it is also necessary to provide the actual cat commands as hex strings to key (`-k` or `--keystring`) and unkey (`-u` or `--unkeystring`) your radio.  These will be specific to the model of radio or interface that you are using, or to the middleware such as Hamlib/rigctld that you are connecting to.  So you will have to find them in your radio manual or on the internet.

For example, my Xiegu G90 can do PTT via CAT control with `-c /dev/ttyUSB1 --keystring FEFE88E01C0001FD --unkeystring FEFE88E01C0000FD`.  For this radio `FEFE` is the fixed preamble to all CI-V commands, `88` is the transceiver address expected by Xiegu radios, and `E0` is the controller address.  Then command number `1C` is used for PTT/ATU control, sub-command `00` means set PTT status, data = `01` means press PTT while `00` means release PTT, and `FD` means end of message.  For Kenwood, QRP-Labs, Elecraft, and TX-500 `-c /dev/ttyUSB1:BAUD --keystring 54583B --unkeystring 52583B` should work, which is the HEX values for 'TX;' and 'RX;'.  I don't know what baud rate is requried for these, but the default works with my Xiegu.  Remember that `-c /dev/ttyUSB1` is equivalent to `-c /dev/ttyUSB1:19200` because **ardopcf** uses a default of 19200 baud if a value is not provided.  While CAT control of PTT works for my Xiegu, using `-p /dev/ttyUSB1` for RTS PTT is simpler and works just as well.  With the [Digirig](https://www.digirig.net) interface that I use, another advantage of using RTS PTT is that it requires only the audio cable, so I can leave the serial cable disconnected.

If you are using Hamlib/rigctld, then the hex strings for PTT ON and PTT OFF must be the Hamlib/rigctld command strings rather than the command strings specific to your radio.  So, if you are using `--cat TCP:4532` with rigctld, you would always use `-k 5420310A -u 5420300A`, regardless of the type of radio you are using.  These are the hex representations of `T 1\n` and `T 0\n` for TX and RX respectively.  Since these three options are likely to be used together for most people using Hamlib/rigctld with **ardopcf**, the `--ptt RIGCTLD` or `-p RIGCTLD` shortcut is provided.  This is equivalent to `-c TCP:4532 -k 5420310A -u 5420300A`.  The option used to start rigctld specifies the radio model you are using and how it is connected to your computer.  Installation and setup of Hamlib/rigctld is beyond the scope of this document.

If you are not already using Hamlib/rigctld, there is probably no advantage to installing and configuring it only for use by **ardopcf**.  However, if you are already running Hamlib/rigctld and have it configured to work with your radio, then it may be convenient to use these options.  One advantage of using Hamlib/rigctld via a TCP CAT port is that, unlike a serial device, multiple programs can connect to one TCP CAT port simultaneously.  For example, you can have **ardopcf** use it for PTT control, while Pat uses the same TCP CAT port to set the radio frequency.

It is possible to control PTT using a GPIO pin of a Raspberry Pi computer.  **WARNING**: This option attempts to directly control the hardware of your Raspberry Pi, and thus may cause injury or physical damage if used incorrectly.  So, don't use this option unless you know what you are doing.  Some online sources suggest that this may not work with the PI5, but I cannot confirm this.  The details of the electrical/hardware connections required for this are beyond the scope of these instructions.  The `-g` command line option is used to set a GPIO pin number to use for PTT control.  Earlier versions of **ardopcf** (and piardopc) set the pin number to 17 if this option was used without an argument.  The current version requires a pin number.  Thus, `-g 17` causes a Raspberry Pi's GPIO 17 pin to be high for transmit, and low for receive.  To invert the pin signal, use the negative of the pin number.  Thus, `-g -17` causes a Raspberry Pi's GPIO 17 pin to be low for transmit, and high for receive.  This option is not available for other than the Raspberry Pi computers.

CM108 devices are USB devices that combine an audio interface with PTT control.  These include user modified USB sound devices as well as commercial products including: the [digirig Lite](https://digirig.net/product/digirig-lite) (not to be confused with the [digirig mobile](https://digirig.net/product/digirig-mobile) which I refer to as simply a digirig elsewhere in this documentation), the [AIOC or All-in-one-Cable](https://github.com/skuep/AIOC) which is also available from https://na6d.com, and various products from [masterscommunications.com](masterscommunications.com).

These CM108 devices typically appear as `/dev/hidraw0`.  Unfortunately, by default, the access permissions for these devices do not allow them to be used.  If you try to use such a device without fixing this you will see an error message instructing you to try `sudo chmod 666 /dev/hidraw0` and then try agian.  This should normally work, but is only a temporary solution.  Unplugging and reconnecting the device, or rebooting the computer will require repeating this process.

For a persistent solution to the problem of insufficient access permisions for a CM108 device, it is necessary to create a udev rule.  This is done by creating a file in the `/etc/udev/rules.d` directory.  For a more detailed description of how these rules work, see https://reactivated.net/writing_udev_rules.html.

For my [AIOC](https://github.com/skuep/AIOC) with VID:PID of 1209:7388, I created a file named `/etc/udev/rules.d/40-cm108-1209-7388.rules` that contains a single line of text `ATTRS{idVendor}=="1209", ATTRS{idProduct}=="7388", MODE:="0666"`.  The filename is somewhat arbitrary, but should start with a number less than 50 and must have a `.rules` extension.  Note the use of VID and PID values as hex strings.  If you don't know these values, you can find them with the command `lsusb -v`.  The last part sets the permissions to read/write.  Various online sources disagree about whether to use `ATTRS` or `ATTR` and whether to use `MODE:=` or `MODE=` without the colon.  This is what works for me.

For my [digirig Lite](https://digirig.net/product/digirig-lite) with VID:PID of 0D8C:0012, I created a file named `/etc/udev/rules.d/40-cm108-0D8C-0012.rules` that contains a single line of text `ATTRS{idVendor}=="0d8c", ATTRS{idProduct}=="0012", MODE:="0666"`.  One reference I saw also indicated that if your VID or PID contains any letters, they may need to be lowercase.  This appears to be true.  Using `ATTRS{idVendor}=="0D8C"` in that udev rule file did not work.

While the digirig lite is fully supported by ardopcf, I do not recommend it for use with a Raspberry Pi for portable use.  Trying it with my Raspberry Pi Zero 2W, I found that I often have to connect it, disconnect it, and repeat this several times before the Pi recognizes it.  I don't have this problem with the more common but slightly more expensive and slightly larger [digirig mobile](https://digirig.net/product/digirig-mobile).  So, especially for portable use where such lack of reliability may be particularly inconvenient, I think that the digirig mobile is a better choice.  I also described my experience with this in a [comment on the digirig forum](https://forum.digirig.net/t/digipi-raspberry-pi-zero-not-finding-digirig-lite/5159).

## Ardopcf audio devices and other options.

Your Linux computer may have multiple audio Capture (input) and Playback (output) devices.  **Ardopcf** must be told which of these devices to use.  **ardopcf** is designed to work with ALSA audio devices.  This low level audio interface system should be available on all Linux machines.

However, especially when running a Linux desktop interface, a higher level sound server like Pulse Audio, PipeWire, or Jack may also be running.  In a message to the users subgroup at ardop.groups.io an **ardopcf** user [wrote](https://ardop.groups.io/g/users/message/5411) that **ardopcf** will also work if `pulse` is specified for the audio devices.  I have confirmed that this works, but need to explore it further to understand it better.  For example, I do not know how to select a specific audio device with pulse if more than one is available.  I will update this documentation when I understand this better.

ALSA also allows the configuration of plugins that may allow multiple programs to use the same audio device.  John Wiseman provided a link to a [.asoundrc](https://www.cantab.net/users/john.wiseman/Downloads/alsa-shared-ardopcf-QtSM.txt) configuration file in a [discussion](https://ardop.groups.io/g/users/topic/108567735) of how to make this possible.  That discussion ended with a suggestion that some modification of **ardopcf** might be required for it to work well with specific other programs, and that the order in which those programs are started may also be relvant.  I don't fully understand this approach.  I will update this documention when I understand it better.

While the previous paragraphs describe possible alternatives, the remainder of this section will describe using simple ALSA devices directly.

Every time that **ardopcf** is started (except when the `--help` or `-h` option are used, or if you set `CONSOLELOG` too high), it will print a list of all of the ALSA audio devices that it finds.  It does not detect and print info about devices such as `pulse` mentioned above.  This can be used to identify which device numbers should be used.  So, run `ardopcf` with no other options from a command line to see the list of audio devices.  If it does not exit after printing this list, press CTRL-C to kill it.  This should print something that looks similar to:
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

This list shows 'hw:1,0' as the Device for both of these.  Your device is likely similar, but the numbers may be different.  Using a 'hw:' device directly with **ardopcf** doesn't usually work, because most sound cards do not natively support the 12 kHz sample rate that **ardopcf** uses.  The listing above shows that these 'hw:' devices support sampling rates of 41000..48000 Hz.  Thus, they do not natively support the 12000 Hz rate required by **ardopcf**.  However, ALSA provides a plugin that allows it to resample the audio to sample rates not directly supported by the hardware.  To use this, tell **ardopcf** to use 'plughw:1,0' instead of 'hw:1,0'.  If you specify just a number, for the device, **ardopcf** uses the corresponding 'plughw' device.  So, if you specify `1`, ardopcf uses `plughw:1,0`.  Unless you are using a non-ALSA device like `pulse` or a plugin-based custom configured ALSA device, specifying each device by a number this way is probably what you want to do.  If you don't specify the audio devices, **ardopcf** uses a default of `0` (which expands to `plughw:0,0`) for both of them.

Use the `-i` option to set the (input) audio capture device and the `-o` option to set the (output) audio playback device.  Earlier versions of **ardopcf** did not support these options, and thus required that you also specify the host port number to set these values.  So, for my system, I would use:
`ardopcf -i 1 -o 1`

which is equivalent to the more verbose

`ardopcf -i plughw:1,0 -o plughw:1,0`

This starts **ardopcf** and may be sufficient if you decided after reading the earlier section on PTT/CAT control that you do not need **ardopcf** to handle PTT because a host program like [Pat](https://getpat.io) will handle this or because you will use VOX.  If you decided that you want **ardopcf** to handle PTT, then add those additional options.  For example:

`ardopcf -p /dev/ttyUSB1 -i 1 -o 1`

or

`ardopcf -c /dev/ttyUSB1 --keystring FEFE88E01C0001FD --unkeystring FEFE88E01C0000FD -i 1 -o 1`

There are some additional command line options that you might want to use.  If you are using the `-i` and `-o` options to set your audio devices (and not using the legacy method of providing three positional parameters), then the order in which the command line options are given does not matter.  See [Commandline_options.md](Commandline_options.md) for info on all possible options.

A.  By default, **ardopcf** writes some log messages to the console and writes more detailed log messages to log files in the directory where it is started.  You can change where these files are created with the `-l` or `--logdir` option.  For example, I created `$HOME/ardop_logs` and use `--logdir ~/ardop_logs`.  To not write any log files, use the `--nologfile` or `-m` options.  To redirect log messages to the Linux syslog that would otherwise be printed to the console, use `--syslog` or `-S` (Linux only).  To write more or less detail to the console and log files, use the `CONSOLELOG` and `LOGLEVEL` host commands respectively.  These each take a value from 1 (most detail) to 6 (least detail), and can be set using the `-H` or `--hostcommands` option described below.

B.  The `-H` or `--hostcommands` option can be used to automatically apply one or more semicolon separated commands that **ardopcf** accepts from host programs like [Pat](https://getpat.io).  See [Host_Interface_Commands.md](Host_Interface_Commands.md) for more information about these commands.  If the first commands are `LOGLEVEL` and/or `CONSOLELOG` (as described above), then these are applied before most other command line options are evaluated.  Processing them before the logging system is initiated ensures that the log settings you want are in place before any log messages are processed.

The host commands are applied in the order that they are written. Except for those leading `LOGLEVEL` and `CONSOLELOG` commands, the order usually doesn't matter.  As an example, `--hostcommands "LOGLEVEL 1;CONSOLELOG 2;MYCALL AI7YN"` sets the log file to be as detailed as possible, and data printed to the console to be only slightly less detailed, and sets my callsign to `AI7YN`.  Pat will also set my callsign, so this isn't usually necessary as a startup option, but it is a convenient example.  Because most commands will include a command, a space, and a value, you usually need to put quotation marks around the commands string.  After you adjust your sound audio levels, you may discover that you want the **ardopcf** transmit drive level to be less than the default of 100%.  As another example, `--hostcommands "CONSOLELOG 4;MYCALL AI7YN;DRIVELEVEL 90"` leaves the log file settings at their default value, but prints only more important messages to the console, sets my callsign, and set the transmit drive level to 90%.

C.  To enable the WebGui use `-G 8514` or `--webgui 8514`.  This sets the WebGui to be available by typing `localhost:8514` into the navgation bar of your web browser.  You may choose a different port number if 8514 causes a conflict with other software.  The WebGui is likely to be useful when you adjust the transmit and receive audio levels as described later.

So, annother example of the complete command you might want to use to start **ardopcf** is:

`ardopcf --logdir ~/ardopc_logs -p /dev/ttyUSB1 -G 8514 --hostcommands "CONSOLELOG 4" -i 1 -o 1`

With this running, **ardopcf** is functional and ready to be used by a host program like [Pat](https://getpat.io).  However, you probably don't want to type all of this every time you want to start **ardopcf**.  So, the next sections describe some better options for starting **ardopcf**.  All of them will use the sequence of options that you identified in this section.


## Starting **ardopcf** from the command line.

Starting **ardopcf** from the command line is useful if you access your Linux machine through a text only interface such as ssh, or if you use a Linux desktop but prefer the use of a terminal window to GUI menus.  For this use, a bash script containing all of the necessary command line options make starting **ardopcf** easy.  So, use a text editor to create `$HOME/bin/ardop` with contents similar to the following.  See the [section](#ardopcf-audio-devices-and-other-options) on audio devices and other options for help understanding the ardopcf command line.
```
#! /bin/bash
ardopcf --logdir ~/ardopc_logs -p /dev/ttyUSB1 -G 8514 --hostcommands "CONSOLELOG 4;MYCALL AI7YN;DRIVELEVEL 85" -i 1 -o 1
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

## Adjusting your audio levels

Once you have confirmed that **ardopcf** is successfully connected to your radio, you need to adjust the audio transmit and receive levels.

In addition to making adjustments in **ardopcf** and with your radio, alsamixer or amixer are used to adjust your computer's audio settings.  See https://linux.die.net/man/1/alsamixer and https://linux.die.net/man/1/amixer for descriptions of the use of the two ALSA control programs.  Of these alsamixer is interactive, while amixer can be used to set values directly from the command line or a bash script.

### Adjusting your transmit audio.

If your trasmitter has speech compression or other features that modify/distort transmit audio, these should be disabled when using any digital mode, including Ardop.

Your transmit audio level can be adjusted using a combination of your radio's settings, the ALSA controls, and the **ardopcf** Drivelevel setting.  Drivelevel can be set when starting **ardopcf** using the `DRIVELEVEL` command with the `--hostcommands` option or with the slider in the **ardopcf** WebGui.  In theory, drivelevel can also be controlled from a host program like Pat, but I don't believe that any existing host programs provide this function.

Together these settings influence the strength and quality of your transmitted radio signal.

Reduced audio amplitude can be used to decrease the RF power of your transmitted signals when using a single sideband radio transmitter. In addition to limiting your power to only what is required to carry out the desired communications (for US Amateur operators this is required by Part 97.313(a)), reducing your power output may also be necessary if your radio cannot handle extended transmissions at its full rated power when using high duty cycle digital modes. While sending data, Ardop can have a very high duty cycle as it sends long data frames with only brief breaks to hear short DataACK or DataNAK frames from the other station.

If the output audio is too loud, your radio's Automatic Level Control (ALC) will adjust this audio before using it to modulate the RF signal. This adjustment may distort the signal making it more difficult for other stations to correctly receive your transmissions. Some frame types used by Ardop may be more sensitive to these distortions than others.

To properly configure these settings you need to know how to monitor and interpret your radio's ALC response and power output.  On most radios a low ALC value means that the ALC is not distorting your signal.  However, on other radios, including my Xiegu G90, a high ALC value means that the ALC is not distoring your signal.  You should be able to find this information in the manual for your radio or on the internet.  You can also determine it experiemntally by adjusting the **ardopcf** drivelevel and ALSA speaker level both to very low values.  Clicking on the `Send2Tone` button on the **ardopcf** WebGui should produce a low power and low distortion transmitted signal.  Whatever your ALC shows in this condition is the desireable value.  As you increase the **ardopcf** drivelevel and ALSA speaker level, the tranmsit power of your radio should increase.  At some point, usually close to the configured power level of your radio, the transmitted signal will start to become more distorted.  When this occurs, the ALC level will start to change toward a worse setting.  As you continue to increase your audio settings, the power output should remain relatively constant, while the ALC setting will indicate a progressively worse value.  You want to choose the combination of Drivelevel, ALSA speaker level, and radio settings that produce the desired amount of power without significantly disorting your audio (as indicated by too much change in the ALC indicator).  How much change in the ALC indicator is "too much" may vary from radio to radio.

If your radio does not have an ALC indicator, but either it has a power output indicator or you have a separate power meter that you can use to measure transmitted power while making adjustments, you can also use this to choose appropriate transmit audio levels.  If you slowly increase your audio level until the measured transmit power stops increasing, you have probably identified the audio level at which the ALC is starting to distort your signal.  So, use an audio level that produces slightly less than the maximum measured transmit power.

On some (higher quality?) radios, suitable audio level settings are independent of the band/frequency and power level settings of your radio.  On other radios, including my Xiegu G90, different bands require different audio settings, and reducing the power level setting also requires reducing the audio level.  This appears to indicate that the power level setting of the Xiegu G90 simply causes it to engage the ALC at lower audio levels.  My recommendation is that you choose radio settings and ALSA speaker settings that allow you to use only the **ardopcf** drivelevel slider to make ongoing adjustments (using the WebGui).  I also recommend that you write down the radio and ALSA speaker settings that work well so that if they get changed (intentionally or accidentally), you can quickly restore them to settings that you know should work well.

While transmit audio settings using the `Send2Tone` function are usually pretty good, monitoring of ALC and/or power level while sending actual Ardop data frames may indicate that further (usually minor) changes to transmit audio levels are appropriate.

If you set your radio for a higher power level than you intend to transmit at, and then use a reduced drivelevel to reduce your power output, then minor fluctuations are unlikely to engage the ALC causing any distortion.  Using this approach, you really only need to be concerned about ALC and distortion if you are trying to use the full rated power of your transmitter.

### Adjusting your receive audio level.

Normally, you should turn off the AGC function on your radio while working with digital signals, especially digital signals (including Ardop) that occupy only a small part of your radio's receive bandwidth.  Instead, I use manual adjustments to RF gain as needed.  AGC attempts to keep the average power level across the total receive bandwidth relatively steady.  When using digital modes like the 200 Hz or 500 Hz Ardop bandwidths, there may be other signals within the receiver's bandwidth that are unrelated to the Ardop signal that I am trying to receive.  Under these conditions, AGC may significantly alter the strength of the Ardop signal as an unrelated strong signal turns on or off.

Unlike transmit audio level which can be partially controlled through the **ardopcf** Drivelevel setting, there is no mechanism to control received audio level through **ardopcf**.  Received audio level is controlled only by settings on your radio and the ALSA mic settings.

Adjusting the receive audio level is also slightly more difficult than adjusting the transmit audio level because it depends on some signal being received by your radio.  Conveniently, the received signal does not need to be an Ardop signal to do initial setup.  Inconveniently, unlike your transmit audio settings, it may require adjustment each time you use **ardopcf** depending on noise level and band conditions.

The **ardopcf** WebGui `Rcv Level` indicates the instantaneous level of the audio being received.  If you tune your radio to a quiet portion of a band with low background noise, this should be mostly or entirely grey, with perhaps only a little bit of a green signal idicator near the left edge.  Under these conditions, if the **ardopcf** WebGui shows a yellow `(Low Audio)` warning next to the `Rcv Level` graph, that is OK.

When you receive a strong signal or when the backgound noise is high, that same indicator should be up to about half green, or even more.  Luckily, **ardopcf** can handle a relatively large range of acceptable audio levels, and seems to decode relatively low audio volume signals reasonably well if the signal is stronger than the background noise.  Too loud of a received signal is more likely to cause problems than too soft of a received signal.  So, if in doubt, reduce the receive audio level a bit.  With the current popularity of FT-8, tuning to one of the FT-8 frequencies is often a convenient way to receive a relatively strong signal, which is actually the combination of several simultaneous FT-8 signals.  These signals are also usually steady on for about 12 second, then off for a few seconds, and then repeat this pattern.  So, if you tune your radio to an active FT-8 frequency, you can often adjust the audio settings so that the `Rcv Level` graph peaks at about 50% green in each 15-second period and then drops down to near zero in the gaps between these transmissions.  That is often a good initial audio setting.  If the `Rcv Level` graph turns orange, the audio is too loud.  It is important that you pay attention to the `Rcv Level` graph and not the colors of the signals appearing on the waterfall graph.  The waterfall graph has a slow but strong automatic gain control feature that tries to always show high contrast between any received signals and the background noise independent of the total audio volume..

While these settings based on received FT-8 are a good starting point, watching the `Rcv Level` graph while receiveing actual Ardop transmissions may indicate that you should make further adjustments to your receive audio levels.  As with adjusting drivelevel settings, I recommend that you choose one control element that you will use for final adjustment of receive audio levels, and set any other controls (on the radio and/or computer) to fixed values.  Making quick adjustments to more than one control are not practical.
