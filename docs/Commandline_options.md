# ARDOPCF Command Line Options

The following is a summary of all of the available command line options.  Additional details about how to use ardopcf, including what command line arguments to use can be found in [USAGE_linux.md](USAGE_linux.md) and [USAGE_windows.md](USAGE_windows.md).

### Interactive Configuration

The remainder of this document describes options to configure ardopcf from the command line at startup, and this is recommended for most repeated use of ardopcf.  However, it is also possible to almost completely configure ardopcf interactively using the WebGUI.  This includes the ability to choose and configure the audio and CAT/PTT devices.  To set some configuration parameters interactively, it may be neccesary to enable the WebGUI Developer Mode by specifying a negative port number with the `-G` or `--webgui` command line option.

The configuration options that can only be set from the command line are those relating to selection of Host interface and WebGUI ports, and some logging related options.  The level of detail written to the console and debug log file can be controlled interactively via the `CONSOLELOG` and `LOGLEVEL` host commands, but entirely disabling writing of the log file, selection of the log file directory, and redirection of the console log to syslog are only possible from the command line.

## 1. Usage

ardopcf [options] &lt;host-tcp-port&gt; [ &lt;audio-capture-device&gt; ] [ &lt;audio-playback-device&gt; ]

- **options**, which might also be referred to as non-positional parameters, are anything from the tables below.  They all begin with either **&#8209;** or **&#8209;&#8209;**.  Some of them must be followed by an argument, while others take no argument.  They can be in any order,
and may come before or after the positional arguments.
- **host-tcp-port** is the TCP port used by host programs to communicate with ardopcf
- **audio-capture-device** specifies which audio input device to use as input for ardopcf. Different formats are used in Linux and Windows, see below.  To specify this as a positional parameter, it is necessary to also specify the host TCP port.  To avoid this requirement, the audio capture device can also be specified with the **-i** non-positional parameter.  If both **-i** and this positional parameter are used, then the value provided with the **-i** non-positional parameter will be ignored (and a WARNING will be logged).  If an audio input device is not specified with a positional or non-positional parameter, or if the device specified cannot be opened and configured, then an audio device must be specified by a host program or by using the configuration controls in the WebGUI.  Until this is done, ardopcf will be unable to recieve any incoming signals.  `NOSOUND` may be specified as an audio device for testing/debugging purposes.
- **audio-playback-device** specifies audio output device. Different formats are used in Linux and Windows, see below.  To specify this as a positional parameter, it is necessary to also specify the host TCP port and the audio capture device.  To avoid this requirement, the audio playback device can also be specified with the **-o** non-positional parameter.  If both **-o** and this positional parameter are used, then the value provided with the **-o** non-positional parameter will be ignored (and a WARNING will be logged).  If an audio output device is not specified with a positional or non-positional parameter, or if the device specified cannot be opened and configured, then an audio device must be specified by a host program or by using the configuration controls in the WebGUI.  Until this is done, ardopcf will be unable to transmit, though if an audio input device was specified, it may be able to receive incoming signals.  `NOSOUND` may be specified as an audio device for testing/debugging purposes.

### Audio device selection in Windows

_ardopcf_ will list all available capture and playback devices when started. Each device has a name, and is followed by a description in square brackets.  To select one of these devices from the command line, the full exact case sensitive name may be used.  If no match for this is found, then a search for a case insensitive substring match in the descriptions is done, and the first match is used.  A small integer `N` may be used as a shortcut for `iN` for capture/input devices or `oN` for playback/output devices.

Example: startup on a Windows machine might show the following:
```
ardopcf Version 1.0.4.1.3 (https://www.github.com/pflarue/ardop)
Copyright (c) 2014-2025 Rick Muething, John Wiseman, Peter LaRue
See https://github.com/pflarue/ardop/blob/master/LICENSE for licence details including
information about authors of external libraries used and their licenses.
Capture Devices
   i0 [Microphone (USB Audio Device)] (capture)
   i1 [Microphone Array (Intel Smart ] (capture)
   NOSOUND [A dummy audio device for diagnostic use.] (capture) (playback)
[* before capture or playback indicates that the device is currently in use for that mode by this or another program]
Playback (output) Devices
   o0 [Speakers (USB Audio Device)] (playback)
   o1 [Speakers (Realtek(R) Audio)] (playback)
   NOSOUND [A dummy audio device for diagnostic use.] (capture) (playback)
[* before capture or playback indicates that the device is currently in use for that mode by this or another program]
```
In this example, Capture and Playback devices `i0` and `o0` represent a Digirig Lite radio interface device. So to select this device, start the program with these parameters:
```
ardopcf 8515 i1 o1
```
or
```
ardopcf -i i1 -o o1
```
or
```
ardopcf -i USB -o USB
```
or
```
ardopcf -i "USB Audio" -o "usb audio device"
```


