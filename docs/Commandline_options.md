# ARDOPCF Command Line Options

The following is a summary of all of the available command line options.  Additional details about how to use ardopcf, including what command line arguments to use can be found in [USAGE_linux.md](USAGE_linux.md) and [USAGE_windows.md](USAGE_windows.md).

## 1. Usage

ardopcf [options] &lt;host-tcp-port&gt; [ &lt;audio-capture-device&gt; ] [ &lt;audio-playback-device&gt; ]

- **options**, which might also be referred to as non-positional parameters, are anything from the tables below.  They all begin with either **&#8209;** or **&#8209;&#8209;**.  Some of them must be followed by an argument, while others take no argument.  They can be in any order,
and may come before or after the positional arguments.
- **host-tcp-port** is the TCP port used by host programs to communicate with ardopcf
- **audio-capture-device** specifies which audio input device to use as input for ardopcf. Different formats are used in Linux and Windows, see below.  To specify this as a positional parameter, it is necessary to also specify the host TCP port.  To avoid this requirement, the audio capture device can also be specified with the **-i** non-positional parameter.  If both **-i** and this positional parameter are used, then the value provided with the **-i** non-positional parameter will be ignored (and a WARNING will be logged).  `NOSOUND` may be specified as an audio device for testing/debugging purposes.
- **audio-playback-device** specifies audio output device. Different formats are used in Linux and Windows, see below.  To specify this as a positional parameter, it is necessary to also specify the host TCP port and the audio capture device.  To avoid this requirement, the audio playback device can also be specified with the **-o** non-positional parameter.  If both **-o** and this positional parameter are used, then the value provided with the **-o** non-positional parameter will be ignored (and a WARNING will be logged).  `NOSOUND` may be specified as an audio device for testing/debugging purposes.

If an audio device is not specified by either a positional parameter or the `-i` or `-o` option, then it defaults to a value of `0`.

### Audio device selection in Windows

_ardopcf_ will list all available capture and playback devices when started. Each device has a number. Use this number as the respective parameter in the command line.

Example: startup shows the following list
```
ardopcf Version 1.0.4.1.3 (https://www.github.com/pflarue/ardop)
Copyright (c) 2014-2024 Rick Muething, John Wiseman, Peter LaRue
See https://github.com/pflarue/ardop/blob/master/LICENSE for licence details including
information about authors of external libraries used and their licenses.
Capture Devices
0 Microphone (2 - USB Audio Device)
1 Microphone (4 - USB Audio CODEC )
2 Stereo Mixer (Realtek High Definition)
Playback Devices
0 Speaker (Realtek High Definition)
1 Speaker (4 - USB Audio CODEC)
2 Speaker (2 - USB Audio Device)
3 PL2474H (Intel(R) Display Audio)
```
In this example, Capture and Playback devices 1 labeled _4 - USB Audio CODEC_ is the built-in audio device in Kenwood TS-590. So to select the Kenwood transceiver, start the program with these parameters:
```
ardopcf 8515 1 1
```
or
```
ardopcf -i 1 -o 1
```

_(In this example we omitted CAT/PTT options, see below in section 3)_


### Audio device selection in Linux

_ardopcf_ will list all available ALSA capture and playback devices when started.  If your Linux computer is running PulseAudio, it may also work to specify `pulse` as an audio device.  `pulse` will never be listed as an available audio device by ardopcf.

Example: startup shows the following list
```
ardopcf Version 1.0.4.1.3 (https://www.github.com/pflarue/ardop)
Copyright (c) 2014-2025 Rick Muething, John Wiseman, Peter LaRue
See https://github.com/pflarue/ardop/blob/master/LICENSE for licence details including
 information about authors of external libraries used and their licenses.
No audio input device was specified, so using default of 0.
No audio output device was specified, so using default of 0.
Capture Devices

Card 0, ID 'vc4hdmi', name 'vc4-hdmi'

Card 1, ID 'Device', name 'USB PnP Sound Device'
  Device hw:1,0 ID 'USB Audio', name 'USB Audio', 1 subdevices (1 available)
    1 channel,  sampling rate 44100..48000 Hz

Playback Devices

Card 0, ID 'vc4hdmi', name 'vc4-hdmi'
  Device hw:0,0 ID 'MAI PCM i2s-hifi-0', name 'MAI PCM i2s-hifi-0', 1 subdevices (1 available)
ALSA Error at pcm_hw.c:1722 snd_pcm_hw_open(): Unknown error 524
Error -524 opening output device

Card 1, ID 'Device', name 'USB PnP Sound Device'
  Device hw:1,0 ID 'USB Audio', name 'USB Audio', 1 subdevices (1 available)
    2 channels,  sampling rate 44100..48000 Hz
```

