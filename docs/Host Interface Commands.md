# TCP Host Interface Commands

Please read the ARDOP overview document before reading this document.

When writing host applications, commands are issued to the modem's TCP socket (default 8515). By default, ardopcf listens on 0.0.0.0. 

Commands sent to the ardopcf command socket can be upper or lowercase, but must be terminated with a `/r` (carriage return) `0x0d` ascii byte.

For example, sending `BUFFER/r` to the command socket will return `BUFFER N/r` where N is the decimal length of any data currently loaded into ardopcf's buffer via its data socket.

## ARDOP Modes

Not all commands are for all modes. There are three main operating modes of ardopcf, and each have their own commands.

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

**RX0** (Decode all frames with extra debug output)
  This mode is mainly used for logging/debugging purposes. It will decode any heard frame.
  You may set this protocol mode via:
>`INITIALIZE`
>`PROTOCOLMODE RX0`


## ARDOPCF Command List

These are all commands that the host application can send to ardopcf, and what to expect in response. Other commands from ardopcf can be recieved at any time, and it is up to the host application to continuously read the command socket and deal with any other responses ardop may send. See "ARDOPCF Command Socket Messages" section below for anything ardopcf may send that is not directly queried.

##### ABORT (or DD)

This is a "dirty disconnect" from a current session. It will clear the current buffer, set ardopcf state to `DISC`, and reset the internal state. 

- Mode: ANY
- Arguments: None
- Example: `ABORT` returns `ABORT`


#### ARQBW
Sets the desired bandwith for an ARQ connection. If there is a mismatch in desired bandwith (both sides force different bandwidths) then the `CONREJBW` frame will be sent by the called station.

Acceptible bandwidths are 200, 500, 1000, and 2000.
Acceptible bandwith enforcement modes are MAX and FORCE.

If no arguments are specified, it will return the current ARQBW value.

- Mode: ARQ
- Arguments: Optional desired bandwith postfixed with MAX/FORCE.
- `ARQBW 500MAX` returns `ARQBW now 500MAX`
- `ARQBW` returns `ARQBW 500MAX`



#### ARQCALL

Needs developer review (needs more testing)

Initiates an ARQ session by transmitting `CONREQ` frames with the bandwidth mode specified by `ARQBW`. Once the session is connected, any data loaded into the data buffer of either end will be transmitted in turn until the session is ended via disconnect, or an idle timeout occurs.


Requires: `MYCALL` to be set and `PROTOCOLMODE` == `ARQ`

- Mode: ARQ
- Arguments: Target Callsign or `CQ`; Number of `CONREQ` frames to send, 2-15.
- `ARQCALL callsign 2` returns ??


If a connection attempt fails: `STATUS CONNECT TO {callsign} FAILED!`


#### ARQTIMEOUT
This is the length of time, in seconds, for how long an ARQ connection can remain IDLE (no data sent) before the session is automatically disconnected.

Valid arguments is an integer between 30 and 240

- Mode: ARQ
- Arguments: An interger of seconds
- `ARQTIMEOUT` returns `ARQTIMEOUT 120`
- `ARQTIMEOUT 30` returns `ARQTIMEOUT now 30`


#### BREAK

Normally in ARQ mode, ardopcf will automatically `BREAK` to make sure both stations can send any data they have in their buffers.

If this command is sent, it means that this station wants to send data right now, and not wait for normal flow control. It will repeat this command and not process any received ARQ session frames until the `BREAK` is acknowledged.

- Mode: ARQ
- Arguments: None
- `BREAK` returns `BREAK`


#### BUFFER

Queries ardopcf for how many bytes of data are currently in the outgoing data buffer.

- Mode: ANY
- Arguments: None
- `BUFFER` returns `BUFFER N` - where N is an integer length of data to be sent.


#### BUSYBLOCK

Enabling this will reject any incoming ARQ connection requests, and respond with a `CONREJBUSY` frame.

- Mode: ARQ
- Arguments: `TRUE` or `FALSE`
- `BUSYBLOCK` returns `BUSYBLOCK TRUE`
- `BUSYBLOCK FALSE` returns `BUSYBLOCK now FALSE`


#### BUSYDET

Selects the sensitivity of busy channel detection.

- Mode: ANY
- Arguments: Integer 0-9, with 0 being disabled, 1 being most sensitive, and 9 being least sensitive.
- `BUSYDET` returns `BUSYDET 5`
- `BUSYDET 6` returns `BUSYDET now 6`
  
#### CALLBW


- Mode: ARQ
- Arguments: Optional desired bandwith postfixed with MAX/FORCE.
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

