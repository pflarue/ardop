# ARDOP Protocol Native TNC Commands

Rick Muething, KN6KB
Revised: Rev 0.3 Nov 30, 2017
(This should agree with ARDOP_Win TNC release 1.0.2)

Please send questions, comments or suggestions to
mailto:rmuething@cfl.rr.com

## 1.0 Purpose

This document provides detailed information on Native (plain text) commands and data for the ARDOP protocol. The commands may be sent using a TCPIP interface (2 ports) or via a serial (RS232, RS422, USB using a virtual serial COM port, or Bluetooth) interface. The ARDOP TNC may be a virtual (software) TNC or a physical (hardware or SBC firmware) implementation of the ARDOP protocol.

## 2.0 Relevant Documents

*Host Interface Spec for Winlink Supported Protocols/TNCs*
This document describes the Host mode and CRC host mode (serial) interfaces and the TCPIP interface.

*ARDOP Protocol Specification*
Details of the ARDOP protocol including detailed spreadsheets on frame types, max throughput etc.

## 3.0 ARDOP Protocol Commands and Responses

The following sections list the commands with their parameters and responses. Parameters use this notation:

- `[...]` optional parameter or block
- `{a|b|c}` mutually exclusive choices
- `n..m` inclusive numeric range
- `name` placeholder value

Not all commands are required in every application.

~~Commands in BLUE are for a virtual (software) implementation meant to operate with local (same physical computer) sound card hardware.~~

~~Commands in RED are Radio control commands primarily intended for optional local radio control via a TCPIP connection~~

> Previously in the PDF version of this document, Blue and Red were used to provide additional context. For what was RED, "radio commands", no additional illustration is needed, as all radio commands start with "RADIO". What was Blue, "virtual (software)" commands have been indicated as such with the [SOFTWARE] decorator.

**ABORT**
Immediately aborts an ARQ Connection (dirty disconnect) or a FEC Send session.

**ARQBW** {200MAX|500MAX|1000MAX|2000MAX|200FORCED|500FORCED|1000FORCED|2000FORCED}

Set/gets the bandwidth for ARQ mode. This sets the maximum negotiated bandwidth or
sets the forced bandwidth to a specific value. Attempting to change bandwidth while a
connection is in process will generate a FAULT. If no parameter is given will return the
current bandwidth setting. This bandwidth setting applies to all call signs used
(MYCALL plus optional call signs MYAUX)

**ARQCALL** Target Callsign Repeat Count
Target Call sign must be a legitimate call sign syntax or “CQ” Repeat count must be 2 to 15 e.g. ARQCALL W1AW 5

**ARQTIMEOUT** 30..600
Set/get the ARQ Timeout in seconds. If no data has flowed in the channel in ARQTimeout seconds the link is declared dead. A DISC command is sent and a reset to the DISC state is initiated. If either end of the ARQ session hits its ARQTIMEOUT without data flow the link will automatically be terminated.

**AUTOBREAK** {False|True}
Disables/enables automatic link turnover (BREAK) by IRS when IRS has outbound data pending and ISS reaches IDLE state. Default = True.

**BREAK**
Initiates a BREAK (link turnover request to the ISS) if in IRS state otherwise it has no effect. Forces ISS to clear its outbound queue and acknowledge with ACK. Normally not required if AUTOBREAK is enabled.

**BUFFER**
Gets the current outbound data buffer size in bytes. BUFFER is also sent asynchronously
whenever there is a change in the outbound buffer size. The reported size includes any
data that is currently in process (being sent but not yet acknowledged received by the
Information Receiving Station, IRS)

**BUSYBLOCK** {False|True}
Disable/Enable Busy channel blocking. Busyblock will block a connection request unless there have been Tquiet ms of non-busy status preceding the connect request. (See appendix B) Default = False