In this example, the Capture and Playback devices labeled _USB PnP Sound Device_ are a digirig device.  They are recognized as ALSA device `hw:1,0`.  However, specifying `hw:1,0` for this device will fail because this device does not natively support the audio sampling rate of 12000 Hz required by ardopcf.  As shown above, this device natively supports only sampling rates of 41000..48000 Hz, as is common for many audio devices.  However, ALSA provides a plugin that can resample the audio to wider range of values.  To use this plugin connected to the `hw:1,0` device, replace the `hw:` with `plughw:` to specify `plughw:1,0`.  To simplify this, it is also possible to specify use of ALSA `plughw:` with just the card number.  So, any non-negative integer `X` specified for an audio device is interpreted as an alias for `plughw:X,0`.  Thus `1` is an alias for `plughw:1,0`, and the default of `0` used when no audio device is specified, is an alias for `plughw:0,0`.  To select the digirig from the example above for both capture (receive) and playback (transmit), start the program with these parameters:
```
ardopcf 8515 plughw:1,0 plughw:1,0
```
or
```
ardopcf -i plughw:1,0 -o plughw:1,0
```
or
```
ardopcf 8515 1 1
```
or
```
ardopcf -i 1 -o 1
```

_(In this example we omitted CAT/PTT options, see below in section 3)_

All command line options are listed in the following sections. If the option has an argument, it follows the option after a space.

## 2. Initialization and help

| short option | long option | argument | description |
|----|----|----|----|
| **-H** | **&#8209;&#8209;hostcommands** | &lt;string&gt; | A single string containing a sequence of host commands to be applied (in the order given) at the start of the program.  Multiple commands must be separated by semicolons (;). This option provides capabilities previously provided by some obsolete command line options.  See [Host_Interface_Commands.md](Host_Interface_Commands.md) for descriptions of the various commands that are available.  With a few exceptions, these commands are applied after other command line options have been parsed and the audio and PTT control systems are initialized.  However, leading LOGLEVEL and/or CONSOLELOG commands are parsed early so that log messages created during the startup process will be logged in accordance with these settings. |
| **-h** | **&#8209;&#8209;help** | _none_ | Print a help screen and exit.  (Not processed via the logging system.) |

## 3. CAT and PTT

