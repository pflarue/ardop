# TCP Host Interface Commands

**Disclaimer**: The content of this documentation is DESCRIPTIVE of the current TCP host interface, not PRESCRIPTIVE - meaning that we went through and tried to understand what the commands do based on review of the source code and older (possibly out of date) documentation. This means that the behavior of some of these commands under certain situations may not be as expected, and some may cause a crash. Some commands are not fully understood, and some responses are not fully documented, but this documentation will be updated as they are experimented with or after further code review. Feel free to clone the respository and submit a PR or to open up an issue with what should be changed and why.

## Use with the `--hostcommands` command line option

The `-H` or `--hostcommands` option can be used to automatically apply one or more semicolon separated host commands at startup.  The remainder of this document describes all of the available host commands.  If the first commands are `LOGLEVEL` and/or `CONSOLELOG`, then these are applied before most other command line options are evaluated.  Processing them before the logging system is initiated ensures that the desired log settings are in place before any log messages are processed.

The commands are applied in the order that they are written.  Except for those leading `LOGLEVEL` and `CONOSOLELOG` commands, the order usually doesn't matter.  As an example, `--hostcommands "LOGLEVEL 1;CONSOLELOG 2;MYCALL AI7YN"` sets the log file to be as detailed as possible, and data printed to the console to be only slightly less detailed, and sets my callsign to `AI7YN`.  Because most commands will include a command, a space, and a value, you usually need to put quotation marks around the commands string.   As another example, `--hostcommands "CONSOLELOG 4;MYCALL AI7YN;DRIVELEVEL 90"` leaves the log file settings at their default value, but prints only more important messages to the console, sets my callsign, and set the transmit drive level to 90%.

Some command line options that were available before ardopcf v1.0.4.1.3 now require use of `--hostcommands` to produce the same result.  The following list includes host comands that can be used to replace obsolete command line arguments plus a few others that may be useful.  See the detailed descriptions of these commands later in this document for more information.

* CONSOLELOG: Controls amount of data printed to the screen. 1-6. Lower values print more info. (replaces `-V` or `--verboseconsole`, but values are different)
* DRIVELEVEL: Linearly scales the amplitude of the transmitted audio. 0-100.  Not setting this value is equivalent to setting it to 100.
* EXTRADELAY: Increased delay in ms between RX and TX to allow for long delay paths (replaces `-e` or `--extradelay`)
* LEADER: Sets leader length in ms. (replaces `-x` or `--leaderlength`)
* LOGLEVEL: Controls amount of data written to debug log. 1-6. Lower values write more info to log. (replaces `-v` or `--verboselog`, but values are different)
* MYCALL: Set callsign
* PROTOCOLMODE: Sets the protocol mode.  ("PROTOCOLMODE RXO" replaces `-r` or `--receiveonly`)
* TRAILER: Sets trailer length in ms. (replaces `-t` or `--trailerlength`)
* TWOTONETEST: Transmit two-tone test signal for 5 seconds. ("TWOTONETEST;CLOSE" replaces `-n` or `--twotone`)


## Use by host applications

When writing host applications, commands are issued to the modem's TCP socket (default 8515). By default, ardopcf listens on 0.0.0.0 (all interfaces, meaning unless you set up a firewall, any computer on your network can access ardopcf's tcp interface)

Commands sent to the ardopcf command socket can be upper or lowercase, but must be terminated with a `/r` (carriage return) `0x0d` ascii byte.

For example, sending `BUFFER/r` to the command socket will return `BUFFER N/r` where N is the decimal length of any data currently loaded into ardopcf's transmit buffer via its data socket.


### Usage Example

[diagnostichost.py](../host/python/diagnostichost.py) provides an example of how the ardopcf host interface can be used from Python with both Windows and Linux.  This was developed as a diagnostic tool for use by ardopcf developers, not as a tool for actaully communicating data using ardopcf.  Documentation for [diagnostichost.py](../host/python/diagnostichost.py) is provided in [README.md](../host/python/README.md) as well as in comments within the python file.

## ARDOP Modes

Not all commands are for all modes. There are three main operating modes of ardopcf, and each have their own subset of commands.

**ARQ** (Automatic Repeat reQuest)

This is the primary mode used for software such as PAT and Winlink Express. It will intiate a connection request, negotiate a bandwidth and data rate, and try to adapt to the band conditions. It will continue to try to send a payload until either station aborts, disconnects or a timeout occurs. Each station will have oppurtunity to send their data between each other.

A typical ARQ mode set of initialization commands may look like this:
>`INITIALIZE`
>`PROTOCOLMODE ARQ`
>`ARQTIMEOUT 30`
>`ARQBW 1000MAX`
>`MYCALL K7CALL`
>`GRIDSQUARE CN87`


**FEC** (Forrward Error Correction)

This mode is used for software such as hamChat, ARIM, and gARIM. It is used to send connectionless frames containing any payload, without guarantee of delivery.

