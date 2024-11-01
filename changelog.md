# Changelog

### 2024.10.31: [ardopcf](https://github.com/pflarue/ardop) v1.0.4.1.3 from v1.0.4.1.2

#### Improved documentation

Several new documentation files are added.  While there is still a lot of room for improvement in this documentation, it is much more complete and better organized than it was before.

Comments added to Makefile to help people build ardopcf from source.


#### Changes to Command Line Options

##### Eliminate several command line options

The last release of ardopcf, v1.0.4.1.2, introduced the `--hostcommands` command line option, which allows any host command to be executed at startup from the command line. Several command line options which duplicated functionality that could be achieved with `--hostcommands` were marked as deprecated. When those deprecated command line options were used, the user was informed of an equivalent `--hostcommands` argument and was informed that the deprecated option would be removed in a future release of ardopcf.

This change removes all obsolete command line options that are no longer needed because `--hostcommands` can achieve the same results.

This may require many users to reconfigure the command used to start ardopcf.

##### Complete rewrite of logging system

The functions used to write the debug log and session log are completely rewritten.  While this was done primarily to improve stability and maintainability, it also introduces some changes to both command line options and settings that may be provided via host programs.

The new `--nologfile` or `-m` command line option may be used to prevent the creation of log files.

The values used with the `LOGLEVEL` and `CONSOLELOG` host commands are inverted.  These commands may be used by host programs or from the command line via the `--hostcommands` or `-H` options.  Prior to this change, the argument for these commands used to be an integer in the range of 0 to 8 where a high number meant more detailed logging.  Now the argument for these commands is **an integer in the range from 1 to 6 where a LOWER number means MORE DETAILED logging**.

##### Handle multiple `--decodewav` options

The `--decodewav` or `-d` command line option provides the name of a WAV file to be decoded by ardopcf rather than listening for input from a sound device.  This is useful for development/debugging purposes.  Previously, only a single WAV file could be decoded each time ardopcf was run.  Now, this command line option can be repeated up to five times with different WAV filenames, or repeated copies of the same filename, and ardopcf will attempt to decode them as if they were received one after annother, with brief periods of silence between them.

This feature, along with the new INPUTNOISE host command, was important to the development and testing of the improved Memory ARQ feature and the changes to prevent discarding of FEC data frames of repeated frame type.


#### Other User interface changes

##### More widely usable binaries

Due to an improved understanding of the limitations on portability of compiled binaries, current and future releases of ardopcf will be intended to be usable with a wider range of computer operating system versions. The remaining limitations will be more clearly included in the release announcements.

##### Improved ID handling

A few different bugs were found that resulted in ardopcf not always sending a station ID frame when it should have.  These are corrected for improved legal compliance.

A side effect of these changes is that ardopcf will no longer transmit anything, including the 2-tone test signal, until the station ID has been set.  This is normally set by a host program such as Pat winlink or gARIM.  If you want to be able to send a 2-tone test signal for audio level adjustment or antenna tuning before starting the host program, you should set your callsign with the command line argument such as `--hostcommands "MYCALL AI7YN"`, but of course using your own callsign rather than AI7YN.

##### Improved WebGui display

Several changes are made to the data displayed by the WebGui.  These include displaying the Quality values encoded in ACK/NAK frames, displaying additional details about received Ping/PingAck frames, and dispaying (REPEATED TYPE) after frame types that are a repeat of the previously received data frame.  In ARQ sessions, receiving a repeated data frame type indicates that the sending station did not receive a DataACK.  In FEC mode it either indicates that the sending station is transmitting multiple copies for each data frame for improved robustness or that the second received FEC data frame is from a different station than the first.

Each time the station transmits, the Spectrum plot is now cleared and a white line is added to the waterfall display. The cleared spectrum supplements the "PTT" indicator on the Webgui to make it more obvious when the system is transmitting. This may be useful in situations in which the user can see the Webgui screen, but cannot see or hear indications from the radio that it is transmitting. Adding a white line to the waterfall leaves a clear indication in that plot of the breaks between periods of received audio.

##### Obey CW ID settings

Previously, the setting provided via the host program indicating whether a CW/Morse Code version of a station ID should be transmitted each time an ID frame is transmitted was not always obeyed.

This change ensures that this setting works as expected.

##### Eliminate explicit handling of CQ

While this doesn't appear to be in current usage (and maybe never was), ardopcf contained some code specific to sending a CQ connect request.  This code is removed.

