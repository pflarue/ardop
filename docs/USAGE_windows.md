# Using ardopcf on Windows

There are three main steps to using **ardopcf** on Windows.  You need to obtain the `ardopcf.exe` file, determine what command line options you need to use, and finally create a Desktop Shortcut (and perhaps pin it to your taskbar) to start **ardopcf**.  You might also choose to create a Desktop Shortcut for the ardopcf WebGui (and perhaps pin it to your taskbar)

## Getting `ardopcf.exe`
If you want to try the latest changes that have been made to **ardopcf** since the last release, you can [build](BUILDING.md) it from source.  However, most Windows users will download one of the pre-built binary executable files from the [releases](https://github.com/pflarue/ardop/releases/latest) page at GitHub.  Before you can do either of these, you need to know whether your computer is running 32-bit or 64-bit Windows.  All Windows 11 and most Windows 10 operating systems are 64-bit.  To find out if a Windows 10 operating system is 32 or 64 bit press the **Start** button and select **Settings**, click on **System** then on **About** and read the line starting with **System type**.  If you are using a version of Windows older than Windows 10, you will also need to install the [Universal C Runtime update](https://support.microsoft.com/en-us/topic/update-for-universal-c-runtime-in-windows-c0514201-7fe6-95a3-b0a5-287930f3560c).

If you downloaded a pre-built binary executable, you should rename it from `ardopcf_amd64_Windows_64.exe` or `ardopcf_amd64_Windows_32.exe` to just `ardopcf.exe`.

**ardopcf** requires only a single excutable file, `ardopcf.exe`, and it requires no special installation procedure.  However, you probably don't want to run it from your Downloads directory (though you can if you want to).  You might consider creating a directory for ardop such as `C:\Users\<USERNAME>\ardop` and maybe also create `C:\Users\<USERNAME>\ardop\logs` to hold the log files that **ardopcf** creates.

The first time that you run `ardopcf` without the `-h` option, Windows will ask you whether you want to allow public and private networks to access this app.  Host programs like [Pat](https://getpat.io) use a TCP "network" connection to work with **ardopcf**.  Without a program like Pat, **ardopcf** isn't useful.  Also, the **ardopcf** WebGui connects from a browser window to **ardopcf** using a TCP "network" connection.  If you choose `Allow` in this windows dialog, then you will be able to use the Webgui in a browser on any computer on your local network and/or run a host program like Pat on any computer on your local network.  If instead you choose 'Cancel' in this windows dialog, then these programs/features will still work, but only if they are all running on the same computer.  If you do not have admin privledges on this Windows computer, then `Allow` won't work.  On a Windows computer this usually isn't a problem since you probably intend to run everything on this one computer anyway.  The ability to run **ardopcf** on one computer and a host or the WebGui on another is more likely to be useful when **ardopcf** is running on a small computer like a Raspberrry Pi that does not have a monitor conencted to it.

## Ardopcf PTT/CAT conrol

**ardopcf** has the ability to handle the PTT of your radio in a variety of ways, but other than activating PTT, it does not do other CAT control.  (ardopcf does allow a host program to provide hex strings to pass to a CAT serial device or TCP port, but since these strings must be appropriate for the specific model of radio or TCP middleware that you are using, this feature is not generally used.)  **ardopcf** can also allow a host program like [Pat](https://getpat.io) Winlink to handle PTT.  With Pat, this is convenient because it can also use CAT control to set your radio's frequency.  Other host programs may not have CAT or PTT capabilities, so they require **ardopcf** to handle PTT and require you to manually set the frequency on your radio.  A third option is to use the VOX capability of your radio to engage the PTT.  This may work OK for FEC mode operation with some host programs, but can be unreliable for the ARQ mode operation used by Pat because it may not engage or disengage quickly enough.

If you want Pat to do PTT and CAT control then do not use any of the `-p`, `--ptt`, `-c`, `--cat`, `-g`, `-k`, `--keystring`, `-u`, or `--unkeystring` options described in the remainder of this section.  Instead, set `"ptt_ctrl": true` in the ardop section of the Pat configuration and use the `"rig"` setting to point to a rig defined in the `"hamlib_rigs"` section.  The remainder of this section will assume that you want **ardopcf** to control PTT.  Note that you should **NOT** try to have **ardopcf** do PTT using the same COM port that you will ask PAT (via Hamlib/rigctld) or any other program to use for CAT control.  However, you may set Pat to provide CAT control via Hamlib/rigctld to set the frequency, while setting **ardopcf** to handle PTT via the same Hamlib/rigctld middleware.

**ardopcf** has a few methods available for controlling PTT.  Serial device RTS/DTR, CAT commands send to a serial device or TCP port (as used with Hamlib/rigctld), and use of CM108 sound devices using IO pin 3 for PTT will be described here.

Both RTS/DTR and CAT (except TCP CAT) PTT require the COM port number of the radio interface. Some radio interfaces may require installing a driver for the COM port to be usable.  See your radio or interface manual for more details.  To find the COM port number for your radio interface, open the Windows Device Manager by right clicking on the Windows Start button and then on Device Manager.  With the Device Manager open, if only one COM port is shown when the radio interface is connected, this is likely the one you want to use.  If there is more than one port shown, or to be certain that the only one shown is correct, unplug and reconnect the radio interface and watch which COM port disappears and reappears.

If the interface between your computer and your radio supports PTT control via RTS, this is often the simplest solution.  To use RTS PTT use the `-p COMX:BAUD` or `--ptt COMX:BAUD` option where `X` is the COM port number found using Device Manager and `BAUD` is the port speed.  If `:BAUD` is ommitted, the default speed of 19200 baud is used.  Often this default is acceptable.  You can find the baud rate required for your radio interface in the manual for your radio or on the internet.

If the interface between your computer and your radio supports PTT control via DTS, this is very similar to the solution for RTS, but with a `DTR:` prefix applied to the device name.  To use DTR PTT use `-p DTR:COMX:BAUD` or `--ptt DTR:COMX:BAUD` option where `X` is the COM port number found using Device Manager and `BAUD` is the port speed.  If `:BAUD` is ommitted, the default speed of 19200 baud is used.  Often this default is acceptable.  You can find the baud rate required for your radio interface in the manual for your radio or on the internet.

If RTS/DTR PTT does not work, you may be able to use CAT PTT.  For this use the `-c COMX:BAUD` or `--cat COMX:BAUD` option where `X` is the COM port number found using Device Manager and `BAUD` is the port speed.  If `:BAUD` is ommitted, the default speed of 19200 baud is used.  Often this default is acceptable.  You can find the baud rate required for your radio interface in the manual for your radio or on the internet.

You can also direct **ardopcf** to pass CAT commands via a TCP port rather than a serial device.  This has been tested with Hamlib/rigctld.  Given the correct PTT ON and PTT OFF command strings, it should work with any radio or other middleware that accepts CAT commands via a TCP port.  By default, rigctld uses TCP port 4532.  So, with rigctld running on your local machine, `-c TCP:4532` will allow **ardopcf** to connect to it.  If rigctld is running on a different networked computer, you can use an option such as `-c TCP:192.168.100.5:4532` to connect to that remote rigctld process.  Any IPv4 address and port expressed as ddd.ddd.ddd.ddd:port should work.

The `--cat` or `-c` option only sets the CAT serial device or TCP port to be used.  It is also necessary to provide the actual cat commands as hex strings to key (`-k` or `--keystring`) and unkey (`-u` or `--unkeystring`) your radio.  These will be specific to the model of radio or interface that you are using, or to the middleware such as Hamlib/rigctsd that you are connecting to.  So you will have to find them in your radio manual or on the internet.

For example, my Xiegu G90 can do PTT via CAT control with `-c COM5 --keystring FEFE88E01C0001FD --unkeystring FEFE88E01C0000FD`.  For this radio `FEFE` is the fixed preamble to all CI-V commands, `88` is the transceiver address expected by Xiegu radios, and `E0` is the controller address.  Then command number `1C` is used for PTT/ATU control, sub-command `00` means set PTT status, data = `01` means press PTT while `00` means release PTT, and `FD` means end of message.  For Kenwood, QRP-Labs, Elecraft, and TX-500 `-c COM5 --keystring 54583B --unkeystring 52583B` should work, which is the HEX values for 'TX;' and 'RX;'.  I don't know what baud rate is requried for these, but the default works with my Xiegu.  Remember that `-c COM5` is equivalent to `-c COM5:19200` because **ardopcf** uses a default of 19200 baud if a value is not provided.  While CAT control of PTT works for my Xiegu, using `-p COM5` for RTS/DTR PTT is simpler and works just as well.  With the [Digirig](https://www.digirig.net) interface that I use, another advantage of using RTS/DTR PTT is that it requires only the audio cable, so I can leave the serial cable disconnected.

If you are using Hamlib/rigctld, then the hex strings for PTT ON and PTT OFF must be the Hamlib/rigctld command strings rather than the command strings specific to your radio.  So, if you are using `--cat TCP:4532` with rigctld, you would always use `-k 5420310A -u 5420300A`, regardless of the type of radio you are using.  These are the hex representations of `T 1\n` and `T 0\n` for TX and RX respectively.  Since these three options are likely to be used together for most people using Hamlib/rigctld with **ardopcf**, the `--ptt RIGCTLD` or `-p RIGCTLD` shortcut is provided.  This is equivalent to `-c TCP:4532 -k 5420310A -u 5420300A`.  The option used to start rigctld specifies the radio model you are using and how it is connected to your computer.  See [Hamlib_Windows11.md](Hamlib_Windows11.md) for more help installing and configuring Hamlib/rigctld for use with Pat and **ardopcf**.

If you are not already using Hamlib/rigctld, there is probably no advantage to installing and configuring it only for use by **ardopcf**.  However, if you are already running Hamlib/rigctld and have it configured to work with your radio, then it may be convenient to use these options.  One advantage of using Hamlib/rigctld via a TCP CAT port is that, unlike a serial device, multiple programs can connect to one TCP CAT port simultaneously.  For example, you can have **ardopcf** use it for PTT control, while Pat uses the same TCP CAT port to set the radio frequency.

CM108 devices are USB devices that combine an audio interface with PTT control.  These include user modified USB sound devices as well as commercial products including: the [digirig Lite](https://digirig.net/product/digirig-lite) (not to be confused with the [digirig mobile](https://digirig.net/product/digirig-mobile) which I refer to as simply a digirig elsewhere in this documentation), the [AIOC or All-in-one-Cable](https://github.com/skuep/AIOC) which is also available from https://na6d.com, and various products from [masterscommunications.com](https://masterscommunications.com).

It may be possible to find the name of a CM108 device from the Windows Device Manager.  However, it is more convenient to specify the device by its Vendor ID (VID) and Product ID (PID).  Given these two values, as hex strings, you can use `--ptt CM108:VID:PID`.  As examples, my [AIOC](https://github.com/skuep/AIOC) has VID=1209 and PID=7388.  So, I use `--ptt CM108:1209:7388`.  My [digirig Lite](https://digirig.net/product/digirig-lite) has VID=0D8C and PID=0012.  So, I use `-p CM108:0D8C:0012`.  The leading zeros are optional, and the VID:PID string is not case sensitive.  So, `-p CM108:d8c:12` also works.  If you don't know the VID and PID of your device, run:
```
ardopcf --ptt CM108:?
```

This will list all known CM108 compatible devices connected to a Windows computer, giving their VID:PID, product name if one is defined, and their full device id string.  If you have a new or unusual CM108 device that is not known to ardopcf, it might not be listed.  In that case try:
```
ardopcf --ptt CM108:??
```

This will list all available HID devices connected to a Windows computer, giving their VID:PID, product name if one is defined, and their full device id string.  If you discover a device in this list that works for PTT control, but is not included in the listing of known CM108 compatible devices, please post a message to the users subgroup of ardop.groups.io or create an Issue for the ardopcf repository at (https://www.GitHub.com/pflarue/ardop) so that it can be added.

CM108 devices can also be specified by the full device id listed with the `--ptt CM108:?` or `--ptt CM108:??` option.  This may be useful if you have two devices attached with the same VID:PID values, in which case each device has a different full device id.  When using this form, it is necessary to put double quotes around the argument.  Otherwise Windows will misinterpret the multiple `&` characters typically found in the full device id string as separators between multiple programs to run.  This will result in multiple errors.  For example, with my Digirig Lite connected, `--ptt CM108:?` shows:
```
VID:PID="0D8C:0012" USB Audio Device \\?\HID#VID_0D8C&PID_0012&MI_03#7&12ef1561&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}
```

So, to use this device by specifying its full device id, notice my use of double quotation marks in:
```
ardopcf -p "CM108:\\?\HID#VID_0D8C&PID_0012&MI_03#7&12ef1561&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030}" -i 1 -o 1
```
which is equivalent to:
```
ardopcf -p CM108:0D8C:0012 -i 1 -o 1
```

## Ardopcf audio devices and other options.

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

The list of Capture and Playback devices should be similar to what you saw in the Sound control dialog in the Recording and Playback tabs.  What is important is the numbers that **ardopcf** shows before each device.  In this example, the USB PnP devices represent the connection to my radio.  In this case, each is assigned number `1`.  Running **ardopcf** without any options defaults to using the devices labeled `0`.  Use the `-i` option to set the (input) audio capture device and the `-o` option to set the (output) audio playback device.  Earlier versions of **ardopcf** did not support these options, and thus required that you also specify the host port number to set these values.  So, for my system, I would use:

`ardopcf -i 1 -o 1`

This starts **ardopcf** and may be sufficient if you decided after reading the earlier section on PTT/CAT control that you do not need **ardopcf** to handle PTT because a host program like [Pat](https://getpat.io) will handle this or because you will use VOX.  If you decided that you want **ardopcf** to handle PTT, then add those additional options.  For example:

`ardopcf -p COM5:19200 -i 1 -o 1`

or

`ardopcf -c COM5 --keystring FEFE88E01C0001FD --unkeystring FEFE88E01C0000FD -i 1 -o 1`

4. There are some additional command line options that you might want to use.  If you are using the `-i` and `-o` options to set your audio devices (and not using the legacy method of providing three positional arguments), then the order in which the command line options are given does not matter.

A.  By default **ardopcf** writes some log messages to the console and writes more detailed log messages to log files in the directory where it is started.  You can change where these files are created with the `-l` or `--logdir` option.  For example, I created an `ardop_logs` directory and use `--logdir C:\Users\<USERNAME>\ardop\ardop_logs`.  To not write any log files, use the `--nologfile` or `-m` options.  To write more or less detail to the console and log files, use the `CONSOLELOG` and `LOGLEVEL` host commands respectively.  These each take a value from 1 (most detail) to 6 (least detail), and can be set using the `-H` or `--hostcommands` option described below.

B.  The `-H` or `--hostcommands` option can be used to automatically apply one or more semicolon separated commands that **ardopcf** accepts from host programs like [Pat](https://getpat.io).  See [Host_Interface_Commands.md](Host_Interface_Commands.md) for more information about these commands.  If the first commands are `LOGLEVEL` and/or `CONSOLELOG` (as described above), then these are applied before most other command line options are evaluated.  Processing them before the logging system is initiated ensures that the log settings you want are in place before any log messages are processed.

The host commands are applied in the order that they are written. Except for those leading `LOGLEVEL` and `CONSOLELOG` commands, the order usually doesn't matter.  As an example, `--hostcommands "LOGLEVEL 1;CONSOLELOG 2;MYCALL AI7YN"` sets the log file to be as detailed as possible, and data printed to the console to be only slightly less detailed, and sets my callsign to `AI7YN`.  Pat will also set my callsign, so this isn't usually necessary as a startup option, but it is a convenient example.  Because most commands will include a command, a space, and a value, you usually need to put quotation marks around the commands string.  After you adjust your sound audio levels, you may discover that you want the **ardopcf** transmit drive level to be less than the default of 100%.  As another example, `--hostcommands "CONSOLELOG 4;MYCALL AI7YN;DRIVELEVEL 90"` leaves the log file settings at their default value, but prints only more important messages to the console, sets my callsign, and set the transmit drive level to 90%.

C.  To enable the WebGui use `-G 8514` or `--webgui 8514`.  This sets the WebGui to be availble by typing `localhost:8514` into the navgation bar of your web browser.  You may choose a different port number if 8514 causes a conflict with other software.  The WebGui is likely to be useful when you adjust the transmit and receive audio levels as described later.

So, an example of the complete command you might want to use to start **ardopcf** is:

`ardopcf --logdir C:\Users\<USERNAME>\ardop\ardopc_logs -p COM5 -G 8514 --hostcommands "DRIVELEVEL 90" -i 1 -o 1`

With this running, **ardopcf** is functional and ready to be used by a host program like [Pat](https://getpat.io).  However, for windows users, it is probably more convenient to create a Desktop Shortcut to start **ardopcf** each time you want to use it.  For **ardopcf** to work well, you will also probably need to make adjustments to your receive and transmit audio levels.


## Create a Windows Desktop Shortcut

Unlike some programs such as [Pat](https://getpat.io) and [Hamlib rigctld](Hamlib_Windows11.md) that may be suitable for automatically starting when you log on to Windows and leaving running all the time, **ardopcf** is not currently well suited to this mode of use.  However, creating a Desktop Shortcut to start it is appropriate.  **ardopcf** should then be stopped with CTRL-C or by closing the window that it is running in when you are finished using it.

The following is for Windows 11.  Windows 10 is very similar.
1. Find 'ardopcf.exe' with Windows File Explorer, Right click on it, Select `Show more options`, Select `Create shortcut`.  This will create a new file in the same directory as 'ardopcf.exe' with the name 'ardopcf.exe - Shortcut'.
2. Move 'ardopcf.exe - Shortcut' to your Desktop by dragging it either to a blank area on your desktop or to where 'Desktop' appears in the tree panel of Windows File Explorer.  If offered a choice to 'copy' or 'move' the shortcut, choose 'move'.
3. Configure the shortcut: Right click on the shortcut icon on your desktop, Select `Properties`.  Add the command line options to set the port, audio devices, PTT options, enable the WebGui, etc. after 'ardopcf.exe' in the `Target` line.  Note that this line begins with the full path to 'ardopcf.exe', so that it does not need to be in any special location.  If there is a space anywhere in the path to 'ardopcf.exe', then this will be wrapped in double quotation marks.  In that case, the command line options are placed after the closing quotation marks.  For example:

`C:\Users\<USERNAME>\ardop\ardopcf.exe -G 8514 --logdir C:\Users\<USERNAME>\ardop\logs --hostcommands "CONSOLELOG 4;MYCALL AI7YN;DRIVELEVEL 85" -i 1 -o 1`

or

`"C:\Users\<USERNAME>\Documents\radio stuff\ardop\ardopcf.exe" -G 8514 --logdir C:\Users\<USERNAME>\ardop\logs --hostcommands "CONSOLELOG 4;MYCALL AI7YN;DRIVELEVEL 85" -i 1 -o 1`

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

**ardopcf** can use Hamlib/rigctld for PTT control via TCP CAT, but there is probably no advantage to installing and configuring it only for use by **ardopcf**.  However, Hamlib/rigctld can make using [Pat](https://getpat.io), which is often used with **ardopcf**, more convenient since it allows Pat to automatically change your radio's band and frequency.  [Hamlib_Windows11.md](Hamlib_Windows11.md) provides instructions on how to set up Hamlib to use on a Windows 11 machine.  If you are using another version of Windows, you should also be able to adapt those instructions for your version.

## [Pat](https://getpat.io) Winlink with ardopcf on Windows.

See [Pat_windows.md](Pat_windows.md) for instructions on installing and configuring the popular Pat winlink client for use on Windows with **ardopcf**.