A typical FEC mode set of initialization commands may look like this:
>`INITIALIZE`
>`PROTOCOLMODE FEC`
>`MYCALL K7OTR`
>`GRIDSQUARE CN88`
>`BUSYDET 0`
>`FECREPEATS 0`
>`FECMODE 4FSK.500.100S`

**RXO** (Receive Only - Decode all frames with extra debug output)

This mode is mainly used for logging/debugging purposes. It will attemtp to decode every heard frame and write a description of that frame to the debug log. (O as in Oscar, not 0 as in zero)

You may set this protocol mode via:
>`INITIALIZE`
>`PROTOCOLMODE RXO`


## ARDOPCF Command List

These are all commands that the host application can send to ardopcf, and what to expect in response. Other commands from ardopcf can be received at any time, and it is up to the host application to continuously read the command socket and deal with any other responses ardop may send. See "ARDOPCF Command Socket Messages" section below for anything ardopcf may send that is not directly queried.

##### ABORT (or DD)

This is a "dirty disconnect" from a current ARQ session. It will clear the current buffer, set ardopcf state to `DISC`, and reset the internal state. The current frame will complete transmission.

- Mode: ARQ
- Arguments: None
- Example: `ABORT` returns `ABORT`


#### ARQBW
Sets the desired bandwith for an ARQ connection. If there is a mismatch in desired bandwith (both sides force different bandwidths) then the `CONREJBW` frame will be sent by the called station.

This also sets the bandwidth considered by the Busy Detector.

This also sets the bandwidth used by ARQCALL if CALLBW is UNDEFINED.

Acceptible bandwidths are 200, 500, 1000, and 2000.
Acceptible bandwith enforcement modes are MAX and FORCE.

If no arguments are specified, it will return the current ARQBW value.

- Mode: ARQ
- Arguments: Optional desired bandwith postfixed with MAX/FORCE.
- `ARQBW 500MAX` returns `ARQBW now 500MAX`
- `ARQBW` returns `ARQBW 500MAX`



#### ARQCALL

Initiates an ARQ session by transmitting `CONREQ` frames with the bandwidth mode specified by `CALLBW`.  If `CALLBW` is `UNDEFINED`, then the bandwidth mode specified by `ARQBW` is used. Once the session is connected, any data loaded into the data buffer of either end will be transmitted in turn until the session is ended via disconnect, or an idle timeout occurs.


Requires: `MYCALL` to be set and `PROTOCOLMODE` == `ARQ`

- Mode: ARQ
- Arguments: Target Callsign ; Number of `CONREQ` frames to send, 2-15.
- `ARQCALL callsign 2` returns `ARQCALL callsign 2`

If a connection attempt fails: `STATUS CONNECT TO {callsign} FAILED!`
or `STATUS: END ARQ CALL`


#### ARQTIMEOUT
This is the length of time, in seconds, for how long an ARQ connection can remain IDLE (no data sent) before the session is automatically disconnected.

Valid arguments is an integer between 30 and 240

- Mode: ARQ
- Arguments: An interger of seconds
- `ARQTIMEOUT` returns `ARQTIMEOUT 120`
- `ARQTIMEOUT 30` returns `ARQTIMEOUT now 30`


#### AUTOBREAK

Setting this to `TRUE` (default) will allow ardopcf ARQ session to automatically handle the ARQ flow control. Disabling this requires that the host application on each side implement application level flow control with the `BREAK` command. Not recommended to set to `FALSE`

- Mode: ARQ
- Arguments: `TRUE` or `FALSE`
- `AUTOBREAK` returns `AUTOBREAK TRUE`
- `AUTOBREAK FALSE` returns `AUTOBREAK now FALSE`

#### BREAK

Normally in ARQ mode, ardopcf will automatically `BREAK` to make sure both stations can send any data they have in their buffers.

If this command is sent, it means that this station wants to send data right now, and not wait for normal flow control. It will repeat this command and not process any received ARQ session frames until the `BREAK` is acknowledged.

- Mode: ARQ
- Arguments: None
- `BREAK` returns `BREAK`


#### BUFFER

Queries ardopcf for how many bytes of data are currently in the outgoing data buffer. ARDOPCF automatically sends a buffer report whenever a host application loads data into the buffer.

- Mode: ANY
- Arguments: None
- `BUFFER` returns `BUFFER N` - where N is an integer length of data to be sent.


#### BUSYBLOCK

Enabling this will reject any incoming ARQ connection requests, and respond with a `CONREJBUSY` frame if there is not enough time between the channel being 'busy' and the incoming ARQ connection request.

- Mode: ARQ
- Arguments: `TRUE` or `FALSE`
- `BUSYBLOCK` returns `BUSYBLOCK TRUE`
- `BUSYBLOCK FALSE` returns `BUSYBLOCK now FALSE`