| short option | long option | argument | description |
|----|----|----|----|
| **-c** | **&#8209;&#8209;cat** | [TCP:]&lt;device&gt;[:&lt;baudrate&gt;] | device or TCP port to send CAT commands to the radio, e.g. _-c COM9:38400_ for Windows or _-c /dev/ttyUSB0:115200_ for Linux.  This same serial port may also be specified as the device for the --ptt or -p option for RTS or DTR PTT control.  The TCP: prefix may be used to specify a TCP port number rather than a serial device to use for CAT control.  The most likely use for this is to use Hamlib/rigctld for cat control.  e.g. -c TCP:4532 uses TCP port 4532, the default rigctld port, on the local machine for CAT control.  TCP:ddd.ddd.ddd.ddd:port, where ddd are numbers between 0 and 255, may be used to specify a port on a networked machine rather than on the local machine.  The `--ptt RIGCTLD` option may be used as a shortcut for `--cat TCP:4532 -k 5420310A -u 5420300A` to use Hamlib/rigctld for PTT using the default port of 4532 on the local machine.  In addition to setting the TCP CAT port, that shortcut also defines the Hamlib/rigctld commands for PTT ON and PTT OFF. |
| **-p** | **&#8209;&#8209;ptt** | [RTS:&#124;DTR:&#124;CM108:]&lt;device&gt; | device to activate radio PTT using RTS or DTR signaling on a physical or virtual serial port, or to use pin 3 of a CM108 compatible device, or to use Hamlib/rigctld.  Since PTT via RTS is very common, it is the default if no prefix is applied.  An RTS: prefix may be used, but is unnecessary.  The DTR: prefix specifies use of DTR signaling.  Both RTS and DTR are used with a serial device identified as on Windows as COM# or on Linux as /dev/ttyUSB# or /dev/ttyACM#.  This same serial device may also be specified with the --cat or -c option.  The CM108: prefix indicates that the device is a CM108 compatible HID device such as a [digirig-lite](https://digirig.net/product/digirig-lite),  [AIOC](https://github.com/skuep/AIOC), or various products from [masterscommunications.com](https://masterscommunications.com).  On Windows CM108:? can be used to list attached devices with the VID:PID string for each, and then `--ptt CM108:VID:PID` is used to specify that device.  On Linux, CM108 devices appear as /dev/hidraw#.  It is typically necessary to adjust the access permissions for hidraw devices using `sudo chmod` or a udev rule.  Using the psuedo-device RIGCTLD is equivalent to using the combination of options `-c TCP:4532 -k 5420310A -u 5420300A`.  This uses CAT control on TCP port 4532, the default port used by rigctld, and specifies the Hamlib/rigctld commands 'T 1\n' and 'T 0\n' as hex for PTT ON and PTT OFF respectively.  See [USAGE_linux.md](USAGE_linux.md) and [USAGE_windows.md](USAGE_windows.md) for more details. |
| **-g** | | &lt;pin&#8209;number&gt; | Raspberry Pi GPIO pin used as PTT.  Must be integer.  If a negative pin number is given, then the corresponding positive value is used, but the PTT signal is inverted.  Works only for Raspberry Pi devices.  Some online sources suggest that it may not work with the PI5, but I don't have one, so I cannot confirm this.  Earlier versions of ardopcf set the value to 17 if no argument was given.  Now, an argument must always be provided.  **WARNING**: This option attempts to directly control the hardware of your Raspberry Pi, and thus may cause physical damage if used incorrectly.  So, don't use this option unless you know what you are doing. |
| **-k**| **&#8209;&#8209;keystring** |  &lt;hex&#8209;string&gt; | CAT command to switch radio to transmit mode. E.g. for Kenwood, Elecraft, QDX, QMX, TX-500 the command is "TX;", the actual command line option will be _-k 54583B_.  This is used with the **-c** or **&#8209;&#8209;cat** option and is NOT needed when the **-p** or **&#8209;&#8209;ptt** option is used. |
| **-u** | **&#8209;&#8209;unkeystring** | &lt;hex&#8209;string&gt; | CAT command to switch radio to receive mode. E.g. for Kenwood, Elecraft, QDX, QMX, TX-500 the command is "RX;", the actual command line option will be _-u 52583B_.  This is used with the **-c** or **&#8209;&#8209;cat** option and is NOT needed when the **-p** or **&#8209;&#8209;ptt** option is used.  |

*Note: Dial frequency and other CAT control functions are not provided by ardopcf.  However, if a CAT port is specified then the RADIOHEX host command can be used (by a host program or with --hostcommands at startup) to pass an arbitrary string of bytes to the radio.  Since these strings are radio specific, they are not commonly used.*

## 4. Audio Options