Kills ardopcf.
  
#### CMDTRACE

For debugging, determines if command sent from the host are recorded in the logfile or not.

- Mode: ANY
- Arguments: None, `TRUE` or `FALSE`
- `CMDTRACE` returns `CMDTRACE TRUE`
- `CMDTRACE FALSE` returns `CMDTRACE now FALSE`
  
#### CONSOLELOG

Changes the console log level, the information printed to the terminal where ardopcf was invoked. The higher the loglevel, the more is logged to the console.

- Mode: ANY
- Arguments: None, loglevel integer 0-8
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

Same as the `BUFFER` command.
  
#### DEBUGLOG
- Mode: ANY
- Arguments: None
- Returns: None
  
#### DISCONNECT

If in a ARQ session, commands ardopcf to gracefully end the session and places the `STATE` into `DISC`

- Mode: ANY
- Arguments: None
- `DISCONNECT` when not in a session returns `DISCONNECT IGNORED`
  
#### DRIVELEVEL

Sets the volume of the generated frame audio. Lowering this may help with overdriven audio.

- Mode: ANY
- Arguments: None, Integer 1 through 100
- `DRIVELEVEL` returns `DRIVELEVEL 100`
- `DRIVELEVEL 50` returns `DRIVELEVEL now 50`
  
#### ENABLEPINGACK

If enabled, ardopcf returns a `DATAACK` frame to acknowledge the a `PING`

- Mode: ANY
- Arguments: None, `TRUE` or `FALSE`
- `ENABLEPINGBACK` returns `ENABLEPINGBACK TRUE`
- `ENABLEPINGBACK FALSE` returns `ENABLEPINGBACK now FALSE`
  
#### EXTRADELAY

Adds extra time in between reception and transmission for high-latency signal paths.

- Mode: ANY
- Arguments: Time in milliseconds, 0-100000+
- `EXTRADELAY` returns `EXTRADELAY 0`
- `EXTRADELAY 10` returns `EXTRADELAY now 10`
  
#### FASTSTART

Unknown, but causes the radio to key, and will not unkey, when invoked.

Needs developer review.

- Mode: ANY
- Arguments: None
- Returns: None
  
#### FECID

Automatically transmits an ID frame after every FEC transmission. Will automatically transmit an ID every 10 minutes to comply with FCC identification rules.

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

FEC mode relies on multiple repeated frames because there is no automatic request for missing data like in ARQ mode. Duplicate frames will be discarded, if a frame is repeated and decoded 3 times, the host will only get the data once.

- Mode: FEC
- Arguments: None, or an integer 0 through 5
- `FECREPEATS` returns `FECREPEATS 0`
- `FECREPEATS 5` returns `FECREPEATS now 5`
  
#### FECSEND

If there is data in the `BUFFER`, ardopcf will await for a clear channel and then transmit the selected `FECMODE` frame `FECREPEATS` amount of times. If there is no data in the buffer, it will wait for data to be loaded. You cannot query this to see if `FECSEND` is true or not.

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
  
#### LEADER

Synchronization leader length in milliseconds, the longer this is the easier it is for the listener to decode the frame, but the more overhead there is.

- Mode: ANY
- Arguments: None, integer 120-2500
- `LEADER` returns `LEADER 120`
- `LEADER 140` returns `LEADER now 140`
  
#### LISTEN

Actively attempts to find an decode any frames in the audio passband.

- Mode: ANY
- Arguments: None, `TRUE` or `FALSE`
- `LISTEN` returns `LISTEN TRUE`
- `LISTEN FALSE` returns `LISTEN now FALSE`
  
#### LOGLEVEL

Sets the loglevel for the debug log that is generated when the program is run. Loglevel 1 is for critical errors only, and loglevel 8 logs everything. Loglevel 8 can generate very large files.

- Mode: ANY
- Arguments: None, integer between 0 and 8
- `LOGLEVEL` returns `LOGLEVEL 7`
- `LOGLEVEL 1` returns `LOGLEVEL now 1`

#### MONITOR

Source code comment says "allows ARQ mode to operate like FEC when not connected"
SoundInput.c:1425

- Mode: ANY
- Arguments: None, `TRUE`or `FALSE`
- `MONITOR` returns `MONITOR TRUE`
- `MONITOR FALSE` returns `MONITOR now FALSE`

#### MYAUX

Sets auxillary (tactical) callsigns that the modem will respond to in addition to `MYCALL`.

- Mode: ANY
- Arguments: None or CALLSIGN
- `MYAUX` returns `MYAUX TACCAL` (or `MYAUX `+ empty string)
- `MYAUX ABC123` returns `MYAUX now ABC123`


