# [Pat](https://getpat.io) Winlink with ardopcf for macOS

[Pat](https://getpat.io) is a [Winlink](https://winlink.org) client program that can be used to send and receive email messages using amateur radio, including when Internet and Cell service is not available. [ardopcf](https://github.com/pflarue/ardop) is one of several programs that Pat can use to connect to a radio. This page provides instructions specific to setting up Pat to use with **ardopcf** on a macOS computer.

Other related pages provide instructions on configuring and running [ardopcf for macOS](USAGE_macos.md), and configuring and running [Hamlib/rigctld](https://hamlib.github.io) (which Pat can use to control your radio).

Using Pat and Ardop may seem complex and confusing. If you need additional help, there are some users groups where you can ask for help:

This group is for everybody using Pat, though most are probably using Linux instead of macOS.

<https://groups.google.com/g/pat-users>

This group is for everybody using Ardop, including users of **ardopcf** and other Ardop implementations such as Ardop_Win.

<https://ardop.groups.io/g/users>

This group is for everybody using Winlink, though most are probably using Winlink Express rather than Pat, and most Winlink users don't use Ardop.

<https://groups.io/g/Winlink>

## Installing [Pat](https://getpat.io) Winlink on macOS

If you have already installed and used Pat with something other than **ardopcf**, you can skip the first few steps. However, in this case you might want to check whether a newer version of Pat is available using the link in step 1.

Follow [Pat's Installation Guide for MacOS](https://github.com/la5nta/pat/wiki/Install-FAQ#apple-macos)

Then follow Pat's [documentation for configuration and command line use](https://github.com/la5nta/pat/wiki/The-command-line-interface).

## Configuring [Pat](https://getpat.io) Winlink to use with ardopcf for macOS

1. Open Terminal (Applications > Utilities > Terminal or search for "Terminal" in Spotlight).

2. Type `pat configure` and press Enter. This will open your default text editor to edit the pat configuration file. The first time Pat is run, it creates a default version of this file at `~/.config/pat/config.json`. Unfortunately, if you damage the json structure of this file, pat won't be able to read it correctly and you will see an error like `Unable to load/write config: unexpected end of JSON input` when you try to run it. If that happens, you can delete this file and the next time you run `pat configure`, it will create a new default version. Because of this, once you get Pat working, it may also be a good idea to create a backup of this file. To avoid this problem, pay close attention to the required format and punctuation in this file.

3. Pat general configuration. Pat has the ability to use several different connection methods, one of which is Ardop. Before configuring Ardop, there are some general settings to configure. Here are the first few lines of my config.json file:

```json
{
  "mycall": "AI7YN",
  "secure_login_password": "PASSWORD",
  "auxiliary_addresses": [],
  "locator": "DM29ng",
```

Edit your config.json to include your callsign, your Winlink Password, and a locator value that indicates your location. If you change locations, such as for portable operations, you don't always need to update this value. However, if you move very far it will be useful to update this value, since this will help Pat to show you the correct distance to known Winlink gateway stations.

If you don't already have an account at `winlink.org`, then you can leave the "secure_login_password" value blank (just empty quotes followed by a comma). The first time you connect to a winlink server it will recognize you as a new user and send you a message with a temporary password. You will use this temporary password to login online and configure your account.

If you don't know your Maidenhead locator, try using <https://www.f5len.org/tools/locator>. This will show you an interactive world map with an overlay showing grid locator values up to 6 characters in length.

4. (OPTIONAL) Pat CAT control configuration. Pat has the ability to control your radio using CAT commands issued via Hamlib/rigctld. If you are not already running Hamlib/rigctld and you want to, you can install it using Homebrew:

```bash
brew install hamlib
```

If you do not configure Pat to use Hamlib to control your radio, then you will need to manually tune your radio to the correct frequency and you must configure **ardopcf** to handle PTT as described in [USAGE_macos.md](USAGE_macos.md).

Here is the portion of my config.json that configures Pat to use Hamlib (rigctld):

```json
  "hamlib_rigs": {
    "G90": {
        "address": "localhost:4532",
        "network": "tcp"
    }
  }
```

The name "G90" is arbitrary, but will be used again in the "ardop" portion of the configuration. I could just as easily called it "my-rig". What kind of radio you have is defined in the Hamlib/rigctld setup, not here. The address "localhost:4532" has two parts. The part before the colon is the IP address of the machine where rigctld is running. 'localhost' means that it is the same computer where Pat is running, and is equivalent to '127.0.0.1'. If it is on a different machine, you would use the IP address of that machine: probably something like '192.168.100.103' or '10.0.0.5'. The part after the colon is the TCP port number, which is set with the `-t` or `--port` option of rigctld. The value for "network" will usually (always?) be "tcp".

If Hamlib/rigctld is not running, or if the address:port number are not correct, then Pat will produce an error like "Unable to get frequency from rig G90: dial tcp [::1]:4532: connectx: Connection refused." Pat will print an error like this if it has a problem with any rig defined in "hamlib_rigs", even if the rig is not referenced by "ardop" as in the next step or anywhere else. This happens because on startup, Pat attempts to get the current frequency setting for every rig in "hamlib_rigs".

5. Ardop specific configuration. Pat has the ability to use several different connection methods, one of which is "ardop". To use **ardopcf** (or ardopc), find the "ardop" section of the configuration file and change it to be similar to the following. This is what I use when I want Pat to do CAT control and PTT using Hamlib as configured in the previous step. If you will not be using Hamlib CAT control, then the "rig" and "ptt_ctrl" lines will be different as explained later:

```json
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

The addr "localhost:8515" is the default value and doesn't usually need to be changed. It has two parts. The part before the colon is the IP address of the machine where **ardopcf** is running. 'localhost' means that **ardopcf** is running on the same computer where Pat is running, and is equivalent to '127.0.0.1'. If **ardopcf** is running on a different machine, you would use the IP address of that machine: probably something like '192.168.100.103' or '10.0.0.5'. The part after the colon is the TCP port number of the **ardopcf** host interface. This is usually 8515 but can be set to a different value when starting **ardopcf**. This is **NOT** the WebGui port number, which is typically 8514.

The arq_bandwidth "Forced" can always be set to false. Otherwise, it would refuse a connection request with a different bandwidth. The "Max" value has two effects. First, it sets the amount of bandwidth that **ardopcf** is permitted to use if it responds to another station calling it. This does not change the amount of bandwidth that **ardopcf** will use if initiating a connection to a Winlink Gateway. So, this is only important if you will also be using **ardopcf** for a Peer-to-Peer connection initiated by another station. However, the other thing that the "Max" value does is controls how wide of a bandwidth the **ardopcf** busy detector considers before allowing you to transmit a Connect Request to another station. So, this busy detector will be most appropriately engaged if "Max" is set to the bandwidth that you intend to use. Acceptable values are 200, 500, 1000, and 2000. Setting it wider than the Connect Requests that you intend to send may prevent you from sending them if **ardopcf** detects traffic that is within the "Max" bandwidth, even if you would not be interfering with them. On the other hand, setting it narrower than the Connect Requests that you intend to send may cause you to interfere with another station's traffic, which will also impair your own ability to communicate. Since I normally connect to a Winlink gateway using a 500 Hz maximum bandwidth, I set Max to 500. After you have used Pat for a while and determine what bandwidth settings you normally use, you may want to adjust this value.

The "rig" value of "G90" matches the label used in the "hamlib_rigs" section, telling Pat to use this connection for CAT control when using Ardop.

Setting "ptt_ctrl" to true tells Pat to handle PTT on and off (using Hamlib). If it is false, then **ardopcf** must handle PTT itself (or use VOX).

"beacon_interval" should always be set to zero to avoid sending Ardop beacons. If a number is given here, it is interpreted as an interval in seconds at which an ardop FrameID will be sent when listening for an ardop connection.

As legally required, every 10 minutes during a long Ardop connection, and at the end of a winlink session, ardop will automatically send an IDFrame. An IDFrame includes your callsign and location. If "cwid_enabled" is true, then it will follow each IDFrame with a CW/Morse code version of your callsign. By my reading of FCC rules, sending a CW ID is not required, though others may disagree. So, I leave "cwid_enabled" as false. If the amateur radio rules of your country require ID in CW, or if you choose to ID in CW for any other reason, you may set "cwid_enabled" to true.

If I do not want Pat to do CAT control or handle PTT, then I configure **ardopcf** to handle PTT itself, I manually tune the radio to the correct frequency, and I change the following two lines in the "ardop" section of my Pat configuration file:

```json
    "rig": "",
    "ptt_ctrl": false,
```

If "ptt_ctrl" is true, but a valid "rig" is not defined, then Pat will produce an error like "unable to set PTT rig '': not defined or not loaded." As mentioned above, if Pat cannot connect to a rig defined in "hamlib_rigs" it will also produce an error.

6. The settings configured in the last few steps are sufficient to connect to another station using Ardop if you initiate the connection. If you also want your station to listen for other stations calling it (for a Peer-to-Peer Winlink connection), you also must add "ardop" to the "listen" setting. This is right before "hamlib_rigs" in the configuration file, and looks like:

```json
"listen": ["ardop"],
```

or (if it is also listening for another connection, it might look like):

```json
"listen": ["ax25", "ardop"],
```

Without this setting, **ardopcf** will not respond to a heard connection request.

7. When you are done editing config.json, save the file and exit your text editor. The `pat configure` command will not terminate until it detects that your editor has closed.

## Starting Pat on macOS

### Running Pat from Terminal

Pat has a web browser based GUI that works well in conjunction with the **ardopcf** WebGui. This GUI is what inspired the creation of the **ardopcf** WebGui. To start Pat so that you can connect to its GUI, you can run `pat http` from Terminal:

```bash
pat http
```

Unless you changed the "http_addr" setting when you ran `pat configure`, this creates a web server listening on `http://localhost:8080`. Open your web browser and navigate to `localhost:8080` to access the Pat web GUI.

### Creating a Script for Easy Startup

You can create a shell script to make starting Pat easier:

1. Create a script file:

```bash
nano ~/bin/start-pat
```

2. Add the following content:

```bash
#!/bin/bash
pat http
```

3. Make it executable:

```bash
chmod +x ~/bin/start-pat
```

4. Now you can start Pat by running:

```bash
start-pat
```

### Creating an Automator Application (Optional)

For a more macOS-native experience, you can create an Automator application:

1. Open Automator (Applications > Automator)
2. Choose "Application" as the document type
3. Search for "Run Shell Script" and drag it to the workflow area
4. In the shell script box, enter:

```bash
/usr/local/bin/pat http
# or ~/bin/pat http if you installed it there
```

5. Save the application as "Pat Winlink" in your Applications folder
6. You can now start Pat by double-clicking the application

## Using [Pat](https://getpat.io) Winlink with ardopcf for macOS

Pat's documentation for configuration and command line use is available at <https://github.com/la5nta/pat/wiki/The-command-line-interface>.

However, its web browser based http interface is much more convenient for most uses. Pat's documentation for its Web GUI is available at <https://github.com/la5nta/pat/wiki/The-web-GUI>.

If everything is working, you should see your callsign in the upper left corner of the Pat web GUI. There are plenty of tutorials and demos on the internet for Pat. The following describes only the basics of composing and sending a message using Pat and **ardopcf**.

### Compose a message using Pat http

1. In the Pat web GUI, click on `Action` and then `Compose ...`. This opens a dialog that looks similar to a typical email program. If you put a normal internet style email address like `somebody@gmail.com` in the `To` field, it will be sent to that email address. If you put just a callsign like `AI7YN` in the `To` field then it will be sent to the Winlink email inbox of `AI7YN` the next time that `AI7YN` connects to a Winlink gateway.

If `somebody@gmail.com` sends an email to `AI7YN@winlink.org`, `AI7YN` will also receive that email the next time he connects to a Winlink gateway, but only if `somebody@gmail.com` has previously been sent an email by `AI7YN` or if `AI7YN` has manually added `somebody@gmail.com` to his whitelist by logging in to Winlink.org. Once you have an account at winlink.org, this will also work with your callsign. Allowing emails from non-winlink addresses only when added to a user specific whitelist helps prevent spam from being sent over the radio.

The `CC` and `Subject` fields work as expected. If you want to understand use of the `P2P Only` checkbox and the `Template...` button, look for additional help on the internet. Attachments can be added, but remember that large attachments as well as long messages may take a long time to transmit by HF radio, which can be very slow.

2. When you are done composing the message, click on `Post`. This queues the message in your outbox, but doesn't actually send it until you connect to a Winlink Gateway (or another Winlink station using a P2P connection). You can cancel creating a message by clicking the X in the upper right corner of the message dialog.

### Connecting to a Winlink Gateway using Pat http and Ardop

When you connect to a Winlink Gateway with Pat, it will send any queued messages that you have posted, and it will retrieve any messages that were sent to your callsign.

1. In the Pat web GUI, click on `Action` and then `Connect...`. This opens the session dialog. To connect using Ardop, set `transport` to `ARDOP`.

2. You can manually set `target` to the callsign of the station you want to connect to and `freq` to one of the frequencies that station is listening on. Winlink gateway stations usually listen on multiple frequencies/bands so that they can be reached using whatever band is currently providing the best propagation to their location. (If you are not using CAT control, then `freq` may be displayed, but is not used by Pat. In that case you must manually tune your radio to the correct frequency.)

While you can set `freq` manually, the more common approach is to click the `Show RMS list` button to see a list of available Winlink gateway stations. (If the list is empty or outdated, AND you have an internet connection then clicking `Update cache` will download an updated version.) To see only Ardop stations, set the mode pulldown to `ARDOP`. You can also filter the list to show only a specific band such as `40m`. The list is shown in order from closest to furthest away, based on your location as set with `pat configure`. If you click on one of these, it automatically populates the `target`, `freq`, and `bandwidth` fields. Note that `bandwidth` is set to the maximum that the station accepts. However, if you know (from experience) that due to distance, power, antenna, etc. that you are unlikely to be able to actually use a high bandwidth setting like 1000 Hz or 2000 Hz, it can be advantageous to manually reduce `bandwidth` to 500 Hz.

**ardopcf** and other Ardop implementations adjust the data frame types being used to try to achieve the highest data transfer rate possible for the given band conditions. They continue to make these adjustments throughout the session so as to react to changing conditions. However, not all valid data frame types are available for use in all Ardop sessions. The connection bandwidth negotiated by the two stations upon establishing a connection establishes a subset of the data frame types that may be used during that session. These include frame types of the connection bandwidth as well as some lower bandwidth data frame types. However, the full complement of lower bandwidth frame types are not available when a higher bandwidth connection is established. As a result, choosing a connection bandwidth greater than you believe will actually be usable may result in decreased performance.

3. When the settings are configured as desired, and you have made any necessary manual adjustments to your radio (such as antenna selection or tuning, or setting frequency if CAT control is not being used) and ardopcf (such as adjusting DRIVELEVEL setting), then click `Connect`. This will initiate an automated process by which radio signals are alternately sent and received until a successful conclusion is reached, a failed connection times out, or you manually terminate the connection by clicking on the text to the right of your callsign on the main Pat screen. Pat shows a limited amount of information about the progress of the connection. For more detail, use the **ardopcf** WebGui to monitor the connection.

## macOS-Specific Considerations

### Audio Permissions

macOS may require microphone permissions for Pat and ardopcf to work with audio devices. If you encounter permission errors:

1. Go to System Preferences > Security & Privacy > Privacy > Microphone
2. Add Terminal, Pat, or your script launcher to the allowed applications
3. Restart the applications after granting permissions

### Running Multiple Applications

You can run both Pat and ardopcf simultaneously. A typical workflow is:

1. Start **ardopcf** in one Terminal window:

```bash
ardopcf --logdir ~/ardop_logs -G 8514 8515 "USB Audio Device" "USB Audio Device"
```

2. Start Pat in another Terminal window:

```bash
pat http
```

3. Open two browser tabs:
   - `localhost:8080` for Pat
   - `localhost:8514` for ardopcf WebGui

### Using Terminal Multiplexer

For better organization, you can use tmux or screen:

```bash
# Install tmux if not already installed
brew install tmux

# Start tmux session
tmux new-session -d -s winlink

# Create windows for ardopcf and pat
tmux new-window -t winlink -n ardopcf 'ardopcf --logdir ~/ardop_logs -G 8514 8515 "USB Audio Device" "USB Audio Device"'
tmux new-window -t winlink -n pat 'pat http'

# Attach to session
tmux attach-session -t winlink
```

This allows you to easily switch between ardopcf and Pat, and both will continue running even if you close Terminal windows.