_(In this example we omitted CAT/PTT options, see below in section 3)_


### Audio device selection in Linux

_ardopcf_ will list all available capture and playback devices when started.  Each device has a name, and is followed by a description in square brackets.  Some devices include an alias of the form `prefix:N,M` followed by a period before the description.  To select one of these devices from the command line, the full exact case sensitive name or alias may be used.  If no match for this is found, then a search for a case insensitive substring match in the descriptions is done, and the first match is used.  A small integer `N` may be used as a shortcut for the alias `plughw:N,0`.

Example: startup on a Linux machine might show the following:
```
ardopcf Version 1.0.4.1.3 (https://www.github.com/pflarue/ardop)
Copyright (c) 2014-2025 Rick Muething, John Wiseman, Peter LaRue
See https://github.com/pflarue/ardop/blob/master/LICENSE for licence details including
 information about authors of external libraries used and their licenses.
Capture (input) Devices
   null [Discard all samples (playback) or generate zero samples (capture)] (capture) (playback)
   default [Default Audio Device] (capture) (playback)
   sysdefault [Default Audio Device] (capture) (playback)
   snoop0 [Resample and snoop (capture) hw:0,0] (capture)
   hw:CARD=Transceiver,DEV=0 [hw:0,0. QMX Transceiver, USB Audio] (capture) (playback)
   plughw:CARD=Transceiver,DEV=0 [plughw:0,0. QMX Transceiver, USB Audio] (capture) (playback)
   default:CARD=Transceiver [default:0. QMX Transceiver, USB Audio] (capture) (playback)
   sysdefault:CARD=Transceiver [sysdefault:0. QMX Transceiver, USB Audio] (capture) (playback)
   front:CARD=Transceiver,DEV=0 [front:0,0. QMX Transceiver, USB Audio] (capture) (playback)
   dsnoop:CARD=Transceiver,DEV=0 [dsnoop:0,0. QMX Transceiver, USB Audio] (capture)
   NOSOUND [A dummy audio device for diagnostic use.] (capture) (playback)
[* before capture or playback indicates that the device is currently in use for that mode by this or another program]
Playback (output) Devices
   null [Discard all samples (playback) or generate zero samples (capture)] (capture) (playback)
   default [Default Audio Device] (capture) (playback)
   sysdefault [Default Audio Device] (capture) (playback)
   mix0 [Resample and mix (play) hw:0,0] (playback)
   hw:CARD=Transceiver,DEV=0 [hw:0,0. QMX Transceiver, USB Audio] (capture) (playback)
   plughw:CARD=Transceiver,DEV=0 [plughw:0,0. QMX Transceiver, USB Audio] (capture) (playback)
   default:CARD=Transceiver [default:0. QMX Transceiver, USB Audio] (capture) (playback)
   sysdefault:CARD=Transceiver [sysdefault:0. QMX Transceiver, USB Audio] (capture) (playback)
   front:CARD=Transceiver,DEV=0 [front:0,0. QMX Transceiver, USB Audio] (capture) (playback)
   iec958:CARD=Transceiver,DEV=0 [iec958:0,0. QMX Transceiver, USB Audio] (playback)
   dmix:CARD=Transceiver,DEV=0 [dmix:0,0. QMX Transceiver, USB Audio] (playback)
   NOSOUND [A dummy audio device for diagnostic use.] (capture) (playback)
[* before capture or playback indicates that the device is currently in use for that mode by this or another program]
```

