# Using ardopcf on Linux

There are three main steps to using **ardopcf** on Linux.  You need to obtain the `ardopcf` binary executable file, determine what command line options you need to use, and choose a convenient way to start it.  The ways to start it discussed below include from a command line prompt using a bash script, using [PATMENU2](http://github.com/km4ack/patmenu2) as installed by [Build-a-Pi](https://github.com/km4ack/pi-build) or [73Linux](https://github.com/km4ack/73Linux), from the desktop menu, or from a desktop shortcut,

Complete configuration of **ardopcf** can be done via the options used to start it, and this is recommended for most repeated use.  However, it is also possible to almost completely configure ardopcf interactively using the WebGUI.  This includes the ability to choose and configure the audio and CAT/PTT devices.  To set some configuration parameters interactively, it may be neccesary to enable the WebGUI Developer Mode by specifying a negative port number with the `-G` or `--webgui` command line option.  The remainder of this document mostly assumes that you want to fully configure **ardopcf** at startup.

Throughout this page, I assume that you know how to use basic Linux commands like `cd`, `mkdir`, `cp`, `mv`, and `chmod`, and/or how to achieve similar results using a Linux desktop file manager.  I also assume that you know how to create and edit text files, and run a few command line programs like `dmesg`, even if you normally don't do much with the command line.  It is not necessary to have root access to build, install, and use **ardopcf**.  Where appropriate, I will try to indicate what you might need to do differently if you do not have root access.  If you do have root access, you may need to use `sudo` with some commands or an equivalent action if you are using a Linux desktop file manager.

## Getting `ardopcf`

If you want to try the latest changes that have been made to **ardopcf** since the last release, you can [build](BUILDING.md) it from source.  If you are using a recent version of Linux on a Intel/AMD machine or on a Raspberry Pi, then you can also download one of the pre-built binary executable files from the [releases](https://github.com/pflarue/ardop/releases/latest) page at GitHub.

If you downloaded a pre-built binary executable, you should rename it from something like `ardopcf_amd64_Linux_64` or `ardopcf_arm_Linux_32` to just `ardopcf`.

**ardopcf** requires only a single excutable file, `ardopcf`, and it requires no special installation procedure.  However, you probably don't want to run it from your Downloads directory (though you can if you want to).  I recommend moving it to either `/usr/local/bin` or (especially if you don't have root access) `$HOME/bin`.  This file must also be set as executable.

## Ardopcf PTT/CAT conrol

**ardopcf** has the ability to handle the PTT of your radio in a variety of ways, but other than activating PTT, it does not do other CAT control.  The following describes how to specify the PTT control device with the command used to start **ardopcf**.  However, it is also possible to select and configure such devices interactively using the WebGUI.  (ardopcf does allow a host program to provide hex strings to pass to a CAT serial device or TCP port, but since these strings must be appropriate for the specific model of radio or TCP middleware such as Hamlib/rigctld that you are using, this feature is not generally used.)  **ardopcf** can also allow a host program like [Pat](https://getpat.io) to handle PTT.  With Pat, this is convenient because it can also use CAT control to set your radio's frequency.  Other host programs, such as [ARIM](https://www.whitemesa.net/arim/arim.html) and [gARIM](https://www.whitemesa.net/garim/garim.html) do not have CAT or PTT capabilities, so they require **ardopcf** to handle PTT and require you to manually set the frequency on your radio.  A third option is to use the VOX capability of your radio to engage the PTT.  This may work OK for FEC mode operation with ARIM or gARIM, but can be unreliable for the ARQ mode operation used by Pat because it may not engage or disengage quickly enough.

If you want Pat to do PTT and CAT control then do not use any of the `-p`, `--ptt`, `-c`, `--cat`, `-k`, `--keystring`, `-u`, or `--unkeystring` options described in the remainder of this section.  Instead, set `"ptt_ctrl": true` in the ardop section of the Pat configuration and use the `"rig"` setting to point to a rig defined in the `"hamlib_rigs"` section.  The remainder of this section will assume that you want **ardopcf** to control PTT.  Note that you should **NOT** try to have **ardopcf** do PTT using the same USB or serial device that you will ask PAT (via Hamlib/rigctld) or any other program use for CAT control.  However, you may set Pat to provide CAT control via Hamlib/rigctld to set the frequency, while setting **ardopcf** to handle PTT via the same Hamlib/rigctld middleware.

**ardopcf** has a few methods available to controlling PTT.  Serial device RTS/DTR, CAT commands sent to a serial device or TCP port (as used with Hamlib/rigctld), Raspberry Pi GPIO pin, and use of CM108 sound devices using IO pin 3 for PTT will be described here.

RTS/DTR, CAT (except TCP CAT), and CM108 PTT all require the device name of the radio interface.  Usually this will be something line `/dev/ttyUSB1` for a serial device for RTS/DTR or `/dev/hidraw0` for a CM108 device.  While there may be other ways of determining this, I recommend the following.  If your radio interface is already connected to your computer, disconnect it and wait a few seconds.  Connect/reconnect your radio to your computer and run:

`dmesg | tail`

This will print the last 10 lines of the dmesg log.  You should see something like either a reference to `/dev/ttyUSB1` or `attached to ttyUSB1` for a serial device or `hidraw0: USB HID` for a CM108 device.

For the RTS/DTR serial devices, you can also specify the serial device by its id.  (This cannot be used for a CM108 device.)  This can be useful if, for example, sometimes your device shows up as /dev/ttyUSB0, and other times it is /dev/ttyUSB1.  `ls /dev/serial/by-id` will list the available serial device id values.  The serial device of my DigiRig appears as `/dev/serial/by-id/usb-Silicon_Labs_CP2102N_USB_to_UART_Bridge_Controller_3cc92e511387ed11b72131d7a603910e-if00-port0` while the serial device of my QRP-Labs QMX appears as `/dev/serial/by-id/usb-QRP_Labs_QMX_Transceiver-if00`.  These are a lot to type if you were typing the ardopcf command manually every time.  However, if you put this into a Bash script or desktop shortcut, the length of this id is not a problem.

If the interface between your computer and your radio supports PTT control via RTS, this is often the simplest solution.  To use RTS PTT use the `-p /dev/ttyUSB1:BAUD` or `--ptt /dev/ttyUSB1:BAUD` option, but using the device name found above using dmesg and `BAUD` for the required baud rate.  If `:BAUD` is ommitted, the default speed of 19200 baud is used.  Often this default is acceptable.  You can find the baud rate required for your radio interface in the manual for your radio or on the internet.

If the interface between your computer and your radio supports PTT control via DTR, this is very similar to the solution for RTS, but with a `DTR:` prefix applied to the device name.  To use DTR PTT use the `-p DTR:/dev/ttyUSB1:BAUD` or `--ptt DTR:/dev/ttyUSB1:BAUD` option, but using the device name found above using dmesg and `BAUD` for the required baud rate.  If `:BAUD` is ommitted, the default speed of 19200 baud is used.  Often this default is acceptable.  You can find the baud rate required for your radio interface in the manual for your radio or on the internet.

If RTS/DTR PTT does not work, you may be able to use CAT PTT.  For this use the `-c /dev/ttyUSB1:BAUD` or `--cat /dev/ttyUSB1:BAUD` option, but using the device name found above using dmesg and `BAUD` for the required baud rate.  If `:BAUD` is ommitted, the default speed of 19200 baud is used.  Often this default is acceptable.  You can find the baud rate required for your radio interface in the manual for your radio or on the internet.

You can also direct **ardopcf** to pass CAT commands via a TCP port rather than a serial device.  This has been tested with Hamlib/rigctld.  Given the correct PTT ON and PTT OFF command strings, it should work with any radio or other middleware that accepts CAT commands via a TCP port.  By default, rigctld uses TCP port 4532.  So, with rigctld running on your local machine, `-c TCP:4532` will allow **ardopcf** to connect to it.  If rigctld is running on a different networked computer, you can use an option such as `-c TCP:192.168.100.5:4532` to connect to that remote rigctld process.  Any IPv4 address and port expressed as ddd.ddd.ddd.ddd:port should work.

The `--cat` or `-c` option only sets the CAT serial device or TCP port to be used.  To use CAT for PTT control, it is also necessary to provide the actual cat commands as hex strings to key (`-k` or `--keystring`) and unkey (`-u` or `--unkeystring`) your radio.  These will be specific to the model of radio or interface that you are using, or to the middleware such as Hamlib/rigctld that you are connecting to.  So you will have to find them in your radio manual or on the internet.

For example, my Xiegu G90 can do PTT via CAT control with `-c /dev/ttyUSB1 --keystring FEFE88E01C0001FD --unkeystring FEFE88E01C0000FD`.  For this radio `FEFE` is the fixed preamble to all CI-V commands, `88` is the transceiver address expected by Xiegu radios, and `E0` is the controller address.  Then command number `1C` is used for PTT/ATU control, sub-command `00` means set PTT status, data = `01` means press PTT while `00` means release PTT, and `FD` means end of message.  For Kenwood, QRP-Labs, Elecraft, and TX-500 `-c /dev/ttyUSB1:BAUD --keystring 54583B --unkeystring 52583B` should work, which is the HEX values for 'TX;' and 'RX;'.  I don't know what baud rate is requried for these, but the default works with my Xiegu.  Remember that `-c /dev/ttyUSB1` is equivalent to `-c /dev/ttyUSB1:19200` because **ardopcf** uses a default of 19200 baud if a value is not provided.  While CAT control of PTT works for my Xiegu, using `-p /dev/ttyUSB1` for RTS PTT is simpler and works just as well.  With the [Digirig](https://www.digirig.net) interface that I use, another advantage of using RTS PTT is that it requires only the audio cable, so I can leave the serial cable disconnected.

If you are using Hamlib/rigctld, then the hex strings for PTT ON and PTT OFF must be the Hamlib/rigctld command strings rather than the command strings specific to your radio.  So, if you are using `--cat TCP:4532` with rigctld, you would always use `-k 5420310A -u 5420300A`, regardless of the type of radio you are using.  These are the hex representations of `T 1\n` and `T 0\n` for TX and RX respectively.  Since these three options are likely to be used together for most people using Hamlib/rigctld with **ardopcf**, the `--cat RIGCTLD` or `-c RIGCTLD` shortcut is provided.  This is equivalent to `-c TCP:4532 -k 5420310A -u 5420300A`.  The option used to start rigctld specifies the radio model you are using and how it is connected to your computer.  Installation and setup of Hamlib/rigctld is beyond the scope of this document.

If you are not already using Hamlib/rigctld, there is probably no advantage to installing and configuring it only for use by **ardopcf**.  However, if you are already running Hamlib/rigctld and have it configured to work with your radio, then it may be convenient to use these options.  One advantage of using Hamlib/rigctld via a TCP CAT port is that, unlike a serial device, multiple programs can connect to one TCP CAT port simultaneously.  For example, you can have **ardopcf** use it for PTT control, while Pat uses the same TCP CAT port to set the radio frequency.

It is possible to control PTT using a GPIO pin of a Raspberry Pi computer by connecting speccialized hardware to one of its electrical connections.  **WARNING**: This option attempts to directly control the hardware of your Raspberry Pi, and thus may cause physical damage if used incorrectly.  So, don't use this option unless you know what you are doing.  Some online sources suggest that this may not work with the PI5, but I cannot confirm this.  The details of the electrical/hardware connections required for this are beyond the scope of these instructions.  To use a Raspberry Pi GPIO pin for PTT control (with appropriate hardware attached) use `-p GPIO:pin` or `--ptt GPIO:pin`, where `pin` is the integer pin number to use.  Thus, `--ptt GPIO:17` causes a Raspberry Pi's GPIO 17 pin to be high for transmit, and low for receive.  To invert the pin signal, use the negative of the pin number.  Thus, `--ptt GPIO:-17` causes a Raspberry Pi's GPIO 17 pin to be low for transmit, and high for receive.  This option is not available for other than the Raspberry Pi computers.

CM108 devices are USB devices that combine an audio interface with PTT control.  These include user modified USB sound devices as well as commercial products including: the [digirig Lite](https://digirig.net/product/digirig-lite) (not to be confused with the [digirig mobile](https://digirig.net/product/digirig-mobile) which I refer to as simply a digirig elsewhere in this documentation), the [AIOC or All-in-one-Cable](https://github.com/skuep/AIOC) which is also available from https://na6d.com, and various products from [masterscommunications.com](https://masterscommunications.com).

These CM108 devices typically appear as `/dev/hidraw0`.  To use this device for PTT use the `-p CM108:/dev/hidraw0` or `--ptt CM108:/dev/hidraw0` option.  Unfortunately, by default, the access permissions for these devices do not allow them to be used.  If you try to use such a device without fixing this you will see an error message instructing you to try `sudo chmod 666 /dev/hidraw0` and then try agian.  This should normally work, but is only a temporary solution.  Unplugging and reconnecting the device, or rebooting the computer will require repeating this process.

For a persistent solution to the problem of insufficient access permisions for a CM108 device, it is necessary to create a udev rule.  This is done by creating a file in the `/etc/udev/rules.d` directory.  For a more detailed description of how these rules work, see https://reactivated.net/writing_udev_rules.html.

For my [AIOC](https://github.com/skuep/AIOC) with VID:PID of 1209:7388, I created a file named `/etc/udev/rules.d/40-cm108-1209-7388.rules` that contains a single line of text `ATTRS{idVendor}=="1209", ATTRS{idProduct}=="7388", MODE:="0666"`.  The filename is somewhat arbitrary, but should start with a number less than 50 and must have a `.rules` extension.  Note the use of VID and PID values as hex strings.  If you don't know these values, you can find them with the command `lsusb -v`.  The last part sets the permissions to read/write.  Various online sources disagree about whether to use `ATTRS` or `ATTR` and whether to use `MODE:=` or `MODE=` without the colon.  This is what works for me.

For my [digirig Lite](https://digirig.net/product/digirig-lite) with VID:PID of 0D8C:0012, I created a file named `/etc/udev/rules.d/40-cm108-0D8C-0012.rules` that contains a single line of text `ATTRS{idVendor}=="0d8c", ATTRS{idProduct}=="0012", MODE:="0666"`.  One reference I saw also indicated that if your VID or PID contains any letters, they may need to be lowercase.  This appears to be true.  Using `ATTRS{idVendor}=="0D8C"` in that udev rule file did not work.

While the digirig lite is fully supported by ardopcf, I do not recommend it for use with a Raspberry Pi for portable use.  Trying it with my Raspberry Pi Zero 2W, I found that I often have to connect it, disconnect it, and repeat this several times before the Pi recognizes it.  I don't have this problem with the more common but slightly more expensive and slightly larger [digirig mobile](https://digirig.net/product/digirig-mobile).  So, especially for portable use where such lack of reliability may be particularly inconvenient, I think that the digirig mobile is a better choice.  I also described my experience with this in a [comment on the digirig forum](https://forum.digirig.net/t/digipi-raspberry-pi-zero-not-finding-digirig-lite/5159).

## Ardopcf audio devices and other options.

Your Linux computer may have multiple audio Capture (input) and Playback (output) devices.  **Ardopcf** must be told which of these devices to use.  The following describes how to specify the audio devices with the command used to start **ardopcf**.  However, it is also possible to select and configure these devices interactively using the WebGUI.  **ardopcf** is designed to work with ALSA audio devices.  This low level audio interface system should be available on all Linux machines.

However, especially when running a Linux desktop interface, a higher level sound server like Pulse Audio, PipeWire, or Jack may also be running.  If the list of audio devices displayed by **ardopcf** at startup includes `pulse`, then you may choose to use that instead of one of the ALSA devices such as `plughw:N,0` described in this section.  Using `pulse` is recommended if it is an available option and you need multiple programs to share the same audio devices.  See the section called **Using pulse audio devices** for more details.  If `pulse` is available on your Linux computer, but you don't need multiple programs to share the same audio devices, then you can choose either to use `pulse` or one of the other devices described in this section.

ALSA also allows the configuration of plugins to allow multiple programs to use the same audio device.  These custom configured plugins should not be used if the `pulse` device is available on your computer as described in the previous paragraph.  To configure custom plugins on Linux systems where `pulse` is not available requires creating a `.asoundrc` file in your home directory.  An [example](example.asoundrc) of such a file is provided with this documentation.  This example creates four new software audio devices, mix0, mix1, snoop0, and snoop1.  The mixN devices are for playback (output), while the snoopN devices are for capture (input).  As described in the example file, you can easily create additional versions for card numbers greater than 1.

Every time that **ardopcf** is started (except when the `--help` or `-h` option are used, or if you set `CONSOLELOG N` too high), it will print a list of all of the audio devices that it finds and can open.  Earlier versions of **ardopcf** displayed only hardware devices, but it now provides a more comprehensive list.  So, run `ardopcf` with no other options from a command line to see the list of audio devices.  Then press CTRL-C to exit **ardopcf**.  This should print something that looks similar to:
```
ardopcf Version 1.0.4.1.3 (https://www.github.com/pflarue/ardop)
Copyright (c) 2014-2025 Rick Muething, John Wiseman, Peter LaRue
See https://github.com/pflarue/ardop/blob/master/LICENSE for licence details including
 information about authors of external libraries used and their licenses.
Capture (input) Devices
   null [Discard all samples (playback) or generate zero samples (capture)] (capture) (playback)
   default [Default Audio Device] (capture) (playback)
   sysdefault [Default Audio Device] (capture) (playback)
   snoop0 [Resample and snoop (capture) hw:1,0] (capture)
   hw:CARD=Device,DEV=0 [hw:1,0. USB PnP Sound Device, USB Audio] (capture) (playback)
   plughw:CARD=Device,DEV=0 [plughw:1,0. USB PnP Sound Device, USB Audio] (capture) (playback)
   default:CARD=Device [default:1. USB PnP Sound Device, USB Audio] (capture) (playback)
   sysdefault:CARD=Device [sysdefault:1. USB PnP Sound Device, USB Audio] (capture) (playback)
   front:CARD=Device,DEV=0 [front:1,0. USB PnP Sound Device, USB Audio] (capture) (playback)
   dsnoop:CARD=Device,DEV=0 [dsnoop:1,0. USB PnP Sound Device, USB Audio] (capture)
   NOSOUND [A dummy audio device for diagnostic use.] (capture) (playback)
[* before capture or playback indicates that the device is currently in use for that mode by this or another program]
Playback (output) Devices
   null [Discard all samples (playback) or generate zero samples (capture)] (capture) (playback)
   default [Default Audio Device] (capture) (playback)
   sysdefault [Default Audio Device] (capture) (playback)
   mix0 [Resample and mix (play) hw:1,0] (playback)
   hw:CARD=Device,DEV=0 [hw:1,0. USB PnP Sound Device, USB Audio] (capture) (playback)
   plughw:CARD=Device,DEV=0 [plughw:1,0. USB PnP Sound Device, USB Audio] (capture) (playback)
   default:CARD=Device [default:1. USB PnP Sound Device, USB Audio] (capture) (playback)
   sysdefault:CARD=Device [sysdefault:1. USB PnP Sound Device, USB Audio] (capture) (playback)
   front:CARD=Device,DEV=0 [front:1,0. USB PnP Sound Device, USB Audio] (capture) (playback)
   iec958:CARD=Device,DEV=0 [iec958:1,0. USB PnP Sound Device, USB Audio] (playback)
   dmix:CARD=Device,DEV=0 [dmix:1,0. USB PnP Sound Device, USB Audio] (playback)
   NOSOUND [A dummy audio device for diagnostic use.] (capture) (playback)
[* before capture or playback indicates that the device is currently in use for that mode by this or another program]

  No audio devices were successfully opened based on the options
  specified on the command line.  Suitable devices must be
  configured by a host program or using the WebGUI before Ardop
  will be usable.


  No PTT control method has been successfully enabled based on
  the options specified on the command line.  This configuration
  is suitable for TX ONLY IF the host program is configured to
  handle PTT or if the radio is configured to use VOX (which is
  often not reliable).  Otherwise, a suitable method of PTT
  control must be configured by a host program or using the
  WebGUI before Ardop will be able to transmit.

ardopcf listening for host connection on host_port 8515
Webgui available at wg_port 8514.
Setting ProtocolMode to ARQ.
```

In this case the various Capture and Playback devices whose name includes `CARD=Device` and whose description includes `USB PnP Sound Device, USB Audio` relate to the [Digirig](https://www.digirig.net) interface that I use to connect to my Xiegu G90.  If you are unsure which device represents the interface to your radio, compare the results of running `ardopcf` with and without your interface connected to your computer.

The entry for each device begins with the device name such as `plughw:CARD=Device,DEV=0` or `mix0`.  This is followed by a description enclosed in square brackets.  If the name of the device has the form `prefix:CARD=XXX.DEV=M`, then the first item within the square brackets is an alias of the form `prefix:N,M`.  If the input for an audio device is an exact case sensistive match for the name or alias of an available audio device, then it will be used.  If not, then the descriptions will be searched for a case insensitive substring match, and the first such match will be used.  A small integer `N` may also be used as a shortcut for the alias `plughw:N,0`.

Some of the audio devices in this list may not be appropriate for use with **ardopcf**.  For example, the device `hw:CARD=Device,DEV=0 [hw:1,0. USB PnP Sound Device, USB Audio] (capture) (playback)`, or any device with a `hw:` prefix  doesn't usually work.  This is because most sound cards do not natively support the 12 kHz sample rate that **ardopcf** uses.  However, the corresponding device with a `plughw:` prefix uses an ALSA default plugin that allows it to resample the audio to sample rates not directly supported by the hardware.

Using a substring match for part of the desciption such as `USB` often does not work well on Linux machines because the first match for `USB` in the example above would be the `hw:` device rather than the `plughw:` device that is more likely to be usable.

Use the `-i` option to set the (input) audio capture device and the `-o` option to set the (output) audio playback device.  Earlier versions of **ardopcf** did not support these options, and thus required that you also specify the host port number to set these values.  So, for my system, I would use:
`ardopcf -i 1 -o 1`

which is equivalent to the more verbose

`ardopcf -i plughw:CARD=Device,DEV=0 -o plughw:CARD=Device,DEV=0`
or
`ardopcf -i plughw:1,0 -o plughw:1,0`

If you have created `.asoundrc` in your home directory from the provided example file, and you want to share card 1 (based on hw:1,0) with another program, then instead you would use:
`ardopcf -i snoop1 -o mix1`

Any of these options start **ardopcf** and may be sufficient if you decided after reading the earlier section on PTT/CAT control that you do not need **ardopcf** to handle PTT because a host program like [Pat](https://getpat.io) will handle this or because you will use VOX.  If you decided that you want **ardopcf** to handle PTT, then add those additional options.  For example:

`ardopcf -p /dev/ttyUSB1 -i 1 -o 1`

or

`ardopcf -c /dev/ttyUSB1 --keystring FEFE88E01C0001FD --unkeystring FEFE88E01C0000FD -i 1 -o 1`

There are some additional command line options that you might want to use.  If you are using the `-i` and `-o` options to set your audio devices (and not using the legacy method of providing three positional parameters), then the order in which the command line options are given does not matter.  See [Commandline_options.md](Commandline_options.md) for info on all possible options.

A.  By default, **ardopcf** writes some log messages to the console and writes more detailed log messages to log files in the directory where it is started.  You can change where these files are created with the `-l` or `--logdir` option.  For example, I created `$HOME/ardop_logs` and use `--logdir ~/ardop_logs`.  To not write any log files, use the `--nologfile` or `-m` options.  To redirect log messages to the Linux syslog that would otherwise be printed to the console, use `--syslog` or `-S` (Linux only).  To write more or less detail to the console and log files, use the `CONSOLELOG N` and `LOGLEVEL N` host commands respectively.  These each take a value from 1 (most detail) to 6 (least detail), and can be set using the `-H` or `--hostcommands` option described below.

B.  The `-H` or `--hostcommands` option can be used to automatically apply one or more semicolon separated commands that **ardopcf** accepts from host programs like [Pat](https://getpat.io).  See [Host_Interface_Commands.md](Host_Interface_Commands.md) for more information about these commands.  If the first commands are `LOGLEVEL N` and/or `CONSOLELOG N` (as described above), then these are applied before most other command line options are evaluated.  Processing them before the logging system is initiated ensures that the log settings you want are in place before any log messages are processed.

The host commands are applied in the order that they are written. Except for those leading `LOGLEVEL N` and `CONSOLELOG N` commands, the order usually doesn't matter.  As an example, `--hostcommands "LOGLEVEL 1;CONSOLELOG 2;MYCALL AI7YN"` sets the log file to be as detailed as possible, and data printed to the console to be only slightly less detailed, and sets my callsign to `AI7YN`.  Pat will also set my callsign, so this isn't usually necessary as a startup option, but it is a convenient example.  Because most commands will include a command, a space, and a value, you usually need to put quotation marks around the commands string.  After you adjust your sound audio levels, you may discover that you want the **ardopcf** transmit drive level to be less than the default of 100%.  As another example, `--hostcommands "CONSOLELOG 4;MYCALL AI7YN;DRIVELEVEL 90"` leaves the log file settings at their default value, but prints only more important messages to the console, sets my callsign, and set the transmit drive level to 90%.

C.  By default, the WebGui is enabled using port 8514 so that the WebGui is available by typing `localhost:8514` into the navgation bar of your web browser.  The WebGui is likely to be useful when you adjust the transmit and receive audio levels as described later.  You may disable with WebGui by specifying a port number of zero using `-G 0` or `--webgui 0`.  You may choose a different port number with `-G 5014` or `--webgui 5014` or any other integer if 8514 causes a conflict with other software.  If a negative port number is given, such as `-G -8514`, then the corresponding positive number is used, but the WebGui is opened in developer mode.  Developer mode allows arbitrary host commands to be entered from within the WebGui, writes additional details in the WebGui Log display, and provides a button to start/stop recording of RX audio to a WAV file.  Developer mode is not intended for normal use, but is a useful tool for debugging purposes.

So, annother example of the complete command you might want to use to start **ardopcf** is:

`ardopcf --logdir ~/ardopc_logs -p /dev/ttyUSB1 --hostcommands "CONSOLELOG 4" -i 1 -o 1`

With this running, **ardopcf** is functional and ready to be used by a host program like [Pat](https://getpat.io).  However, you probably don't want to type all of this every time you want to start **ardopcf**.  So, the next sections describe some better options for starting **ardopcf**.  All of them will use the sequence of options that you identified in this section.

## Using pulse audio devices

As mentioned in the previous section, **ardopcf** can also use `pulse` as the audio input and output devices with:
`ardopcf -i pulse -o pulse`

**WARNING** While using `pulse` instead of an ALSA device seems to normally work well, on one of my test systems it caused a problem.  When testing how **ardopcf** responds to turning off or unplugging a radio or radio interface while it is being used, on my Raspberry Pi 0W running a 32-bit OS with the desktop and PulseAudio systems, this sometimes caused **ardopcf** to lock up.  When this occured, then CTRL-C would not kill the program.  So, to stop it I had to use `ps -e | grep ardopcf` to find the process id, and then `kill -9 NNN`, where `NNN` is the process id of the program.  For all other systems that I tested, turning off or unplugging a radio or radio interface while it was in use did not adversely affect **ardopcf**.  After then turning the device back on or plugging it back in, continued use was possible after using WebGui to press the `ENABLE Audio Processing` button and entering a value of `RESTORE` in the `PTT device` or `CAT device` input box.  The `Show Config Controls` button is used to expose these controls.  On the machine where this problem occured, using an audio device name like `plughw:0,0` rather than `pulse` works correctly.  I was unable to find any change to the source code which would mitigate this problem.

If `pulse` devices are not available on your computer, then this will produce an error message such as `Error opening audio device pulse for capture (No such file or directory)`, and you must use ALSA devices such as `plughw:0,0` as described in the previous section.

If `pulse` devices are available on your computer, then you should not install the `.asoundrc` file and use the `mixN` and `snoopN` devices defined there and described in the previous section.

If you want to share a single audio device beween **ardopcf** and another program and `pulse` audio devices are available on your system, then you should use them for this purpose rather than `mixN` and `snoopN`.  If `pulse` devices are available, but you do not need to share an audio device with another program, then you can either use `pulse` or an ALSA device such as `plughw:0,0`.

If the `pulse` devices are available and you choose to use them with **ardopcf**, you will generally need to configure your audio system to indicate what audio hardware you wish to use.  This can be done with some desktop GUI tools not described here, or it can be done using environment variables as follows.  To do this, you first need to determine the names of the pulse devices that correspond to your radio interface.  With a DigiRig Mobile device connecting my Xiegu G90 to my computer I run the following command.
`pactl list short`

This displays a long list of devices including:
```
428     alsa_output.usb-C-Media_Electronics_Inc._USB_Audio_Device-00.analog-stereo      PipeWire        s16le 2ch 48000Hz       SUSPENDED
428     alsa_output.usb-C-Media_Electronics_Inc._USB_Audio_Device-00.analog-stereo.monitor      PipeWire        s16le 2ch 48000Hz       SUSPENDED
429     alsa_input.usb-C-Media_Electronics_Inc._USB_Audio_Device-00.mono-fallback       PipeWire        s16le 1ch 48000Hz       SUSPENDED
```

Notice that in this case there are two `alsa_output` devices, one of which has the suffix `.monitor`.  That is a special device that can be used to hear what you are playing to the corresponding `alsa_output` device.  Don't use that `.monitor` device.  To start **ardopcf** using `pulse` devices while indicating that the DigiRig Mobile should be used, I use the following command:
```
PULSE_PROP_APPLICATION_NAME=ARDOPCF \
PULSE_SINK=alsa_output.usb-C-Media_Electronics_Inc._USB_Audio_Device-00.analog-stereo \
PULSE_SOURCE=alsa_input.usb-C-Media_Electronics_Inc._USB_Audio_Device-00.mono-fallback \
ardopcf -i pulse -o pulse
```

Note that this is a single bash command.  The slash characters at the end of each but the final line mean that it is equivalent to typing all of this on a single line.  Setting `PULSE_PROP_APPLICATION_NAME` is optional, and simply identifies the name of the program that is being configured.  The specific value is not important, but using `ARDOPCF` seems like a good choice.  The second line sets `PLUSE_SINK` to the correct `alsa_output` device, while the third line sets `PULSE_SOURCE` to the correct `alsa_input` device.  Finally, the last line starts ardopcf with the `-i pulse -o pulse` indicating that `pulse` devices are to be used for both audio input (capture) and output (playback).

See the previous section **Ardopcf audio devices and other options** for how to combine the `-i` and `-o` options to select the audio devices with other options.  The next section **Starting ardopcf from the command line** also includes an example that uses `pulse` audio devices and several additional options.


## Starting **ardopcf** from the command line.

Starting **ardopcf** from the command line is useful if you access your Linux machine through a text only interface such as ssh, or if you use a Linux desktop but prefer the use of a terminal window to GUI menus.  For this use, a bash script containing all of the necessary command line options make starting **ardopcf** easy.  So, use a text editor to create `$HOME/bin/ardop` with contents similar to the following.  See the [section](#ardopcf-audio-devices-and-other-options) on audio devices and other options for help understanding the ardopcf command line.
```
#! /bin/bash
ardopcf --logdir ~/ardopc_logs -p /dev/ttyUSB1 --hostcommands "CONSOLELOG 4;MYCALL AI7YN;DRIVELEVEL 85" -i 1 -o 1
```

If you are using `pulse` audio devices, this might instead be similar to:
```
#! /bin/bash
PULSE_PROP_APPLICATION_NAME=ARDOPCF \
PULSE_SINK=alsa_output.usb-C-Media_Electronics_Inc._USB_Audio_Device-00.analog-stereo \
PULSE_SOURCE=alsa_input.usb-C-Media_Electronics_Inc._USB_Audio_Device-00.mono-fallback \
ardopcf --logdir ~/ardopc_logs -p /dev/ttyUSB1 --hostcommands "CONSOLELOG 4;MYCALL AI7YN;DRIVELEVEL 85" -i pulse -o pulse
```

Once you have created this file, use `chmod` or an equivalent feature in a file manager to make it executable.

**ardopcf** on Linux ignores SIGHUP.  This means that if the terminal (such as an ssh session or a Linux desktop terminal window) where **ardopcf** was started is closed, **ardopcf** will continue to run.  This may be useful in some use cases.  If this behavior is not desired, see the section below about starting **ardopcf** from a desktop menu.  The script used for that kills **ardopcf** when its terminal closes.

If you are connecting to your Linux computer via ssh and want to run several programs such as **ardopcf**, [Hamlib/rigctld](https://hamlib.github.io), and [Pat](https://getpat.io), then it may be useful to either run these programs in the background using a trailing ampersand (&), or to use a terminal multiplexer such as [screen](https://www.gnu.org/software/screen) or [tmux](https://github.com/tmux/tmux/wiki).  One advantage of using a terminal multiplexer is that any output printed by each program is kept separate which may make it easier to interpret.  Another tool which I have found useful when running Pat and **ardopcf** on a headless raspberry pi is [ttyd](https://github.com/tsl0922/ttyd).  This provides access to the Linux command line from a web browser (including the one on your smartphone).  If the programs are run in the background, it may be useful to redirect their output to a file (or even to /dev/null).  Details for such options are beyond the scope of this document.

## Starting **ardopcf** with patmenu2

KM4ACK's [PAT MENU 2](http://github.com/km4ack/patmenu2) as installed by [Build-a-Pi](https://github.com/km4ack/pi-build) or [73Linux](https://github.com/km4ack/73Linux) is a popular tool for using [Pat](https://getpat.io) on Linux systems.  The Pat Menu 2 Settings/Config screen has a space for an 'ARDOP Command'.  Put `ardopcf` with all of the appropriate options as described in the [section](#ardopcf-audio-devices-and-other-options) on audio devices and other options here.

That Settings screen also has a spot for an 'ARDOP GUI Comand'.  This was intended for use with John Wiseman's ARDOP GUI program, but you might choose to use it to open your web browser to **ardopcf**'s WebGui.  To do this, set 'ARDOP GUI Command' to `xdg-open 'http://localhost:8514`.  You might also choose to create a bookmark in your web browser to this address for use if the browser is already open.

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

In addition to making adjustments in **ardopcf** and with your radio, alsamixer or amixer are used to adjust your computer's audio settings.  See https://linux.die.net/man/1/alsamixer and https://linux.die.net/man/1/amixer for descriptions of the use of the two ALSA control programs.  Of these alsamixer is interactive, while amixer can be used to set values directly from the command line or a bash script.  If you are using `pulse` audio devices, you can still use alsamixer or amixer, but you can also use interactive desktop GUI programs or the `pactl` command line tool.

### Adjusting your transmit audio.

If your trasmitter has speech compression or other features that modify/distort transmit audio, these should be disabled when using any digital mode, including Ardop.

Your transmit audio level can be adjusted using a combination of your radio's settings, the ALSA/Pulse controls, and the **ardopcf** Drivelevel setting.  Drivelevel can be set when starting **ardopcf** using the `DRIVELEVEL` command with the `--hostcommands` option or with the slider in the **ardopcf** WebGui.  In theory, drivelevel can also be controlled from a host program like Pat, but I don't believe that any existing host programs provide this function.

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