#### BUSYDET

Selects the sensitivity of, or disables, busy channel detection.

- Mode: ANY
- Arguments: Integer 0-9, with 0 being disabled, 1 being most sensitive, and 9 being least sensitive.
- `BUSYDET` returns `BUSYDET 5`
- `BUSYDET 6` returns `BUSYDET now 6`

#### CALLBW

Set a bandwidth value to be used for ARQCALL which, unless set to UNDEFINED, will override the value set with ARQBW.

- Mode: ARQ
- Arguments: Optional UNDEFINED or desired bandwith postfixed with MAX/FORCE.
- `CALLBW 500MAX` returns `CALLBW now 500MAX`
- `CALLBW` returns `CALLBW 500MAX`

#### CAPTURE

Returns the currently selected capture device. This is supposed to let the user change the audio source, but currently it does not seem to work as intended. Changing this does not change where audio is routed for decoding.

- Mode: ANY
- Arguments: None, String of the capture device.
- `CAPTURE` returns `CAPTURE plughw:2,0`
- `CAPTURE plughw:1,0` returns `CAPTURE now plughw:1,0`


#### CAPTUREDEVICES

Returns the currently selected capture device. This is supposed to let the user change the audio source, but currently it does not seem to work as intended. Changing this does not change where audio is routed for decoding.

- Mode: ANY
- Arguments: None, String of the capture device.
- `CAPTUREDEVICES` returns `CAPTUREDEVICES plughw:2,0`
- `CAPTUREDEVICES plughw:1,0` returns `CAPTUREDEVICES now plughw:1,0`


#### CL

Same as `PURGEBUFFER` but as a SCS/Pactor modem emulation adapter. To be removed.

#### CLOSE

Kills ardopcf properly.  A host program will not normally close ardopcf.  However, this command is useful in some testing scenarios.

#### CMDTRACE

For debugging, determines if command sent from the host are recorded in the logfile or not.

- Mode: ANY
- Arguments: None, `TRUE` or `FALSE`
- `CMDTRACE` returns `CMDTRACE TRUE`
- `CMDTRACE FALSE` returns `CMDTRACE now FALSE`

#### CONSOLELOG

Changes the console log level, the information printed to the terminal where ardopcf was invoked. The lower the loglevel, the more is logged to the console.  The parameters used for CONSOLELOG were changed in ardopcf v1.0.4.1.3 with the introduction of a new logging system.

- Mode: ANY
- Arguments: None, loglevel integer 1-6
- `CONSOLELOG` returns `CONSOLELOG 6`
- `CONSOLELOG 1` returns `CONSOLELOG 1`

#### CWID

Send a morse code identifier of `MYCALL` after the `IDFRAME` frame is sent, if enabled.
Two 'truthy' settings:
- `TRUE` - Uses a high and a low tone for mark and space.
- `ONOFF` -Traditional On-Off keying morse. Maybe not compatible with some VOX settings.

- Mode: ANY
- Arguments: None, `TRUE` or `FALSE` or `ONOFF`
- `CWID` returns `CWID FALSE`

#### DATATOSEND

Same as the `BUFFER` command, except passing a `0` arguement will clear the current buffer.

- Mode: ANY
- Arguments: None, or `0`
- `DATATOSEND` returns `DATATOSEND 0`

#### DEBUGLOG

Enable or disable writing the debug log to disk.

- Mode: ANY
- Arguments: None, `TRUE` or `FALSE`
- `DEBUGLOG` returns `DEBUGLOG TRUE`
- `DEBUGLOG FALSE` returns `DEBUGLOG now FALSE`

#### DISCONNECT

If in a ARQ session, commands ardopcf to gracefully end the session and places the `STATE` into `DISC`, otherwise ignored.

- Mode: ANY
- Arguments: None
- `DISCONNECT` when not in a session returns `DISCONNECT IGNORED`

#### DRIVELEVEL

Linearly scales the amplitude of the transmitted audio.  When transmitting with a SSB radio, this can be used to adjust the RF output power.  Lower values may also be used in conjunction with or as an alternative to sound card and radio settings to avoid overdriving the audio to the radio.  Radios typically include an Automatic Level Control (ALC) feature to reduce the overdriven input audio before using it to modulate the RF signal.  This ALC feature usually distorts digital mode signals, making them more difficult to decode.  So, using DRIVELEVEL settings that prevent excessive ALC action can result in better performance.

- Mode: ANY
- Arguments: None, Integer 1 through 100
- `DRIVELEVEL` returns `DRIVELEVEL 100`
- `DRIVELEVEL 50` returns `DRIVELEVEL now 50`

#### ENABLEPINGACK

If enabled, ardopcf returns a `DATAACK` frame to acknowledge the a `PING`

- Mode: ANY
- Arguments: None, `TRUE` or `FALSE`
- `ENABLEPINGACK` returns `ENABLEPINGACK TRUE`
- `ENABLEPINGACK FALSE` returns `ENABLEPINGACK now FALSE`

