# Using ardopcf on macOS

There are three main steps to using **ardopcf** on macOS. You need to obtain the `ardopcf` binary executable file, determine what command line options you need to use, and choose a convenient way to start it. The ways to start it discussed below include from a command line prompt using a shell script, from the macOS Applications folder, or from the Dock.

Unlike some programs such as [Pat](https://getpat.io) and [Hamlib rigctld](https://github.com/Hamlib/Hamlib) that may be suitable for automatically starting when you boot your Mac and leaving running all the time, **ardopcf** is not currently well suited to this mode of use.

Throughout this page, I assume that you know how to use basic Terminal commands like `cd`, `mkdir`, `cp`, `mv`, and `chmod`, and/or how to achieve similar results using the macOS Finder. I also assume that you know how to create and edit text files, and run a few command line programs. It is not necessary to have administrator access to build, install, and use **ardopcf**. Where appropriate, I will try to indicate what you might need to do differently if you do not have administrator access. If you do have administrator access, you may need to use `sudo` with some commands or an equivalent action if you are using the Finder.

## Getting `ardopcf`

If you want to try the latest changes that have been made to **ardopcf** since the last release, you can [build](BUILDING.md) it from source. If you are using macOS on an Intel Mac or Apple Silicon Mac, then you can also download one of the pre-built binary executable files from the [releases](https://github.com/pflarue/ardop/releases/latest) page at GitHub.

If you downloaded a pre-built binary executable, you should rename it from something like `ardopcf_amd64_Darwin_64` or `ardopcf_arm64_Darwin_64` to just `ardopcf`.

**ardopcf** requires only a single executable file, `ardopcf`, and it requires no special installation procedure. However, you probably don't want to run it from your Downloads directory (though you can if you want to). I recommend moving it to either `/usr/local/bin` or (especially if you don't have administrator access) `$HOME/bin`. This file must also be set as executable.

When you first run **ardopcf**, macOS may display a security warning because the downloaded binary is not code-signed by an identified developer. To run it anyway, go to System Preferences > Security & Privacy > General, and click "Allow Anyway" next to the message about ardopcf being blocked. Alternatively, you can right-click on the `ardopcf` file in Finder and select "Open" from the context menu, which will give you the option to run it despite the warning.

## Ardopcf PTT/CAT control

**ardopcf** has the ability to handle the PTT of your radio in a variety of ways, but other than activating PTT, it does not do other CAT control. (ardopcf does allow a host program to provide hex strings to pass to a CAT port, but since these strings must be appropriate for the specific model of radio that you are using, this feature is not generally used.) **ardopcf** can also allow a host program like [Pat](https://getpat.io) to handle PTT. With Pat, this is convenient because it can also use CAT control to set your radio's frequency. Other host programs, such as [ARIM](https://www.whitemesa.net/arim/arim.html) and [gARIM](https://www.whitemesa.net/garim/garim.html) do not have CAT or PTT capabilities, so they require **ardopcf** to handle PTT and require you to manually set the frequency on your radio. A third option is to use the VOX capability of your radio to engage the PTT. This may work OK for FEC mode operation with ARIM or gARIM, but can be unreliable for the ARQ mode operation used by Pat because it may not engage or disengage quickly enough.

> NOTE: As of July 20205 ARIM and gARIM were not tested as they are windows only applications.

If you want Pat to do PTT and CAT control then do not use any of the `-p`, `--ptt`, `-c`, `--cat`, `-g`, `-k`, `--keystring`, `-u`, `--unkeystring`, or `-g` options described in the remainder of this section. Instead, set `"ptt_ctrl": true` in the ardop section of the Pat configuration and use the `"rig"` setting to point to a rig defined in the `"hamlib_rigs"` section. The remainder of this section will assume that you want **ardopcf** to control PTT. Note that you should **NOT** try to have **ardopcf** do PTT using the same USB or serial device that you will ask PAT (via Hamlib/rigctld) or any other program use for CAT control.

**ardopcf** has a few methods available to controlling PTT. Serial Port RTS and CAT commands will be described here. It may also be possible to use PTT via a CM108 device or using a GPIO pin on an ARM computer like a Raspberry Pi. However, I haven't tried these myself, so I don't know for sure whether they work or know how to reliably configure them. I may add instructions for these methods to future versions of these instructions. It appears that a partial attempt was also made to allow ardopc to use Hamlib's rigctld to do PTT, but this is not currently usable (it might be usable in the future).

Both RTS and CAT PTT require the device name of the radio interface. On macOS, serial devices typically appear as `/dev/cu.usbserial-XXXXXX` or similar names in the `/dev/` directory. To find your radio interface device name, first disconnect it if it's already connected, then run:

`ls /dev/cu.*`

Note the devices that are present. Now connect your radio interface and run the command again:

`ls /dev/cu.*`

The new device that appears is your radio interface. Common patterns include `/dev/cu.usbserial-A12345`, `/dev/cu.SLAB_USBtoUART`, or `/dev/cu.wchusbserial1410`.

If the interface between your computer and your radio supports PTT control via RTS, this is the simplest solution. To use RTS PTT use the `-p /dev/cu.usbserial-XXXXX:BAUD` or `--ptt /dev/cu.usbserial-XXXXX:BAUD` option, but using the device name found above and `BAUD` for the required baud rate. If `:BAUD` is omitted, the default speed of 19200 baud is used. Often this default is acceptable. You can find the baud rate required for your radio interface in the manual for your radio or on the internet.

If RTS PTT does not work, you may be able to use CAT PTT. For this use the `-c /dev/cu.usbserial-XXXXX:BAUD` or `--cat /dev/cu.usbserial-XXXXX:BAUD` option, but using the device name found above and `BAUD` for the required baud rate. If `:BAUD` is omitted, the default speed of 19200 baud is used. Often this default is acceptable. You can find the baud rate required for your radio interface in the manual for your radio or on the internet. This only sets the CAT port and speed. It is also necessary to provide the actual cat commands as hex strings to key (`-k` or `--keystring`) and unkey (`-u` or `--unkeystring`) your radio. These will be specific to your radio model. So you will have to find them in your radio manual or on the internet.

For example, the Xiegu G90 can do PTT via CAT control with `-c /dev/cu.usbserial-A12345:BAUD --keystring FEFE88E01C0001FD --unkeystring FEFE88E01C0000FD`. Let's break down the unkeystring:

- `FEFE` is the fixed preamble to all CI-V commands
- `88` is the transceiver address expected by Xiegu radios
- `E0` is the controller address
- `1C` Command number is used for PTT/ATU control
- `00` sub-command sets PTT status
- `00` is the data for the sub-command: `00` releases PTT, `01` activates PTT
- `FD` indicates end of message.

For Kenwood, Elecraft, and TX-500 `-c /dev/cu.usbserial-A12345:BAUD --keystring 54583B --unkeystring 52583B` should work, which is the HEX values for 'TX;' and 'RX;'. These HEX strings are also reportedly suitable for the QDX and QMX radios, but those radios are not suitable for use with Ardop because they can only transmit a single tone at a time. I don't know what baud rate is required for these Kenwood or Elecraft, but the default works with Xiegu. Remember that `-c /dev/cu.usbserial-A12345` is equivalent to `-c /dev/cu.usbserial-A12345:19200` because **ardopcf** uses a default of 19200 baud if a value is not provided. While CAT control of PTT works with Xiegu, using `-p /dev/cu.usbserial-A12345` for RTS PTT is simpler and works just as well. With the [Digirig](https://www.digirig.net) interface that I use, another advantage of using RTS PTT is that it requires only the audio cable, allowing you to leave the serial cable disconnected.

## Starting **ardopcf** from the Command Line

Starting **ardopcf** from the command line is useful if you prefer the use of a Terminal window to GUI applications. For this use, a shell script containing all of the necessary command line options makes starting **ardopcf** easy. So, use a text editor to create `$HOME/bin/ardop` with contents similar to the following. See the [section](#ardopcf-audio-devices-and-other-options) on audio devices and other options for help understanding the ardopcf command line.

```bash
#!/bin/bash
ardopcf --logdir ~/ardop_logs -p /dev/cu.usbserial-A12345 -G 8514 --hostcommands "DRIVELEVEL 90" 8515 "Built-in Input" "Built-in Output"
```

Once you have created this file, use `chmod +x $HOME/bin/ardop` or an equivalent feature in Finder to make it executable.

**ardopcf** on macOS ignores SIGHUP. This means that if the Terminal window where **ardopcf** was started is closed, **ardopcf** will continue to run. This may be useful in some use cases. If this behavior is not desired, see the section below about creating macOS applications or dock shortcuts.

If you want to run several programs such as **ardopcf**, [Hamlib/rigctld](https://hamlib.github.io), and [Pat](https://getpat.io), then it may be useful to either run these programs in the background using a trailing ampersand (&), or to use a terminal multiplexer such as [screen](https://www.gnu.org/software/screen) or [tmux](https://github.com/tmux/tmux/wiki). One advantage of using a terminal multiplexer is that any output printed by each program is kept separate which may make it easier to interpret. If the programs are run in the background, it may be useful to redirect their output to a file (or even to /dev/null). Details for such options are beyond the scope of this document.

## Ardopcf audio devices and other options

Your Mac may have multiple audio input (Recording) and output (Playback) devices. **Ardopcf** must be told which of these devices to use. **ardopcf** is designed to work with macOS Core Audio devices, which should be available on all Mac systems.

Every time that **ardopcf** is started, it will print a list of all of the audio devices that it finds. This can be used to identify which devices should be used. So, run `ardopcf` with no other options from a Terminal to see the list of audio devices. If it does not exit after printing this list, press CTRL-C to kill it. This should print something that looks similar to:

```
ardopcf Version 1.0.4.1.3 (https://www.github.com/pflarue/ardop)
Copyright (c) 2014-2024 Rick Muething, John Wiseman, Peter LaRue
See https://github.com/pflarue/ardop/blob/master/LICENSE for licence details including
  information about authors of external libraries used and their licenses.
ARDOPC listening on port 8515
Capture Devices

Device 'Built-in Input' (Built-in Microphone)
  1 channel, sampling rate 44100..96000 Hz

Device 'USB Audio Device' (USB PnP Sound Device)
  1 channel, sampling rate 44100..48000 Hz

Playback Devices

Device 'Built-in Output' (Built-in Speakers)
  2 channels, sampling rate 44100..96000 Hz

Device 'USB Audio Device' (USB PnP Sound Device)
  2 channels, sampling rate 44100..48000 Hz

Using Both Channels of soundcard for RX
Using Both Channels of soundcard for TX
Opening Playback Device Built-in Output Rate 12000
cannot open playback audio device Built-in Output (Invalid argument)
Error in InitSound().  Stopping ardop.
```

In this case 'USB Audio Device' for both Capture and Playback devices is the [Digirig](https://www.digirig.net) interface that I use to connect to my Xiegu G90. If you are unsure which device represents the interface to your radio, compare the results of running `ardopcf` with and without your interface connected to your Mac.

The default audio devices that **ardopcf** uses are "Built-in Input" and "Built-in Output". To use different devices, you specify them by name on the command line. Device names with spaces should be enclosed in quotes.

To set the correct audio devices, you need to give **ardopcf** three command line options: port, capture device, and playback device. The port should normally be 8515, but another value can be used if this causes a conflict with other software. So, for my system using the USB audio interface, I would use:

`ardopcf 8515 "USB Audio Device" "USB Audio Device"`

This starts **ardopcf** and may be sufficient if you decided after reading the earlier section on PTT/CAT control that you do not need **ardopcf** to handle PTT because a host program like [Pat](https://getpat.io) will handle this or because you will use VOX. If you decided that you want **ardopcf** to handle PTT, then add those additional options. For example:

`ardopcf -p /dev/cu.usbserial-A12345 8515 "USB Audio Device" "USB Audio Device"`

or

`ardopcf -c /dev/cu.usbserial-A12345 --keystring FEFE88E01C0001FD --unkeystring FEFE88E01C0000FD 8515 "USB Audio Device" "USB Audio Device"`

There are some additional command line options that you might want to use. Other than port, capture device, playback device, the order in which the command line options are given does not matter. See [Commandline_options.md](Commandline_options.md) for info on all possible options.

A. By default, **ardopcf** writes log files in the directory where it is started. You can change where these files are created with the `-l` or `--logdir` option. For example, I created `$HOME/ardop_logs` and use `--logdir ~/ardop_logs`.

B. To enable the WebGui use `-G 8514` or `--webgui 8514`. This sets the WebGui to be available by typing `localhost:8514` into the navigation bar of your web browser. You may choose a different port number if 8514 causes a conflict with other software. The WebGui is likely to be useful when you adjust the transmit and receive audio levels as described later.

C. The `-H` or `--hostcommands` option can be used to automatically apply one or more semicolon separated commands that **ardopcf** accepts from host programs like [Pat](https://getpat.io). See [Host_Interface_Commands.md](Host_Interface_Commands.md) for more information about these commands. The commands are applied in the order that they are written, but usually this doesn't matter. As an example, `--hostcommands "MYCALL AI7YN"` sets my callsign to `AI7YN`. Pat will do this, so it isn't usually necessary as a startup option, but it is a convenient example. Because most commands will include a command, a space, and a value, you usually need to put quotation marks around the commands string. After you adjust your sound audio levels, you may discover that you want the **ardopcf** transmit drive level to be less than the default of 100%. `--hostcommands "MYCALL AI7YN;DRIVELEVEL 90"` would set my callsign and set the transmit drive level to 90%.

So, an example of the complete command you might want to use to start **ardopcf** is:

`ardopcf --logdir ~/ardop_logs -p /dev/cu.usbserial-A12345 -G 8514 --hostcommands "DRIVELEVEL 90" 8515 "USB Audio Device" "USB Audio Device"`

With this running, **ardopcf** is functional and ready to be used by a host program like [Pat](https://getpat.io). However, you probably don't want to type all of this every time you want to start **ardopcf**. So, the next sections describe some better options for starting **ardopcf**. All of them will use the sequence of options that you identified in this section.

## Adjusting your audio levels

Once you have confirmed that **ardopcf** is successfully connected to your radio, you need to adjust the audio transmit and receive levels.

In addition to making adjustments in **ardopcf** and with your radio, macOS's Audio MIDI Setup application can be used to adjust your computer's audio settings. You can find Audio MIDI Setup in Applications > Utilities, or by searching for it in Spotlight.

### Adjusting your transmit audio

If your transmitter has speech compression or other features that modify/distort transmit audio, these should be disabled when using any digital mode, including Ardop.

Your transmit audio level can be adjusted using a combination of your radio's settings, the macOS Audio MIDI Setup controls, and the **ardopcf** Drivelevel setting. Drivelevel can be set when starting **ardopcf** using the `DRIVELEVEL` command with the `--hostcommands` option or with the slider in the **ardopcf** WebGui. In theory, drivelevel can also be controlled from a host program like Pat, but I don't believe that any existing host programs provide this function.

Together these settings influence the strength and quality of your transmitted radio signal.

Reduced audio amplitude can be used to decrease the RF power of your transmitted signals when using a single sideband radio transmitter. In addition to limiting your power to only what is required to carry out the desired communications (for US Amateur operators this is required by Part 97.313(a)), reducing your power output may also be necessary if your radio cannot handle extended transmissions at its full rated power when using high duty cycle digital modes. While sending data, Ardop can have a very high duty cycle as it sends long data frames with only brief breaks to hear short DataACK or DataNAK frames from the other station.

If the output audio is too loud, your radio's Automatic Level Control (ALC) will adjust this audio before using it to modulate the RF signal. This adjustment may distort the signal making it more difficult for other stations to correctly receive your transmissions. Some frame types used by Ardop may be more sensitive to these distortions than others.

To properly configure these settings you need to know how to monitor and interpret your radio's ALC response and power output. On most radios a low ALC value means that the ALC is not distorting your signal. However, on other radios, including my Xiegu G90, a high ALC value means that the ALC is not distorting your signal. You should be able to find this information in the manual for your radio or on the internet. You can also determine it experimentally by adjusting the **ardopcf** drivelevel and Audio MIDI Setup output level both to very low values. Clicking on the `Send2Tone` button on the **ardopcf** WebGui should produce a low power and low distortion transmitted signal. Whatever your ALC shows in this condition is the desirable value. As you increase the **ardopcf** drivelevel and Audio MIDI Setup output level, the transmit power of your radio should increase. At some point, usually close to the configured power level of your radio, the transmitted signal will start to become more distorted. When this occurs, the ALC level will start to change toward a worse setting. As you continue to increase your audio settings, the power output should remain relatively constant, while the ALC setting will indicate a progressively worse value. You want to choose the combination of Drivelevel, Audio MIDI Setup output level, and radio settings that produce the desired amount of power without significantly distorting your audio (as indicated by too much change in the ALC indicator). How much change in the ALC indicator is "too much" may vary from radio to radio.

If your radio does not have an ALC indicator, but either it has a power output indicator or you have a separate power meter that you can use to measure transmitted power while making adjustments, you can also use this to choose appropriate transmit audio levels. If you slowly increase your audio level until the measured transmit power stops increasing, you have probably identified the audio level at which the ALC is starting to distort your signal. So, use an audio level that produces slightly less than the maximum measured transmit power.

On some (higher quality?) radios, suitable audio level settings are independent of the band/frequency and power level settings of your radio. On other radios, including my Xiegu G90, different bands require different audio settings, and reducing the power level setting also requires reducing the audio level. This appears to indicate that the power level setting of the Xiegu G90 simply causes it to engage the ALC at lower audio levels. My recommendation is that you choose radio settings and Audio MIDI Setup output settings that allow you to use only the **ardopcf** drivelevel slider to make ongoing adjustments (using the WebGui). I also recommend that you write down the radio and Audio MIDI Setup settings that work well so that if they get changed (intentionally or accidentally), you can quickly restore them to settings that you know should work well.

While transmit audio settings using the `Send2Tone` function are usually pretty good, monitoring of ALC and/or power level while sending actual Ardop data frames may indicate that further (usually minor) changes to transmit audio levels are appropriate.

If you set your radio for a higher power level than you intend to transmit at, and then use a reduced drivelevel to reduce your power output, then minor fluctuations are unlikely to engage the ALC causing any distortion. Using this approach, you really only need to be concerned about ALC and distortion if you are trying to use the full rated power of your transmitter.

### Adjusting your receive audio level

Normally, you should turn off the AGC function on your radio while working with digital signals, especially digital signals (including Ardop) that occupy only a small part of your radio's receive bandwidth. Instead, I use manual adjustments to RF gain as needed. AGC attempts to keep the average power level across the total receive bandwidth relatively steady. When using digital modes like the 200 Hz or 500 Hz Ardop bandwidths, there may be other signals within the receiver's bandwidth that are unrelated to the Ardop signal that I am trying to receive. Under these conditions, AGC may significantly alter the strength of the Ardop signal as an unrelated strong signal turns on or off.

Unlike transmit audio level which can be partially controlled through the **ardopcf** Drivelevel setting, there is no mechanism to control received audio level through **ardopcf**. Received audio level is controlled only by settings on your radio and the Audio MIDI Setup input settings.

Adjusting the receive audio level is also slightly more difficult than adjusting the transmit audio level because it depends on some signal being received by your radio. Conveniently, the received signal does not need to be an Ardop signal to do initial setup. Inconveniently, unlike your transmit audio settings, it may require adjustment each time you use **ardopcf** depending on noise level and band conditions.

The **ardopcf** WebGui `Rcv Level` indicates the instantaneous level of the audio being received. If you tune your radio to a quiet portion of a band with low background noise, this should be mostly or entirely grey, with perhaps only a little bit of a green signal indicator near the left edge. Under these conditions, if the **ardopcf** WebGui shows a yellow `(Low Audio)` warning next to the `Rcv Level` graph, that is OK.

When you receive a strong signal or when the background noise is high, that same indicator should be up to about half green, or even more. Luckily, **ardopcf** can handle a relatively large range of acceptable audio levels, and seems to decode relatively low audio volume signals reasonably well if the signal is stronger than the background noise. Too loud of a received signal is more likely to cause problems than too soft of a received signal. So, if in doubt, reduce the receive audio level a bit. With the current popularity of FT-8, tuning to one of the FT-8 frequencies is often a convenient way to receive a relatively strong signal, which is actually the combination of several simultaneous FT-8 signals. These signals are also usually steady on for about 12 seconds, then off for a few seconds, and then repeat this pattern. So, if you tune your radio to an active FT-8 frequency, you can often adjust the audio settings so that the `Rcv Level` graph peaks at about 50% green in each 15-second period and then drops down to near zero in the gaps between these transmissions. That is often a good initial audio setting. If the `Rcv Level` graph turns orange, the audio is too loud. It is important that you pay attention to the `Rcv Level` graph and not the colors of the signals appearing on the waterfall graph. The waterfall graph has a slow but strong automatic gain control feature that tries to always show high contrast between any received signals and the background noise independent of the total audio volume.

While these settings based on received FT-8 are a good starting point, watching the `Rcv Level` graph while receiving actual Ardop transmissions may indicate that you should make further adjustments to your receive audio levels. As with adjusting drivelevel settings, I recommend that you choose one control element that you will use for final adjustment of receive audio levels, and set any other controls (on the radio and/or computer) to fixed values. Making quick adjustments to more than one control are not practical.
