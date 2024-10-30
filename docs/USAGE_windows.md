# Using ardopcf on Windows

There are three main steps to using **ardopcf** on Windows.  You need to obtain the `ardopcf.exe` file, determine what command line options you need to use, and finally create a Desktop Shortcut (and perhaps pin it to your taskbar) to start **ardopcf**.  You might also choose to create a Desktop Shortcut for the ardopcf WebGui (and perhaps pin it to your taskbar)

## Getting `ardopcf.exe`
If you want to try the latest changes that have been made to **ardopcf** since the last release, you can [build](BUILDING.md) it from source.  However, most Windows users will download one of the pre-built binary executable files from the [releases](https://github.com/pflarue/ardop/releases/latest) page at GitHub.  Before you can do either of these, you need to know whether your computer is running 32-bit or 64-bit Windows.  All Windows 11 and most Windows 10 operating systems are 64-bit.  To find out if a Windows 10 operating system is 32 or 64 bit press the **Start** button and select **Settings**, click on **System** then on **About** and read the line starting with **System type**.  If you are using a version of Windows older than Windows 10, you will also need to install the [Universal C Runtime update](https://support.microsoft.com/en-us/topic/update-for-universal-c-runtime-in-windows-c0514201-7fe6-95a3-b0a5-287930f3560c).

If you downloaded a pre-built binary executable, you should rename it from `ardopcf_amd64_Windows_64.exe` or `ardopcf_amd64_Windows_32.exe` to just `ardopcf.exe`.

**ardopcf** requires only a single excutable file, `ardopcf.exe`, and it requires no special installation procedure.  However, you probably don't want to run it from your Downloads directory (though you can if you want to).  You might consider creating a directory for ardop such as `C:\Users\<USERNAME>\ardop` and maybe also create `C:\Users\<USERNAME>\ardop\logs` to hold the log files that **ardopcf** creates.

The first time that you run `ardopcf` without the `-h` option, Windows will ask you whether you want to allow public and private networks to access this app.  Host programs like [Pat](https://getpat.io) use a TCP "network" connection to work with **ardopcf**.  Without a program like Pat, **ardopcf** isn't useful.  Also, the **ardopcf** WebGui connects from a browser window to **ardopcf** using a TCP "network" connection.  If you choose `Allow` in this windows dialog, then you will be able to use the Webgui in a browser on any computer on your local network and/or run a host program like Pat on any computer on your local network.  If instead you choose 'Cancel' in this windows dialog, then these programs/features will still work, but only if they are all running on the same computer.  If you do not have admin privledges on this Windows computer, then `Allow` won't work.  On a Windows computer this usually isn't a problem since you probably intend to run everything on this one computer anyway.  The ability to run **ardopcf** on one computer and a host or the WebGui on another is more likely to be useful when **ardopcf** is running on a small computer like a Raspberrry Pi that does not have a monitor conencted to it.

## Ardopcf PTT/CAT conrol

**ardopcf** has the ability to handle the PTT of your radio in a variety of ways, but other than activating PTT, it does not do other CAT control.  (ardopcf does allow a host program to provide hex strings to pass to a CAT port, but since these strings must be appropriate for the specific model of radio that you are using, this feature is not generally used.)  **ardopcf** can also allow a host program like [Pat](https://getpat.io) Winlink to handle PTT.  With Pat, this is convenient because it can also use CAT control to set your radio's frequency.  Other host programs may not have CAT or PTT capabilities, so they require **ardopcf** to handle PTT and require you to manually set the frequency on your radio.  A third option is to use the VOX capability of your radio to engage the PTT.  This may work OK for FEC mode operation with some host programs, but can be unreliable for the ARQ mode operation used by Pat because it may not engage or disengage quickly enough.

If you want Pat to do PTT and CAT control then do not use any of the `-p`, `--ptt`, `-c`, `--cat`, `-g`, `-k`, `--keystring`, `-u`, or `--unkeystring` options described in the remainder of this section.  Instead, set `"ptt_ctrl": true` in the ardop section of the Pat configuration and use the `"rig"` setting to point to a rig defined in the `"hamlib_rigs"` section.  The remainder of this section will assume that you want **ardopcf** to control PTT.  Note that you should **NOT** try to have **ardopcf** do PTT using the same COM port that you will ask PAT (via Hamlib/rigctld) or any other program to use for CAT control.  It doesn't work for two programs to use the same COM port at the same time.

**ardopcf** has a few methods available for controlling PTT.  Serial Port RTS and CAT commands will be described here.  It may also be possible to use PTT via a CM108 device, but I'm currently unsure whether this actually works.  It appears that a partial attempt was also made to allow ardopc to use Hamlib's rigctld to do PTT, but this is not currently usable (it might be usable in the future).

Both RTS and CAT PTT require the COM port number of the radio interface.  Some radio interfaces may require installing a driver for the COM port to be usable.  See your radio or interface manual for more details.  To find the COM port number for your radio interface, open the Windows Device Manager by right clicking on the Windows Start button and then on Device Manager.  With the Device Manager open, if only one COM port is shown when the radio interface is connected, this is likely the one you want to use.  If there is more than one port shown, or to be certain that the only one shown is correct, unplug and reconnect the radio interface and watch which COM port disappears and reappears.

If the interface between your computer and your radio supports PTT control via RTS, this is the simplest solution.  To use RTS PTT use the `-p COMX:BAUD` or `--ptt COMX:BAUD` option where `X` is the COM port number found using Device Manager and `BAUD` is the port speed.  If `:BAUD` is ommitted, the default speed of 19200 baud is used.  Often this default is acceptable.  You can find the baud rate required for your radio interface in the manual for your radio or on the internet.

If RTS PTT does not work, you may be able to use CAT PTT.  For this use the `-c COMX:BAUD` or `--cat COMX:BAUD` option where `X` is the COM port number found using Device Manager and `BAUD` is the port speed.  If `:BAUD` is ommitted, the default speed of 19200 baud is used.  Often this default is acceptable.  You can find the baud rate required for your radio interface in the manual for your radio or on the internet.  This only sets the CAT port and speed.  It is also necessary to provide the actual cat commands as hex strings to key (`-k` or `--keystring`) and unkey (`-u` or `--unkeystring`) your radio.  These will be specific to your radio model.  So you will have to find them in your radio manual or on the internet.

For example, my Xiegu G90 can do PTT via CAT control with `-c COM5 --keystring FEFE88E01C0001FD --unkeystring FEFE88E01C0000FD`.  For this radio `FEFE` is the fixed preamble to all CI-V commands, `88` is the transceiver address expected by Xiegu radios, and `E0` is the controller address.  Then command number `1C` is used for PTT/ATU control, sub-command `00` means set PTT status, data = `01` means press PTT while `00` means release PTT, and `FD` means end of message.  For Kenwood, Elecraft, and TX-500 `-c COM5 --keystring 54583B --unkeystring 52583B` should work, which is the HEX values for 'TX;' and 'RX;'.  These HEX strings are also reportedly suitable for the QDX and QMX radios, but those radios are not suitable for use with Ardop because they can only transmit a single tone at a time.  I don't know what baud rate is requried for these Kenwood or Elecraft, but the default works with my Xiegu.  Remember that `-c COM5` is equivalent to `-c COM5:19200` because **ardopcf** uses a default of 19200 baud if a value is not provided.  While CAT control of PTT works for my Xiegu, using `-p COM5` for RTS PTT is simpler and works just as well.  With the [Digirig](https://www.digirig.net) interface that I use, another advantage of using RTS PTT is that it requires only the audio cable, so I can leave the serial cable disconnected.

## Ardopcf audio device selection and other options.

Your Windows computer may have multiple audio input (Microphone/Recording) and output (Speakers/Playback) devices.  **Ardopcf** must be told which of these devices to use.

The windows audio device numbers may change as (USB) devices are plugged in or unplugged.  For a radio with a built in sound interface, they may also change when you turn that radio on and off.  They may also change if you use the Windows sound controls to change which devices are identified as defaults.  So, before continuing, be sure that your radio is connected to the computer and turned on.

Windows has two 'default' audio devices each for Playback and Recording.  These are 'Default Device' and 'Default Communications Device'.  Your computer will usually play media like music and videos as well as various alert sounds through the 'Default Device'.  It will use the 'Default Communications Device' for audio from calls such as when using Zoom.  You don't want either of these to use your radio's sound interface.  So, make sure that your radio audio interface is not set as any of these defaults.  To do that:
1. On Windows 11, click the Windows Start button and type `Control Panel` into the search bar.  On older versions of Windows, click the Windows Start button and find `Control Panel`, probably in the Windows System folder
2. If you seen `Sound`, click on it.  If not, you may need to click on `Hardware and Sound` and then on `Manage Audio Devices`.
3. In the 'Playback Tab' identify your radio interface.  Often it will be labeled something like 'USB PnP Sound Device'.  If you are unsure which device it is, you can unplug and replug the connection to your radio and see which device disappears and comes back.
4. If there is a green check mark next to this device with a label of 'Default Device' or 'Default Communications Device', then right click on the device that should be the default, then on `Set as Default Device` and `Set as Default Communications Device`.
5. Repeat steps 3. and 4. in the 'Recording Tab'.
6. Try turning your radio on and off, or plugging and unplugging it from your computer to verify that now it never shows up as a default device.
7. Once **ardopcf** is running, you'll need to return to this Sound dialog to make some adjustments.

Every time that **ardopcf** is started, it will print a list of all of the audio devices that it finds.  This can be used to identify which device numbers should be used.  These numbers might be different than they would have been before you set your radio not to be the default device.

1. Open a Windows Command Prompt.  On Windows 11, this can be done by pressing the Windows Start button and typing `Command Prompt` into the search bar.  On older versions of Windows it is probably available in the Windows System folder after pressing the Windows Start button.
2. Use `cd` to navigate to the location of ardopcf.exe.  For example:
`cd /D C:\Users\<USERNAME>\ardop`
3. Type `ardopcf` with no other options and press Enter to see the list of audio devices.

The first time that you run `ardopcf` without the `-h` option, Windows will ask you whether you want to allow public and private networks to access this app.  Host programs like [Pat](https://getpat.io) use a TCP "network" connection to work with **ardopcf**.  Without a program like Pat, **ardopcf** isn't useful.  Also, the **ardopcf** WebGui connects from a browser window to **ardopcf** using a TCP "network" connection.  If you choose `Allow` in this windows dialog, then you will be able to use the Webgui in a browser on any computer on your local network and/or run a host program like Pat on any computer on your local network.  If instead you choose 'Cancel' in this windows dialog, then these programs/features will still work, but only if they are all running on the same computer.  If you do not have admin privledges on this Windows computer, then `Allow` won't work.  On a Windows computer this usually isn't a problem since you probably intend to run everything on this one computer anyway.  The ability to run **ardopcf** on one computer and a host or the WebGui on another is more likely to be useful when **ardopcf** is running on a small computer like a Raspberrry Pi that does not have a monitor conencted to it.

Running `ardopcf` with no other options should print something that look similar to:
```
ardopcf Version 1.0.4.1.2 (https://www.github.com/pflarue/ardop)
Copyright (c) 2014-2024 Rick Muething, John Wiseman, Peter LaRue
See https://github.com/pflarue/ardop/blob/master/LICENSE for license details including
  information about authors of external libraries and their licenses.
Capture Devices
0 Microphone Array (Intel Smart
1 Microphone (USB PnP Sound Devic
Playback Devices
0 Speakers (Realtek(R) Audio)
1 Speakers (USB PnP Sound Device)
ardopcf listening on port 8515
Setting Protocolmode to ARQ.
```

Press Ctrl-C to stop **ardopcf**.

The list of Capture and Playback devices should be similar to what you saw in the Sound control dialog in the Recording and Playback tabs.  What is important is the numbers that **ardopcf** shows before each device.  In this example, the USB PnP devices represent the connection to my radio.  In this case, each is assigned number `1`.  Running **ardopcf** without any options defaults to using the devices labeled `0`.  To set the correct device you need to give **ardopcf** three command line options: port, capture device, and playback device.  The port should normally be 8515, but another value can be used if this causes a conflict with other software.  So, for my system, I would use:

`ardopcf 8515 1 1`

This starts **ardopcf** and may be sufficient if you decided after reading the earlier section on PTT/CAT control that you do not need **ardopcf** to handle PTT because a host program like [Pat](https://getpat.io) will handle this or because you will use VOX.  If you decided that you want **ardopcf** to handle PTT, then add those additional options.  For example:

`ardopcf -p COM5:19200 8515 1 1`

or

`ardopcf -c COM5 --keystring FEFE88E01C0001FD --unkeystring FEFE88E01C0000FD 8515 1 1`

4. There are some additional command line options that you might want to use.  Other than port, capture device, playback device, the order in which the command line options are given does not matter.

A.  By default **ardopcf** writes log files in the directory where it is started.  You can change where these files are created with the `-l` or `--logdir` option.  For example, I created an `ardop_logs` directory and use `--logdir C:\Users\<USERNAME>\ardop\ardop_logs`.

B.  To enable the WebGui use `-G 8514` or `--webgui 8514`.  This sets the WebGui to be availble by typing `localhost:8514` into the navgation bar of your web browser.  You may choose a different port number if 8514 causes a conflict with other software.  The WebGui is likely to be useful when you adjust the transmit and receive audio levels as described later.

C.  The `-H` or `--hostcommands` option can be used to automatically apply one or more semicolon separated commands that **ardopcf** accepts from host programs like [Pat](https://getpat.io).  See [Host_Interface_Commands.md](Host_Interface_Commands.md) for more information about these commands.  The commands are applied in the order that they are written, but usually this doesn't matter.  As an example, `--hostcommands "MYCALL AI7YN"` sets my callsign to `AI7YN`.  Pat will do this, so it isn't usually necessary as a startup option, but it is a convenient example.  Because most commands will include a command, a space, and a value, you usually need to put quotation marks around the commands string.  After you adjust your sound audio levels, you may discover that you want the **ardopcf** transmit drive level to be less than the default of 100%.  `--hostcommands "MYCALL AI7YN;DRIVELEVEL 90"` would both set my callsign and set the transmit drive level to 90%.

So, an example of the complete command you might want to use to start **ardopcf** is:

`ardopcf --logdir C:\Users\<USERNAME>\ardop\ardopc_logs -p COM5 -G 8514 --hostcommands "DRIVELEVEL 90" 8515 1 1`

With this running, **ardopcf** is functional and ready to be used by a host program like [Pat](https://getpat.io).  However, for windows users, it is probably more convenient to create a Desktop Shortcut to start **ardopcf** each time you want to use it.  For **ardopcf** to work well, you will also probably need to make adjustments to your receive and transmit audio levels.


## Create a Windows Desktop Shortcut

Unlike some programs such as [Pat](https://getpat.io) and [Hamlib rigctld](Hamlib_Windows11.md) that may be suitable for automatically starting when you log on to Windows and leaving running all the time, **ardopcf** is not currently well suited to this mode of use.  However, creating a Desktop Shortcut to start it is appropriate.  **ardopcf** should then be stopped with CTRL-C or by closing the window that it is running in when you are finished using it.

The following is for Windows 11.  Windows 10 is very similar.
1. Find 'ardopcf.exe' with Windows File Explorer, Right click on it, Select `Show more options`, Select `Create shortcut`.  This will create a new file in the same directory as 'ardopcf.exe' with the name 'ardopcf.exe - Shortcut'.
2. Move 'ardopcf.exe - Shortcut' to your Desktop by dragging it either to a blank area on your desktop or to where 'Desktop' appears in the tree panel of Windows File Explorer.  If offered a choice to 'copy' or 'move' the shortcut, choose 'move'.
3. Configure the shortcut: Right click on the shortcut icon on your desktop, Select `Properties`.  Add the command line options to set the port, audio devices, PTT options, enable the WebGui, etc. after 'ardopcf.exe' in the `Target` line.  Note that this line begins with the full path to 'ardopcf.exe', so that it does not need to be in any special location.  If there is a space anywhere in the path to 'ardopcf.exe', then this will be wrapped in double quotation marks.  In that case, the command line options are placed after the closing quotation marks.  For example:

`C:\Users\<USERNAME>\ardop\ardopcf.exe -G 8514 --logdir C:\Users\<USERNAME>\ardop\logs --hostcommands "MYCALL AI7YN;DRIVELEVEL 85" 8514 1 1`

or

`"C:\Users\<USERNAME>\Documents\radio stuff\ardop\ardopcf.exe" -G 8514 --logdir "C:\Users\<USERNAME>\Documents\radio stuff\ardop\logs" --hostcommands "MYCALL AI7YN;DRIVELEVEL 85" 8514 1 1`

Change the `Run:` pulldown to `Minimized`.  If you are not using the '-l' or '--logdir' option to control where the ardop log files will be written, you may also want to change the `Start in` line, since that is where the log files will go.  If you want to, you can also click on `Change Icon` and select a different icon to change the appearance of the shortcut on your desktop.  By switching to the `General` tab, you can also change the shortcut's label if you want to.  Finally, click on `OK`.

4. Double clicking on this shortcut will start **ardopcf** with all of the appropriate options.  When you are done using **ardopcf**, you can either right click on the icon in your task bar and then click `Close Window`, or you can left click on the icon in your task bar to open the window, and then either type CTRL-C or click on the red X in the upper right corner of the window.

5. (OPTIONAL) After **ardopcf** has been started using this Desktop Shortcut, you can also right click on the icon for it in the taskbar and then click `Pin To Taskbar`.  This will leave this icon in the taskbar when **ardopcf** is not running, allowing it to be started with a single click.

## Create a Desktop shortcut to open the ardopcf WebGui

For this to work, **ardopcf** must have been started with the option `-G port` or `--webgui port` option, where `port` is usually 8514.

1. Start **ardopcf**.
2. Open your web browser and type `localhost:port` into the navigation bar, where `port` is the number you used with the `-G` or `--webgui` option (usually 8514) and press Enter.  This should display the ardopcf WebGui.
3. Drag the icon left of the `localhost:port` to a blank spot on your desktop.  This will create a desktop shortcut for the WebGui.
4. (OPTIONAL) You can also create a bookmark for the WebGui address to make it easier to open this page if your web browser is already running.  This is done by clicking the star to the right of the navigation bar in your browser while you are viewing the WebGui.

## Create a .cmd or .bat to run ardopcf from a command prompt

If you want to run ardopcf from a command prompt rather than using a Desktop Shortcut, then you probably know how to create a `.bat` or `.cmd` script.  This simple text file would include the same command text that would be used on the `Target` line of a Desktop Shortcut, including the full path to ardopcf.exe.  It could then be run from the command prompt by typing the name of this script.

## Multiple configurations.

If you sometimes use different radios or different radio interfaces, you can create multiple copies of your Desktop Shortcut (or script file), each with different audio device and/or PTT settings.  You can also use this approach if you sometimes use **ardopcf** with a program like [Pat](https://getpat.io) that does CAT control and PTT, but also sometimes use another host program that does not do PTT control, so that **ardop** must be configured to do PTT.


## Adjusting your audio levels

Once you have confirmed that **ardopcf** is successfully connected to your radio, you need to adjust the audio transmit and receive levels.

### Adjusting your transmit audio.

If your trasmitter has speech compression or other features that modify/distort transmit audio, these should be disabled when using any digital mode, including Ardop.

Before adjusting the transmit audio levels, there are some general audio output settings to configure.
1. On Windows 11, click the Windows Start button and type `Control Panel` into the search bar.  On older versions of Windows, click the Windows Start button and find `Control Panel`, probably in the Windows System folder
2. If you see `Sound`, click on it.  If not, you may need to click on `Hardware and Sound` and then on `Manage Audio Devices`.
3. In the 'Playback' tab identify your radio interface.  Often it will be labeled something like 'USB PnP Sound Device'.  If you are unsure which device it is, you can unplug and replug the connection to your radio and see which device disappears and comes back.
4. Double click on the device representing your radio interface.  The 'Levels' tab is where you can adjust the 'Speakers' slider and 'Balance' setting as may be required later in this section.  On the 'Enhacements' tab, set 'Disable all enhancements'.  On the 'Spatial sound' tab, set the 'Spatial sound format' to Off.  Click on 'Apply'.  These settings will prevent Windows from manipulating your audio before passing it to your radio.  These manipulations are often applied by default since they might improve the quality of music or speech played by your computer.  Some of these settings may be slightly different on your computer, or there may be some additional similar settings available.  You should check every tab, and it is generally best to disable anything that might try to "improve" the sound quality.

Your transmit audio level can be adjusted using a combination of your radio's settings, the 'Speakers' level settings (and Balance if available) in 'Levels' tab of the Windows Speakers Properties dialog, and the **ardopcf** Drivelevel setting.  Drivelevel can be set when starting **ardopcf** using the `DRIVELEVEL` command with the `--hostcommands` option or with the slider in the **ardopcf** WebGui.  In theory, drivelevel can also be controlled from a host program like Pat, but I don't believe that any existing host programs provide this function.

Together these settings influence the strength and quality of your transmitted radio signal.

Reduced audio amplitude can be used to decrease the RF power of your transmitted signals when using a single sideband radio transmitter. In addition to limiting your power to only what is required to carry out the desired communications (for US Amateur operators this is required by Part 97.313(a)), reducing your power output may also be necessary if your radio cannot handle extended transmissions at its full rated power when using high duty cycle digital modes. While sending data, Ardop can have a very high duty cycle as it sends long data frames with only brief breaks to hear short DataACK or DataNAK frames from the other station.

If the output audio is too loud, your radio's Automatic Level Control (ALC) will adjust this audio before using it to modulate the RF signal. This adjustment may distort the signal making it more difficult for other stations to correctly receive your transmissions. Some frame types used by Ardop may be more sensitive to these distortions than others.

To properly configure these settings you need to know how to monitor and interpret your radio's ALC response and power output.  On most radios a low ALC value means that the ALC is not distorting your signal.  However, on other radios, including my Xiegu G90, a high ALC value means that the ALC is not distoring your signal.  You should be able to find this information in the manual for your radio or on the internet.  You can also determine it experiemntally by adjusting the **ardopcf** drivelevel and the Windows Sound control Speaker level both to very low values.  Clicking on the `Send2Tone` button on the **ardopcf** WebGui should produce a low power and low distortion transmitted signal.  Whatever your ALC shows in this condition is the desireable value.  As you increase the **ardopcf** drivelevel and the Windows Sound control Speaker level, the tranmsit power of your radio should increase.  At some point, usually close to the configured power level of your radio, the transmitted signal will start to become more distorted.  When this occurs, the ALC level will start to change toward a worse setting.  As you continue to increase your audio settings, the power output should remain relatively constant, while the ALC setting will indicate a progressively worse value.  You want to choose the combination of Drivelevel, Sound control Speakers level, and radio settings that produce the desired amount of power without significantly disorting your audio (as indicated by too much change in the ALC indicator).  How much change in the ALC indicator is "too much" may vary from radio to radio.

If your radio does not have an ALC indicator, but either it has a power output indicator or you have a separate power meter that you can use to measure transmitted power while making adjustments, you can also use this to choose appropriate transmit audio levels.  If you slowly increase your audio level until the measured transmit power stops increasing, you have probably identified the audio level at which the ALC is starting to distort your signal.  So, use an audio level that produces slightly less than the maximum measured transmit power.

On some (higher quality?) radios, suitable audio level settings are independent of the band/frequency and power level settings of your radio.  On other radios, including my Xiegu G90, different bands require different audio settings, and reducing the power level setting also requires reducing the audio level.  This appears to indicate that the power level setting of the Xiegu G90 simply causes it to engage the ALC at lower audio levels.  My recommendation is that you choose radio settings and Windows Sound control Speaker settings that allow you to use only the **ardopcf** drivelevel slider to make ongoing adjustments (using the WebGui).  I also recommend that you write down the radio and Speaker settings that work well so that if they get changed (intentionally or accidentally), you can quickly restore them to settings that you know should work well.

While transmit audio settings using the `Send2Tone` function are usually pretty good, monitoring of ALC and/or power level while sending actual Ardop data frames may indicate that further (usually minor) changes to transmit audio levels are appropriate.

If you set your radio for a higher power level than you intend to transmit at, and then use a reduced drivelevel to reduce your power output, then minor fluctuations are unlikely to engage the ALC causing any distortion.  Using this approach, you really only need to be concerned about ALC and distortion if you are trying to use the full rated power of your transmitter.

### Adjusting your receive audio level.

Normally, you should turn off the AGC function on your radio while working with digital signals, especially digital signals (including Ardop) that occupy only a small part of your radio's receive bandwidth.  Instead, I use manual adjustments to RF gain as needed.  AGC attempts to keep the average power level across the total receive bandwidth relatively steady.  When using digital modes like the 200 Hz or 500 Hz Ardop bandwidths, there may be other signals within the receiver's bandwidth that are unrelated to the Ardop signal that I am trying to receive.  Under these conditions, AGC may significantly alter the strength of the Ardop signal as an unrelated strong signal turns on or off.

Before adjusting the transmit audio levels, there are some general audio input settings to configure.
1. On Windows 11, click the Windows Start button and type `Control Panel` into the search bar.  On older versions of Windows, click the Windows Start button and find `Control Panel`, probably in the Windows System folder
2. If you see `Sound`, click on it.  If not, you may need to click on `Hardware and Sound` and then on `Manage Audio Devices`.
3. In the 'Recording' tab identify your radio interface.  Often it will be labeled something like 'USB PnP Sound Device'.  If you are unsure which device it is, you can unplug and replug the connection to your radio and see which device disappears and comes back.
4. Double click on the device representing your radio interface.  The 'Levels' tab is where you can adjust the 'Microphone' slider as may be required later in this section.  On the 'Listen' tab you have the option of having the incoming audio from your radio also play through your commputer's speakers.  If this is something you want, check the 'Listen to this device' and set the 'Playback through this device' value.  Make sure that the selected playback device is NOT the interface to your radio.  This setting may be useful, at least sometimes, if your radio cannot send audio to a speaker or headphones while also sending it to your computer.  On the 'Custom' tab, uncheck 'AGC'.  On the 'Advanced' tab, uncheck 'Enable audio enhancements'.  These settings will prevent Windows from manipulating your audio before passing it to **ardopcf**.  These manipulations are often applied by default since they might improve the quality of music or speech from a microphone.  Some of these settings may be slightly different on your computer, or there may be some additional similar settings available.  You should check every tab, and it is generally best to disable anything that might try to "improve" the sound quality.

Unlike transmit audio level which can be partially controlled through the **ardopcf** Drivelevel setting, there is no mechanism to control received audio level through **ardopcf**.  Received audio level is controlled only by settings on your radio and the 'Microphone' level settings (and Balance if available) in 'Levels' tab of the Windows Microphone Properties dialog.

Adjusting the receive audio level is also slightly more difficult than adjusting the transmit audio level because it depends on some signal being received by your radio.  Conveniently, the received signal does not need to be an Ardop signal to do initial setup.  Inconveniently, unlike your transmit audio settings, it may require adjustment each time you use **ardopcf** depending on noise level and band conditions.

The **ardopcf** WebGui `Rcv Level` indicates the instantaneous level of the audio being received.  If you tune your radio to a quiet portion of a band with low background noise, this should be mostly or entirely grey, with perhaps only a little bit of a green signal idicator near the left edge.  Under these conditions, if the **ardopcf** WebGui shows a yellow `(Low Audio)` warning next to the `Rcv Level` graph, that is OK.

When you receive a strong signal or when the backgound noise is high, that same indicator should be up to about half green, or even more.  Luckily, **ardopcf** can handle a relatively large range of acceptable audio levels, and seems to decode relatively low audio volume signals reasonably well if the signal is stronger than the background noise.  Too loud of a received signal is more likely to cause problems than too soft of a received signal.  So, if in doubt, reduce the receive audio level a bit.  With the current popularity of FT-8, tuning to one of the FT-8 frequencies is often a convenient way to receive a relatively strong signal, which is actually the combination of several simultaneous FT-8 signals.  These signals are also usually steady on for about 12 second, then off for a few seconds, and then repeat this pattern.  So, if you tune your radio to an active FT-8 frequency, you can often adjust the audio settings so that the `Rcv Level` graph peaks at about 50% green in each 15-second period and then drops down to near zero in the gaps between these transmissions.  That is often a good initial audio setting.  If the `Rcv Level` graph turns orange, the audio is too loud.  It is important that you pay attention to the `Rcv Level` graph and not the colors of the signals appearing on the waterfall graph.  The waterfall graph has a slow but strong automatic gain control feature that tries to always show high contrast between any received signals and the background noise independent of the total audio volume..

While these settings based on received FT-8 are a good starting point, watching the `Rcv Level` graph while receiveing actual Ardop transmissions may indicate that you should make further adjustments to your receive audio levels.  As with adjusting drivelevel settings, I recommend that you choose one control element that you will use for final adjustment of receive audio levels, and set any other controls (on the radio and/or computer) to fixed values.  Making quick adjustments to more than one control are not practical.


## Hamlib setup for Windows 11.

**ardopcf** does not work with Hamlib (rigcltd), but [Pat](https://getpat.io) does.  [Hamlib_Windows11.md](Hamlib_Windows11.md) provides instructions on how to set up Hamlib to use on a Windows 11 machine.  If you are using another version of Windows, you should also be able to adapt those instructions for your version.

## [Pat](https://getpat.io) Winlink with ardopcf on Windows.

See [Pat_windows.md](Pat_windows.md) for instructions on installing and configuring the popular Pat winlink client for use on Windows with **ardopcf**.