#### EXTRADELAY

Adds extra time in between reception and transmission for high-latency signal paths.

- Mode: ANY
- Arguments: Time in milliseconds, 0-100000+
- `EXTRADELAY` returns `EXTRADELAY 0`
- `EXTRADELAY 10` returns `EXTRADELAY now 10`

#### FASTSTART

Controls the first data frame type used in an ARQ session.  If TRUE, then a frame type of moderate speed and robustness relative to the negotiated session bandwidth will be tried first.  If FALSE, then the slowest and most robust frame type relative to the negotiated session bandwidth will be tried first.  Depending on the response received from the other station to this first data frame, ardopcf will adjust to try to use the frame type which provides the best performance for the observed band conditions.

- Mode: ANY
- Arguments: None, `TRUE` or `FALSE`
- `FASTSTART` returns `FASTSTART TRUE`
- `FASTSTART TRUE` returns `FASTSTART now TRUE`

#### FECID

Automatically transmits an ID frame with every FEC transmission.

- Mode: FEC
- Arguments: None, `TRUE` or `FALSE`
- `FECID` returns `FECID FALSE`
- `FECID TRUE` returns `FECID now TRUE`

#### FECMODE

Sets the frame type for transmission of the next data buffer in FEC mode. See the frame documentation for more information on specific data frame types.

- Mode: FEC
- Arguments: None, or one of the following:

>`4FSK.200.50S`
>`4PSK.200.100S`
>`4PSK.200.100`
>`8PSK.200.100`
>`16QAM.200.100`
>`4FSK.500.100S`
>`4FSK.500.100`
>`4PSK.500.100`
>`8PSK.500.100`
>`16QAM.500.100`
>`4PSK.1000.100`
>`8PSK.1000.100`
>`16QAM.1000.100`
>`4PSK.2000.100`
>`8PSK.2000.100`
>`16QAM.2000.100`
>`4FSK.2000.600`
>`4FSK.2000.600S`
- `FECMODE` returns `FECMODE 4PSK.200.100`
- `FECMODE 8PSK.1000.100` returns `FECMODE now 8PSK.1000.100`

#### FECREPEATS

In addition to the Reed Solomon based error correction capability used in the encoding of all Ardop data frames, setting FECREPEATS to a value greater than 0 to send repeated copies of each sent FEC frame further increases the likelihood that a receiving station will be able to correctly decode the data frames transmitted.  Unlike ARQ mode, FEC mode lacks automatic feedback from the receiving station with which to determine whether or not repeated copies of the data frame must be sent.

The Memory ARQ feature allows data retained from frames that could not be correctly decoded to be combined with data from additional copies of that same data frame to further increase the likelihood of accurate decoding with each repetition.  This feature allows FECREPEATS to significantly increase the success rate for decoding of data frame types that are only marginally usable under the existing conditions.

It is often better to choose a slower but more robust FECMODE rather than increase FECREPEATS. However, increasing FECREPEATS is appropriate when single transmissions of very robust FECMODE frame types are unreliable.  Under deep fading conditions it may also be more effective to increase FECREPEATS rather than select a more robust FECMODE.

When more than one copy of a data frame is received, the duplicates are discarded.

- Mode: FEC
- Arguments: None, or an integer 0 through 5
- `FECREPEATS` returns `FECREPEATS 0`
- `FECREPEATS 5` returns `FECREPEATS now 5`

#### FECSEND

If there is data in the `BUFFER`, ardopcf will await for a clear channel and then transmit the selected `FECMODE` frame `FECREPEATS` amount of times. If there is no data in the buffer, it will wait for data to be loaded. You cannot query this to see if `FECSEND` is true or not. If sending many FEC frames, setting this to `FALSE` will stop transmission.

- Mode: FEC
- Arguments: `TRUE` or `FALSE`
- `FECSEND TRUE` returns `FECSEND now TRUE`

#### FSKONLY

Disables PSK and QAM modes for ARQ sessions?

- Mode: ANY
- Arguments: None, `TRUE` or `FALSE`
- `FSKONLY` returns `FSKONLY FALSE`
- `FSKONLY TRUE` returns `FSKONLY now TRUE`

#### GRIDSQUARE

Sets gridsquare for inclusion in the `IDFRAME`

- Mode: ANY
- Arguments: None, or 4,6,8 character grid square
- `GRIDSQUARE` returns `GRIDSQUARE CN88`
- `GRIDSQUARE CN87` returns `GRIDSQUARE now CN87`

#### INITIALIZE

Performs initialization of the modem. The first command that needs to be issued to the modem.

- Mode: ANY
- Arguments: None
- Returns: None

#### INPUTNOISE

This command is NOT intended to be used or useful during normal operation of ardopcf.  Rather, it is useful for diagnostic purposes.

