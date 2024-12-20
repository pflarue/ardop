# Troubleshooting Suggestions

First, ensure that you are invoking ardopcf correctly.  See [USAGE_linux.md](USAGE_linux.md) or [USAGE_windows.md](USAGE_windows.md) for basic instructions to install, configure, and run **ardopcf**.

Then, review your logs to see if you can figure out why an issue is happening.

By default, ardopcf generates two sources of debug information
  - The console/tty/virtual terminal where you launched it
  - A log file in the directory where it was launched.


## Audio Device Related Issues (Linux)

If you get an error message in the console like this:
```
Opening Playback Device ARDOP Rate 12000
ALSA lib pcm.c:2664:(snd_pcm_open_noupdate) Unknown PCM ARDOP
cannot open playback audio device ARDOP (No such file or directory)
Error in InitSound().  Stopping ardop.
```
This means that you did not invoke ardopcf correctly (see the top of this page), or you did not (on Linux) specifiy an alsa rate slave device named ARDOP in `~/.asoundrc` or `/etc/asound.conf` like this: `pcm.ARDOP {type rate slave {pcm "hw:1,0" rate 48000}}`

### ALSA Device Sharing

You may run into issues as well (device or resource busy) if you have another program (like Direwolf) trying to access the same audio device (radio) at the same time. ardopcf is not a jack/pulseaudio/portaudio/oss/pipewire aware program, and alsa does not do mixing and audio redirecting between sources and sinks by default. You might be able to use alsa plugins like dsnoop or create a new pcm definition in ~/.asoundrc to do point to the PulseAudio default device, but your mileage may vary and this author has not tested it. If you find an elegant solution, please open a github issue or let us know in the users groups.


## Audio Device Related Issues (Windows)

Stub.

## Over-the-air Connection Issues

"Why can't I connect to any other ARDOP stations?"

Keep in mind that normal radio communications limitations apply, such as your local noise level, propagation, and it's up to you to determine

Barring any radio link quality related issues...

### Initiating a Connection

For Winlink nodes, often they have a single radio that scans multiple bands for multiple data modes. This takes time, which is why when dialing a winlink node with ARDOP, multiple connection requests are made. If their radios are already in another session, on another band, they may simply not hear you because they are busy.

Otherwise, if you know you have a good radio link (via voice or other data mode), there are a few things to check.

1. Is PTT working correctly?
   1. Between ardopcf commanding ptt and the radio transmitting must be less than 50 milliseconds. With CAT/Hamlib this usually isn't an issue.
   2. For VOX operation (not recommended) you can adjust the "LEADER" time with ardopcf's -H flag in order to allow for the PTT to open in time to actually transmit the frame.
2. Is the tx audio under/overdriven?
   1. Check your ALC meter on your radio. Adjust either computer audio or the gain on the radio where ALC is as low as possible, but your transmit power is as close to your setting as possible.
3. Are you on the correct frequency?
   1. The "center frequency" of ARDOP is 1500Hz in the audio passband. Sometimes radios will have an automatic dial frequency adjustment that will offset the display, or sometimes stations will communicate the "center" frequency instead of the "dial" frequency. Try offsetting by 1500Hz either direction, or adjust the offset in your radio menu.
4. Have you selected the appropriate bandwidth?
   1. If you have always selected 2000MAX as your session bandwidth, ardopcf will always try to reach these bandwidths (high data rates) and it may cause your connection to fail. Try 500MAX for a resonably robust connection with reasonable data transfer rates.

### Transferring Data

Sometimes an ARQ connection can be initiated, but no data transfer is ever made, and both stations eventually give up.

Sometimes correctly decoded FEC frames are simply ignored by the receiving station.

These may be bugs! Please check the user group as well as any open github issues, and try to include as much information as possible for volunteers to diagnose this.


## Community Support
If none of these help you, the best place to seek assistance is the ARDOP users group: https://ardop.groups.io/g/users

## Reporting Bugs

If you have discovered a bug (not just an issue with your own setup) - please create a github issue here with as much information as you can: https://github.com/pflarue/ardop/issues

Please be sure to include your log files.

## Terminology used in this document

| Term | Definition |
|------|------------|
| ALSA | Advanced Linux Sound Architecture, a software framework used for audio functionality in Linux. |
| ALC meter | Automatic Level Control meter, a meter on the radio that indicates the audio input level. |
| ARQ connection | Automatic Repeat reQuest connection, a reliable data transfer protocol. |
| ardopcf | The name of the program being discussed in this document. |
| audio passband | The range of frequencies that are used for audio transmission. |
| capturedevice | The audio device used for receiving. |
| CAT/Hamlib | Computer Aided Transceiver (CAT) and Hamlib, software interfaces for controlling amateur radio transceivers. |
| center frequency | The frequency used in software processing that specifies the middle of the audio passband (1500hz for ardop) |
| console/tty/virtual terminal | The interface where ardopcf is launched and displays debug information. |
| dial frequency | The actual frequency used for communication. |
| device or resource busy | An error that occurs when another program is already using the audio device. |
| dial frequency | The actual frequency used for communication. |
| dsnoop | An ALSA plugin that allows multiple programs to access the same audio device simultaneously. |
| FEC frames | Forward Error Correction frames, connectionless data frames that contains extra information used to correct errors in transmission. |
| jack/pulseaudio/portaudio/oss/pipewire | Different audio frameworks used in Linux. |
| pcm | Pulse Code Modulation, a method used to digitally represent analog audio signals. |
| playbackdevice | The audio device used for transmitting. |
| port | The port number used for the ardop-compatible tcp interface. |
| PTT | Push-to-Talk, a method used to control when the radio transmits. |
| ~/.asoundrc | A configuration file in Linux that specifies audio device settings. |
| /etc/asound.conf | A system-wide configuration file in Linux that specifies audio device settings. |
| user group | A community of users who discuss and support a specific software or technology, in this case on groups.io website, which is a lot like a mailing list. |
| VOX | Voice Operated eXchange, a method of transmitting where the radio automatically starts transmitting when it detects audio. |
| Winlink nodes | Nodes in the Winlink network, a global radio email system. |