Several of the Capture and Playback devices containing the word `Transceiver` are the USB audio devices that are part of a QRPLabs QMX radio.  It is common on Linux computers to see `null`, `default:...`, and `sysdefault:...` devices in addition to those that are expected.  If this Linux machine was running PulseAudio, a `pulse` device with the description `PulseAudio Sound Server` would also be listed.  On this machine, the file `~/.asoundrcand` file described in  [USAGE_linux.md](USAGE_linux.md) includes definitions for `snoop0`, `snoop1`, `mix0`, and `mix1`.  These are derived from `hw:0,0` and `hw:1,0`.  Since there is no `hw:1,0` on this machine, `snoop1` and `mix1` are not listed.  The default `dsnoop` and `dmix` devices are also available, but not generally useful.  Unless the audio device must be shared with another program, either `pulse` or one of the `plughw:N,M` devices should generally be used.  To select this version of the QMX Transcevier from the example above for both capture (receive) and playback (transmit), start the program with these parameters:
```
ardopcf 8515 plughw:CARD=Transceiver,DEV=0 plughw:CARD=Transceiver,DEV=0
```
or
```
ardopcf -i plughw:0,0 -o plughw:0,0
```
or
```
ardopcf 8515 0 0
```
or
```
ardopcf -i 0 -o 0
```

Unlike in the windows example, using a substring match for part of the desciption such as `ardopcf -i QMX -o QMX` would not work.  The first match for `QMX` would be `hw:CARD=Transceiver,DEV=0 [hw:0,0. QMX Transceiver, USB Audio] (capture) (playback)`.  This device, unlike `plughw:0,0`, cannot be opened as a 16-bit mono audio device with a 12 kHz sample rate.

_(In this example we omitted CAT/PTT options, see below in section 3)_

All command line options are listed in the following sections. If the option has an argument, it follows the option after a space.

## 2. Initialization and help

| short option | long option | argument | description |
|----|----|----|----|
| **-H** | **&#8209;&#8209;hostcommands** | &lt;string&gt; | A single string containing a sequence of host commands to be applied (in the order given) at the start of the program.  Multiple commands must be separated by semicolons (;). This option provides capabilities previously provided by some obsolete command line options.  See [Host_Interface_Commands.md](Host_Interface_Commands.md) for descriptions of the various commands that are available.  With a few exceptions, these commands are applied after other command line options have been parsed and the audio and PTT control systems are initialized.  However, leading LOGLEVEL and/or CONSOLELOG commands with new values are parsed early so that log messages created during the startup process will be logged in accordance with these settings.  LOGLEVEL and CONSOLELOG commands without new values are not parsed early. |
| **-h** | **&#8209;&#8209;help** | _none_ | Print a help screen and exit.  (Not processed via the logging system.) |

## 3. CAT and PTT