This command adds Gausian white noise to all incoming audio, including audio from WAV files being decoded.  If the audio plus this noise exceeds the capacity of signed 16-bit integers used for raw audio samples, clipping will occur.  This added noise, with a standard deviation specified by the integer argument to this command, decreases the likelihood that the received frames will be successfully decoded.  As a diagnostic tool, this is useful to study how the demodulators and decoders behave under varying noise levels.

- Mode: ANY
- Arguments: None, integer >= 0
- `INPUTNOISE` returns `INPUTNOISE 10000`
- `INPUTNOISE 5000` returns `INPUTNOISE now 5000`

#### LEADER

Synchronization leader length in milliseconds, the longer this is the easier it is for the listener to decode the frame, but the more overhead there is. In ARQ mode this is automatically adjusted.

- Mode: ANY
- Arguments: None, integer 120-2500
- `LEADER` returns `LEADER 120`
- `LEADER 140` returns `LEADER now 140`

#### LISTEN

When enabled, allows processing of any decodeded ARQ or PING frames addressed to `MYCALL` or `MYAUX`.

- Mode: ANY
- Arguments: None, `TRUE` or `FALSE`
- `LISTEN` returns `LISTEN TRUE`
- `LISTEN FALSE` returns `LISTEN now FALSE`

#### LOGLEVEL

Sets the loglevel for the debug log that is generated when the program is run. Loglevel 6 is for serious errors only, and loglevel 1 is the most verbose. Loglevel 1 can generate very large files. The parameters used for LOGLEVEL were changed in ardopcf v1.0.4.1.3 with the introduction of a new logging system.

- Mode: ANY
- Arguments: None, integer between 1 and 6
- `LOGLEVEL` returns `LOGLEVEL 6`
- `LOGLEVEL 1` returns `LOGLEVEL now 1`

#### MONITOR

Source code comment says "allows ARQ mode to operate like FEC when not connected"
SoundInput.c:1425

Enables/disables monitoring of FEC or ARQ Data Frames, ID frames, or Connect request in disconnected ARQ state.

Does this make that data avaliable to a host application? I think funcationality confusion around this is why RXO was implemented.

- Mode: ANY
- Arguments: None, `TRUE`or `FALSE`
- `MONITOR` returns `MONITOR TRUE`
- `MONITOR FALSE` returns `MONITOR now FALSE`

#### MYAUX

Sets auxillary callsigns that the modem will respond to in addition to `MYCALL`.  Since these will be used to send legally required IDFrames, these must still be legally valid callsigns issued by the appropriate govenment agency.  So, tactical callsigns should not be used with `MYAUX`.  Rather, `MYAUX` should be used for a station that may operate under more than one callsign, such as when both a personal and a club callsign might be used by a station, or when multiple licensed operators are sharing the use of a station.

- Mode: ANY
- Arguments: None or CALLSIGN
- `MYAUX` returns `MYAUX K7CALL`
- `MYAUX K7CALL` returns `MYAUX now K7CALL`


#### MYCALL

Sets operator callsign, used in `IDFRAME` and `CONREQ` frames, and for engaging in ARQ mode.

- Mode: ANY
- Arguments: None or CALLSIGN
- `MYCALL` returns `MYCALL K7CALL`
- `MYCALL K7CALL` returns `MYCALL now K7CALL`

#### PING

Encodes a 4FSK 200 Hz BW Ping frame ( ~ 1950 ms with default leader/trailer) with double FEC leader length for extra robust decoding.

If the pinged station responds with a `PINGACK` frame, this station will stop pinging.

- Mode: ANY
- Arguments: Target Callsign ; Count (1-15)
- `PING K7CALL 1` returns `PING K7CALL 1`

#### PLAYBACK

Returns the currently selected playback device. This is supposed to let the user change the audio destination, but currently it does not seem to work as intended. Changing this does not change where audio is routed for transmission.

- Mode: ANY
- Arguments: None, String of the playback device.
- `PLAYBACK` returns `PLAYBACK plughw:2,0`
- `PLAYBACK plughw:1,0` returns `PLAYBACK now plughw:1,0`

#### PLAYBACKDEVICES

Returns the currently used playback device. This may be supposed to return a list of devices.

- Mode: ANY
- Arguments: None
- `PLAYBACKDEVICES` returns `PLAYBACKDEVICES plughw:2,0`

#### PROTOCOLMODE

Part of initialization, sets the ardop operating mode.

- Mode: ANY
- Arguments: None, FEC, ARQ, RXO
- `PROTOCOLMODE` returns `PROTOCOLMODE ARQ`
- `PROTOCOLMODE FEC` returns `PROTOCOLMODE now FEC`

#### PURGEBUFFER

Empties all data from the outgoing data buffer. Will stop any data transmissions after the current frame is finished sending.

