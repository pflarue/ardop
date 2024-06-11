# Changelog

### 2024.05.30: [ardopcf](https://github.com/pflarue/ardop) v1.0.4.1.2 from v1.0.4.1.1

##### DEPRECATE several command line options, replace with `--hostcommands`

A new command line option `-H` or `--hostcommands` is added.  Either of these may be followed by one or more semicolon separated commands that ardopcf will respond to at startup as if they were issued by a connected host application.  This provides additional capabilities such as the ability to set the initial TX Drive Level to a desired value with `-H "DRIVELEVEL 75"`.  It also duplicates the capabilities of several existing command line options.  The redundant options are now deprecated.  That means that they still work for now, but they will likely be discontinued in a future release of ardopcf.  When any of these deprecated options are used, a prominent warning will be printed to the console and debug log advising that a host command be used instead.  Both these warning messages and the help info available with the `-h` command line option indicate how to provide the same functionality with a host command.

The deprecated command line options include: `-v` and `--verboselog`, `-V` and `--verboseconsole`, `-x` and `--leaderlength`, `-t` and `--trailerlength`, `-r` and `--receiveonly`, `-n` and `--twotonetext`.

If additional help is required to understand how to replace use of these options with `-H` or `--hostcommands`, please contact the developers via ardop.groups.io or by creating an Issue at the GitHub.com/pflarue/ardop.

##### New Webgui!

A new interactive web browser based graphical user interface is now part of ardopcf.  This provides functionality similar to the ARDOP GUI previously produced by John Wiseman.  In addition to providing some additional features, its has two primary advantages over that earlier GUI.  The first is that the new Webgui is built into ardopcf so no additional software needs to be installed.  The second is that since it is displayed in a web browser it is usable on a wider range of devices.  This means that in addition to the Linux or Windows computer on which ardopcf is running, the Webgui can also be viewed on a phone or tablet that can reach that computer via a local network.  So, now a GUI interface is available when ardopcf is running on a headless Raspberry Pi where a smartphone or tablet may provide the only available screen.

To enable the Webgui, use the `-G` or `--webgui` command line options followed by a port number.  I've been using `-G 8514` which makes the Webgui port one less than the default 8515 port used for the ardopcf host interface.  When this option is used, a message of "Webgui available at port 8514." will be printed to the console and debug log file.  A browser pointed to localhost:8514 or 192.168.100.100:8514 if that is the local IP address of the device running ardopcf will then display the Webgui.  The "Show Help" button that is a part of the Webgui provides a detailed discription of its capabilities.  All ardopcf users are encouraged to try the Webgui and use it at least as an aid in intial setup of a new ardopcf installation.

Some of the major features of the Webgui include display of active Spectrum, Waterfall, and Constellation plots for incoming signals; Receive audio level indicators with warning messages if receive audio is too loud or too low; status indicators showing when PTT is engaged and when the tuned channel appears busy at the specified bandwidth; Display of current Protocol mode and state; Display of frame types being sent and received and indication of whether received frames are successfully decoded or not; Statistics showing the Quality and number of Errors corrected by the Reed Solomon error correction coding for the most recently received frame.  A Drivelevel slider allows control of the audio level sent to the radio during transmit.  When transmitting via a single sideband transmitter, this also provides control of transmitted power.  Buttons allow the user to transmit a two-tone test signal or a ID frame on demand for antenna tuning and transmit level adjustments. A few controls are provided to adjust the appearance of the plots.  A log window provides a sequential listing of much of the data that has been displayed.

Feedback is welcome regarding the appearance, contents, and capabilites of this Webgui.

##### Discontinue support for Packet via ardopcf

ardopc and prior versions of ardopcf included code to support Packet/AX25 communications.  This support has been disabled and the remaining source code that related to this feature will be removed in a future release.

Along with some other changes in this release, this eliminates some licencing issues associated with the prior releases of ardopcf.

##### Reduced printing of Input peaks

Previously, "Input peaks" values indicating the recent peak received audio levels were regularly printed to the console by default.  Now, by default, these messages are only printed to the console if the audio level is too high.  In that case a warning is also printed helping new users to understand that this is a problem that needs to be fixed.  Increasing the console log level with the command line option `-H "CONSOLELOG 7"` will regularly print these messages if the user wants to see them.  The new Webgui provides an alternate way to monitor receive audio levels with finer time resolution, and thus is expected to be a better solution for adjusting audio settings for most users.

##### Fixed bug for TX of 1-carrier PSK frame types.

A bug which made 1-carrier PSK frame types transmitted by prior versions of ardopc and ardopcf harder to decode was corrected.

##### Fixed bug relating to 1000MAX bandwidth setting.

A bug was corrected which had allowed a 2000 Hz bandwidth connection request to be accepted when a 1000 Hz maximum bandwidth was set.

##### Fixed rare but serious PTT stuck on bug

A bug was identified and corrected that could cause ardopcf to lock up with PTT engaged and ardopcf unable to be killed with Ctrl-C.  As a part of fixing this bug, additional fault detection features were added to the code for transmitting.  These additions will hopefully prevent any remaining undiscovered bugs from presenting such extreme symptoms if they are activated.

##### Improved fix for ALSA TX Symbol Rate Error

Earlier releases fixed an ALSA problem that caused an incorrect symbol rate when transmitting with certain combinations of hardware and operating system.  A better and more reliable fix for this problem is now implemented.

##### Improved handling of Ping and PingAck

Fixed a bug in the calculation of S:N ratio for received Ping frames.  This is used in the responding PingAck frames.  Data related to both of these frame types are now reported better in RXO ProtocolMode.

##### New `nosound` audio interface option

`nosound` may be specified for the capture and playback audio devices to be used by ardopcf.  This is intended only for diagnostic/devlopment purposes.  Using these dummy devices prevent ardopcf from actually transmitting or receiving any audio, but still allows audio that would have been transmitted to be written to audio files using the `-T` or `--writetxwav` options.  When `nosound` is used for the playback device, none of the delays normally used to give the soundcard time to play the audio produced are implemented.  This, along with host commands provided via the `--hostcommands` option provide a way to quickly and efficiently produce WAV files of transmitted frames for testing purposes.

While implementing this feature, a bug was found and corrected that previously resulted in incorrect behavior if an invalid named sound device was specified.

##### Build system improvements

The Makefile used to build ardopcf is improved to better conform to standard usage, to correctly implement the clean option, and to eliminate the need for a separate makefile for each operating system.

##### New implementation of Reed Solomon Error Correction Coding

Reed Solomon Error Correction Coding is an important tool used by all Ardop implementations to allow received frames to be correctly decoded even if portions of the frame were intitially decoded incorrectly.  A different implementation of this feature is now used that has a simpler interface to the rest of the ardopcf source code and which does not impose additional undesirable licencing restrictions on ardopcf.

##### New TXFRAME Host command

A new TXFRAME command was added to the Host Interface.  This command allows a frame of any type with specified parameters to be transmitted on demand independent of the ARQ or FEC protocol rules.  This feature is intended only for debugginging and development purposes.  It is NOT intended for normal use by Host applications.  It may be removed or modified without notice in future releases of ardopcf.  Additional documentation for this feature is not provided other than comments in the source code.

##### Created Webgui Develeoper Mode

Developer Mode in the Webgui is NOT intended to be used during normal operation of ardopcf.  However, it provides a useful interface to ardopcf for diagnostic and debugging purposes.  This feature may be removed or modified without notice in future releases of ardopcf.  Webgui Developer Mode is enabled by specifying a negative port number following the `-G` or `--webgui` command line option.  The Webgui will then be available at the corresponding positive port number.  Developer Mode displays a single line text input box that can be used to send Host commands directly to ardopcf as if they were sent by a host program.  Additional data is also written to the Webgui log window in Developer Mode.  Knowledge of the Ardop host interface commands is required to make use of this feature.

##### Disable the CODEC host interface command

Using this host interface command would cause ardopcf to crash.  The purpose and usefulness of this command is not clear, and host applications that use ardopcf do not appear to use it.  So, this command was disabled.

##### Improved code consistency for logging between Linux and Windows

Some changes were made to the functions that support writing log files so that Linux and Windows builds are more consistent.

##### Change type of main() to int

The type of the main() functions in ALSASound.c and Waveout.c are changed from void to int.

##### Add/Restore license files for HIDAPI

The HIDAPI code in the lib/hid directory was missing license files that should have been there.  These license files were copied from http://github.com/signal11/hidapi.


### 2024.04.26: [ardopcf](https://github.com/pflarue/ardop) v1.0.4.1.1 from v2.0.3.2.1

##### Version Fix

