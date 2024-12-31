# Installation and configuration of [Hamlib](https://hamlib.github.io) (rigctld) on Windows 11.

These instructions are provided here to help people who want to use **ardopcf** with [Pat](https://getpat.io) Winlink on a Windows computer.  Hamlib allows Pat to tune your radio to the correct frequency and to handle PTT of your radio.  Pat with **ardopcf** can be used without Hamlib.  In that case, you will need to tune your radio to the correct frequency manually, and you will need to configure **ardopcf** to handle PTT itself.  **ardopcf** can also use Hamlib/rigctld for PTT control, but there is probably no advantage to installing and configuring it only for this purpose since **ardopcf** provides many other methods of PTT control.

Hamlib is a library to help other programs control amateur radio hardware.  In addition to allowing other programs to use it directly, it also provides two executables: rigctl and rigctld.  rigctl allows interactive use, in which you can type commands to control your radio.  It can also be used to control your radio from a Command Prompt window or from a .bat or .cmd script.  Only one instance of rigctl or one program that uses the Hamlib library directly can control any radio at once.

rigctld is much more flexible.  It provides a TCP "network" interface that other programs, such as **ardopcf** and [Pat](https://getpat.io), can connect to to control your radio.  An advantage of using rigctld is that multiple programs can use one instance of rigctld at the same time.  So, rigctld is normally run in the background, and then one or more other programs like **ardopcf** and [Pat](https://getpat.io) are configured to connect to it.

The TCP "network" interface provided by rigctld allows multiple programs running on the same computer or on other computers on the same local network to communicate with it via a TCP port.  On Windows, The first time that you run rigctld, Windows will ask you whether you want to allow public and private networks to access this app.  If you choose 'Allow' in this windows dialog, then you will be able to run the host program on any computer on your local network.  If instead you choose 'Cancel' in this windows dialog, then only programs running on this same computer will be able to connect to rigctld.  If you do not have admin rights on this Windows computer, then 'Allow' won't work.  In some cases, if you don't have admin rights you may just be shown a warning dialog with only a "Cancel" button.  On a Windows computer this usually isn't a problem since you probably intend to run everything on this one computer anyway.  The ability to run 'rigctld' (and perhaps **ardopcf**) on one computer and a host like [Pat](https://getpat.io) on another is more likely to be useful when 'rigctld' and **ardopcf** are running on a small computer like a Raspberrry Pi that does not have a monitor conencted to it.

If you have multiple radios that you want to be able to control with rigctld, then you run rigctld once for each radio, assigning a different TCP port number to each radio.

This page provides instructions for installing Hamlib/rigctld, configuring it to control your specific model of radio, running it manually from a Desktop Shortcut or from the Taskbar, and setting it up to run automatically each time you log in to your Windows computer.  Whether or not you want it to run automatically depends on how you use your radio and computer.  Discussion regarding this decision is provided.  rigctl will be used to test the configuration options before setting up rigctld.

In the following instructions, I'll use configuring Hamlib/rigctld to work with my Xiegu G90 connected to my computer with a [digirig](https://digirig.net) radio interface as an example.  Hamlib supports a long [list](https://github.com/Hamlib/Hamlib/wiki/Supported-Radios) of radio models.  Hamlib also supports using [FLRig](http://www.w1hkj.com), in which case FLRig must be configured work with your specific model of radio.  This may be useful if you are already using FLRig, but need Hamlib/rigctld for a host program that doesn't support FLRig directly.  Using Hamlib/rigctld with FLRig may also be helpful if FLRig's support for your model of radio is better than Hamlib/rigctld provides directly.

## How to install Hamlib/rigctld on Windows 11.

Using Hamlib/rigctld does **NOT** require admin rights on your windows computer.  As described below, depending on whether you have admin rights and on where you choose to install hamlib, you may have to do things slightly differently.

1. Go to https://github.com/Hamlib/Hamlib/releases/latest.  As I'm writing this in January 2025, the most recent version is 4.6 dated Decemver, 2024.  If a newer version is available to you, the numbers in the file and directory names I use below will be different.  All Windows 11 systems are 64 bit.  So, you want a file with -w64 in its name: `hamilb-w64-4.6.zip`.  The following instructions describe downloading and extracting the contents from this `zip` file.  An `exe` file is also available that will guide you through extracting the files into a directory of your choice.

If you discover that the latest release of hamlib does not support your model of radio well, you can try a daily snapshot distribution from https://n0nb.users.sourceforge.net.  As with the downloads from GitHub, choose the `zip` file with -w64 in its name for Windows 11.  You can install multiple versions of hamlib on your computer at the same time, each in a different directory.  So, if you installed the latest release, but want to try a daily snapshot, you can have both.  If you discover that the daily snapshot works better, you can then delete the latest release and any shortcuts to it if you want to.

2. When the download is complete, use Windows File Explorer to find the zip file in the Downloads directory.  Right click on it and choose `Extract All...`.  Choose a suitable directory.  Consider choosing `C:\Program Files`.  This requires administrator rights and will ask you to confirm that is where you want it.  It will create and populate `C:\Program Files\hamlib-w64-4.6`.  If you do not have administor rights, Hamlib/rigctld will still work.  In that case, consider choosing `C:\Users\<USERNAME>` to create and populate `C:\Users\<USERNAME>\hamlib-w64-4.6`.

The 'doc' subdirectory within the Hamlib directory you just created contains some html files that may be helpful.  Double clicking on any of these in Windows File Explorer should display them in your web browser.

3. To use Hamlib, you need to know what COM port your radio control interface uses.  You can skip this step if you will be using Hamlib with FLRig. Some radio interfaces may require installing a driver for the COM port to be usable.  See your radio or interface manual for more details.  To find the COM port number, open the Windows Device Manager by right clicking on the Windows Start button and then on Device Manager.  With the Device Manager open, if only one COM port is shown when the radio interface is connected, this is likely the one you want to use.  If there is more than one, or to be certain that the only one shown is correct, unplug and reconnect the radio interface and watch which COM port disappears and reappears.  The other thing you need to know about this COM port is what baud rate your radio interface expects.  You'll find this in your radio's manual or by searching the internet.  For my Xiegu G90, it requires a baud rate of 19200.

4. Next, you need to determine what hamlib model number matches your radio. Open a Windows Command Prompt by pressing the Windows Start button and typing 'Command Prompt' into the search bar and pressing Enter.  Then run rigctl with the -l option (that is a lowercase L) to list all of the supported radio model numbers.  You can also view this list at https://github.com/Hamlib/Hamlib/wiki/Supported-Radios, though that listing might not match the version of rigctl/rigctld you have installed.

`"C:\Program Files\hamblib-w64-4.5.5\bin\rigctl" -l`

or

`C:\Users\<USERNAME\hamblib-w64-4.5.5\bin\rigctl -l`

Notice that if you installed hamlib in a directory that contains spaces (like `Program Files`), then quotes are required around the program name but not the command line options.  These quotes are not needed if there are no spaces in the direcory names.  Other than perhaps adding the quotes, this is just `\bin\rigctl -l` added to the directory created when you extracted the contents of the zip file.  This command prints a long list of supported radio models.  So, you'll need to scroll back through the results to look for your radio model.  If you don't find your exact model, you may need to do some searching on the iternet to see if you find a recommendation for a similar model that works.  For my Xiegu G90, I find the following line:

`3088  Xiegu        G90        20241203.11   Stable    RIG_MODEL_G90`

The `3088` at the start of that line is what I use to start rigctl and rigctld.  If the latest daily snapshot shows a newer/different version for your radio, it may have added features or be more reliable (or not).

If you already use [FLRig](http://www.w1hkj.com), notice model 4:

`   4  FLRig                   20241204.0   Stable    RIG_MODEL_FLRIG`

This virtual radio type allows programs that can use Hamblib, but that do not support using FLRig directly to use FLRig via Hamlib.  If Hamlib, including the recent daily snapshot, does not support your radio well, it may also be worth exploring whether FLRig does, and trying this option.  When using Hamlib with FLRig, you will need to configure FLRig to work with your specific radio model.  Installing, configuring, and starting FLRig is beyond the scope of these instructions.  I assume that most people wanting to use Hamlib with FLRig are doing so because they are have already installed and configured FLRig.

Some additional notes for a few specific radio models including the FT-747, FT-890, FT-920, Ten-Tec Orion and Orion II, Elektor SDR, Si570 AVR-USB/SoftRock, and FUNcube are available at https://github.com/Hamlib/Hamlib/wiki/Radio-Specific-Notes.

5. Manually run rigctl with the settings you think will work, to verify that they are correct.  Use the `-m` or `--model` option to set your model number, `-r` or `--rig-file` to set your COM port, and `-s` or `--serial-speed` to set the baud rate.  The `-vvv` (3 levels of `--verbose`) tells `rigctl` to print more information while it is running, which may be useful if things don't work exactly as expected.

**WARNING:** Executing this command may briefly key the PTT on your radio.  If the settings are incorrect, your radio may mistakenly change the power output settings or frequency on your radio, and may start transmitting and not stop until you kill rigctl with CTRL-C, turn the radio off, or pull a cable.  So, before continuing, it is recommended that you connect your radio to a dummy load.

```
"C:\Program Files\hamblib-w64-4.5.5\bin\rigctl" -h

"C:\Program Files\hamblib-w64-4.5.5\bin\rigctl" -vvv -m 3088 -r COM4 -s 19200
# OR
C:\Users\<USERNAME>\hamblib-w64-4.5.5\bin\rigctl -vvv -m 3088 -r COM4 -s 19200
```

To use Hamblib with [FLRig](http://www.w1hkj.com), only (-vvv) and -m 4 are needed:
```
"C:\Program Files\hamblib-w64-4.5.5\bin\rigctl" -vvv -m 4
# OR
C:\Users\<USERNAME>\hamblib-w64-4.5.5\bin\rigctl -vvv -m 4
```

If `COM4` cannot be used because it doesn't exist (used wrong number) or because it is already in use (Perhaps **ardopcf**, another instance of rigctl or rigctld, or FLRig is already running and using `COM4`), then rigctl (or rigctld if the same thing happens to it) will print an error like 'serial_open: Unable to open COM4 - No such file or directory'.  However, without at least `-vv` it might exit without printing any indication of what went wrong.  That is an example of the usefulness of the `-vvv` option.

The first command display's the help screen for rigctl including a list of COMMANDs.  All commands don't necessarily work for all radios.  Pick one of the other lines based on where you intalled hamlib and whether or not there are spaces in the path.  With my configuration, this prints `Opened rig model 3088, 'G90'`.  It may briefly keys and unkey the radio and/or print some errors.  It then prints a `Rig command:` prompt.  You can type in a command at this prompt to manually control your radio.  rigctl can also be used to automate adjusting some radio settings with command line scripts.  I suggest using it interactively here as a way to verify that it is working correctly for your radio.  When you are done pressing CTRL-C will exit.  The most important commands for use with [Pat](https://getpat.io) and **ardopcf** are those that get and set frequency and key/unkey PTT.  In general, lower case command letters get settings and upper case command letters set them.

Typing `f` and pressing Enter should print the frequency that your radio is currently tuned to in Hz.  So, mine shows `Frequency: 7064000` meaning 7.064 MHz.

Type `F` followed by a frequency in Hz such as `F 7065000` and pressing Enter should tune my radio to 7.065 MHz.

Type `t` and press Enter to get the status of the PTT.  Since PTT is (hopefully) off, this should print `PTT: 0`.

In this next test, briefly cause your radio to transmit, verify that it works, and then enter the command to releast the PTT and stop transmitting.  Type `T 1` and press Enter to transmit, and type `T 0` and press enter to stop transmitting.

Press CTRL-C to exit.

6. If rigctl worked correctly in the previous step, then rigctld should also work.  rigctld is a server program that other software like **ardopcf** and [Pat](https:\\getpat.io) can use to control your radio and its PTT.  You can either start rigctld manually each time you want to run a program that requires it, or you can set it to run automatically each time you log on to your computer.  The former might be a good choice if your radio is not always connected to your computer, or if you connect different radios to your computer at different times.  The latter is more convenient if you have a radio that is always connected to your computer.  However, you would not want to run rigctld automatically if you also use other programs that connect to your radio directly.  If you have multiple radios that are sometimes or always connected to your computer, then you can repeat these steps using a differnt `--port` setting for each radio.


### Create a Desktop Shortcut to rigctld only as needed.

1. Find rigctld.exe in the 'bin' subdirectory within the directory where you installed Hamlib (probably in C:\Program Files or C:\Users\<USERNAME>) with Windows File Explorer. Right click on it, Select `Show more options`, Select `Create shortcut`.  If you installed Hamlib in 'C:\Program Files', Windows will say that it can't create a shortcut here, and ask whether you want to place it on the Desktop instead.  Click `Yes`.  This will create a new icon on your Desktop with the name 'rigctld.exe - Shortcut'.  If you installed Hamlib in 'C:\Users\<USERNAME>', windows will create a shorcut in the same directory where rigctld.exe is located.  In that case, move the shortcut to the Desktop by dragging it to the desktop or using CTRL-X and CTRL-V.
2. Configure the shortcut: Right click on the shortcut icon on your Desktop, Select `Properties`.  Add the desired command line options after rigctld.exe  in the `Target` line.  Note that this line begins with the full path to rigctld.exe.  Like when testing rigctl from the command prompt, there may be required double quotes around the command if there are spaces anywhere in the path.  If these quotes are required, the command line options must be placed after the closing quotes mark, not within then.  Add the same options that you used above for rigctl here.  Also include `-t 4532` (or `--port 4532`) as one additional command line option that wasn't used for rigctl.  This sets the TCP port number that other programs must use to work with rigctld.  4532 is the default value, and thus is not really required here.  Including it makes it easy to find if you forget what value is being used, and makes it simple to use a different value.  If you want to, you can set up rigctld to control multiple radios, each with their own uniqiue port number.  The documentation suggests using even numbered values.  So, in my case, I set the `Target` line to read:

`"C:\Program Files\hamblib-w64-4.5.5\bin\rigctld.exe" -vvv -m 3088 -r COM4 -s 19200 -t 4532`

or

`C:\Users\<USERNAME>\hamblib-w64-4.5.5\bin\rigctld.exe" -vvv -m 3088 -r COM4 -s 19200 -t 4532`

or to use Hamblib with [FLRig](http://www.w1hkj.com)

`"C:\Program Files\hamblib-w64-4.5.5\bin\rigctld.exe" -vvv -m 4 -t 4532`

or

`C:\Users\<USERNAME>\hamblib-w64-4.5.5\bin\rigctld.exe" -vvv -m 4 -t 4532`

The `-vvv` is optional.  If everything is working well, you can omit this, or come back and remove it later.  However, if something is not working quite right, using `-vvv` makes it easier to diagnose the problem.

3. Change the `Run:` pulldown to `Minimized`.  If you want to, you can also click on `Change Icon` and select a different icon to change the appearance of the shortcut on your desktop.  By switching to the `General` tab, you can also change the shortcut's label.  I use the name of the radio as part of the shorcut's label: `Rigctld G90`.  Finally, click on `OK`.

4. Double clicking on this shortcut will start rigctld.  When you are done using rigctld, you can either right click on the icon in your task bar and then click `Close Window`, or you can left click on the icon in your task bar to open the window, and then either type CTRL-C or click on the red X in the upper right corner of the window.

5. (Optional) While rigctld is running, you may choose to right click on its icon in the taskbar and then click on `Pin to taskbar`.  This will leave an icon for rigctld in your taskbar when it is not running that can be used to start it with a single click.

The first time that you run rigctld, Windows will ask you whether you want to allow public and private networks to access this app.  Host programs like [Pat](https://getpat.io) use a TCP "network" connection to work with rigctld.  If you choose 'Allow' in this windows dialog, then you will be able to run the host program on any computer on your local network.  If instead you choose 'Cancel' in this windows dialog, then the host program will still work, but only if it is running on the same computer.  If you do not have admin rights on this Windows computer, then 'Allow' won't work.  On a Windows computer this usually isn't a problem since you probably intend to run everything on this one computer anyway.  The ability to run rigctld (and perhaps **ardopcf**) on one computer and a host like Pat on another is more likely to be useful when rigctld and **ardopcf** are running on a small computer like a Raspberrry Pi that does not have a monitor conencted to it.

### Set rigctld to automatically start whenever you log on to Windows

1. First create a Desktop Shortcut for rigctld as described above.

2. Use Windows File Explorer to Copy that shortcut from the Desktop to either:

`C:\Users\<USERNAME>\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup`

or

`C:\ProgramData\Microsoft\Windows\Start Menu\Programs\Startup`

The first option starts rigctld only when a specific user logs on to Windows, while the later starts it when any user logs on.  If you do not have admin rights on this windows computer, you can only use the first option.  If your PC has only one user account, then it shouldn't matter which one you choose.

If you want to stop rigctld, you can either right click on the icon in your task bar and then click `Close Window`, or you can left click on the icon in your task bar to open the window, and then either type CTRL-C or click on the red X in the upper right corner of the window.  The next time you logout or restart your computer and log back in, it will be automatically restarted.  In case you do this accidentally, it may be useful to keep the shortcut on your desktop.


### (OPTIONAL) Test rigctld to verify that it is working as expected

If you have [PuTTY](https://www.chiark.greenend.org.uk/~sgtatham/putty) or another telnet client installed on your computer, you can use it to manually test rigctld similarly to how you tested rigctl from the command line.

To do this, start PuTTY, set the hostname to `localhost` and the port number to `4532` or the alternate value that you used when creating the rigctld Shortcut.  Set the connection type to Telnet (not the default of SSH).  Click `Open`.

In the empty window that appears, you can type the same commands that you used to test rigctl from the command line.  The output should be similar, though perhaps not as cleanly formatted.  For example:  type `f` and press Enter to see the current frequency your radio is set to or type `F 70650000` to change the radio's frequency to 7.065 MHz.  When you are done, click the red X in the upper right corner of the window to end the PuTTY session.  This will ask you if you are sure you want to close this session.  Click `OK`.  This closes the PuTTY, but leaves rigctld running.
