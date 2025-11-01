% ardopcf(SECTION) | User Commands
%
% "April 12 2025"

# NAME

ardopcf - C implementation of the amatuer radio digital communication protocol

# SYNOPSIS

**ardopcf** \<host-tcp-port> [ \<audio-capture-device> \<audio-playback-device> ]

Typical invocation: **ardopcf** 8515 plughw:1,0 plughw:1,0

**ardopcf** {**-h** | *\-\-help**}

# DESCRIPTION

This manual page documents briefly the **ardopcf** command.

This manual page was written for the Debian distribution because the
original program does not have a manual page. Instead, it has documentation
in the git repo, here: https://github.com/pflarue/ardop/tree/master/docs

The Amateur Radio Digital Open Protocol (Ardop), is a digital communication
protocol created by Rick Muething KN6KB. It provides ARQ and FEC protocols
for the exchange of digital data encoded as audio and transmitted over amateur radio.

**ardopcf** ardopcf is a fork of ardopc by Peter LaRue AI7YN (formerly KG4JJA).
It is usable on Windows and Linux computers. While ardopcf is useful and being used,
there is still plenty of room for improvement. Development is actively continuing.
The goals for this continuing work include improved stability and better over the
air performance, while maintaining compatability with the other Ardop
implementations and the ARDOP specification. Ongoing efforts to reorganize the
code base to make it easier to understand, debug, and maintain also help
support these goals.


# OPTIONS

The program follows the usual GNU command line syntax.
A summary of options is included below.


## Initialization and Help Options
**-H**, **--hostcommands** *\<string>*
: Host commands to be used at start of the program in a single string. Commands are separated by semicolon. This option provides capabilities previously provided by some obsolete command line options. See Host_Interface_Commands.md for descriptions of the available commands.

**-h**, **--help**
: Show help screen.

## CAT and PTT Options
**-c**, **--cat** *[TCP:]\<device>[:\<baudrate>]|RIGCTLD*
: Device or TCP port to send CAT commands to the radio, e.g. -c COM9:38400 for Windows or -c /dev/ttyUSB0:115200 for Linux. May be the same device as PTT. The TCP: prefix may be used to specify a TCP port number for CAT control (e.g. -c TCP:4532 for hamlib/rigctld on default port). Using --cat RIGCTLD is a shortcut for --cat TCP:4532 -k "T 1\n" -u "T 0\n" to use hamlib/rigctld for PTT.

**-p**, **--ptt** *[RTS:|DTR:|CM108:|GPIO:]\<device>*
: Device to activate radio PTT. Default is RTS signaling if no prefix given. DTR: uses DTR signaling. CM108: uses pin 3 of CM108 compatible HID device (e.g. digirig-lite, AIOC). On Windows use CM108:? to list devices, then --ptt CM108:VID:PID. On Linux CM108 devices appear as /dev/hidraw# (may need permission adjustment). GPIO: uses a GPIO pin on Raspberry Pi (pin number as integer, negative inverts signal). WARNING: GPIO may cause physical damage if used incorrectly. May be the same device as CAT.

**-k**, **--keystring** *[ASCII:]\<string>*
: CAT command to switch radio to transmit mode. Accepts hex (even number of hex chars) or ASCII text (with \\n and \\r substitution). Use ASCII: prefix to force ASCII interpretation. E.g. for Kenwood/Elecraft/QDX/QMX/TX-500: -k 54583B or -k "TX;" or -k "ASCII:TX;". Used with --cat and not needed with --ptt.

**-u**, **--unkeystring** *[ASCII:]\<string>*
: CAT command to switch radio to receive mode. Accepts hex (even number of hex chars) or ASCII text (with \\n and \\r substitution). Use ASCII: prefix to force ASCII interpretation. E.g. for Kenwood/Elecraft/QDX/QMX/TX-500: -u 52583B or -u "RX;" or -u "ASCII:RX;". Used with --cat and not needed with --ptt.