##### New INPUTNOISE host command

Add the new INPUTNOISE host command which takes an integer argument. This argument sets the standard deviation of Gaussian white noise that is added to all incoming audio including audio from WAV files being decoded. If the incoming audio plus the noise exceeds the capacity of the signed 16-bit integers used for raw audio samples, then clipping will occur.

This new command is not intended to be used or useful during normal operation of ardopcf. Rather, it is another feature which is useful for diagnostic purposes.

#### Performance improvements

##### Improve decoding of repeated data frames.

Memory ARQ, as the term is used by ardopcf, refers to retaining data from a failed attempt to decode a data frame, and then using the combination of that retained data with data from a new resent copy of that same data frame to improve the likelihood of successfully decoding it.

In ARQ protocol mode, data frames are resent whenever the sending stations fails to receive a DataACK from the receiving station.  In FEC protocol mode, data frames may be sent multiple times to increase robustness by using the the FECREPEATS host command.

Previously this Memory ARQ was only partially implemented, and part of that implementation was broken.  Now, it is applied to all data frame types in ARQ, FEC, and RXO protocol modes.  This should significantly improve the likelihood of successfully decoding any data frame that is sent more than once.

##### Don't discard FEC data due to repeated frame type

Previously, if an FEC data frame was received that was of the same frame type as the last data frame, and that last data frame was successfully decoded, then the new frame was assumed to be a repeat and was discarded.  While this approach is acceptable during an ARQ session, it was inappropriate for FEC data, where new data may be received using the same frame type as the last frame.  This is most likely to occcur when receiving FEC data from more that one sending station.

With this change, a received data frame is only discarded if its frame type and its content matches the last data frame received.  This should make multi-station FEC nets/conversations more usable.

Unfortunately, this change, along with the full implementation of Memory ARQ introduces the possibility that data from failed attempts to decode different FEC data frames may be mistakenly combined and presented as data from a correctly decoded frame.  Features are included to reduce the likelihood of this occuring, but the possibility cannot be completely eliminated.  To further mitigate this deficiency, host level protocols can be implemented to detect such errors.  This may be appropriate when correctness of data reveived in FEC mode is very important.

Received FEC data that cannot be correctly decoded is passed to the host program with a flag that marks it as containing errors.  The host program chooses whether or not to display this data to the user, and how to distinguish it from correctly received data.  This feature can be useful when it is better to see received data with a few errors than to not see the data at all.  This change also ensures that such data with errors is passed to the host program in a more timely manner than would have been done previously.

##### Improve modulation of 16QAM.2000.100 data frames

Previously a bug in the function that encodes PSK and QAM data frame types made 16QAM.2000.100 frames difficult to decode even under perfect noise free conditions.

This change eliminates that bug, reducing the bit error rate while decoding these 16QAM.2000.100 data frames by about 30% under perfect noise free conditions.  As a result, these fastest but least robust data frames can now be reliably decoded under perfect conditions, and should be more likely to be useful under imperfect but still very good conditions.

Fixing this bug may also result in slight improvements to usability of the 2kHz bandwidth PSK frame types as well as the other multi-carrier 16QAM frame types. However, these improvements are minor compared to the improvements to the 16QAM.2000.100 frames.

This bug was fixed as a part of a reorganization of the functions that encode PSK/QAM data frames.  This change was intended to make these functions easier to understand and debug.

##### Pad unfilled frames with zeros

The Ardop spec (Appendix B, Definition of FrameData) requires that unfilled data frames be padded with zeros. This change undoes an earlier (mistaken) change that violated that requirement.  In some cases, restoring this use of zero padding may improve decoding performance.  It also opens up some opportunities for future optimization of decoding functions to make them more robust when unfilled data frames are received.

##### Always apply Reed Solomon error correction

Previously, rs_correct() was only used when the CRC check failed. During testing, a case was encountered in which the CRC check produced a false positive that would have been corrected by rs_correct(). This change prevents that from occurring again.

##### SDFT: Don't adjust timing based on weak signals

The optional Sliding DFT demodulator for 4FSK was written to greatly improve decode performance when the transmitting station is using a slightly incorrect symbol rate.  However, this was found to sometimes cause decreased performance under poor conditions when the symbol rate was correct.

This change avoids adjusting timing while demodulating 4FSK signals when the requirement for such asjustments is less certain.


