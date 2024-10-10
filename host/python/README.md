### About `diagnostichost.py`

This directory contains a simple command line python based interactive host program that was created 
to facilitate development and debugging work on ardopcf.  **It is not intended for actual communication
using Ardop.  It is a diagnostic/development tool.** It works on both Linux and Windows, and has
a very simple text based interface.  It can be used when connected to a headless Linux machine
via an SSH connection.  It can also be run on any machine on the same local network as the machine
where ardopcf is running.

#### The module docstring from `diagnostichost.py` in this directory:

A diagnostic command line host program for Ardop

This module provides a simple command line interface that can be used to control and monitor
Ardop for diagnostic purposes. It is NOT intended for actual communication using Ardop. Its use
requires an understanding of the host commands supported by Ardop. It was built to use with
ardopcf, but should also work with other Ardop implementations.

This host program does not attempt to do any CAT control or activate the radio's PTT. So, the user
must manually adjust the radio's frequency, mode, and any other required settings. Also, Ardop must
be configured to control PTT itself. Alternatively, PTT may be controled using the radio's VOX
feature.

This python module can run on the same machine where Ardop is running, in which case the default
host of "localhost" can be used.  It can also be run on a different machine on the same local
network using an ip address or machine name for the --host argument.

This should work on both Linux and Windows if a python command line tool is available.  On Linux,
it has been tested in a terminal window using the python command line program typically installed
by default.  On windows, it has been tested using the WinPython Command Prompt from an installation
of Python from https://winpython.github.io.  It may or may not work with other configurations. This
module may be run as a script with no installation required. Only modules included in the python
standard library are required.

Using the --strings and/or --files option, this can also be used to send commands non-interactively
to a running Ardop instance from the command line. For non-interactive use, include "::quit" at the
end of the last string. Without "::quit", --strings and/or --files can be used to configure some
settings before beginning interactive use.  See the example_input.txt file for additional details
and commentary on using --files.

#### Basic help from running `python diagnostichost.py -h`
```
usage: diagnostichost.py [-h] [-H HOST] [-n] [-p PORT] [-f FILES [FILES ...]] [-s STRINGS [STRINGS ...]]

A diagnostic command line host program for Ardop. Interactive usage instructions are printed upon calling the run() method.

optional arguments:
  -h, --help            show this help message and exit
  -H HOST, --host HOST  Ardop host name or ip address (default is "localhost").
  -n, --nohelp          Don't print the interactive usage instructions.
  -p PORT, --port PORT  Ardop command port number (default it 8515, data port is 1 higher).
  -f FILES [FILES ...], --files FILES [FILES ...]
                        filepaths for one or more files containing input text.
  -s STRINGS [STRINGS ...], --strings STRINGS [STRINGS ...]
                        One or more strings containing input text (typically wrapped in double quotes). Each string is treated as if <ENTER> was
                        pressed after it was typed.
```

#### The instructions for interactive use printed by default upon running `python diagnostichost.py`

A diagnostic command line host program for Ardop

Text typed at the "INPUT: " prompt is passed to Ardop as data upon pressing
[ENTER]. Characters other than basic ASCII 0x20-0xFE are discarded, as are tab and
left/right arrow keys. The backspace key works as expected. Up/down arrows scroll back
through a short buffer of recent commands. "\\n" is used to create multiple lines of
text data. "\\xHH" where HH are hex digits is used to enter non-text bytes. "\\\\" is
replaced with "\\", and is used to prevent a "\\" from being interpreted as part of
"\\n" or "\\xHH". Long input lines, especially when longer than the width of the
screen, may cause display problems. However, all commands are short and data can be
entered as multiple shorter lines.

Data passed to Ardop is printed as a "DATA >>>>" line. Data from Ardop is printed as
a "DATA <<" line, which also includes the data type indicator provided by Ardop

A line of input that begins with an colon (:) is passed to the Ardop
command socket. Ardop host commands and their parameters are mostly case insensitive.
Strings passed to the Ardop command port are printed as "CMD  >>>>" lines. Strings
received from the Ardop command port are printed as "CMD  <<" lines.

Internal commands interpreted by this host program begin with "::". "::quit" exits
this host program. "::rndXX", "::rndtXX", and "::zerosXX" sends XX random bytes, random
printable text bytes, and null bytes respectively to the Ardop data port where XX are
one or more digits. "::pause" causes a brief delay to let input from Ardop be read and
processed before processing the next line of user input.

#### `example_input.txt`

This file is an example file that can be used as an input script for `diagnostichost.py`.  
It contains comments to further explain how such files can be used.
