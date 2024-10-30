# ARDOPCF Command Line Options

This section describes usage and command line options for version 1.0.4.1.2. 

_Please note that deprecated options have already been omitted in this document._

## 1. Usage

 ardopcf &lt;host-tcp-port&gt; [ &lt;audio-capture-device&gt; &lt;audio-playback-device&gt; ]

- **host-tcp-port** is telnet port for input of host commands and output of status messages
- **audio-capture-device** specifies which audio input device to use as input for ardopcf. Different formats are used in Linux and Windows, see below.
- **audio-playback-device** specifies audio output device. Different formats are used in Linux and Windows, see below.

### Audio device selection in Windows

_ardopcf_ will list all available capture and playback devices when started. Each device has a number. Use this number as the respective parameter in the command line.

Example: startup show the following list

    ardopcf Version 1.0.4.1.2 (https://www.github.com/pflarue/ardop)
    Copyright (c) 2014-2024 Rick Muething, John Wiseman, Peter LaRue
    See https://github.com/pflarue/ardop/blob/master/LICENSE for licence details including
    information about authors of external libraries used and their licenses.
    Capture Devices
    0 Microphone (4 - USB Audio CODEC )
    1 Microphone (2 - USB Audio Device)
    2 Stereo Mixer (Realtek High Definition)   Playback Devices
    0 Speaker (4 - USB Audio CODEC)
    1 Speaker (Realtek High Definition)
    2 Speaker (2 - USB Audio Device)
    3 PL2474H (Intel(R) Display Audio)

Device _4 - USB Audio CODEC_ is the built-in audio device in Kenwood TS-590. So to select the Kenwood transceiver, start the program with these parameters:

    ardopcf 8515 0 0 

_(In this example we omitted CAT options, see below in section 3)_

All command line options are listed in the following sections. If the option has parameter, it follows the option after a space.

## 2. Initialization and help

| short option | long option | parameter | description |
|----|----|----|----|
| **-H** | **--hostcommands** | &lt;string&gt; | Host commands to be used at start of the program in a single string. Commands are separated by semicolon ; |
| **-h** | **--help** | _none_ | Show help screen. |

## 3. CAT and PTT

| short option | long option | parameter | description |
|----|----|----|----|
| **-c** | **--cat** | &lt;serial-port&gt;[:&lt;baudrate&gt;] | serial port to send CAT commands to the radio, e.g. _-c COM9:38400_ |
| **-p** | **--ptt** | &lt;serial-port&gt; | serial device to activate radio PTT using RTS signal. May be the same device as CAT. |
| **-g** | | [&lt;pin-number&gt;] | ARM CPU GPIO pin used as PTT. Must be integer. If empty (no pin value), 17 will be used as default. Applies only to ARM devices . |
| **-k**| **--keystring** |  &lt;hex-string&gt; | CAT command to switch radio to transmit mode. E.g. for Kenwood, Elecraft, QDX, QMX, TX-500 the command is "TX;", the actual command line option will be _-k 54583B_ |
| **-u** | **--unkeystring** | &lt;hex-string&gt; | CAT command to switch radio to receive mode. E.g. for Kenwood, Elecraft, QDX, QMX, TX-500 the command is "RX;", the actual command line option will be _-k 52583B_ |

*Note: Dial frequency cannot be controlled by ardopcf as of now.*

## 4. Audio Options

| short option | long option | parameter | description |
|----|----|----|----|
| **-L** | | _none_ | use only left audio channel for TX and RX. |
| **-R** | | _none_ | use only right audio channel for TX and RX. |
| **-y** |  | _none_ | use only left audio channel for TX, do not change RX. |
| **-z** | | _none_ | use only left audio channel for TX, do not change RX. |
| **-w** | **--writewav** |  _none_  | Write WAV files of received audio for debugging. |
| **-T** | **--writetxwav** |  _none_  | Write sent WAV files of received audio for debugging. |
| **-d** | **--decodewav** |  &lt;pathname&gt;  | Decode the supplied WAV file instead of the input audio. |
| **-s** | **--sfdt** |  _none_  | Use alternative Sliding DFT based 4FSK decoder. |
| **-A** | **--ignorealsaerror** |  _none_ | Ignore ALSA config error that causes timing error. <br> **DO NOT** use -A option except for testing/debugging, or if ardopcf fails to run and suggests trying this. |

## 5. Web GUI

| short option | long option | parameter | description |
|----|----|----|----|
| **-G** | **--webgui** | &lt;TCP port&gt; | TCP port to access web GUI. By convention it is number of the host TCP port minus one.|