#### Improvements to stability, reliability, and maintainability

Some of these changes remove code that was not being used, tested, or updated as other changes to ardopcf were being made.  By removing unused code from the repository, it makes the remaining code easier to understand and easier to maintain.

##### Introduce python based WAV testing

A new test script is added to in the `test/python` subdirectory. See the README file in that directory for additional details.

##### Reorganize repository layout

Before this change, the directory structure of the ardopcf code respository matched what was used by ardopc.  This structure, including the use of the `ARDOPC` subdirectory no longer makes sense for ardopcf.  So, this change creates a new more appropriate directory structure to hold the various source code files.

This restructuring also includes changes to the Makefile (now located in the top level directory) and to path references within various files.

##### Avoid ambiguous else statements

In a few place nested if/else blocks did not include brackets that would avoid ambiguity of which `if` an `else` was associated with.  Adding some additional brackets eliminiates this ambiguity.

##### Define named values for all non-data frame types

By using named values rather than integer frame type values in the source code, the code is easier to understand and debug.

##### Avoid undefined behavior and related code errors/crashes

As described in [Issue #37](https://github.com/pflarue/ardop/issues/37) ASAN and UBSAN options used when compiling ardopcf for testing purposes can help identify errors including those related to undefined behavior and invalid memory access.  Some such errors could cause ardopcf to crash unexpectedly.  These options are not used to build binaries for ardopcf releases because their use does impact performance.

In addition to helping to identify bugs whose fixes are described elsewhere in this changelog, they also helped identify several other minor bugs not specifically mentioned.

##### Introduce Cmocka test framework.

Until now, all testing of ardopcf to confirm that it works as expected has been ad-hoc.  Use of the Cmocka test framework allows more focused testing to confirm that specific features/functions work as they should.  Some tests have already been implemented, and this testing has helped to identify bugs that were then fixed.  Over time, additional testing will be implemented.  This is expected to help increase the overall quality of the source code, and to improve stability and reliability.

Compiling and running these tests requires installation of the cmocka library, but this is not required build and use ardopcf.

Many other changes described in this changelog also include additional/changed test procedures.  These changes are usually not mentioned in this changelog.

##### Rewrite callsign and location handling system

The code that validates callsigns and grid square locations and converts these between human readable strings and the compressed formats used for sending them over the air is completely rewritten.  This code is now more reliable and consistent.

##### More robust WebGui connection

Minor changes are made to improve the communication between ardopcf and the WebGui.

##### Finish removal of Packet mode

Prior to release v1.0.4.1.2, support for Packet mode was discontinued.  However, much of the code used to implement this mode was not completely removed.  This change removes that remaining unused code.

##### Remove support for Teensy and other microcontrollers

Earlier versions of ardopc and ardopcf included code specifically intended to support running on microcontrollers including Teensy.

In a message posted to the users group of ardop.groups.io on May 28, 2024, I announced my intention to discontinue support for Teensy and asked anyone who felt that this should not be discontinued to respond to that message. Lacking such responses, all support for Teensy and other microcontrollers is removed.

##### Remove SCS Host Interface

Earlier versions of ardopc and ardopcf included support for a serial host interface. This simulated an interface used by SCS Pactor modems, and was useful when ardopc was running on microcontrollers that did not support the primary TCP/IP host interface.

It may have also been useful when Ardop was under development, since it allowed use with host programs that supported Pactor, but did not yet support Ardop directly.

In a message posted to the users group of ardop.groups.io on May 28, 2024, I announced my intention to discontinue support for the this serial interface and asked anyone who felt that this should not be discontinued to respond to that message. Lacking such responses, support for a serial interface between ardopcf and host programs is removed.

ardopcf still retains code used to communicate with radio hardware via (actual or simulated) serial interfaces. For some ardopcf configuration options, this code is used to key (PTT) the radio and may be used to send other control codes to the radio.

##### Delete unused files

Several obsolete files are removed from the code repository.

Among the files deleted are several vcproj files as well as getopt.c and getopt.h. With the removal of these files, continued support for building ardopcf for Windows is more explicitly tied to the use of the MinGW-w64 system for GCC on 32 and 64 bit Windows.

##### Remove unused functions and code fragments

Before this change, the source files used to build ardopcf contained large amounts of unused source code. Much of this consisted of functions that were never referenced or execution branches whose conditions would never be met. Some of this code was intended to support transmit and receive of modulation types and baud rates incompatible with the v1 Ardop specification that ardopcf intends to maintain compatibility with.  Much of that code is removed.

##### Restore CalcTemplates.c

ardopcf (and ardopc before it) uses precalculated waveform sample values from ardopSampleArrays.c as the basis for data sent to the soundcard to transmit. CalcTemplates.c is intended to contain the source code that generated ardopSampleArrays.c. However, CalcTemplates.c had not been maintained and was not fully functional.

This change restores CalcTemplates.c so that it is now capable of producing newArdopSampleArrays.c containing data nearly identical to the existing ardopSampleArrays.c. The format is close, but not exactly the same. The data values are within +/- 2 (out of +/- 32k). By writing to a different file, the results can be compared before deciding whether or not to replace ardopSampleArrays.c with newArdopSampleArrays.c. Running CalcTemplates also prints some data comparing the new values to those found in ardopSamplesArrays.c. This is intended to help the developers to be confident that no unintended changes are being made.

The resulting newArdopSampleArrays.c is intended to be a temporary file that is either used to replace ardopSampleArrays.c or is deleted. It should not be included in the git repository. Therefore .gitignore is updated to exclude both this file and CalcTemplates, the compiled executable file. Instructions for building CalcTemplates with a single call to gcc is provided as a comment near the top of CalcTemplates.c.

Inspection of CalcTemplates.c allows the developers to see and understand the form of what is being transmitted much more easily than looking at the large arrays of numbers in ardopSampleArrays.c.

##### Whitespace cleanup

This change mostly cleans up a lot of whitespace issues in the repository. Included in the change is converting all line endings to Unix style.

As a result, this change affects a large percentage of the ardopcf code.  So, included with this highly disruptive change is also some manual cleanup. The manual changes also mostly affects whitespace, but some other stylistic changes are also included.

A .gitattributes file is added to the repository. This should help ensure that new line-endings problems are not introduced.

All developers are now strongly encouraged to avoid whitespace "errors" in their pull requests so that these don't need to be cleaned up before merging. These include whitespace at the ends of lines, spaces where tabs are expected, and blank lines that contain spaces and/or tabs.

##### Improve Linux signal handler safety

Make improvements to the handling of signals including CTRL-C that allows a user to terminate ardopcf.

This change only affects Linux systems.

##### Permit cross-compiling

Modify Makefile so as to facilitate cross compiling Windows binaries from Linux using MinGW.  This allows Linux developers who do not also have a Windows computer, to identify whether the changes they are making to ardopcf are incompatible with Windows.

The ability to cross comile for windows can be a useful tool for this, but it will not allow all changes that introduce problems for Windows users to be identified.  Building and testing on Window computers is still required.

Because testing of ardopcf on Windows computers will continue to use Windows binaries compiled on windows, rather than the results from cross compiling, the reliability of cross compiled binaries is less certain.

##### Fix unmatched va_start()/va_end()

Eliminate unmatched va_start()/va_end() in code for WebGui.  This avoids possible memory leaks.

##### Create new command line diagnostic host program

A new python based command line host program, diagnostichost.py, was developed to help understand and fix the problems with Memory ARQ and FEC protocol mode.

Like many ardopcf features, this tool is not intended to be useful for typical users of ardopcf. Rather, it is a tool that is useful to me, and that I think may be useful to others who are interested in working on ardopcf or a derivative of it, or who are interested in exploring how it works. So, I am including it in this repository.

For more details, see the [README](host/python/README.md) file provided for this host program.

##### Write command line to log file

When people need help with ardopcf, it is often useful to share a log file.  Including the full command line string used when ardopcf was started can assist the person trying to offer help.

##### New ardopcf.c and main()

This is a minor change to the structure of the source code.

##### Simplified handling of WebGui related files

Changed the location and handling the html and js source files used to implement the WebGui.  Also modified Makefile to eliminate temporary files created during the build process.  This prevents unnecessary files from being included in the ardopcf code repository and simplifies the build process.

##### Minor logging changes

Several changes are made which allow addditional details to be written to the log file or make log messages easier to understand.

##### Allow WebGui to display constellation data from log file.

In WebGui developer mode, constellation data that was written to a debug file (verbose level only) can be plotted by pasting it into the input box.  This can be useful during debugging since the graphical display of this constellation data is much easier to interpret than the printed values.

##### Bump product version from 1.0.4.1.2 to 1.0.4.1.3


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