| short option | long option | argument | description |
|----|----|----|----|
| **-c** | **&#8209;&#8209;cat** | [TCP:]&lt;device&gt;[:&lt;baudrate&gt;]&#124;RIGCTLD | device or TCP port to send CAT commands to the radio, e.g. _-c COM9:38400_ for Windows or _-c /dev/ttyUSB0:115200_ for Linux.  This same serial port may also be specified as the device for the --ptt or -p option for RTS or DTR PTT control.  The TCP: prefix may be used to specify a TCP port number rather than a serial device to use for CAT control.  The most likely use for this is to use Hamlib/rigctld for cat control.  e.g. -c TCP:4532 uses TCP port 4532, the default rigctld port, on the local machine for CAT control.  TCP:ddd.ddd.ddd.ddd:port, where ddd are numbers between 0 and 255, may be used to specify a port on a networked machine rather than on the local machine.  The `--cat RIGCTLD` option may be used as a shortcut for `--cat TCP:4532 -k 5420310A -u 5420300A` or `--cat TCP:4532 -k "T 1\n" -u "T 0\n"` to use Hamlib/rigctld for PTT using the default port of 4532 on the local machine.  In addition to setting the TCP CAT port, that shortcut also defines the Hamlib/rigctld commands for PTT ON and PTT OFF. |
| **-p** | **&#8209;&#8209;ptt** | [RTS:&#124;DTR:&#124;CM108:&#124;GPIO:]&lt;device&gt; | device to activate radio PTT using RTS or DTR signaling on a physical or virtual serial port, or to use pin 3 of a CM108 compatible device, or to use a GPIO pin on a Raspberry Pi.  Since PTT via RTS is very common, it is the default if no prefix is applied.  An RTS: prefix may be used, but is unnecessary.  The DTR: prefix specifies use of DTR signaling.  Both RTS and DTR are used with a serial device identified as on Windows as COM# or on Linux as /dev/ttyUSB# or /dev/ttyACM#.  This same serial device may also be specified with the --cat or -c option.  The CM108: prefix indicates that the device is a CM108 compatible HID device such as a [digirig-lite](https://digirig.net/product/digirig-lite),  [AIOC](https://github.com/skuep/AIOC), or various products from [masterscommunications.com](https://masterscommunications.com).  On Windows CM108:? can be used to list attached devices with the VID:PID string for each, and then `--ptt CM108:VID:PID` is used to specify that device.  On Linux, CM108 devices appear as /dev/hidraw#.  It is typically necessary to adjust the access permissions for hidraw devices using `sudo chmod` or a udev rule.  On a Raspberry Pi computer, the GPIO: prefix may be used, followed by the pin number as an integer.  If a negative pin number is given, then the corresponding positive value is used, but the PTT signal is inverted.  This option is not usable, except on Raspberry Pi computers.  Some online sources suggest that this may not work with the PI5, but I don't have one, so I cannot confirm this.  **WARNING**: The GPIO: prefix attempts to directly control the hardware of your Raspberry Pi, and thus may cause physical damage if used incorrectly.  So, don't use this option unless you know what you are doing. |
| **-k**| **&#8209;&#8209;keystring** |  &lt[ASCII:]string&gt; | CAT command to switch radio to transmit mode.  If the string contains only an even number of valid hex characters (upper or lower case with no whitespace), then it is intrepreted as hex.  Otherwise, if it contains only printable ASCII characters 0x20-0x7E, it is interpreted as ASCII text with substition for "\\n" and "\\r".  The prefix of "ASCII:" may be used to force the string to be interpreted as ASCII text, and this is required for ASCII text that contains only an even number of valid hex characters.  This is backward compatible with the prior implementation that accepted only hex input, except that what would have been invalid hex, may now be accepted as valid ASCII.  E.g. for Kenwood, Elecraft, QDX, QMX, TX-500 the command is "TX;", the actual command line option will be _-k 54583B_ or _-k "TX;"_ or _-k "ASCII:TX;"_.  Depending on the operating system and the contents of the string, it may be necessary to wrap the string in quotes.  This may be required if the string contains spaces, semicolons, etc.  This is used with the **-c** or **&#8209;&#8209;cat** option and is NOT needed when the **-p** or **&#8209;&#8209;ptt** option is used. |
| **-u**| **&#8209;&#8209;unkeystring** |  &lt[ASCII:]string&gt; | CAT command to switch radio to receive mode.  If the string contains only an even number of valid hex characters (upper or lower case with no whitespace), then it is intrepreted as hex.  Otherwise, if it contains only printable ASCII characters 0x20-0x7E, it is interpreted as ASCII text with substition for "\\n" and "\\r".  The prefix of "ASCII:" may be used to force the string to be interpreted as ASCII text, and this is required for ASCII text that contains only an even number of valid hex characters.  This is backward compatible with the prior implementation that accepted only hex input, except that what would have been invalid hex, may now be accepted as valid ASCII.  E.g. for Kenwood, Elecraft, QDX, QMX, TX-500 the command is "RX;", the actual command line option will be _-u 52583B_ or _-u "RX;"_ or _-u "ASCII:RX;"_.  Depending on the operating system and the contents of the string, it may be necessary to wrap the string in quotes.  This may be required if the string contains spaces, semicolons, etc.  This is used with the **-c** or **&#8209;&#8209;cat** option and is NOT needed when the **-p** or **&#8209;&#8209;ptt** option is used. |

*Note: Dial frequency and other CAT control functions are not provided by ardopcf.  However, if a CAT port is specified then the RADIOHEX host command can be used (by a host program or with --hostcommands at startup) to pass an arbitrary string of bytes (as hex or ascii) to the radio.  Since these strings are radio specific, they are not commonly used.*

## 4. Audio Options

