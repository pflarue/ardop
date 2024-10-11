# ARDOPCF Command Line Options

## 1. Usage

 ardopcf &lt;host-tcp-port&gt; [ &lt;audio-capture-device&gt; &lt;audio-playback-device&gt; ]

- **host-tcp-port** is telnet port for input of host commands and output of status messages
- **audio-capture-device** specifies which audio input device to use as input for ardopcf. Different formats are used in Linux and Windows, see below.
- **audio-playback-device** specifies audio output device. Different formats are used in Linux and Windows, see below.

## 2. CAT and PTT

**-c&lt;serial-port&gt;[:&lt;baudrate&gt;]**<br>
**--cat &lt;serial-port&gt;[:&lt;baudrate&gt;]**
<br>serial port to send CAT commands to the radio, e.g. _-c COM9:38400_

**-k &lt;hex-string&gt;** <br>
**--keystring &lt;hex-string&gt;** <br>
CAT command to switch radio to transmit mode. E.g. for Kenwood, Elecraft, QDX, QMX, TX-500 the command is "TX;", the actual command line option will be _-k 54583B_

**-u &lt;hex-string&gt;** <br>
**--unkeystring &lt;hex-string&gt;** <br> 
CAT command to switch radio to receive mode. E.g. for Kenwood, Elecraft, QDX, QMX, TX-500 the command is "RX;", the actual command line option will be _-k 52583B_

Dial frequency cannot be controlled by ardopcf as of now.

**-p &lt;serial-port&gt;**<br>
**--ptt &lt;serial-port&gt;**
<br>serial device to activate radio PTT using RTS signal. May be the same device as CAT.

### PTT control via GPIO pin (ARM only)

**-g [&lt;pin-number&gt;]**<br>
ARM CPU GPIO pin used as PTT. Must be integer. If empty (no pin value), 17 will be used as default.

## 3. Web GUI

**-G &lt;TCP port&gt;**<br> 
**--webgui &lt;TCP port&gt;**<br>
TCP port to access web GUI. By convention it is 
number of the host TCP port minus one.




