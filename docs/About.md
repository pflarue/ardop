# About ARDOP (the protocol)

The ARDOP (Amateur Radio Digital Open Protocol) is a digital communication protocol specifically designed for use over amateur radio bands. The protocol itself is different from any implementation.

The ARDOP protocol specification includes information about the different modes of operation, how stations should automatically excahnge information in an ARQ session, general modem behavior, and a list of the different frame types. The ARDOP specification can be found here: [ARDOP Specification](https://ardop.groups.io/g/users/files/ARDOP%20Specification.pdf)


### Other ARDOP-related protocols
Although they have ARDOP in the name, they are similar but different (incompatible) protocols and modulation schemes:

| Name | Description |
|------|-------------|
|ARDOP_OFDM | Same frame type and underlying protocol as ARDOP, but incompatible modulation modes. |
|ARDOP2     | Incompatible with ARDOP at a protocol/frame type level. |
|ARDOP2_OFDM | Incompatible protocol/frame structure and modulation modes. |

# About ardopcf (the software)

ardopcf is a C implementation of the ARDOP protocol with many bug fixes, performance/behavior improvements, better documentation and a cleaner code base. It is released under the MIT open source license.

ardopcf is a fork by Peter LaRue KG4JJA of [ardopc](https://github.com/g8bpq/ardop) by John Wiseman G8BPQ, which is a multi-platform port of ARDOP_Win (Windows only) by Rick Muething KN6KB.  ARDOP_Win is distributed as a part of [Winlink Express](https://winlink.org/WinlinkExpress).  John's [ARDOPC web page](https://www.cantab.net/users/john.wiseman/Documents/ARDOPC.html) provides documentation about use of [ardopc](https://github.com/g8bpq/ardop) and links to the binaries he produces.

For a comprehensive overview of the changes relating to ardopc, see the [changelog](changelog.md).

Like most amatuer radio software, ardopcf is considered in perpetual Beta. This software generally works as intended, but should not be relied on for preservation of life or property.

### Using ardopcf

#### Downloading ardopcf

See the [releases](https://github.com/pflarue/ardop/releases) page to download 'more stable' ardopcf binaries for Linux and Windows.

#### Compiling ardopcf

If you wish to compile and test the latest development branch:
Linux:
```
git clone -b develop https://github.com/pflarue/ardop
cd ardop/ARDOPC
make -j4
```

Windows:
```
Stub.
```

MacOS:
```
Stub.
```

### Usage

ardopcf can be normally invoked like this, assuming you are using an external sound card device like a SignaLink USB or a DigiRig, or a radio's built-in sound card:

Linux: `./ardopcf 8515 plughw:1,0 plughw:1,0`
Windows: `stub`
MacOS: `stub`

Yes, you must specify the sound device twice. See the [Troubleshooting](Troubleshooting.md) guide if you have issues connecting with another station (if not a host application issue)

Then you will need software that uses ardopcf as a transport, which is called a Host Application. Here is a non-comprehensive list:

| Name | Description |
|------|-------------|
| [Pat](https://getpat.io/) | Cross-platform Winlink compatible radio email client. Pat provides both a modern GUI and a powerful command-line interface. See [ARDOP setup instructions](https://github.com/la5nta/pat/wiki/ARDOP). |
| [gARIM](https://www.whitemesa.net/garim/garim.html) | ARIM means "Amateur Radio Instant Messaging" and the gARIM program is a GUI host mode program for the ARDOP TNC developed by Rick KN6KB and John G8BPQ. See feature list on their website.|
| [Mercury-connector](https://github.com/Rhizomatica/mercury-connector) | Mercury-connector connects a modem/TNC for transmitting or receiving files, through an inotify-driven interface. Mercury-connector expects a file to be copied to a specified input directory, and automatically transmits it throught the TNC, while received files are written to a specified output directory. |
| [hamChat](https://github.com/Dinsmoor/hamChat) | Desktop chat and file transfer application that makes it easy for developers to add their own features via a plugin system. Pre-alpha and under heavy development, not currently suitable for regular use. Under development by Tyler K7OTR. |
| [Winlink Express](https://winlink.org/WinlinkExpress) | It is not currently known if a user can use ardopcf instead of ARDOP_win with Winlink express. Needs user testing with someone that has a Windows computer. |



### Contributing to ardopcf

It is the hope of the authors that this project can be an effective learning tool for amatuers that intend to get involved with software modem development. Contributing to this software is welcome, especially when it comes to finding and fixing bugs, improving user experience, and improving performance (without breaking compatibility with host applications or other ARDOP implementations). Please see [CONTIBUTING](CONTRIBUTING.md).

If you fork this project and make any changes are made that break compatibility with other ARDOP-compatible stations, your fork should not carry the ARDOP name, to avoid confusion for users.

### Other ARDOP Protocol Implementations

| Name | Link | Description |
|------|------|-------------|
| ardopc | [ardopc](https://github.com/g8bpq/ardop) | C implementation of the ARDOP protocol, but not maintained and stopped working on machines running Linux Kernel ~5.10+ |
| ardop_win | [ARDOP_Chat Setup](https://ardop.groups.io/g/users/files/ARDOP_Chat%20Setup%201.0.4.zip) | Original implementation of the ARDOP protocol, Microsoft Windows-only. Currently distributed with Winlink Express. |

### ardopcf performance

ardopcf should perform as-good-as or better-than both ardop_win and ardopc as far as decoder performance.

There are ongoing discussions about the best way to benchmark decoder performance, and some sample data already generated, however, it's important that going forward a standardized method of testing is developed so there isn't an 'apples-to-oranges' performance comparision down the road.

Current ideas are related to automatic realtime path simulation with software such as GNURadio (which will permit full end-to-end host control behavior profiling), or the recording and playback of prerecorded WAV files generated by the program, injecting increasing levels of noise in a batch operation, much like how the soundcard packet modem DIREWOLF does.

Input regarding performance testing can be added here: https://github.com/pflarue/ardop/issues/45


## Main Authors

Listed by order of history of contributions this project.

| Name | Callsign | Contribution Summary |
|------|----------|----------------------|
| Peter LaRue | AI7YN (formerly KG4JJA) | Current active ardopcf developer, a fork of ardopc.|
| John Wiseman | G8BPQ | Created ardopc, a fork of ardop_win. |
| Rick Muething | KN6KB | Created the ARDOP protocol and the ardop_win reference implementation.|