| short option | long option | argument | description |
|----|----|----|----|
| **-i** | | CaptureDevice | This is an alternative to specifying the audio capture device with a positional parameter.  If both this and the corresponding positional parameter are used, this option will be ignored and a WARNING message will be logged.  An argument of `NOSOUND` or `-1`  may be specified for testing/debugging purposes.  `NOSOUND` (but not `-1`) can also be used as a positional parameter. |
| **-o** | | PlaybackDevice | This is an alternative to specifying the audio playback device with a positional parameter.  If both this and the corresponding positional parameter are used, this option will be ignored and a WARNING message will be logged.  An argument of `NOSOUND` or `-1`  may be specified for testing/debugging purposes.  `NOSOUND` (but not `-1`) can also be used as a positional parameter. |
| **-L** | | _none_ | use only left audio channel of a stereo device for RX. |
| **-R** | | _none_ | use only right audio channel of a stereo device for RX. |
| **-y** | | _none_ | use only left audio channel of a stereo device for TX. |
| **-z** | | _none_ | use only right audio channel of a stereo device for TX. |
| **-w** | **&#8209;&#8209;writewav** |  _none_  | Write WAV files of received audio for debugging.  (The RECRX host command and a button in the the developer mode version of the WebGui can also be used to start/stop recording of received audio to a WAV file independent this option.) |
| **-T** | **&#8209;&#8209;writetxwav** |  _none_  | Write WAV files of transmitted audio for debugging. |
| **-d** | **&#8209;&#8209;decodewav** |  &lt;pathname&gt;  | Decode the supplied WAV file instead of the input audio.  This option can be repeated up to five times to provide up to five WAV files to be decoded as if they were received in the order provided, with a brief period of silence between them.  Unlike handling of most other command line arguments, the --hostcommands (or -H) option is processed before the WAV files are processed.  After these WAV files have been processed, ardopcf will exit. |
| **-s** | **&#8209;&#8209;sfdt** |  _none_  | Use alternative Sliding DFT based 4FSK decoder. |

## 5. Other options

| short option | long option | argument | description |
|----|----|----|----|
| **-m** | **&#8209;&#8209;nologfile** | _none_ | Don't write log files. Use only logging to console (or syslog). |
| **-S** | **&#8209;&#8209;syslog** | _none_ | Redirect log messages to syslog that would otherwise be printed to the console.  Useful mainly for systemd services or daemons. Use the `CONSOLELOG N` host command to control the verbosity of syslog messages. Combine with **&#8209;&#8209;nologfile** to use only syslog/journald, and not also write a ardop specific log file.  (Linux only) |
| **-l** | **&#8209;&#8209;logdir** | &lt;pathname&gt; | The absolute or relative path where log files and WAV files are written.  Without this option, these files are written to the start directory. |
| **-G** | **&#8209;&#8209;webgui** | &lt;TCP port&gt; | TCP port to access WebGui.  If a value of  0 is given, then disable the WebGui.  Without this option, the WebGui uses the default port of 8514.  If this value interferes with the Host port or Data port, then an alternative value is automatically selected, and written to the log.  If a negative TCP port number is given, such as -8514, then the corresponding positive number is used, but the WebGui is opened in developer mode. Developer mode allows arbitrary host commands to be entered from within the WebGui, writes additional details in the WebGui Log display, and provides a button to start/stop recording of RX audio to a WAV file. Developer mode is not intended for normal use, but is a useful tool for debugging purposes. |

## 6. Order of evaluation

Normally, the non-positional parameters are all evaluated in the order given, followed by the positional parameters.  The exceptions are that the **&#8209;&#8209;nologfile**, **-m**, **&#8209;&#8209;syslog**, **-S**, **&#8209;&#8209;help**, **-h**, host TCP port positional parameter, **&#8209;&#8209;logdir**, **-l**, and any leading LOGLEVEL and/or CONSOLELOG commands that include new values in the argument to **&#8209;&#8209;hostcommands** or **-H** are always processed first.  These are given priority because they configure the logging system.  By processing them first, the logging system can be initialized and configured before the rest of the command line parameters are evaluated.  This allows any informational, warning, or error messages generated while processing the command line arguments to be appropriately logged.  With the exception of the results from the **&#8209;&#8209;help** or **-h** option, and responses printed while handling shutdown signals, ardopcf should not print anything to the console that is not passed through the logging system, where it can be controlled by these settings.  Of course, if a bad argument is provided to one of these options, then logging system may not be configured entirely as expected before that error is logged.  No more than the first two host commands in the argument to **&#8209;&#8209;hostcommands** or **-H** are considered for early evaluation.  Therefore, no more than one each of `LOGLEVEL N` and `CONSOLELOG N` should be provided for early evaluation.  If the first host command is not either `LOGLEVEL N` or `CONSOLELOG N`, or if the first host command cannot be properly parsed, then the second host command is not be evaluated.