| short option | long option | argument | description |
|----|----|----|----|
| **-i** | | CaptureDevice | This is an alternative to specifying the audio capture device with a positional parameter.  If both this and the corresponding positional parameter are used, this option will be ignored and a WARNING message will be logged.  If the audio capture device is not specified by either a positional parameter or this `-i` option, then a default value of `0` is used.  An argument of `NOSOUND` or `-1`  may be specified for testing/debugging purposes.  `NOSOUND` (but not `-1`) can also be used as a positional parameter. |
| **-o** | | PlaybackDevice | This is an alternative to specifying the audio playback device with a positional parameter.  If both this and the corresponding positional parameter are used, this option will be ignored and a WARNING message will be logged.  If the audio playback device is not specified by either a positional parameter or this `-o` option, then a default value of `0` is used.  An argument of `NOSOUND` or `-1`  may be specified for testing/debugging purposes.  `NOSOUND` (but not `-1`) can also be used as a positional parameter. |
| **-L** | | _none_ | use only left audio channel of a stereo device for RX. |
| **-R** | | _none_ | use only right audio channel of a stereo device for RX. |
| **-y** | | _none_ | use only left audio channel of a stereo device for TX. |
| **-z** | | _none_ | use only left audio channel of a stereo device for TX. |
| **-w** | **&#8209;&#8209;writewav** |  _none_  | Write WAV files of received audio for debugging.  (The RECRX host command and a button in the the developer mode version of the WebGui can also be used to start/stop recording of received audio to a WAV file independent this option.) |
| **-T** | **&#8209;&#8209;writetxwav** |  _none_  | Write WAV files of transmitted audio for debugging. |
| **-d** | **&#8209;&#8209;decodewav** |  &lt;pathname&gt;  | Decode the supplied WAV file instead of the input audio.  This option can be repeated up to five times to provide up to five WAV files to be decoded as if they were received in the order provided, with a brief period of silence between them.  Unlike handling of most other command line arguments, the --hostcommands (or -H) option is processed before the WAV files are processed.  After these WAV files have been processed, ardopcf will exit. |
| **-s** | **&#8209;&#8209;sfdt** |  _none_  | Use alternative Sliding DFT based 4FSK decoder. |
| **-A** | **&#8209;&#8209;ignorealsaerror** |  _none_ | Ignore ALSA config error that causes timing error. <br> **DO NOT** use -A option except for testing/debugging, or if ardopcf fails to run and suggests trying this. |

## 5. Other options

| short option | long option | argument | description |
|----|----|----|----|
| **-m** | **&#8209;&#8209;nologfile** | _none_ | Don't write log files. Use only logging to console (or syslog). |
| **-S** | **&#8209;&#8209;syslog** | _none_ | Redirect log messages to syslog that would otherwise be printed to the console.  Useful mainly for systemd services or daemons. Use the `CONSOLELOG` host command to control the verbosity of syslog messages. Combine with **&#8209;&#8209;nologfile** to use only syslog/journald, and not also write a ardop specific log file.  (Linux only) |
| **-l** | **&#8209;&#8209;logdir** | &lt;pathname&gt; | The absolute or relative path where log files and WAV files are written.  Without this option, these files are written to the start directory. |
| **-G** | **&#8209;&#8209;webgui** | &lt;TCP port&gt; | TCP port to access WebGui. By convention, it is the number of the host TCP port minus one, so usually 8514.  If a negative TCP port number is given, such as -8514, then the corresponding positive number is used, but the WebGui is opened in developer mode. Developer mode allows arbitrary host commands to be entered from within the WebGui, writes additional details in the WebGui Log display, and provides a button to start/stop recording of RX audio to a WAV file. Developer mode is not intended for normal use, but is a useful tool for debugging purposes. |

## 6. Order of evaluation

Normally, the non-positional parameters are all evaluated in the order given, followed by the positional parameters.  The exceptions are that the **&#8209;&#8209;nologfile**, **-m**, **&#8209;&#8209;syslog**, **-S**, **&#8209;&#8209;help**, **-h**, host TCP port positional parameter, **&#8209;&#8209;logdir**, **-l**, and any leading LOGLEVEL and/or CONSOLELOG commands in the argument to **&#8209;&#8209;hostcommands** or **-H** are always processed first.  These are given priority because they configure the logging system.  By processing them first, the logging system can be initialized and configured before the rest of the command line parameters are evaluated.  This allows any informational, warning, or error messages generated while processing the command line arguments to be appropriately logged.  With the exception of the results from the **&#8209;&#8209;help** or **-h** option, and responses printed while handling shutdown signals, ardopcf should not print anything to the console that is not passed through the logging system, where it can be controlled by these settings.  Of course, if a bad argument is provided to one of these options, then logging system may not be configured entirely as expected before that error is logged.