- Mode: ANY
- Arguments: None
- `PURGEBUFFER` returns `BUFFER 0`

#### RADIOFREQ

Presumably a CAT string that would get or set the radio's current frequency. Possibly an embedded device specific command.

Currently only used for setting GUI Freq field

- Mode: ANY
- Arguments: None
- Returns: None

#### RADIOHEX

Immediately send a radio specific CAT command, defined by a hex string, to the
connected radio.

- Mode: ANY
- Arguments: Hex string
- `RADIOHEX` Returns `FAULT RADIOHEX command string missing`
- `RADIOHEX XXX` Returns `FAULT RADIOHEX XXX failed` if
a CAT port was not set with the -c or --cat command line option, if XXX is not
a valid hex string, or if XXX is too long to be sent as a CAT command.
- `RADIOHEX XXX` Returns `RADIOHEX XXX` and sends the command to the radio if
XXX is a valid hex string.

#### RADIOPTTOFF

Gets or Sets the radio specific hex string that can be sent to the radio as a
CAT command to switch from transmit to receive (PTT Off).  With a hex string
argument, this is equivalent to the -u or --unkeystring command line option.

If a CAT PTT Off string was previously set and is being used for PTT control,
then a new valid string will immediately replace it and be used for future
transitions from transmit to receive.  If no CAT device is set or if no
corresponding CAT PTT On command has been set, then a new valid string will
not be used immediately.  In that case, it will not be used until a CAT device
and a CAT PTT On command are also set.

- Mode: ANY
- Arguments: None or Hex string
- `RADIOPTTOFF` Returns `RADIOPTTOFF VALUE NOT SET` if no PTT Off CAT string is
already defined.
- `RADIOPTTOFF` Returns `RADIOPTTOFF XXX` where XXX is the hex string of the
current CAT PTT Off command.
- `RADIOPTTOFF XXX` Returns `FAULT RADIOPTTOFF command string is invalid.` if
XXX is not a valid hex string.  When this occurs, the previously defined CAT PTT
Off command, if one was set, remains unchanged.
- `RADIOPTTOFF XXX` Returns `RADIOPTTOFF now XXX`, where XXX is a valid hex
string.  As described above, this does not necessarily indicate that Ardop will
begin using CAT PTT control if it was not already doing so.
- `RADIOPTTOFF NONE` Returns `RADIOPTTOFF now NONE`.  This discards the previous
CAT PTT Off command.  If CAT PTT control was being used, it is disabled.

#### RADIOPTTON

Gets or Sets the radio specific hex string that can be sent to the radio as a
CAT command to switch from receive to transmit (PTT On).  With a hex string
argument, this is equivalent to the -k or --keystring command line option.

If a CAT PTT On string was previously set and is being used for PTT control,
then a new valid string will immediately replace it and be used for future
transitions from receive to transmit.  If no CAT device is set or If no
 corresponding CAT PTT Off command has been set, then a new valid string will
not be used immediately.  In that case, it will not be used until a CAT device
and a CAT PTT Off command is also set.

- Mode: ANY
- Arguments: None or Hex string
- `RADIOPTTON` Returns `RADIOPTTON VALUE NOT SET` if no PTT On CAT string is
already defined.
- `RADIOPTTON` Returns `RADIOPTTON XXX` where XXX is the hex string of the
current CAT PTT On command.
- `RADIOPTTON XXX` Returns `FAULT RADIOPTTON command string is invalid.` if
XXX is not a valid hex string.  When this occurs, the previously defined CAT PTT
On command, if one was set, remains unchanged.
- `RADIOPTTON XXX` Returns `RADIOPTTON now XXX`, where XXX is a valid hex
string.  As described above, this does not necessarily indicate that Ardop will
begin using CAT PTT control if it was not already doing so.
- `RADIOPTTON NONE` Returns `RADIOPTTON now NONE`.  This discards the previous
CAT PTT On command.  If CAT PTT control was being used, it is disabled.

#### RECRX

Start (with `RECRX TRUE`) or Stop (with `RECRX FALSE`) recording of received
audio to a WAV file.  Like those created with the -w or --writewav command line
options, WAV files are named according to the UTC date and time they are
started, and are located in the same directory as the debug log file.  The WAV
files are single channel, 16-bit, 12 kHz sample rate, and thus are suitable to
be read and decoded by ardopcf with the -d or --decodewav command line option.

`RECRX TRUE` while RECRX is already TRUE or `RECRX FALSE` while RECRX is already
FALSE is permitted and does nothing.

This command is not intended to be used or useful during normal operation of
ardopcf.  Rather, it is useful for diagnostic purposes.