**BUSYDET** 0..10
Returns or sets the current Busy detector threshold value (default = 5). The default value should be sufficient for most installations. BUSYDET affects the sensitivity of the busy detector (low values = higher sensitivity but increased false triggering). A value of 0 disables the Busy detector (only should be disabled in emergency situations or very high local noise environments)

[SOFTWARE] **CAPTURE** device name
Sets desired sound card capture device If no device name will reply with the current assigned capture device.

[SOFTWARE] **CAPTUREDEVICES**
Returns a comma delimited list of all currently installed capture devices.

**CLOSE**
Provides an orderly shutdown of all connections, release of all sound card resources and closes the Virtual TNC Program or hardware.

**CMDTRACE** {True|False}
Get/Set Command Trace flag to log all commands to from the TNC to the ARDOP_Win TNC debug log. (normally used in debugging)

[SOFTWARE] **CODEC** {True|False}
Start the Codec with True, Stop with False. No parameter will return the Codec state.

**CWID** {True|False}
Disable/Enable the CWID option. CWID is optionally sent at the end of each ID frame.

**DATATOSEND** 0
If sent with the parameter 0 (zero) it will clear the TNC’s data to send (Outbound Queue). Cleared data can be recovered using the RESTOREBUFFER command. If sent without a parameter will return the current number of data to send bytes queued.

**DEBUGLOG** {True|False}
Enable/disable the debug log.

**DISCONNECT**
Initiates a normal disconnect cycle for an ARQ connection. If not connected command is ignored.

**DISPLAY** Frequency in KHz
Sets the Dial frequency display of the Waterfall or Spectrum display. If sent without parameters will return the current Dial frequency display. If > 100000 Display will read in MHz.

**DRIVELEVEL** 0..100
Set Drive level. Default = 100 (max)

**ENABLEPINGACK** {True|False}
Enable or Disable PINGACK reply to a PING targeted to MYCALL or one of the MYAUX call signs. Default = True No parameter will return the ENABLEPINGACK state. (PINGACK only sent when in the DISC protocol state). Applies to both ARQ and FEC modes.

**FECID** {True|False}
Disable/Enable ID (with optional grid square) at start of FEC transmissions

**FECMODE** {4FSK.200.50S|4PSK.200.100S|4PSK.200.100|8PSK.200.100|16QAM.200.100|4FSK.500.100S|4FSK.500.100|4PSK.500.100|8PSK.500.100|16QAM.500.100|4PSK.1000.100|8PSK.1000.100|16QAM.1000.100|4PSK.2000.100|8PSK.2000.100|16QAM.2000.100|4FSK.2000.600S|4FSK.2000.600}

Sets the modulation mode and bandwidth for FEC (broadcast/multicast) transmission.
Details on the specific frame types can be found in the ARDOP frame type spreadsheet.
In general the first component of the frame ID is the modulation type e.g. 4FSK, 8PSK
etc. The second is the bandwidth in Hz (@ -26 dB). The third is the baud rate. Some
modes also allow a shortened frame designated by a trailing “S”. Note the last two modes
are 600 baud and intended for FM. These 600 baud modes are not currently permitted in
the US below 29 MHz.

**FECREPEATS** 0..5 Sets the number of times a frame is repeated in FEC (multicast)
mode. Higher number of repeats increases good copy probability under marginal
conditions but reduces net throughput.

**FECSEND** {True|False}
Start/Stop FEC broadcast/multicast mode for specific FECMODE. FECSEND
False will abort a FEC broadcast.

**GRIDSQUARE** 4, 6 or 8 character grid square
Sets or retrieves the 4, 6, or 8 character Maidenhead grid square (used in ID Frames) an
improper grid square syntax will return a FAULT.

**INITIALIZE**
Clears any pending queued values in the TNC interface. Should be sent upon initial connection and before any other parameters are sent.

**LEADER** 120..2500
Get/Set the leader length in ms. (Default is 160 ms). Rounded to the nearest 20 ms. Note for ARQ connections leader length is automatically adjusted based on the leader
reported received by the remote station.

