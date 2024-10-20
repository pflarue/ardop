# TCP Host Interface Commands



When writing host applications, commands are issued to the modem's TCP socket (default 8515). By default, ardopcf listens on 0.0.0.0 (all interfaces, meaning unless you set up a firewall, any computer on your network can access ardopcf's tcp interface)

Commands sent to the ardopcf command socket can be upper or lowercase, but must be terminated with a `/r` (carriage return) `0x0d` ascii byte.

For example, sending `BUFFER/r` to the command socket will return `BUFFER N/r` where N is the decimal length of any data currently loaded into ardopcf's buffer via its data socket.

**Disclaimer**: The content of this documentation is DESCRIPTIVE of the current TCP host interface, not PRESCRIPTIVE - meaning that we went through and tried to understand what the commands do based on review of the source code and older (possibly out of date) documentation. This means that the behavior of some of these commands under certain situations may not be as expected, and some may cause a crash. Some commands are not fully understood, and some responses are not fully documented, but this documentation will be updated as they are experimented with or after further code review. Feel free to clone the respository and submit a PR or to open up an issue with what should be changed and why.

### Interfacing Example

In python3 to send a command, this can be done like this:
```
import socket
sock_cmd = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock_cmd.connect('localhost', 8515)

def send_command(string: str):
  string += '\r'
  sock_cmd.sendall(string.encode()) # it will expect 'bytes'

send_command(`BUFFER`) # this will store 'BUFFER n\r' in the socket when ardop responds
```
To simply read the command response buffer, it can be done like this:
```
def read_command_response() -> str:
  line = b''
  while True:
    part = sock_cmd.recv(1)
    if part != b"\r":
        line+=part
    elif part == b"\r":
        break
  return(line.decode())
```
Note that the above is the simpliest example, and this will block the main thread if there is no data to read from the socket. You can make this asyncronous via multiple methods, but what works reasonably well is running the function in its own thread, setting `sock_cmd.set_blocking('False')` and wrapping the socket reading loop with a try/except block (terminating on a threading.Event being false), continuing on a `BlockingIOError`. If you find a better way to do this, please let us know!



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

**RXO** (Decode all frames with extra debug output)
  This mode is mainly used for logging/debugging purposes. It will decode any heard frame.
  (O as in Oscar, not 0 as in zero)
  You may set this protocol mode via:
>`INITIALIZE`
>`PROTOCOLMODE RXO`


## ARDOPCF Command List

These are all commands that the host application can send to ardopcf, and what to expect in response. Other commands from ardopcf can be recieved at any time, and it is up to the host application to continuously read the command socket and deal with any other responses ardop may send. See "ARDOPCF Command Socket Messages" section below for anything ardopcf may send that is not directly queried.

##### ABORT (or DD)

This is a "dirty disconnect" from a current ARQ session. It will clear the current buffer, set ardopcf state to `DISC`, and reset the internal state. The current frame will complete transmission.

- Mode: ARQ
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

Needs Developer review.

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

Kills ardopcf properly.
  
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

Sets the volume of the generated frame audio. Lowering this may help with overdriven audio.

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

Needs developer review.

- Mode: ANY
- Arguments: None
- Returns: None
  
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

FEC mode relies on multiple repeated frames because there is no automatic request for missing data like in ARQ mode. Duplicate frames will be discarded, if a frame is repeated and decoded 3 times, the host will only get the data once.

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

Sets the loglevel for the debug log that is generated when the program is run. Loglevel 1 is for critical errors only, and loglevel 8 logs everything. Loglevel 8 can generate very large files.

- Mode: ANY
- Arguments: None, integer between 0 and 8
- `LOGLEVEL` returns `LOGLEVEL 7`
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

Sets auxillary (tactical) callsigns that the modem will respond to in addition to `MYCALL`.

- Mode: ANY
- Arguments: None or CALLSIGN
- `MYAUX` returns `MYAUX TACCAL` (or `MYAUX `+ empty string)
- `MYAUX ABC123` returns `MYAUX now ABC123`


#### MYCALL

Sets operator callsign, used in `IDFRAME` and `CONREQ` frames, and for engaging in ARQ mode.

- Mode: ANY
- Arguments: None or CALLSIGN
- `MYCALL` returns `MYCALL K7CALL` (or `MYCALL `+ empty string)
- `MYCALL K7CALL` returns `MYCALL now K7CALL`

#### PING

Encodes a 4FSK 200 Hz BW Ping frame ( ~ 1950 ms with default leader/trailer) with double FEC leader length for extra robust decoding.

If the pinged station responds with a `PINGACK` frame, this station will stop pinging.

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

Send a CAT hex string to the connected radio.

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

How long after transmission does the PTT remain ON. Used for radios with processing delays, such as SDR based radios.

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

Transmits a pair of tones at the normal leader amplitude. May be used in adjusting drive level to the radio.

- Mode: ANY
- Arguments: None
- Returns: None

#### TXLEVEL

Embedded platform control of ADC output volume.

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

#### BUFFER

The `BUFFER` message is recieved whenever data is added to the data buffer. It is a verification opportunity for the host to be sure that the data that was sent to ardopcf is the same length that it received.

Example: sending to the ardopcf data port `<2 length bytes><mode>Hello` would return a `BUFFER 5` - the length of the acutal data.
The `2 length bytes` is a big endian representation of the length of the data to load into the data buffer.
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
`SNdB` is the signal-to-noise ratio of the recieved 
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