#### MYCALL

Sets operator callsign, used in `IDFRAME` and `CONREQ` frames, and for engaging in ARQ mode.

- Mode: ANY
- Arguments: None
- Returns: None

#### PAC

Packet mode subcommands - to be removed.

#### PING

Encodes a 4FSK 200 Hz BW Ping frame ( ~ 1950 ms with default leader/trailer) with double FEC leader length for extra robust decoding.

** this needs more research on how to use with a host.
** Check function ProcessPingFrame in ARDOPC.c

- Mode: ANY
- Arguments: Target Callsign ; Count (1-15)
- `PING 12345 1` returns `PING 12345 1`

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
- Arguments: None, FEC, ARQ, RX0
- `PROTOCOLMODE` returns `PROTOCOLMODE ARQ`
- `PROTOCOLMODE FEC` returns `PROTOCOLMODE now FEC`

#### PURGEBUFFER

Empties all data from the outgoing data buffer. Will stop any data transmissions after the current frame is finished sending.

- Mode: ANY
- Arguments: None
- `PURGEBUFFER` returns `BUFFER 0`

#### RADIOFREQ

Presumably a CAT string that would get or set the radio's current frequency. Possibly an embedded device specific command.

- Mode: ANY
- Arguments: None
- Returns: None

#### RADIOHEX

Define CAT hex string to send to a radio. Possibly embedded device specific.

- Mode: ANY
- Arguments: None
- Returns: None

#### RADIOPTTOFF

Sends predefined CAT PTT hex string to a radio. Possibly embedded device specific. 

- Mode: ANY
- Arguments: UNK
- Returns: UNK

#### RADIOPTTON

Sends predefined CAT PTT hex string to a radio. Possibly embedded device specific. 

- Mode: ANY
- Arguments: UNK
- Returns: UNK
  
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

Sets the minimum audio level. 1 is basically open squelch, and 10 requires a strong audio baseband to trigger. An open squelch will often lead to false frames beign decoded and possibly passing ERR data frames to the host.

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

How long after transmission does the PTT remain ON.

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

#### TXLEVEL

Embedded platform control of ADC output volume.

- Mode: ANY
- Arguments: None
- Returns: None

#### TWOTONETEST

Transmits a pair of carriers for testing modulation quality.

- Mode: ANY
- Arguments: None
- Returns: None

#### USE600MODES

Enables 600 baud rate frame types 4FSK.2000.600S and 4FSK.2000.600. For use on FM/2m. Use on HF bands constitute a data rate violation per FCC rules.

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

#### REJECTEDBUSY

This message can appear for several reasons.

In ARQ Mode:
- In a 600ms gap between disconnecting from another station, or when the receiving station detects that the current channel is busy? (Would like a 2nd opinion: ARQ.c:1391, in ProcessRcvdARQFrame)
  - Sends `REJECTEDBUSY` to host
- When calling a remote station and they say they are busy
  - Sends `REJECTEDBUSY CALLSIGN` to host

#### DISCONNECTED

A `DISCONNECTED` host message will be sent on any of the following conditions

- `ARQTIMEOUT` is reached
- Tried sending `DISC` frame 5 times and never hear a response
- If Information Sending Station sends a `DISC` frame
- If Information Sending Station sends an `END` frame
- If Information Receiving Station sends a `DISC` frame
- If Information Receiving Station sends an `END` frame

#### BUFFER

The `BUFFER` message is recieved whenever data is added to the data buffer. It is a verification opportunity for the host to be sure that the data that was sent to ardopcf is the same length that it received.

Example: sending to the ardopcf data port `<2 length bytes><mode>Hello` would return a `BUFFER 5` - the length of the acutal data.
The `2 length bytes` is a big endian representation of the length of the data to load into the data buffer.
The `mode` is either `FEC` or `ARQ`.

#### PING caller>target SNdB Quality

`PING {caller}>{target} {SNdB} {Quality}` is sent to the host when a `PING` frame is decoded.

`caller` is the station that sent the ping.
`target` is the station the caller tried to ping.
`SNdB` is the signal-to-noise ratio of the recieved 
`Quality` is the frame decode quality rating, usualy 70-100

If ardopcf has `ENABLEPINGBACK` set to `TRUE` and the target is this station, it will respond with `PINGACK`

#### PINGREPLY

`PINGREPLY` is sent to the host when ardopcf has transmitted a `PINGACK` frame to the station that has pinged it.

A `PINGACK` frame will only be sent if:
- ardopcf is in the `DISC` state
- ardopcf has decoded a `PING` frame directed at the station (`MYCALL` or `MYAUX`)
- `ENABLEPINGACK` is set to `TRUE`

#### PINGACK SNdB Quality

`PINGACK {SNdB} {Quality}` is sent to the host when ardopcf has sent a `PING` to another station and receives a `PINGACK` in response.

It includes the Signal-to-Noise ratio expressed in deciBells, and the decoded frame's "quality"

#### CANCELPENDING

`CANCELPENDING` occurs in the following situations:
- A `PING` has been decoded but ardopcf:
  - is not in `DISC` state
  - `LISTEN` is set to `FALSE`
  - `PING` is not addressed to `MYCALL`
  - `ENABLEPINGACK` is set to `FALSE`
- An `ARQ` frame is not intended for this station
- A `CONREQ` frame is recognized but decoded improperly
- A `PING` frame is recognized but decoded improperly
- A `PING` frame is received but not decoded when ardopcf is not in `DISC` state or `RX0` mode. (duplicate?)

#### STATUS QUEUE BREAK new Protocol State IRStoISS

The message `STATUS QUEUE BREAK new Protocol State IRStoISS` is sent to the host when the protocol state changes from IRS (Idle Receiver State) to ISS (Idle Sender State). This typically happens when a connection has been established and the software is transitioning from receiving to sending data. 

#### STATUS BREAK received from Protocol State IDLE, new state IRS

`STATUS BREAK received from Protocol State IDLE, new state IRS` can be received when:
- ardopcf is in an ARQ session but not actively sending or receiving data
- A `BREAK` frame is receieved

The `BREAK` frame is used to tell the other station to get ready to receive data.

#### STATUS BREAK received from Protocol State ISS, new state IRS

`STATUS BREAK received from Protocol State ISS, new state IRS` can be received when:
- ardopcf is in an ARQ session and is the Information Sending Station
- A `BREAK` frame is receieved

The `BREAK` frame is used to tell the other station to get ready to receive data.

The inline comments say:
>With new rules IRS can use BREAK to interrupt data from ISS. It will only
>be sent on IDLE or changed data frame type, so we know the last sent data
>wasn't processed by IRS

#### PTT TRUE

`PTT TRUE` is sent to the host when ardopcf is generating frame data.

#### PTT FALSE

`PTT FALSE` is sent to the host when ardopcf is finished sending frame data.

#### STATUS [RXO {SessionIDByte}] {FrameType} frame received OK.

This message gives basic information about a decoded frame type.

#### STATUS [RXO {SessionIDByte}] {FrameType} frame decode FAIL.

This message gives basic information about a frame that failed to decode.

#### ~Various CAT Rig Commands

Needs developer review.

When CAT commands are sent to a serial port, they will be echod to the command port.

#### BUSY TRUE

Needs developer review.


If in `DISC` state, tells the host application that BUSY is true if BusyDetect3 returns true.?

#### STATUS END ARQ CALL

Needs developer review.

When another station calls this station, but never responds to `CONACK` frames.?

#### PENDING

Needs developer review.

#### "STATUS ARQ Timeout from Protocol State:  %s", ARDOPStates[ProtocolState]

Needs developer review.

#### STATUS ARQ CONNECT REQUEST TIMEOUT FROM PROTOCOL STATE: %s",ARDOPStates[ProtocolState]

Needs developer review.

#### "NEWSTATE %s ", ARDOPStates[ProtocolState])

Needs developer review.

#### STATUS END NOT RECEIVED CLOSING ARQ SESSION WITH %s", strRemoteCallsign);

Needs developer review.

#### "STATUS CONNECT TO %s FAILED!", strRemoteCallsign);

Needs developer review.

#### "REJECTEDBUSY %s", strRemoteCallsign)

Needs developer review.

#### "STATUS ARQ CONNECTION REQUEST FROM %s REJECTED, CHANNEL BUSY.", strRemoteCallsign);

Needs developer review.

#### "TARGET %s", strCallsign)

Needs developer review.

#### REJECTEDBW %s", strRemoteCallsign);

Needs developer review.

#### "STATUS ARQ CONNECTION REJECTED BY %s", strRemoteCallsign);

Needs developer review.

#### "CONNECTED %s %d", strRemoteCallsign, intSessionBW);

Needs developer review.

#### "STATUS ARQ CONNECTION FROM %s: SESSION BW = %d HZ", strRemoteCallsign, intSessionBW);

Needs developer review.

#### "STATUS ARQ CONNECTION ENDED WITH %s", strRemoteCallsign);

Needs developer review.