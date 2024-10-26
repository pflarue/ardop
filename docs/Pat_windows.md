# [Pat](https://getpat.io) Winlink with ardopcf for Windows

[Pat](https://getpat.io) is a [Winlink](https://winlink.org) client program that can be used to send and recieve email messages using amateur radio, including when Internet and Cell service is not available. [ardopcf](https://github.com/pflarue/ardop) is one of several programs that Pat can use to connect to a radio.  This page provides instructions specific to setting up Pat to use with **ardopcf** on a Windows computer.

Other related pages provide instructions on configuring and running [ardopcf for Windows](USAGE_windows.md), and configuring and running [Hamlib/rigctld for Windows](Hamlib_Windows11.md) (which Pat can use to control your radio).  If you are using Linux instead of Windows, see [Pat_linux.md](Pat_linux.md) instead.

Using Pat and Ardop may seem complex and confusing.  If you need additional help, there are some users groups where you can ask for help:

This group is for everybody using Pat, though most are probably using Linux instead of Windows.
https://groups.google.com/g/pat-users

This group is for everybody using Ardop, including users of **ardopcf** and other Ardop implementations such as Ardop_Win.
https://ardop.groups.io/g/users

This group is for everybody using Winlink, though most are probably using Winlink Express rather than Pat, and most Winlink users don't use Ardop.
https://groups.io/g/Winlink


## Configuring [Pat](https://getpat.io) Winlink to use with ardopcf for Windows

If you have already installed and used Pat with something other than **ardopcf**, you can skip the first few steps.  However, in this case you might want to check whether a newer version of Pat is available using the link in step 1.

Pat's own documentation for configuration and command line use is available at https://github.com/la5nta/pat/wiki/The-command-line-interface.

1. Download Pat for Microsoft Windows from https://github.com/la5nta/pat/releases/latest.  As I am writing this in October 2024, the latest version is 0.16.0.  If a newer version is available to you, the numbers in the filename I mention below will be different.  Click on pat_0.16.0_windows_i386.zip to download it.

2. When the download is complete, use Windows File Explorer to find the zip file in the Downloads directory.  Right click on it and choose `Extract All...`.  Choose a suitable directory.  Consider choosing `C:\Program Files`.  This requires administrator rights and will ask you to confirm that is where you want it.  That will create and populate `C:\Program Files\pat_0.16.0_windows_i386`.  If you do not have administor rights, Pat will still work.  In that case, consider choosing `C:\Users\<USERNAME>` to create and populate `C:\Users\<USERNAME>\pat_0.16.0_windows_i386`. 

3. Open a Windows Command Prompt.  On Windows 11, this can be done by pressing the Windows Start button and typing `Command Prompt` into the search bar.  On older versions of Windows it is probably available in the Windows System folder after pressing the Windows Start button.

4. Use `cd` to navigate to the location of 'pat.exe'.  For example:

`cd /D C:\Program Files\pat_0.16.0_windows_i386`

or

`cd /D C:\Users\<USERNAME>\pat_0.16.0_windows_i386` 

5. Type `pat configure` with no other options and press Enter.  This will open Notepad to edit the pat configuration file.  The first time Pat is run, it creates a default version of this file at `C:\Users\<USERNAME>\AppData\Local\pat\config.json`.  Unfortunately, if you damage the json structure of this file, pat won't be able to read it correctly and you will see an error like `Unable to load/write config: unexpected end of JSON input` when you try to run it.  If that happens, you can delete this file and the next time you run `pat configure`, it will create a new default version.  Because of this, once you get Pat working, it may also be a good idea to create a backup of this file.  To avoid this problem, pay close attention to the required format and punctuation in this file.

6. Pat general configuration.  Pat has the ability to use several different connection methods, one of which is Ardop.  Before configuring Ardop, there are some general settings to configure.  Here are the first few lines of my config.json file:

```
{
  "mycall": "AI7YN",
  "secure_login_password": "PASSWORD",
  "auxiliary_addresses": [],
  "locator": "DM29ng",
```

Edit your config.json to include your callsign, your Winlink Password, and a locator value that indicates your location.  If you change locations, such as for portable operations, you don't always need to update ths value.  However, if you move very far it will be useful to update this value, since this will help Pat to show you the correct distance to known Winlink gateway stations.

If you don't already have an account at `winlink.org`, then you can leave the "secure_loging_password" value blank (justs empty quotes followed by a comma).  The first time you connect to a winlink server it will recognize you as a new user and send you a message with a temporary password.  You will use this temporary password to login online and configure your account.  

If you don't know your Maidenhead locator, try using https://www.f5len.org/tools/locator.  This will show show you an interactive world map with an overlay showing grid locator values up to 6 characters in length.

7. (OPTIONAL) Pat CAT control configuration.  Pat has the ability to control your radio using CAT commands issued via Hamlib/rigctld.  I don't normally use this when I am running Pat and **ardopcf** on my Raspberry Pi Zero 2W.  However, I have recently configured Pat to use it when I run Pat and **ardopcf** on my Windows 11 laptop.  If you are not already running Hamlib/rigctld and you want to, see [Hamlib_Windows11.md](Hamlib_Windows11.md) for instructions on how to install and configure it.  If you do not configure Pat to use Hamlib to control your radio, then you will need to manually tune your radio to the correct frequency and you must configure **ardopcf** to do handle PTT as described in [USAGE_windows.md](USAGE_windows.md).

Here is the portion of my config.json that configures Pat to use Hamlib (rigctld):

```
  "hamlib_rigs": {
    "G90": {
        "address": "localhost:4532",
        "network": "tcp"
    } 
  } 
```

The name "G90" is arbitrary, but will be used again in the "ardop" portion of the configuration.  I could just as easily called it "my-rig".  What kind of radio you have is defined in the Hamlib/rigctld setup, not here.  The address "localhost:4532" has two parts.  The part before the colon is the IP address of the machine where rigctld.exe is running.  'localhost' means that it is the same computer where Pat is running, and is equivalent to '127.0.0.1'.  If it is on a different machine, you would use the IP address of that machine: probably something like '192.168.100.103' or '10.0.0.5'.  The part after the colon is the TCP port number, which is set with the `-t` or `--port` option of rigcrld.  The value for "network" will usually (always?) be "tcp".

If Hamlib/rigctld is not running, or if the address:port number are not correct, then Pat will produce an error like "Unable to get frequency from rig G90: dial tcp [::1]:4532: connectx: No connection could be made because the target machine actively refused it."  Pat will print an error like this if it has a problem with any rig defined in "hamlib_rigs", even if the rig is not referenced by "ardop" as in the next step or anywhere else.  This happens because on startup, Pat attempts to get the current frequency setting for every rig in "hamlib_rigs".

8.  Ardop specific configuration.  Pat has the ability to use several different connection methods, one of which is "ardop".  To use **ardopcf** (or ardopc), find the "ardop" section of the configuration file and change it to be similar the following.  This is what I use when I want Pat to do CAT control and PTT using Hamlib as configured in the previous step.  If you will not be using Hamlib CAT control, then the "rig" and "ptt_ctrl" lines will be different as explained later:
```
  "ardop": {
    "addr": "localhost:8515",
    "arq_bandwidth": {
      "Forced": false,
      "Max": 500
    },
    "rig": "G90",
    "ptt_ctrl": true,
    "beacon_interval": 0,
    "cwid_enabled": false
  },
```

The addr "localhost:8515" is the default value and doesn't usually need to be changed.  It has two parts.  The part before the colon is the IP address of the machine where **ardopcf** is running.  'localhost' means that it **ardopcf** is running on the same computer where Pat is running, and is equivalent to '127.0.0.1'.  If **ardopcf** is running on a different machine, you would use the IP address of that machine: probably something like '192.168.100.103' or '10.0.0.5'.  The part after the colon is the TCP port number of the **ardopcf** host interface.  This is usually 8515 but can be set to a different value when starting **ardopcf**.  This is **NOT** the WebGui port number, which is typically 8514.

The arq_bandwidth "Forced" can always be set to false.  Otherwise, it would refuse a connection request with a different bandwidth.  The "Max" value has two effects.  First, it sets the amount of bandwidth that **ardopcf** is permitted to use if it responds to another station calling it.  This does not change the amount of bandwidth that **ardopcf** will use if initiating a connection to a Winlink Gateway.  So, it this is only important if you will also be using **ardopcf** for a Peer-to-Peer connection initiated by another station.  However, the other thing that the "Max" value does is controls how wide of a bandwidth the **ardopcf** busy detector considers before allowing you to transmit a Connect Request to another station.  So, this busy detector will be most appropraitely engaged if "Max" is set to the bandwidth that you intend to use.  Acceptable values are 200, 500, 10000, and 2000.  Setting it wider than the Connect Requests that you intend to send may prevent you from sending them if **ardopcf** detects traffic that is within the "Max" bandwidth, even if you would not be interfering with them.  On the other hand, setting it narrower that the Connect Requests that you inted to send may cause you to interfere with another station's traffic, which will also impair your own ability to communicate.  Since I normally connect to a Winlink gateway using a 500 Hz maximum bandwidth, I set Max to 500.  After you have used Pat for a while and determine what bandwidth settings you normally use, you may want to adjust this value..

The "Rig" value of "G90" matches the label used in the "hamlib_rigs" section, telling Pat to use this connection for CAT control when using Ardop. 

Setting "ptt_control" to true tells Pat to handle PTT on and off (using Hamlib).  If it is false, then **ardopcf** must handle PTT itself (or use VOX).

"beacon_interval" should always be set to zero to avoid sending Ardop beacons.  If a number is given here, it is interpreted an an interval in seconds at which an ardop FrameID will be sent when listening for an ardop connection.

As legally required, every 10 minutes during a long Ardop connection, and at the end of a winlink session, ardop will automatically send an IDFrame.  An IDFrame includes your callsign and location.  If "cwid_enabled" is true, then it will follow each IDFrame with a CW/Morse code version of your callsign.  By my reading of FCC rules, sending a CW ID is not required, though others may disagree.  So, I leave "cwid_enabled" as false.  If the amateur radio rules of your country require ID in CW, or if choose to ID in CW for any other reason, you may set "cwid_enabled" to true.

If I do not want Pat to do CAT ccontrol or handle PTT, then I congfigure **ardopcf** to handle PTT itself, I manually tune the radio to the correct frequency, and I change the following two lines in the "ardop" section of my Pat configuration file:
```
    "rig": "",
    "ptt_ctrl": false,
```

If "ptt_ctrl" is true, but a valid "rig" is not defined, then Pat will produce an error like "unable to set PTT rig '': not defined or not loaded."  As mentioned above, if Pat cannot connect to a rig defined in "hamlib_rigs" it will also produce an error. 

9. The settings configured the last few steps are sufficient to connect to another station using Ardop if you initiate the connection.  If you also want your station to listen for other stations calling it (for a Peer-to-Peer Winlink connection), you also most add "ardop" to the "listen" setting.  This is right before "hamlib_rigs" in the configuration file, and looks like:

` "listen": ["ardop"],`

or (if it is also listening for another connection, it might look like):

` "listen": ["ax25", "ardop"],`

Without this setting, **ardopcf** will not respond to a heard connection request.

10. When you are done editing config.json, type CTRL-S to save the file and then close Notepad.  The `pat configure` command will not terminate until it detects that Notepad has either been closed or at least has closed the 'config.json' file.


## Creating a Desktop Shortcut to start [Pat](https://getpat.io) Winlink

Pat has a web browser based GUI that works well in conjunction with the **ardopcf** WebGui.  This GUI is what inspired the creation of the **ardopcf** WebGui.  To start Pat so that you can connect to its GUI you can run `pat http` from the command line just like you ran `pat configure`.  However, for Windows users, it is more convenient to use a Desktop Shortcut and/or set Pat to start automatically every time you log on to your computer.  

### Create a Desktop Shortcut to start pat http as needed.

1. Find pat.exe with Windows File Explorer, Right click on it, Select `Show more options`, Select `Create shortcut`.  If you installed Pat in 'C:\Program Files', Windows will say that it can't create a shortcut here, and ask whether you want to place it on the Desktop instead.  Click `Yes`.  This will create a new icon on your Desktop with the name 'pat.exe - Shortcut'.  If you installed Pat in 'C:\Users\<USERNAME>', windows will create a shorcut in the same directory where pat.exe is located.  In that case, move the shortcut to the Desktop by dragging it to the desktop or using CTRL-X and CTRL-V.
2. Configure the shortcut: Right click on the shortcut icon on your Desktop, Select `Properties`.  Add ` http` to the end of the command line after pat.exe in the `Target` line.  If there is a space anywhere in the path to pat.exe, then this will be wrapped in double quotation marks.  In that case, ` http` must be placed after the closing quotion mark not within them.  So, in my case, I set the `Target` line to read:
`"C:\Program Files\pat_0.16.0_windows_i386\pat" http`

or 

`C:\User\<USERNAME>\pat_0.16.0_windows_i386\pat http`

3. Change the `Run:` pulldown to `Minimized`.  If you want to, you can also click on `Change Icon` and select a different icon to change the appearance of the shortcut on your desktop.  By switching to the `General` tab, you can also change the shortcut's label.  I change the label to `pat http`.  Finally, click on `OK`.  

4. Double clicking on this shortcut will start Pat.  When you are done using Pat, you can either right click on the icon in your task bar and then click `Close Window`, or you can left click on the icon in your task bar to open the window, and then either type CTRL-C or click on the red X in the upper right corner of the window.

5. (Optional) While Pat is running, you may choose to right click on its icon in the taskbar and then click on `Pin to taskbar`.  This will leave an icon for Pat in your taskbar when it is not running that can be used to start it.


### Set Pat http to automatically start whenever you log on to Windows

Depending on how you use your radio, computer, and other software on it, this may or may not work well.  If it does not, then starting Pat with the Desktop Shortcut each time you want to use it may be the better solution.

1. First create a Desktop Shortcut for Pat as described above. 

2. Use Windows File Explorer to Copy that shortcut from the Desktop to either:
`C:\Users\<USERNAME>\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup` or `C:\ProgramData\Microsoft\Windows\Start Menu\Programs\Startup`  The first option starts Pat http only when a specific user logs on to Windows, while the later starts it when any user logs on.  If you do not have admin rights on this windows computer, you can only use the first option.  If your PC has only one user account, then it shouldn't matter which one you choose.

If you want to stop Pat http, you can either right click on the icon in your task bar and then click `Close Window`, or you can left click on the icon in your task bar to open the window, and then either type CTRL-C or click on the red X in the upper right corner of the window.  The next time you logout or restart your computer and log back in, it will be automatically restarted.  In case you do this accidentally, it may be useful to keep the shortcut on your desktop.


## Using [Pat](https://getpat.io) Winlink with ardopcf for Windows

Pat's documentation for configuration and command line use is available at https://github.com/la5nta/pat/wiki/The-command-line-interface.

However, its web browser based http interface is much more convenient for most uses.  Pat's documentation for its Web GUI is available at https://github.com/la5nta/pat/wiki/The-web-GUI.

Unless you changed the "http_addr" setting when you ran `pat configure`, running `pat http` from the command line or using a Desktop Shortcut creates a web server listening on `http:\\localhost:8080`.  So, start `pat http` using the Desktop Shortct.  Then Open your web browser and type `localhost:8080` into the navigation bar and press Enter.  This should display the Pat web GUI.  

If everything is working, you should see your callsign in the upper left corner.  There are plenty of tutorials and demos on the internet for Pat.  The following describes only the basics of composing and sending a message using Pat and **ardopcf**.

### Compose a message using Pat http

1. In the Pat web GUI, click on `Action` and then `Compose ...`.  This opens a dialog that looks similar to a typical email program.  If you put a normal internet style email address like `somebody@gmail.com` in the `To` field, it will be sent to that email address.  If you put just a callsign like `AI7YN` in the `To` field then it will be sent to the Winlink email inbox of `AI7YN` the next time that `AI7YN` connects to a Winlink gateway.  

If `somebody@gmail.com` sends an email to `AI7YN@winlink.org`, `AI7YN` will also recieve that email the next time he connects to a Winlink gateway, but only if `somebody@gmail.com` has previously been sent an email by `AI7YN` or if `AI7YN` han manually added `somebody@gmail.com` to his whitelist by logging in to Winlink.org.  Once you have an account at winlink.org, this will also work with your callsign.  Allowing emails from non-winlink addresses only when added to a user specific whitelist helps prevent spam from being sent over the radio.

The `CC` and `Subject` fields work as expected.  If you want to understand use of the `P2P Only` checkbox and the `Template...` button, look for additional help on the internet.  Attachments can be added, but remember that large attachments as well as long messages may take a long time to transmit by HF radio, which can be very slow.

2.  When you are done composing the message, click on `Post`.  This queues the message in your outbox, but doens't actually send it until you connect to a Winlink Gateway (or another Winlink station using a P2P connection).  You can cancel creating a message by clicking the X in the upper right corner of the message dialog.

### Connecting to a Winlink Gateway using Pat http and Ardop

When you connect to a Winlink Gateway with Pat, it will send any queued messages that you have posted, and it will retrieve any messages that were sent to your callsign.

1. In the Pat web GUI, click on `Action` and then `Connect...`.  This opens the session dialog,  To connect using Ardop, set `transport` to `ARDOP`.

2. You can manually set `target` to the callsign of the station you want to connect to and `freq` to one of the frequencies that station is listening on.  Winlink gateway stations usually listen on multiple frequencies/bands so that they can be reached using whatever band is currently providing the best propogation to their location.  (If you are not using CAT control, then `freq` may be displayed, but is not used by Pat.  In that case you must manually tune your radio to the correct frequency.)  

While you can set `freq` manually, the more common approach is to click the `Show RMS list` button to see a list of available Winlink gateway stations.  (If the list is empty or outdated, AND you have an internet connection then clicking `Update cache` will download an updated version.)  To see only Ardop station, set the mode pulldown to `ARDOP`.  You can also filter the list to show only a specific band such as `40m`.  The list is shown in order from closest to furthest away, based on your location as set with `pat configure`.  If you click one one of these, it automatically populates the `target`, `freq`, and `bandwidth` fields.  Note that `bandwidth` is set to the maximum the the station accepts.  However, if you know (from experience) that due to distance, power, antenna, etc. that you are unlikely to be able to actaully use a high bandwidth setting like 1000 Hz or 2000 Hz, it can be advantageous to manually reduce `bandwidth` to 500 Hz.  

On HF, the closest stations are not neccessarily the ones most likely to work well for you.  This is especially true on the higher frequency bands like 20m, in which you cannot normally communicate with stations in the skip zone.  If you need help determining what Winlink stations you are likely to be able to communicate with, these resources may be helpful.  https://winlink.org/RMSChannels shows a map of all of the Winlink gateway stations.  Be sure to select ARDOP at the top of the map to show the stations that use Ardop.  Clicking on any of these stations shows a variety of information including what frequencies/bands it is listening on and the Gridsqure where it is located.  Using this information, as well as your own location, you can use the tools available at https://www.voacap.com/hf to predict the band/time combinations that are most likely to allow you to connect to that station.  By using Winlink regularly, you can learn which stations you can reliably connect to at what times of day and season.  This knowledge is will allow you to make these connections without having to use these sites or other online resources.

3.  When the settings are configured as desired, and you have made any necessary manual adjustments to your radio (such as antenna selection or tuning, or setting frequency if CAT control is not being used) and ardopcf (such as adjusting DRIVELEVEL setting), then click `Connect`.  This will initiate an automated process by which radio signals are alternately sent and recieved until a successfull conclusion is reached, a failed connection times out, or you manually terminate the connection by clicking on the text to the right of your callsign on the main Pat screen.  Pat shows a limited amount of information about the progress of the connection.  For more detail, use the **ardopcf** WebGui to monitor the connection.  