## Audio Options
**-i** *\<CaptureDevice>*
: Alternative to specifying audio capture device as positional parameter. Use NOSOUND or -1 for testing/debugging.

**-o** *\<PlaybackDevice>*
: Alternative to specifying audio playback device as positional parameter. Use NOSOUND or -1 for testing/debugging.

**-L**
: Use only left audio channel of stereo device for RX.

**-R**
: Use only right audio channel of stereo device for RX.

**-y**
: Use only left audio channel of stereo device for TX.

**-z**
: Use only right audio channel of stereo device for TX.

**-w**, **--writewav**
: Write WAV files of received audio for debugging. (RECRX host command and WebGui developer mode button can also control recording.)

**-T**, **--writetxwav**
: Write WAV files of transmitted audio for debugging.

**-d**, **--decodewav** *\<pathname>*
: Decode the supplied WAV file instead of the input audio. Can be repeated up to five times to provide up to five WAV files to be decoded sequentially with brief silence between them. Ardopcf exits after processing.

**-s**, **--sdft**
: Use alternative Sliding DFT based 4FSK decoder.

## Other Options
**-m**, **--nologfile**
: Don't write log files. Use console (or syslog) output only.

**-S**, **--syslog**
: Redirect console log messages to syslog. Useful for systemd services or daemons. Use CONSOLELOG host command to control verbosity. Combine with --nologfile to use only syslog/journald. (Linux only)

**-l**, **--logdir** *\<pathname>*
: The absolute or relative path where log files and WAV files are written. Without this option, files are written to the start directory.

**-G**, **--webgui** *\<TCP port>*
: TCP port to access WebGui. Default is 8514. Use 0 to disable WebGui. A negative port number (e.g. -8514) enables developer mode, which allows arbitrary host commands to be entered from the WebGui, shows additional log details, and provides a button to record RX audio to WAV file.

# NOTES
Dial frequency and other CAT control functions are not provided by ardopcf. However, if a CAT port is specified then the RADIOHEX host command can be used (by a host program or with --hostcommands at startup) to pass an arbitrary string of bytes to the radio. Since these strings are radio specific, they are not commonly used. Check your radio's manual for the correct byte string.

# SEE ALSO
https://github.com/pflarue/ardop/tree/master/docs

# BUGS
Report bugs to the issue tracker at https://github.com/pflarue/ardop/issues


# FILES

${HOME}/.asoundrc
:   Ardopcf will look for an ALSA source and sink named ARDOP and try
    to use those, if you did not specify an audio device at invocation.
    Note that DMIX/DSNOOP do not currently work with ardopcf.

# DIAGNOSTICS

The following diagnostics may be issued on stdout:

Error in InitSound().  Stopping ardop.
:   Usually this means you did not specify a proper audio device,
    or it is busy.

**ardopcf** does not provide error standard error codes that can be
used by scripts, but a log file will be created and written to, the
amount writen depending on your settings. See the section above
"Other Options."

# BUGS

Sometimes when connecting to a remote station that runs ARDOP_WIN
implementation, an endless IDLE/ACK loop will be entered.

DMIX/DSNOOP ALSA plugins do not work with ardopcf due to a workaround
needed for a kernel change around 5.15. 


# SEE ALSO

ardopcf compatible Winlink Radio Email
:   https://getpat.io/

gARIM messaging program
:   https://www.whitemesa.net/garim/garim.html

# AUTHORS

Rick Muething
:   Created the ARDOP protocol and a Windows reference implementation.

John Wiseman
:   Ported ARDOP_WIN to C as ardopc

Peter LaRue
:   Forked and maintains ardopcf (this package)

# COPYRIGHT

Copyright © 2024 Rick Muething, John Wiseman, Peter LaRue

This manual page was written for the Debian system (and may be used by
others).

Permission is granted to copy, distribute and/or modify this document under
the terms of the MIT License.

[comment]: #  Local Variables:
[comment]: #  mode: markdown
[comment]: #  End:
