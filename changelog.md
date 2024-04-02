# Changelog

### 2024.03.28: [ardopcf](https://github.com/pflarue/ardop) v2.0.3.2.1 from [ardopc](https://github.com/g8bpq/ardop) v2.0.3.2

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

Additional use and testing of this new SDFT demodulator is required to verify its usefulness and identify any bugs.  So, for now, the -s or --sdft ardopcf command line arguments must be specified to employ the new alternative SDFT 4FSK demodulator. Otherwise, the original Goertzel algorithm based 4FSK demodulator will continue to be used.

Preliminary testing of the new SDFT demodulator shows that it allows correct demodulation of audio produced by some hardware/software combinations that cannot be correctly demodulated using prior versions of ardopc. Conditions in which it is inferior to prior versions of ardopc have not yet been identified.  The weakest processor tested so far, a first generation Raspberry Pi Zero, can handle the additional computation of the SDFT demodulator.

An equivalent demodulator for PSK modulated frames has not (yet?) been developed.  Thus, those frames are unlikely to be reliably demodulated if the transmitting station is sending at an unexpected symbol rate.  In that event, ardopcf should eventually downshift to use the 4FSK modulated frames that the alternative SDFT demodulator can reliably demodulate.

##### Fix TX symbol timing error due to bad ALSA configuration

Some hardware and software configurations that include ardopc on Linux machines were observed to produce transmissions with symbol timing errors sufficient to increase decoding errors.  At least one source of such errors was identified as an ALSA configuration error related to resampling of audio for hardware that does not directly support the 12kHz sample rate used by ardop. This error causes the the effective transmission sample rate to differ from the stated and desired rate of 12kHz. Since Windows implementations of ardop do not use ALSA, this error did not affect those systems.  This problem was confirmed to occur with ardopc running on some Raspberry Pi computers.  It is unknown whether it might also occur on other computers with Linux operation systems. 

This fix detects and corrects the ALSA configuration errors that cause frames transmitted by this station to be difficult to correctly decode.  Thus, this fix should significantly improve the transmit performance of any station that was suffering from this ALSA configuration error.

This fix issues a warning notifying the user that this ALSA configuration error has been detected, and that an attempt is being made to reconfigure the system to fix the problem. If the problem cannot be fixed, ardopcf will issue an additional error message and reverts to the default ALSA configuration. The logic behind continuing even though the problem has not been fixed is that while the ALSA configuration error may make it more difficult for other stations to decode the transmissions, it may still be usable. Even marginally usable is better than not usable at all.  Since an example in which this problem is detected but cannot be fixed has not been identified, the code for this fallback condition remains untested.

It is not known how widespread these ALSA configuration errors were among Linux ardop stations. Nor is it clear whether all of the 4FSK symbol timing errors that have been observed, and which can be detected and measured using the SDFT demodulator, were due to this problem.

While this fix is enabled by default and should always be employed for normal use, the -A command line argument can be used to disable it.  This is may be useful for purposes of testing of other stations' ability to demodulate/decode stations transmitting without this fix.  This argument was useful while refining the alternative SDFT 4FSK demodulator.

##### Add-v/-V arguments to set console/file log levels

The ConsoleLogLevel and FileLogLevel variables in ardopcf control how verbose the console and the debug log file are. ConsoleLogLevel can be changed with the host CONSOLELOG command. FileLogLevel can be changed with the LOGLEVEL command. These features are not new.

The new -v or --verboselog command line arguments can be used to set the initial value of FileLogLevel with respect to the default of 6 (LOGINFO). The new -V or --verboseconsole command line arguments can be used to set the initial value of ConsoleLogLevel with respect to the default of 7 (LOGDEBUG). Thus, arguments of -V -2 -v 1 would set ConsoleLogLevel two levels lower to a less verbose 4 (LOGWARNING) and FileLogLevel one level higher to a more verbose 8 (LOGDEBUGPLUS).

The default console and file log levels were also adjusted from their previous values. Many individual log commands were modified, and some new ones were added. These changes are intended to write less information to the console by default, while adding some key information that was not previously written, or which was hard to find because it was mixed in with too much detail that this author believes is better put only into the log file. The new command line arguments make it easier for users to choose the amount of information that they want to see. A normal user may want to see very little log information, while someone trying to debug a problem may want more detail. 

While anyone can now change these log levels, the information written for any given log level cannot be changed without editing the source code and recompiling, which many users are probably not willing to do.  So, the author welcomes suggested changes to what information is written at the various log levels.

##### Use value from DRIVELEVEL host command to reduce output level.

For ardopc, the DRIVELEVEL host command sets a variable, but this variable is not used for anything. This feature for ardopcf uses the value, which must be an integer between 0 and 100, to linearly scale the amplitude of the transmitted audio. This variable is set to 100 if no DRIVELEVEL host command has been received, and this produces the same audio amplitude that was used prior to implementing this feature. Thus, the drive level defaults to maximum, but can be reduced with a host command.  Use of this host command may now be used to adjust the amount of transmit power and to reduce or avoid overdriving of the radio as evidenced by excessive ALC action.

##### Added -n argument to send 5 second twotone signal and exit.

While ardopc will send a 5 second two tone test signal in response to a TWOTONETEST command received from a host, this may be inconvenient or not supported by some hosts. So, using this -n command line argument provides an easy way to produce that same signal and then exit.  This may be useful for debugging a new installation or a hardware change.

##### Support Windows 32-bit and 64-bit builds

ardopc only has a Makefile for compiling on Linux. This feature adds Makefile_mingw32 to compile on a Windows PC using winlibs Intel/AMD 32-bit and 64-bit ([https://winlibs.com/](https://winlibs.com/)). It may also work for other Windows based c compiler systems, but these are the only ones that has been tested at this time.

In addition to creating a suitable Makefile, several minor changes to the source files were also required for ardopcf to compile cleanly for Windows.

##### Add ProtocolMode RXO (receive only)

The behavior of ardopc, and what heard frames it will attempt to decode, depends upon its ProtocolMode. The available modes for ardopc are ARQ and FEC. This new feature creates an additional RXO mode for ardopcf. This receive-only ProtocolMode attempts to decode all frames heard regardless of frame Type or Session ID. This is intended primarily as a tool to facilitate development and debugging of routines for demodulating and decoding. It may also be of interest to anyone interested in monitoring heard Ardop traffic between other stations.

While SessionID is not used by RXO mode to decide which frames to decode, its value is extracted and logged for all frames. This can be useful to indicate which frames are part of a session between the same stations/callsigns. Thus, if the corresponding Connect Request frame was previously decoded, then the Session ID can be used to determine the callsigns for the sending and receiving stations for later received frames.

Like ARQ and FEC modes, RXO mode may be set by a host using the PROTOCOLMODE host command. Current host applications are not configured to use this mode or issue the PROTOCOLMODE RXO command.  Results are logged to console and to the debug log file. They are also sent to connected hosts as STATUS messages.  Additional work may be required to further support any host applications choosing to make use of this mode.  Anyone interested to extending a host applications to support RXO mode is encouraged to contact the author to discuss requested changes.

The -r or --receiveonly command line arguments may also be used to start ardopcf in RXO mode. This allows it to write the content of heard frames to the console and the debug log file without using any host application. Note that most current host applications will override this setting and switch to ARQ or FEC mode upon connecting to ardopcf.

##### Write RX wav files

Add -w or --writewav argument to ardopcf to write audio received after transmitting to a WAV file.

A new audio file is opened after the end of a transmission, and remains open for 10 seconds. If another transmission ends before 10 seconds elapses, then keep the file open for 10 seconds from then, and so on. This is intended to capture all of the received audio for a connection to a given station into one file. No data is added to the file during transmission, only while receiving. Files are written to the log path if specified, else to the current directory. The port, date, and time are included in the filename.

This feature allows samples of received audio to be collected for development and debugging purposes.  The files produced by this option have a size of about 24kB per second of audio.

##### Write TX WAV files

Add -T or --writetxwav argument to ardopcf to write 12kHz WAV audio files containing the filtered TX audio for each transmitted frame. The audio files are written to the log path if specified, else to the current directory. The port, date, and time are included in the filename so that they can be easily associated with timestamps in the debug log file.

This is intended for development and debugging purposes. So, like the -w or --writewav arguments to write received audio to WAV files, this should not be activated for normal usage.  The files produced by this option have a size of about 24kB per second of audio.

##### Decode WAV file instead of listening to soundcard.

This feature allows ardopcf to attempt to demodulate and decode the contents of a 16-bit signed integer 12kHz mono WAV audio recording, and then exit. While processing the contents of such a recording, it uses the new RXO (receive only) ProtocolMode, thus attempting to decode all frames regardless of frame Type or SessionID. It writes information about the decoded frames to the console and debug log file.

The expected WAV file format matches that produced by the -w or --writewav option and the -T or --writetxwav option of ardopcf, which records the received and transmitted audio from an ardop session respectively.  Audio files from other sources may also be used.  If the parameters (mono vs stereo, sample rate, etc.) do not match, an error will be printed advising the user to consider using SoX to convert it.  This is a reference to https://sourceforge.net/projects/sox/, a multi-platform command line audio editing tool which can easily do such conversions.

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