- Mode: ANY
- Arguments: None or `TRUE` or `FALSE`
- `REXRX` returns `RECRX TRUE` or `RECRX FALSE`
- `RECRX TRUE` returns `RECRX now TRUE`
- `RECRX FALSE` returns `RECRX now FALSE`
- `RECRX Something else` returns `FAULT Syntax Err: RECRX Something else`
- BUT: If Ardop is currently recording received audio due to the use of the -w
or --writewav command line options, this command fails and sends
`FAULT RECRX IGNORED while recording due to -w or --writewav.` to the host.
Similarly, while RECRX is TRUE, the -w and --writewav command line option is
temporarily disabled.  Recording due to -w and --writewav may start at the end
of the next transmission after RECRX is set to FALSE.


#### RXLEVEL

Embedded platform control of ADC input volume.

- Mode: ANY
- Arguments: UNK
- Returns: UNK

#### SENDID

Transmits one `IDFRAME` with the callsign set with `MYCALL`

- Mode: ANY
- Arguments: None
- Returns: None

#### SQUELCH

Sets the sensitivity of the frame leader detector. 1 is basically open squelch, and 10 requires a strong audio baseband to trigger. An open squelch will often lead to false frames beign decoded and possibly passing ERR data frames to the host.

- Mode: ANY
- Arguments: Integer value between 1 and 10
- `SQUELCH` returns `SQUELCH 5`
- `SQUELCH 6` returns `SQUELCH now 6`

#### STATE

Returns the current state of ardopcf.

Valid states are:

> `OFFLINE` - uninitialized and not listening to audio
> `DISC` - initialized and listening to audio, but not sending/receiving data
> `ISS` - sending data in an ARQ session
> `IRS` - receiving data in an ARQ session
> `IDLE` - idle in an ARQ session
> `IRStoISS` - changing terminal mode from receiving to sending
> `FECSEND` - sending data in a FEC frame
> `FECRCV` - receiving data from a FEC frame


- Mode: ANY
- Arguments: None
- `STATE` returns `STATE DISC`

#### TRAILER

Adds a single tone of the specified length (in milliseconds) at the ends of all transmitted frames.  This may be required to keep the PTT on (including while using VOX) with certain SDR based radios to accomodate processing delay.  For such radios, without this setting they may release the PTT before all of the the useful part of the frame has been transmitted.

This is not required for most radios.

- Mode: ANY
- Arguments: Integer value between 0 and 200
- `TRAILER` returns `TRAILER 20`
- `TRAILER 40` returns `TRAILER now 40`

#### TUNINGRANGE

How many hertz from center frequency (1500Hz) an incoming signal can be decoded.

- Mode: ANY
- Arguments: Integer value between 0 and 200
- `TUNINGRANGE` returns `TUNINGRANGE 100`
- `TUNINGRANGE 110` returns `TUNINGRANGE now 110`

#### TWOTONETEST

Transmits a pair of tones at the normal leader amplitude for five seconds. May be used in adjusting drive level to the radio.

- Mode: ANY
- Arguments: None
- Returns: None

#### TXLEVEL

Embedded platform control of ADC output volume.

- Mode: ANY
- Arguments: None
- Returns: None

#### USE600MODES

Enables 600 baud rate frame types 4FSK.2000.600S and 4FSK.2000.600. For use on FM/2m.

- Mode: ANY
- Arguments: `TRUE` or `FALSE`
- `USE600MODES` returns `USE600MODES FALSE`
- `USE600MODES TRUE` returns `USE600MODES now TRUE`

#### VERSION

Returns the version of ardopcf.

- Mode: ANY
- Arguments: None
- `VERSION` returns `VERSION ardopcf_1.0.4.1.2`

## ARDOPCF Command Socket Messages

These are messages that ardopcf can send to the host at any time.

There are more than are listed here! If you find a different message, please let us know.

#### BUFFER

The `BUFFER` message is received whenever data has been added to the transmit data buffer.

Example: In FEC mode after sending some data to the data port, a host program should wait until after it receives a BUFFER response before sending `FECSEND` to the command port.  Otherwise, the `FECSEND` might be processed before the data is added to the transmit buffer, in which case the data won't be sent.

The `mode` is either `FEC` or `ARQ`.


#### BUSY TRUE

If in `DISC` state, tells the host application there is a busy channel.

#### BUSY FALSE

If in `DISC` state, tells the host application there is a clear channel.


#### CANCELPENDING

This is used primarially when your station is scanning multiple frequencies for connection requests, it begins decoding a heard frame, but finds that it is not addressed to `MYCALL` or `MYAUX`

`CANCELPENDING` occurs in the following situations:
- A `PING` frame header has been decoded but ardopcf:
  - is not in `DISC` state
  - `LISTEN` is set to `FALSE`
  - `PING` is not addressed to `MYCALL` or `MYAUX`
  - `ENABLEPINGACK` is set to `FALSE`
- An `ARQ` frame header is not intended for this station
- A `CONREQ` frame header is recognized but decoded improperly
- A `PING` frame header is recognized but decoded improperly
- A `PING` frame header is received but not decoded when ardopcf is not in `DISC` state or `RXO` mode. (duplicate?)