Using version number v2.0.3.2.1 for the first release of **ardopcf** was a mistake.  This was due to a bug and misunderstanding of the ardop version numbering scheme.  So version v1.0.4.1.1 comes after and replaces v2.0.3.2.1.

##### More ASLA Rate Fix Data to Log

Write some additional information to the debug log (and console) if adjustments fail to correct ALSA configuration problems associated with symbol rate error when transmitting using certain hardware/OS combinations.  This may aid in fixing recently reported cases of some these adjustments failing.

##### Log PING Data in RXO ProtocolMode

Write data carried by PING frames to the debug log when in RXO (receive only) ProtocolMode, including when reading WAV files.  There appears to be a problem with either the encoding or decodeing of the data carried PING frames as described in a [message dated Apr 23, 2024](https://ardop.groups.io/g/users/message/5262) to the Ardop Users group.  This remains an open issue to be resolved.

##### Pad WAV For Decode

Simulate some silence at the start and end of a WAV file to be decoded with the `--decodewav` option.  Without the samples added to the end, a recorded frame that ended too close to the end of the WAV file might not be decoded.  Before this change, ardopcf would fail to decode WAV files created with the `--writetxwav` option.  Some silent samples were also added before the start of the WAV file, in case there might also be a problem identifying the start of the frame.

##### Print Copyright

Print a link to the github respository containing ardopcf, a copyright statement, and a link to the LICENSE file indicating that license details, including information about the authors of external libraries used and their licenses can be found there.  With the default ConsoleLogLevel and FileLogLevel settings, this will be written both to console and to the debug log file.

### 2024.04.01: [ardopcf](https://github.com/pflarue/ardop) v2.0.3.2.1 from [ardopc](https://github.com/g8bpq/ardop) v2.0.3.2

##### Name change

The instructions for compiling and linking found in Makefile (for Linux) and Makefile_mingw32 (for Windows) were changed to produce executables named **ardopcf** instead of **ardopc**. This is intended to clearly distinguish executables produced from this repository from those produced and distributed by John Wiseman.

The `ProductName` variable was also changed from `ardopc` to `ardopcf` so that the new name is printed to console and to log files upon starting ardopcf, and is reported to host programs in reponse to a `VERSION` command.

##### New LICENSE file

With the consent of Rick Muething KN6KB who wrote the ARDOP Specification and the original ardop implementation, and John Wiseman G8BPQ who created the multi-platform ardopc implementation which was forked to create ardopcf, ardopcf is now explicitly released under the MIT open source license.  While source code for some earlier implementations has been available on the Internet, the terms under which it could be used were never clearly stated.  The MIT license clearly states the generous terms under which anyone may now make use of this source code.  See the `LICENSE` file for details.

Note that there are some external libraries employed by ardopcf whose source code is found in subdirectories under the `lib` directory.  Files within each of these subdirectories identify the author of the library and the various open source license terms chosen by that author.  The release of the ardopcf source code under the MIT license does not apply to the external libraries in the lib directory.  All of those libraries include some sort of open source license that allows you to modify and redistribute them.  However, some include restrictions that differ from the MIT license applied to the ardopcf code.

##### Version number update

The version number was changed from v2.0.3.2 to v2.0.3.2.1, and definition of this version number was moved to a separate `version.h` file.  Moving this to a separate file is intended to make it more convenient to adjust the version number independent of changes to the source code in git tracked files.  This is useful when building binaries not intended for public release as a part of the development process.

##### Reorganization of source code

The source code for ardopc v2.0.3.2 by John Wiseman G8BPQ is available at https://github.com/g8bpq/ardop.  It is found in the `ARDOPC` and `ARDOPCommonCode` directories of that repository.  Additional directories in that repository contain variations on this source code.  Those additional directories have been removed from the repository for ardopcf at  https://github.com/pflarue/ardop.

The source code for ardopc and ardopcf both make use of libraries written by people other than the authors of ardop.  In the repository for ardopcf, the source code files for these libraries have been moved to a separate `lib` directory.  This separation allows the authors and licensing terms associated with those libraries to be more clearly identified.

##### Reduce delay decoding rx audio after tx

This fixes the problem of ptt being held on too long after TX such that the first part of the response is not heard and decoded.  Without this fix, some ardopc users could hear responses from stations they were trying to make contact with, but ardopc was unable to decode that audio because the first part of each frame was not heard.

##### Add alternative sliding DFT (sdft) based 4FSK demodulator

ardopc uses a 4FSK demodulator based on the Goertzel algorithm. While this is very computationally efficient and normally works well, this demodulator does not adapt to unknown variation in symbol timing.  Some ardop stations, at least some of which are running ardopc, are observed to transmit at a symbol rate as low as 49.8 baud rather than the expected 50 baud symbol rate.  The effects of this 0.4% symbol rate error accumulate over the length of a 4FSK frame, such that toward the end of a 4FSK.200.50S frame, the ardopc Geoertzel algorithm based demodulator attempts to determine the tone value well into the transition between adjacent symbols.  This results in a high probability of a demodulation error in the final symbols of the frame, and in frequent failure to correctly decode such frames.  Decoding of PSK transmissions from these stations with unexpected 4FSK symbol rates also have a high failure rate.

This change adds a new alternative sliding DFT (SDFT) based 4FSK demodulator to better handle such symbol timing errors.

4FSK modulation is used for the frame type of all ardop frames and for all short control frames. It is also used in the slowest but most robust data transfer frames. For the frame types and short control frames, even relatively severe symbol timing error is not usually a problem for the existing Goertzel based demodulator. However, when demodulating robust 4FSK.200.50S, 4FSK.500.100, and 4FSK.500.100S frames intended for poor channel conditions, the symbol timing error accumulates with each additional symbol and can cause an increase in demodulation error toward the end of the frame.

The new alternative SDFT demodulator requires slightly more computing power than the Goertzel algorithm, but it has the advantage of producing results for each sample heard (240 times per symbol for 50 baud 4FSK or 120 times per symbol for 100 baud 4FSK). These extra results can be used to monitor and adjust to the symbol timing. Of course, determination of symbol timing also requires additional computations. Thus, this SDFT demodulator may not be suitable for very low powered processors.

Additional use and testing of this new SDFT demodulator is required to verify its usefulness and identify any bugs.  So, for now, the `-s` or `--sdft` ardopcf command line arguments must be specified to employ the new alternative SDFT 4FSK demodulator. Otherwise, the original Goertzel algorithm based 4FSK demodulator will continue to be used.

Preliminary testing of the new SDFT demodulator shows that it allows correct demodulation of audio produced by some hardware/software combinations that cannot be correctly demodulated using prior versions of ardopc. Conditions in which it is inferior to prior versions of ardopc have not yet been identified.  The weakest processor tested so far, a first generation Raspberry Pi Zero, can handle the additional computation of the SDFT demodulator.

An equivalent demodulator for PSK modulated frames has not (yet?) been developed.  Thus, those frames are unlikely to be reliably demodulated if the transmitting station is sending at an unexpected symbol rate.  In that event, ardopcf should eventually downshift to use the 4FSK modulated frames that the alternative SDFT demodulator can reliably demodulate.

##### Fix TX symbol timing error due to bad ALSA configuration

Some hardware and software configurations that include ardopc on Linux machines were observed to produce transmissions with symbol timing errors sufficient to increase decoding errors.  At least one source of such errors was identified as an ALSA configuration error related to resampling of audio for hardware that does not directly support the 12kHz sample rate used by ardop. This error causes the the effective transmission sample rate to differ from the stated and desired rate of 12kHz. Since Windows implementations of ardop do not use ALSA, this error did not affect those systems.  This problem was confirmed to occur with ardopc running on some Raspberry Pi computers.  It is unknown whether it might also occur on other computers with Linux operation systems.

This fix detects and corrects the ALSA configuration errors that cause frames transmitted by this station to be difficult to correctly decode.  Thus, this fix should significantly improve the transmit performance of any station that was suffering from this ALSA configuration error.

This fix issues a warning notifying the user that this ALSA configuration error has been detected, and that an attempt is being made to reconfigure the system to fix the problem. If the problem cannot be fixed, ardopcf will issue an additional error message and reverts to the default ALSA configuration. The logic behind continuing even though the problem has not been fixed is that while the ALSA configuration error may make it more difficult for other stations to decode the transmissions, it may still be usable. Even marginally usable is better than not usable at all.  Since an example in which this problem is detected but cannot be fixed has not been identified, the code for this fallback condition remains untested.

It is not known how widespread these ALSA configuration errors were among Linux ardop stations. Nor is it clear whether all of the 4FSK symbol timing errors that have been observed, and which can be detected and measured using the SDFT demodulator, were due to this problem.

While this fix is enabled by default and should always be employed for normal use, the `-A` command line argument can be used to disable it.  This is may be useful for purposes of testing of other stations' ability to demodulate/decode stations transmitting without this fix.  This argument was useful while refining the alternative SDFT 4FSK demodulator.

##### Add `-v`/`-V` arguments to set console/file log levels

The ConsoleLogLevel and FileLogLevel variables in ardopcf control how verbose the console and the debug log file are. ConsoleLogLevel can be changed with the host CONSOLELOG command. FileLogLevel can be changed with the LOGLEVEL command. These features are not new.

The new `-v` or `--verboselog` command line arguments can be used to set the initial value of FileLogLevel with respect to the default of 6 (LOGINFO). The new `-V` or `--verboseconsole` command line arguments can be used to set the initial value of ConsoleLogLevel with respect to the default of 7 (LOGDEBUG). Thus, arguments of `-V -2 -v 1` would set ConsoleLogLevel two levels lower to a less verbose 4 (LOGWARNING) and FileLogLevel one level higher to a more verbose 8 (LOGDEBUGPLUS).

The default console and file log levels were also adjusted from their previous values. Many individual log commands were modified, and some new ones were added. These changes are intended to write less information to the console by default, while adding some key information that was not previously written, or which was hard to find because it was mixed in with too much detail that this author believes is better put only into the log file. The new command line arguments make it easier for users to choose the amount of information that they want to see. A normal user may want to see very little log information, while someone trying to debug a problem may want more detail.

While anyone can now change these log levels, the information written for any given log level cannot be changed without editing the source code and recompiling, which many users are probably not willing to do.  So, the author welcomes suggested changes to what information is written at the various log levels.

##### Use value from DRIVELEVEL host command to reduce output level.

For ardopc, the DRIVELEVEL host command sets a variable, but this variable is not used for anything. This feature for ardopcf uses the value, which must be an integer between 0 and 100, to linearly scale the amplitude of the transmitted audio. This variable is set to 100 if no DRIVELEVEL host command has been received, and this produces the same audio amplitude that was used prior to implementing this feature. Thus, the drive level defaults to maximum, but can be reduced with a host command.  Use of this host command may now be used to adjust the amount of transmit power and to reduce or avoid overdriving of the radio as evidenced by excessive ALC action.

##### Added `-n` argument to send 5 second twotone signal and exit.

While ardopc will send a 5 second two tone test signal in response to a TWOTONETEST command received from a host, this may be inconvenient or not supported by some hosts. So, using this `-n` command line argument provides an easy way to produce that same signal and then exit.  This may be useful for debugging a new installation or a hardware change.

##### Support Windows 32-bit and 64-bit builds

ardopc only has a Makefile for compiling on Linux. This feature adds Makefile_mingw32 to compile on a Windows PC using winlibs Intel/AMD 32-bit and 64-bit ([https://winlibs.com/](https://winlibs.com/)). It may also work for other Windows based c compiler systems, but these are the only ones that has been tested at this time.

In addition to creating a suitable Makefile, several minor changes to the source files were also required for ardopcf to compile cleanly for Windows.

##### Add ProtocolMode RXO (receive only)

The behavior of ardopc, and what heard frames it will attempt to decode, depends upon its ProtocolMode. The available modes for ardopc are ARQ and FEC. This new feature creates an additional RXO mode for ardopcf. This receive-only ProtocolMode attempts to decode all frames heard regardless of frame Type or Session ID. This is intended primarily as a tool to facilitate development and debugging of routines for demodulating and decoding. It may also be of interest to anyone interested in monitoring heard Ardop traffic between other stations.

While SessionID is not used by RXO mode to decide which frames to decode, its value is extracted and logged for all frames. This can be useful to indicate which frames are part of a session between the same stations/callsigns. Thus, if the corresponding Connect Request frame was previously decoded, then the Session ID can be used to determine the callsigns for the sending and receiving stations for later received frames.

Like ARQ and FEC modes, RXO mode may be set by a host using the PROTOCOLMODE host command. Current host applications are not configured to use this mode or issue the PROTOCOLMODE RXO command.  Results are logged to console and to the debug log file. They are also sent to connected hosts as STATUS messages.  Additional work may be required to further support any host applications choosing to make use of this mode.  Anyone interested to extending a host applications to support RXO mode is encouraged to contact the author to discuss requested changes.

The `-r` or `--receiveonly` command line arguments may also be used to start ardopcf in RXO mode. This allows it to write the content of heard frames to the console and the debug log file without using any host application. Note that most current host applications will override this setting and switch to ARQ or FEC mode upon connecting to ardopcf.

##### Write RX wav files

Add `-w` or `--writewav` argument to ardopcf to write audio received after transmitting to a WAV file.

A new audio file is opened after the end of a transmission, and remains open for 10 seconds. If another transmission ends before 10 seconds elapses, then keep the file open for 10 seconds from then, and so on. This is intended to capture all of the received audio for a connection to a given station into one file. No data is added to the file during transmission, only while receiving. Files are written to the log path if specified, else to the current directory. The port, date, and time are included in the filename.

This feature allows samples of received audio to be collected for development and debugging purposes.  The files produced by this option have a size of about 24kB per second of audio.

##### Write TX WAV files

Add `-T` or `--writetxwav` argument to ardopcf to write 12kHz WAV audio files containing the filtered TX audio for each transmitted frame. The audio files are written to the log path if specified, else to the current directory. The port, date, and time are included in the filename so that they can be easily associated with timestamps in the debug log file.

This is intended for development and debugging purposes. So, like the `-w` or `--writewav` arguments to write received audio to WAV files, this should not be activated for normal usage.  The files produced by this option have a size of about 24kB per second of audio.

##### Decode WAV file instead of listening to soundcard.

This feature allows ardopcf to attempt to demodulate and decode the contents of a 16-bit signed integer 12kHz mono WAV audio recording, and then exit. While processing the contents of such a recording, it uses the new RXO (receive only) ProtocolMode, thus attempting to decode all frames regardless of frame Type or SessionID. It writes information about the decoded frames to the console and debug log file.

The expected WAV file format matches that produced by the `-w` or `--writewav` option and the `-T` or `--writetxwav` option of ardopcf, which records the received and transmitted audio from an ardop session respectively.  Audio files from other sources may also be used.  If the parameters (mono vs stereo, sample rate, etc.) do not match, an error will be printed advising the user to consider using SoX to convert it.  This is a reference to https://sourceforge.net/projects/sox/, a multi-platform command line audio editing tool which can easily do such conversions.

This is intended primarily as a tool to facilitate development and debugging of routines for demodulating and decoding. It allows different versions of ardopcf, or the same version with different parameters, to process the same audio so that the output can be compared. It might also be used to process modified versions of the same audio file. As an example, this might be useful to test the impact of editing a recording to adjust the audio volume or to add noise.

Since the clock time markers in debug log files are not useful when decoding a WAV file, the log file also includes a reference to the approximate time offset from the start of the WAV file with each log message.

##### Add demoduated 4FSK tones to debug log

For deeper debugging purposes, write individual 1 carrier 4FSK tones sent and received to the debug log, including the tones used for Frame Type. Since this is a more detailed level of debugging data than most users might be interested in, this data is only written to the log file if the FileLogLevel is set to 8 (LOGDEBUGPLUS) .

In addition to writing the received demodulated tone values, the relative magnitudes of the four candidates are also written to the log. This allows the level of uncertainty for each tone to be determined from the log.

When logs from both the sending and receiving stations are available, comparison of these logs can provide insight into why certain frames failed to decode correctly. If the log from the sending station is not available, but a recording of the received audio is available in addition to the log from the receiving station, then comparing the logged tone values to a spectrogram of the audio recording may be able to provide similar insight.

This feature was instrumental in understanding the cause of poor frame decoding that led to development of the alternative SDFT demodulator and eventually to the fix for the ALSA configuration error.

When the alternative SDFT demodulator is used, the estimated symbol rate of demodulated 4FSK frames is also written to the log if FileLogLevel is set to LOGDEBUGPLUS.  This may be useful to identify transmitting stations using a version of ardopc that does not correct for ALSA configuration errors.  It may also help identify whether there are ardop stations whose symbol rate deviates from the expected values due to other unknown causes.  While all frames use 4FSK modulation to encode frame type, estimated symbol rate is only reported for those frames that also use 4FSK modulation for blocks of data.  This is because the received symbol rate can only be accurately estimated if averaged over a large number of symbols.

##### Print UTC time to console with session stats

Session statistics are printed to the console after every successful contact with a remote station.  Similar information is written to a session log file.  Unlike the data written to the log file, most information printed to the console is not time stamped.  This feature prints the UTC date and time to the console before printing the session stats since having this information displayed on the screen may be convenient for users who are manually logging these contacts.

##### Minor bugfixes

Fixed some minor bugs.
