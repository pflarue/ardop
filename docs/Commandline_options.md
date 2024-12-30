# ARDOPCF Command Line Options

The following is a summary of all of the available command line options.  Additional details about how to use ardopcf, including what command line arguments to use can be found in [USAGE_linux.md](USAGE_linux.md) and [USAGE_windows.md](USAGE_windows.md).

## 1. Usage

ardopcf &lt;host-tcp-port&gt; [ &lt;audio-capture-device&gt; &lt;audio-playback-device&gt; ]

- **host-tcp-port** is the TCP port used by host programs to communicate with ardopcf
- **audio-capture-device** specifies which audio input device to use as input for ardopcf. Different formats are used in Linux and Windows, see below.
- **audio-playback-device** specifies audio output device. Different formats are used in Linux and Windows, see below.

### Audio device selection in Windows

_ardopcf_ will list all available capture and playback devices when started. Each device has a number. Use this number as the respective parameter in the command line.

Example: startup shows the following list
```
ardopcf Version 1.0.4.1.2 (https://www.github.com/pflarue/ardop)
Copyright (c) 2014-2024 Rick Muething, John Wiseman, Peter LaRue
See https://github.com/pflarue/ardop/blob/master/LICENSE for licence details including
information about authors of external libraries used and their licenses.
Capture Devices
0 Microphone (4 - USB Audio CODEC )
1 Microphone (2 - USB Audio Device)
2 Stereo Mixer (Realtek High Definition)
Playback Devices
0 Speaker (4 - USB Audio CODEC)
1 Speaker (Realtek High Definition)
2 Speaker (2 - USB Audio Device)
3 PL2474H (Intel(R) Display Audio)
```
Capture and Playback devices 0 labeled _4 - USB Audio CODEC_ is the built-in audio device in Kenwood TS-590. So to select the Kenwood transceiver, start the program with these parameters:
```
ardopcf 8515 0 0
```
_(In this example we omitted CAT options, see below in section 3)_

All command line options are listed in the following sections. If the option has parameter, it follows the option after a space.

## 2. Initialization and help

| short option | long option | parameter | description |
|----|----|----|----|
| **-H** | **&#8209;&#8209;hostcommands** | &lt;string&gt; | Host commands to be used at start of the program in a single string. Commands are separated by semicolon ; This option provides capabilities previously provided by some obsolete command line options.  See [Host_Interface_Commands.md](Host_Interface_Commands.md) for descriptions of the various commands that are available. |
| **-h** | **&#8209;&#8209;help** | _none_ | Show help screen. |

## 3. CAT and PTT

| short option | long option | parameter | description |
|----|----|----|----|
| **-c** | **&#8209;&#8209;cat** | &lt;serial&#8209;port&gt;[:&lt;baudrate&gt;] | serial port to send CAT commands to the radio, e.g. _-c COM9:38400_ |
| **-p** | **&#8209;&#8209;ptt** | [DTR:]&lt;serial&#8209;port&gt; | serial device to activate radio PTT using RTS or DTR signal.  RTS is used by default, but the DTR: prefix specifies use of DTR rather than RTS.  May be the same device as CAT. |
| **-g** | | [&lt;pin&#8209;number&gt;] | ARM CPU GPIO pin used as PTT. Must be integer. If empty (no pin value), 17 will be used as default.  If a negative pin number is given, such as -17, then the corresponding positive value will be used, but the PTT signal will be inverted.   Applies only to ARM devices . |
| **-k**| **&#8209;&#8209;keystring** |  &lt;hex&#8209;string&gt; | CAT command to switch radio to transmit mode. E.g. for Kenwood, Elecraft, QDX, QMX, TX-500 the command is "TX;", the actual command line option will be _-k 54583B_.  This is used with the **-c** or **&#8209;&#8209;cat** option and is NOT needed when the **-p** or **&#8209;&#8209;ptt** option is used. |
| **-u** | **&#8209;&#8209;unkeystring** | &lt;hex&#8209;string&gt; | CAT command to switch radio to receive mode. E.g. for Kenwood, Elecraft, QDX, QMX, TX-500 the command is "RX;", the actual command line option will be _-u 52583B_.  This is used with the **-c** or **&#8209;&#8209;cat** option and is NOT needed when the **-p** or **&#8209;&#8209;ptt** option is used.  |

*Note: Dial frequency and other CAT control functions are not provided by ardopcf.  However, if a CAT port is specified then the RADIOHEX host command can be used (by a host program or with --hostcommands at startup) to pass an arbitrary string of bytes to the radio.  Since these strings are radio specific, they are not commonly used.*

## 4. Audio Options

| short option | long option | parameter | description |
|----|----|----|----|
| **-L** | | _none_ | use only left audio channel for RX. |
| **-R** | | _none_ | use only right audio channel for RX. |
| **-y** |  | _none_ | use only left audio channel for TX. |
| **-z** | | _none_ | use only left audio channel for TX. |
| **-w** | **&#8209;&#8209;writewav** |  _none_  | Write WAV files of received audio for debugging. |
| **-T** | **&#8209;&#8209;writetxwav** |  _none_  | Write sent WAV files of received audio for debugging. |
| **-d** | **&#8209;&#8209;decodewav** |  &lt;pathname&gt;  | Decode the supplied WAV file instead of the input audio.  This option can be repeated up to five times to provide up to five WAV files to be decoded as if they were received in the order provided with a brief period of silence between them. |
| **-s** | **&#8209;&#8209;sfdt** |  _none_  | Use alternative Sliding DFT based 4FSK decoder. |
| **-A** | **&#8209;&#8209;ignorealsaerror** |  _none_ | Ignore ALSA config error that causes timing error. <br> **DO NOT** use -A option except for testing/debugging, or if ardopcf fails to run and suggests trying this. |

## 5. Other options

| short option | long option | parameter | description |
|----|----|----|----|
| **-m** | **&#8209;&#8209;nologfile** | _none_ | Don't write log files. Use console output only. |
| **-S** | **&#8209;&#8209;syslog** | _none_ | Send console logs to syslog instead of stderr. Useful mainly for systemd services or daemons. Use the `CONSOLELOG` host command to control the verbosity of syslog messages. Combine with **&#8209;&#8209;nologfile** to use syslog/journald only. (Linux only) |
| **-l** | **&#8209;&#8209;logdir** | &lt;pathname&gt; | The absolute or relative path where log files and WAV files are written.  Without this option, these files are written to the start directory. |
| **-G** | **&#8209;&#8209;webgui** | &lt;TCP port&gt; | TCP port to access web GUI. By convention it is number of the host TCP port minus one, so usually 8514.  If a negative TCP port number is given, such as -8514, then the corresponding positive number is used, but the web GUI is opened in developer mode. Developer mode allows arbitrary host commands to be entered from within the web GUI. This is not intended for normal use, but is a useful tool for debugging purposes. |