#### CONNECTED

When connected to a remote station, this message includes the remote callsign and the session bandwith.
Such as: `CONNECTED K7CALL 500`


#### DISCONNECTED

A `DISCONNECTED` host message will be sent on any of the following conditions

- `ARQTIMEOUT` is reached
- Tried sending `DISC` frame 5 times and never hear a response
- If Information Sending Station sends a `DISC` frame
- If Information Sending Station sends an `END` frame
- If Information Receiving Station sends a `DISC` frame
- If Information Receiving Station sends an `END` frame

#### FAULT

This will usually occur when a command syntax error is made.
`FAULT <fault info>`

#### NEWSTATE

Will tell the host application if there is a protocol state change.
See `STATE` above for definitions.


#### PING caller>target SNdB Quality

`PING {caller}>{target} {SNdB} {Quality}` is sent to the host when a `PING` frame is decoded.

`caller` is the station that sent the ping.
`target` is the station the caller tried to ping.
`SNdB` is the signal-to-noise ratio of the received
`Quality` is the frame decode quality rating, usualy 70-100

If ardopcf has `ENABLEPINGBACK` set to `TRUE` and the target is this station, it will respond with `PINGACK`

#### PENDING

`PENDING`

Indicates to the host application a Connect Request or PING frame type has been detected. Used if a radio is scanning multiple frequencies and needs to pause to see if the frame is addressed to `MYCALL` or `MYAUX`.

If improperly decoded, or not addressed to this station, the next command will be `CANCELPENDING`


#### PING

`PING SenderCallsign>TargetCallsign S:NdB DecodeQuality`

If the TNC receives a `PING` and is in the `DISC` state it reports the decoded Sender’s `callsign>TargetCallsign`, S:N (in dB relative to 3 KHz noise bandwidth) and the decoded constellation quality (30-100)

Example: `PING N7CAII>K6CALL 10 95`

#### PINGACK SNdB Quality

`PINGACK SNdB Quality`

Sent to the host when ardopcf has sent a `PING` to another station and receives a `PINGACK` in response.

It includes the Signal-to-Noise ratio expressed in deciBells, and the decoded frame's constellation quality


#### PINGREPLY

`PINGREPLY` is sent to the host when ardopcf has transmitted a `PINGACK` frame to the station that has pinged it.

A `PINGACK` frame will only be sent if:
- ardopcf is in the `DISC` state
- ardopcf has decoded a `PING` frame directed at the station (`MYCALL` or `MYAUX`)
- `ENABLEPINGACK` is set to `TRUE`

#### PTT TRUE or FALSE

`PTT TRUE` is sent to the host when ardopcf is generating frame data and is ready for the transmitter to key. Between this command being sent, and the radio being keyed, should be less than 50 milliseconds.

`PTT FALSE` is sent to the host when ardopcf is finished sending frame data and the radio needs to be receiving.

#### REJECTEDBW

`REJECTEDBW CALLSIGN`

Used to signal the host that a connect request to or from Remote Call sign was rejected due to bandwidth incompatibility

#### REJECTEDBUSY

`REJECTEDBUSY CALLSIGN`

Used to signal the host that a connect request to/from Remote Call sign was rejected due to channel busy detection.

#### STATUS

`STATUS QUEUE/BREAK/END/ARQ/CONNECT text`

There are several status messages, here are some of them:

`STATUS QUEUE BREAK new Protocol State IRStoISS`

`STATUS BREAK received from Protocol State IDLE, new state IRS`

`STATUS BREAK received from Protocol State IDLE, new state IRS`

`STATUS BREAK received from Protocol State ISS, new state IRS`

`STATUS [RXO [SessionIDByte]] [FrameType] frame received OK.`

`STATUS [RXO [SessionIDByte]] [FrameType] frame decode FAIL.`

`STATUS END ARQ CALL`

`STATUS ARQ Timeout from Protocol State:  [protocol state]`

`STATUS ARQ CONNECT REQUEST TIMEOUT FROM PROTOCOL STATE: [protocol state]`

`STATUS END NOT RECEIVED CLOSING ARQ SESSION WITH [callsign]`

`STATUS CONNECT TO [callsign] FAILED!`

`STATUS ARQ CONNECTION REQUEST FROM [callsign] REJECTED, CHANNEL BUSY.`

`STATUS ARQ CONNECTION REJECTED BY [callsign]`

`STATUS ARQ CONNECTION FROM [callsign]: [bandwidth] HZ`

`STATUS ARQ CONNECTION ENDED WITH [callsign]`

#### TARGET

`TARGET K7CALL`

Identifies the target call sign of the incoming connect request. The target call will be either `MYCALL` or one of the `MYAUX` call signs.

##### ~Various CAT Rig Commands

Needs developer review.

When CAT commands are sent to a serial port, they will be echod to the command port.
