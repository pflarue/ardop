""" A diagnostic command line host program for Ardop

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
"""

import argparse
import os
from random import randbytes, choices
import re
import selectors
import socket
import sys
import time

if os.name == 'posix':
    import termios
    import tty
else:
    # Windows
    import msvcrt

class DiagnosticHost():
    """A diagnostic command line host program for Ardop"""
    _READ_SIZE = 1024
    _INPUT_PROMPT = 'INPUT: '
    _PROMPT_LEN = len(_INPUT_PROMPT)

    def __init__(self, host='localhost', port=8515, files=None, strings=None, nohelp=False):
        self._nohelp = nohelp
        self._state = 0   # 0 for initialized, 1 for running, -1 for closed
        self._cbuf = b''  # Buffer for input from Ardop's command port
        self._dbuf = b''  # Buffer for input from Ardop's data port
        self._inbuf = ''  # Buffer for user input
        self._old_inbuf = ''  # Backup copy of self._inbuf for change detection and erasure.
        # Keep a copy of recent input lines which can be accessed using up and down arrow keys.
        self._history = [''] + [None] * 10
        self._hindex = 0  # Index into self._history
        # Command socket
        self._csock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self._csock.connect((host, port))
        except ConnectionError:
            print("\nUnable to connect to command socket.  Exiting.")
            self.close()
            return
        self._csock.setblocking(0)
        # Data socket
        self._dsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self._dsock.connect((host, port + 1))
        except ConnectionError:
            print("\nUnable to connect to data socket.  Exiting.")
            self.close()
            return
        self._dsock.setblocking(0)

        self._sel = selectors.DefaultSelector()
        self._sel.register(self._csock, selectors.EVENT_READ, None)
        self._sel.register(self._dsock, selectors.EVENT_READ, None)

        # If both files and strings are provided, all files will be processed before strings.
        # These are loaded into the input buffer as if they were typed by the user.
        for file in files or []:
            with open(file) as f:
                self._inbuf += f.read()
        # A line from a file starting with # is treated as a comment to be ignored.
        self._inbuf = re.sub('(?m)^#.*\n', '', self._inbuf)
        for string in strings or []:
            self._inbuf += string + '\n'

        if os.name == 'posix':
            self._ttyattr = termios.tcgetattr(sys.stdin)
            tty.setcbreak(sys.stdin.fileno())
            os.set_blocking(sys.stdin.fileno(), False)

    def run(self):
        """Run the main input/output processing loop"""
        if self._state != 0:
            return  # either already running or already closed.
        self._state = 1  # running
        if not self._nohelp:
            print(self.helpstr)
        print(f'{self._INPUT_PROMPT}', end='', flush=True)
        self._process_inbuf()  # Process input from files and/or strings before starting main loop.

        while self._state == 1:  # running
            self._from_kbd()  # Get and process input from keyboard
            self._from_ardop()  # Get and process (print) input from Ardop
            time.sleep(0.02)  # Pause before repeating this loop since nothing in loop is blocking.

    def close(self):
        """Close this host program"""
        if self._state == -1:
            return  # already closed
        self._state = -1  # closed
        if os.name == 'posix':
            # restore tty attributes to prior settings.
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, self._ttyattr)
        if hasattr(self, "_sel"):
            self._sel.close()
        if hasattr(self, "_csock"):
            self._csock.close()
        if hasattr(self, "_dsock"):
            self._dsock.close()
        print()

    # __enter__() and __exit__() allow use as a context for better cleanup.
    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()

    # Text display handling uses a simple approach of erasing and reprinting the content of the
    # input buffer whenever it changes.  When something else needs to be printed, such as input
    # from Ardop or a copy of what has been sent to an Ardop port, the content of the input
    # buffer and the input prompt are deleted, the new text is printed, and then the prompt and
    # input buffer are reprinted.  If the input buffer contains a '\n', then printing of it is
    # deferred until all complete lines of text have been processed and removed. This is not
    # particularly efficient, but it works OK for this application.

    def _println(self, s):
        # Print s, and then reprint the input line with the current self._inbuf
        print('\b \b' * (self._PROMPT_LEN + len(self._old_inbuf)) + s + f'\n{self._INPUT_PROMPT}',
            end='', flush=True)
        if '\n' not in self._inbuf: # Defer printing self._inbuf with multiple lines of input
            print(f'{self._inbuf}', end='', flush=True)
            self._old_inbuf = self._inbuf[:]

    def _update_inputline(self):
        # reprint the input line if self._inbuf has changed
        if self._inbuf == self._old_inbuf:
            return  # no update required.
        print('\b \b' * (len(self._old_inbuf)), end='', flush=True)
        if '\n' not in self._inbuf: # Defer printing self._inbuf with multiple lines of input
            print(f'{self._inbuf}', end='', flush=True)
            self._old_inbuf = self._inbuf[:]


    def _process_inbuf(self):
        # Process the contents of self._inbuf.
        if self._state != 1:
            return  # Not running
        # Parse _inbuf for ansi terminal code \x1b[A for up arrow and \x1b[B
        if '\x1b[A' in self._inbuf:  # Up arrow
            # Replace current contents in self._inbuf with a recent input line
            self._hindex = min(self._hindex + 1, len(self._history) - 1)
            if self._history[self._hindex] is None:
                self._hindex -= 1
            self._inbuf = self._history[self._hindex]
            self._update_inputline()
            return
        if '\x1b[B' in self._inbuf:  # Down arrow
            # Replace current contents in self._inbuf with a recent input line
            self._hindex = max(self._hindex - 1, 0)
            self._inbuf = self._history[self._hindex]
            self._update_inputline()
            return
        if (m := re.search('[^\n\x20-\x7F]', self._inbuf)):
            # Discard any out of range character and anything that
            # follows it.
            self._inbuf = self._inbuf[:m.start()]
        # Process backspace(s).
        while '\x7f' in self._inbuf:
            self._inbuf = self._inbuf.lstrip('\x7F')
            self._inbuf = re.sub('[^\x7F]\x7F', '', self._inbuf)
        # discard any blank lines
        self._inbuf = self._inbuf.lstrip('\n')
        while '\n\n' in self._inbuf:
            self._inbuf = self._inbuf.replace('\n\n', '\n')
        while '\n' in self._inbuf:
            # Process a completed input line that ends with \n.
            linelen = self._inbuf.index('\n')
            inputline = self._inbuf[:linelen]
            self._inbuf = self._inbuf[linelen + 1:]
            # Store a copy of this current line in self._history
            self._history[1:] = [inputline[:]] + self._history[1:-1]
            self._hindex = 0
            if not self._process_input_line(inputline):
                return
        self._update_inputline()

    def _process_input_line(self, inputline):
        # Process a single line of input from the keyboard (or files or strings)
        data = b''  # no data currently queued to pass to Ardop
        if inputline.startswith('::'):
            # A command internal to this host program
            if inputline.rstrip().lower() == '::quit':
                self._println('(quitting)')
                time.sleep(0.05)  # Give Ardop time to respond to prior input
                # This gets responses from Ardop for recent host commands before closing.
                self._from_ardop()  # Get and process (print) input from Ardop
                self.close()
                return False
            if inputline.rstrip().lower() == '::pause':
                self._println('(pause)')
                time.sleep(0.05)  # Give Ardop time to respond to prior input
                self._from_ardop()  # Get and process (print) input from Ardop
            elif (m := re.fullmatch('::rnd([0-9]+)', inputline.rstrip().lower())) is not None:
                data = randbytes(int(m.group(1)))  # random bytes
            elif (m := re.fullmatch('::rndt([0-9]+)', inputline.rstrip().lower())) is not None:
                data = bytes(choices(range(0x20, 0x7E), k=int(m.group(1))))  # printable text
            elif (m := re.fullmatch('::zeros([0-9]+)', inputline.rstrip().lower())) is not None:
                data = b'\x00' * int(m.group(1))  # zero bytes
            else:
                self._println(f'Ingoring invalid internal command: {inputline}')
        elif inputline.startswith(':'):
            # Send to Ardop as a host command
            self._println(f'CMD  >>>> "{inputline[1:]}"')
            try:  # TODO: buffer and select() to send?
                self._csock.send((inputline[1:] + '\r').encode())
            except BrokenPipeError:
                print("\nCommand socket write failure.  Exiting.")
                self.close()
        else:
            # Send to Ardop as data
            data = inputline.encode()
            # Replace '\\n' (but not '\\\\n') in data with '\n'
            data = re.sub(rb"(?<!\\)\\n", b'\n', data)
            # Replace '\\xHH' (but not '\\\\xHH') in data byte values
            data = re.sub(
                rb"(?<!\\)\\x[0-9a-fA-F][0-9a-fA-F]",
                lambda m: bytes.fromhex(m.group(0).decode()[2:]),
                data)
            # Replace '\\\\' in data with '\\' (to prevent replacement of '\\n' or '\\xHH').
            data = data.replace(b'\\\\', b'\\')
        if (size := len(data)) != 0:
            data = bytes([size >> 8, size & 0xFF]) + data
            self._println(f'DATA >>>> (size={size}=0x{data[:2].hex()}): {data[2:]}')
            try:  # TODO: buffer and select() to send?
                self._dsock.send(data)
            except BrokenPipeError:
                print("\nData socket write failure.  Exiting.")
                self.close()
                return False
        self._update_inputline()
        return True

    def _from_kbd(self):
        # Put keyboard input into self._inbuf
        if self._state != 1:
            return  # Not running
        # For linux, this could have been done as part of self._sel.select(), but that doesn't work
        # for Windows, so handle keyboard input here for both OS.
        if os.name == 'posix':
            # stdin has been set to non-blocking
            self._inbuf += sys.stdin.read(self._READ_SIZE)
        else:
            if msvcrt.kbhit():
                key = msvcrt.getch()
                if key in b'\x00\xe0':
                    key = msvcrt.getch()
                    if key == b'\x48':  # (H) Up arrow.  Insert corresponding ANSI terminal code
                        self._inbuf += '\x1b[A'
                    if key == b'\x50':  # (P) Down arrow.  Insert corresponding ANSI terminal code
                        self._inbuf += '\x1b[B'
                    # discard all other control characters
                elif key == b'\r':  # Enter
                    self._inbuf += '\n'
                elif key == b'\x08':  # Backspace
                    self._inbuf += '\x7F'
                else:  # all other keys
                    self._inbuf += key.decode()
        self._process_inbuf()  # Process the input read from the keyboard

    def _from_ardop(self):
        # Get data from Ardop command and data sockets and process (print) it.
        if self._state != 1:
            return  # Not running
        # For Linux, select() could have include stdin, and select() could have been allowed to
        # block. Windows does not allow select() to be used with stdin, so keyboard input is
        # handled separately and timeout=0 is used to make select() non-blocking.
        events = self._sel.select(timeout=0)
        for key, _ in events:
            if key.fileobj == self._csock:
                self._cbuf += (new_data := self._csock.recv(self._READ_SIZE))
                if len(new_data) == 0:
                    print("\nCommand socket read failure.  Exiting.")
                    self.close()
                    return
            elif key.fileobj == self._dsock:
                self._dbuf += (new_data := self._dsock.recv(self._READ_SIZE))
                if len(new_data) == 0:
                    print("\nData socket read failure.  Exiting.")
                    self.close()
                    return
        self._process_cbuf()  # Process input from the Ardop command socket
        self._process_dbuf()  # Process input from the Ardop data socket

    def _process_cbuf(self):
        # Process input from the Ardop command socket
        if self._state != 1:
            return  # Not running
        # Input lines from the Ardop command socket end with b'\r'
        while b'\r' in self._cbuf:
            cmdlen = self._cbuf.index(b'\r')
            if b'INPUTPEAKS' not in self._cbuf[:cmdlen]:
                # Print all CMD except INPUTPEAKS (which clutter the screen)
                self._println(f'CMD  << "{self._cbuf[:cmdlen].decode()}"')
            self._cbuf = self._cbuf[cmdlen + 1:]

    def _process_dbuf(self):
        # Process input from the Ardop data socket
        if self._state != 1:
            return  # Not running
        # Input from the Ardop data socket is in blocks that begin with a 2-byte size, followed
        # by a 3-byte data type indicator, followed by the raw data. size includes the length of
        # the type indicator, but not the 2-bytes used for the size itself.
        # So, no data block will be smaler than 5 bytes (for an empty block).
        while len(self._dbuf) >= 5:
            if len(self._dbuf) < 2 + (size := (self._dbuf[0] << 8) + self._dbuf[1]):
                return  # Data block is incomplete
            is_utf8 = True
            try:  # Try printing this as utf-8 (default encoding) text.
                self._println(
                    f'DATA << (size={size-3} type={self._dbuf[2:5]}) (UTF8 text)'
                    f' : "{self._dbuf[5:size+2].decode()}"')
            except UnicodeDecodeError:
                is_utf8 = False  # Something in the received data cannot be decoded as utf-8.
            if not is_utf8:
                # Print as a bytes object which will display non-ascii characters as \xHH hex bytes.
                self._println(
                    f'DATA << (size={size-3} type={self._dbuf[2:5]}) (bytes) :'
                    f' {self._dbuf[5:size+2]}')
            self._dbuf = self._dbuf[size + 2:]

    helpstr = (
        f'A diagnostic command line host program for Ardop\n\n'
        f'   Text typed at the "{_INPUT_PROMPT}" prompt is passed to Ardop as data upon pressing'
        f' <ENTER>. Characters other than basic ASCII 0x20-0xFE are discarded, as are tab and'
        f' left/right arrow keys. The backspace key works as expected. Up/down arrows scroll back'
        f' through a short buffer of recent commands. "\\n" is used to create multiple lines of'
        f' text data. "\\xHH" where HH are hex digits is used to enter non-text bytes. "\\\\" is'
        f' replaced with "\\", and is used to prevent a "\\" from being interpreted as part of'
        f' "\\n" or "\\xHH". Long input lines, especially when longer than the width of the'
        f' screen, may cause display problems. However, all commands are short and data can be'
        f' entered as multiple shorter lines.\n'
        f'   Data passed to Ardop is printed as a "DATA >>>>" line. Data from Ardop is printed as'
        f' a "DATA <<" line, which also includes the data type indicator provided by Ardop\n'
        f'   A line of input that begins with an colon (:) is passed to the Ardop'
        f' command socket. Ardop host commands and their parameters are mostly case insensitive.'
        f' Strings passed to the Ardop command port are printed as "CMD  >>>>" lines. Strings'
        f' received from the Ardop command port are printed as "CMD  <<" lines.\n'
        f'   Internal commands interpreted by this host program begin with "::". "::quit" exits'
        f' this host program. "::rndXX", "::rndtXX", and "::zerosXX" sends XX random bytes, random'
        f' printable text bytes, and null bytes respectively to the Ardop data port where XX are'
        f' one or more digits. "::pause" causes a brief delay to let input from Ardop be read and'
        f' processed before processing the next line of user input.\n')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description=(
            'A diagnostic command line host program for Ardop.  Interactive'
            ' usage instructions are printed upon calling the run() method.'))
    parser.add_argument('-H', '--host', type=str, default='localhost',
        help='Ardop host name or ip address (default is "localhost").')
    parser.add_argument('-n', '--nohelp', action='store_true', default=False,
        help='Don\'t print the interactive usage instructions.')
    parser.add_argument('-p', '--port', type=int, default=8515,
        help='Ardop command port number (default it 8515, data port is 1 higher).')
    parser.add_argument('-f', '--files', type=str, nargs='+',
        help='filepaths for one or more files containing input text.')
    parser.add_argument('-s', '--strings', type=str, nargs='+',
        help=(
            'One or more strings containing input text (typically wrapped in double quotes).'
            ' Each string is treated as if <ENTER> was pressed after it was typed.'))
    args = parser.parse_args()

    with DiagnosticHost(
            host=args.host,
            port=args.port,
            files=args.files,
            strings=args.strings,
            nohelp=args.nohelp,
    ) as dh:
        dh.run()