**LISTEN** {True|False}
Enables/disables server’s response to an ARQ connect request to MYCALL or any of MYAUX call signs. Default = True. Also enabled the decoding of a PING frame to MYCALL or any of the MYAUX call signs in either ARQ or FEC modes. May be used to block connect requests during scanning or periods when server is offline or in the
process of changing frequency.

**MONITOR** {True|False}
Enables/disables monitoring of FEC or ARQ Data Frames, ID frames, or connect requests in the disconnected ARQ state. Default=True

**MYAUX** aux call sign1, aux call sign2, … aux call sign10
Sets up to 10 auxiliary call signs that will answer ARQ connect requests. Call signs must be valid radio call signs and separated by commas. If sent with an illegal call sign (e.g. “MYAUX x” it will clear the MYAUX list and return a FAULT on the first syntax error. If sent without a parameter will return a comma delimited string of current MYAUX call signs. Legitimate call signs include from 3 to 7 ASCII characters (A-Z, 0-9) followed by an optional “-” and an SSID of -0 to -15 or -A to -Z. An SSID of -0 is
treated as no SSID.

**MYCALL** call sign
Sets current call sign. If not a valid call generates a FAULT. Legitimate call signs include from 3 to 7 ASCII characters (A-Z, 0-9) followed by an optional “-” and an SSID of -0 to -15 or -A to -Z. An SSID of -0 is treated as no SSID.

**PING** Target Callsign Repeat Count
Target call sign must be a legitimate call sign syntax. Repeat count must be 2 to 15 e.g. PING W1AW 5. If the target call sign is not connected and decodes a PING and has ENABLEPINGACK = True and LISTEN = True it will reply with a PINGACK which includes the received PING S:N and decode quality. A properly decoded PINGACK will terminate the PING. An attempt to PING if not in the DISC state will cause a Fault response from the TNC.

[SOFTWARE] **PLAYBACK** device name
Sets desired sound card playback device. If no device name will reply with the current assigned playback device.

[SOFTWARE] **PLAYBACKDEVICES**
Returns a comma delimited list of all currently installed playback devices.

**PROTOCOLMODE** {ARQ|FEC}
Sets/Gets the protocol mode. If ARQ and LISTEN above is TRUE will answer Connect requests to MYCALL or any call signs in MYAUX. If FEC will decode but not respond to any connect request.

**PURGEBUFFER**
Clears any data in the outbound buffer. Should precipitate a “BUFFER 0” asynchronous response. Data purged goes into a TNC temporary buffer that can be restored to the outbound buffer (outbound Queue) with a RESTOREBUFFER command.

The following RADIO commands support optional radio control that may be used in some applications (e.g. Virtual TNC is running on a remote computer located with the radio). All radio commands begin with “RADIO”.

**RADIOANT** {0|1|2}
Selects the radio antenna 1 or 2 for those radios that support antenna switching. If the parameter is 0 will not change the antenna setting even if the radio supports it. If sent without a parameter will return 0, 1 or 2. If RADIOCONTROL Is false or RADIOMODEL has not been set will return FAULT

**RADIOCTRL** {True|False}
Enables/disables the radio control capability of the ARDOP_Win TNC. If sent without a parameter will return the current value of RADIOCONTROL enable.

**RADIOCTRLBAUD** 1200..115200
(Note: baud rates greater than 4800 are recommended for PTT control to minimize T-to-R latency)

**RADIOCTRLDTR** {True|False}
Enable/disable DTR Line on Control port.

**RADIOCTRLPORT** COMn
Set/get the radio control com port to use for radio control.

**RADIOCTRLRTS** {True|False}
Enable/disable RTS Line on Control port.

**RADIOFILTERBW** {0|200|500|1000|2000}
This sets (for selected radios) the filter bandwidth for the radio. A value of 0 will disable filter control. The actual bandwidth set in the radio is a function of the desired ARDOP bandwidth (200, 500, 1000, or 2000 Hz) plus allowance for up to +/- 200 Hz tuning range centered on 1500 Hz. If a radio will not support the requested filter bandwidth then the next largest bandwidth will be used. A requested Filter Bandwidth of 0 (default) disables filter control.

**RADIOFREQ** Frequency in Hz
If Radio Control is enabled in the ARDOP_Win TNC sets the Dial frequency of the radio and the display of the Waterfall or Spectrum display. If sent without parameters will return the current Dial frequency of the radio.

**RADIOICOMADD** 00..FF
Sets/reads the current Icom Address for radio control (Icom radios only). Values must be hex 00 through FF

**RADIOISC** {True|False}
Enable/Disable radio’s internal sound card (some radios)

**RADIOMENU** {True|False}
Enable/Disable the Radio menu item on the TNC Form.

**RADIOMODE** {USB|USBD|FM}
Sets the radio modulation mode to USB, USB Digital (some radios) or FM (some radios). If sent without a parameter will return the current value of RADIOMODE.

**RADIOMODEL** Radio Model
If radio control is enabled accepts the radio model. If sent without a parameter returns the current radio model. If radio not supported returns Fault.

**RADIOMODELS**
Returns a comma delimited list of supported radio models.

**RADIOPTT** {CATPTT|VOX/SIGNALINK|COMn}
Selects CATPTT, VOX (SignaLink or RigBlaster Advantage) mode or COM port for PTT control

**RADIOPTTDTR** {True|False}
Enables/disables PTT keying using DTR signal on RADIOPTT Com port

**RADIOPTTRTS** {True|False}
Enables/disables PTT keying using RTS signal on RADIOPTT com port

**RADIOTUNER** {True|False}
Enable/Disable internal radio tuner (some radios).

End of Radio Commands.

**RESTOREBUFFER**
Restores data from the most recent PURGEBUFFER command. Should precipitate an asynchronous “BUFFER nnnn” response.

**SENDID**
This will send an ID frame and if CWID above is enabled followed by a FSK CW ID. The protocol must be in the DISC state or a “Fault: Not from state…” reply will be sent.

**SETUPMENU** {True|False}
Enabled/Disable the Setup Menu on the ARDOP_Win TNC main form.

**SLOWCPU** {True|False}
Reduces DSP loading at some expense in sensitivity when True (Default = False)

**SQUELCH** 1..10
Returns or sets the current squelch value (default = 5). The default value should be sufficient for most installations. Squelch affects the sensitivity of the leader detector (low values = higher sensitivity but increased false triggering). Typical range is 3-7.

**STATE**
Gets the current ARDOP protocol state {OFFLINE|DISC|ISS|IRS|IRStoISS|IDLE|FECSend|FECRcv}. Every state change is also reported asynchronously with the NEWSTATE reply below.

**TRAILER** 0..200
Get/Set the trailer length in ms. (Default is 0 ms). Rounded to the nearest 20 ms. Normally not required except for some SDR Radios.

**TUNERANGE** {0|50|100|150|200}
Get/set the DSP tuning range in Hz. 0 should only be used in FM or AM radio connections.

**TWOTONETEST**
Send 5 second two-tone burst at the normal leader amplitude. May be used in adjusting drive level to the radio. If sent while in any state except DISC will result in a fault “not from state …..”

**VERSION**
Returns the name and version of the ARDOP TNC program or hardware implementation.

## 4.0 ARDOP TNC Initiated Asynchronous Responses

### Asynchronous Responses

The ARDOP TNC codec will respond on the command port with possible asynchronous responses. All asynchronous responses terminate with `CR` plus a 2 byte CRC (No CRC used for TCPIP interfaces). There is no response expected or processed from the Host.

This is the list of the current asynchronous responses:

**BUFFER** data out queued
Value is in integer bytes. BUFFER may also be polled using the BUFFER command with no parameters. BUFFER value includes any data frame currently being sent but not yet ACKed by the remote IRS.

**BUSY FALSE**
Clear channel detected

**BUSY TRUE**
Busy channel detected

**CANCELPENDING**
Indicates to the host that the prior PENDING Connect Request or PING was not to MYCALL, or one of the MYAUX call signs. This allows the Host to resume scanning.

**CONNECTED** remoteCall Bandwidth in Hz
e.g. “CONNECTED W1ABC 500” An ARQ connection has been established. `remote Call` contains the connected call sign with a negotiated bandwidth. This follows the TARGET `target call sign` asynchronous response if the requested bandwidths are compatible and the connection negotiation is complete.

**DISCONNECTED**
An existing ARQ link has been disconnected

**FAULT** description
A program fault or error condition.

**FREQUENCY** Frequency in Hz
If TNC Radio control is enabled the FREQUENCY command is sent to the Host upon a change in frequency of the radio. This can be caused by either a new programmed frequency, a Dial turn on radios with frequency read back or a mouse click on the Waterfall or Spectrum displays (in USB or USBD modes causing a retune). Does not return the new frequency when the command RADIOFREQ sent to the modem. The frequency reported is the DIAL frequency of the radio.

**NEWSTATE**
Reports any protocol state change. Reply options: {OFFLINE|DISC|ISS|QUIET|IRS|IRStoISS|FECSend|FECRcv}

**PENDING**
Indicates to the host application a Connect Request or PING frame type has been detected (may not necessarily be to MYCALL or one of the MYAUX call signs). This provides an early warning to the host that a connection may be in process so it can hold any scanning activity.

PING SenderCallsign TargetCallsign S:NdB DecodeQuality (space delimited)
If the TNC receives a PING and is in the DISC state it reports the decoded sender’s call sign TargetCallsign, S:N (in dB relative to 3 KHz noise bandwidth) and the decoded constellation quality (30-100). E.g. “PING KN6KB>W1AW 10 95”

**PINGACK** S:NdB DecodeQuality
If the TNC receives a PINGACK and is in the DISC state it reports the S:N (in dB relative to 3 KHz noise bandwidth) that the PINGACK sender received and the decoded constellation quality (30-100). E.g. “PINGACK 10 80” A S:N value of 21 indicates the true S:N value > 20 dB. The PINGACK asynchronous reply to host is sent ONLY if a prior PING to this station was received within the last 5 seconds.

**PINGREPLY**
If the TNC receives a PING to MYCALL or any of the MYAUX call signs, is in the DISC state, and has LISTEN=True and ENABLEPINGACK = True it will send a PINGACK frame and send confirmation to the host with a PINGREPLY asynchronous reply.

**PTT** {True|False}
Indicates to the host application to key the PTT on (PTT True) or off (PTT False) To operate correctly the transmitter PTT should be activated within 50 ms of receipt of this response. Excessive delay in PTT application or removal may cause a failure in ARQ modes if measured latency exceeds 250 ms. Not sent if local TNC radio control is used.

**REJECTEDBW** Remote Call sign
Used to signal the host that a connect request to or from Remote Call sign was rejected due to bandwidth incompatibility

**REJECTEDBUSY** Remote Call sign
Used to signal the host that a connect request to/from Remote Call sign was rejected due to channel busy detection.

**STATUS** status text
Used to signal information text to user. e.g. “STATUS CONNECT WITH W1AW FAILED”. The syntax of STATUS commands is not rigidly controlled so if used should be for user display purposes only.

**TARGET** target call
Identifies the target call sign of the connect request. The target call will be either MYCALL or one of the MYAUX call signs. This command precedes the CONNECTED remote Call asynchronous response.

**TUNE** Tuning offset in integer Hz
Sent when the waterfall or spectrum is clicked and can be used by the host to adjust radio settings. This is bounded to approx. +/- 1200 Hz. If radio control is enabled will also tune the radio to the new frequency.
