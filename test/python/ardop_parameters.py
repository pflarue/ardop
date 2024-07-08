"""
This module provides some constants for use by other Python test modules.
"""

# Path to the ardopcf executable
APATH = "../../ardopcf"
# Suggested location for temporary files including WAV recordings
# and log files.  WAV files written to this directory should be deleted
# after they have been used.  However, when a test failure occurs, the
# associated WAV file is generally retained to allow the possibility of
# manually rerunning the test for debugging purposes.  Log files written
# to this directory are not automatically deleted.
TMPPATH = "./tmp"

# This lists all of the non-data frame types.
# The name of each type, as usable as a parameter to the TXFRAME host
# command, is given along with a string containing suitable example
# values for the other required parameters.  Some control frames
# also accept a SessionID as a final parameter, but will default to 0xFF
# when none is provided.
CONTROLFRAMES = [
    ("DataNAK", "50"),  # 0x00-0x1F.  Quality(0-100)
    ("BREAK", ""),  # 0x23
    ("IDLE", ""),  # 0x24
    ("DISC", ""),  # 0x29
    ("END", ""),  # 0x2C
    ("ConRejBusy", ""),  # 0x2D
    ("ConRejBW", ""),  # 0x2E
    ("IDFrame", "n0call dm"),  # 0x30.  Callsign Gridsquare
    ("ConReq200M", "n0call n0call-1"),  # 0x31 targetCallsign localCallsign
    ("ConReq500M", "n0call n0call-1"),  # 0x32 targetCallsign localCallsign
    ("ConReq1000M", "n0call n0call-1"),  # 0x33 targetCallsign localCallsign
    ("ConReq2000M", "n0call n0call-1"),  # 0x34 targetCallsign localCallsign
    ("ConReq200F", "n0call n0call-1"),  # 0x35 targetCallsign localCallsign
    ("ConReq500F", "n0call n0call-1"),  # 0x36 targetCallsign localCallsign
    ("ConReq1000F", "n0call n0call-1"),  # 0x37 targetCallsign localCallsign
    ("ConReq2000F", "n0call n0call-1"),  # 0x38 targetCallsign localCallsign
    ("ConAck200", "500"),  # 0x39 ReceivedLeaederLength(0-2550)
    ("ConAck500", "500"),  # 0x3A ReceivedLeaederLength(0-2550)
    ("ConAck1000", "500"),  # 0x3B ReceivedLeaederLength(0-2550)
    ("ConAck2000", "500"),  # 0x3C ReceivedLeaederLength(0-2550)
    ("PingAck", "10 60"),  # 0x3D SignalToNoiseRatio(0-21) Quality(30-100)
    ("Ping", "n0call n0call-1"),  # 0x3E targetCallsign localCallsign
]

# This lists all of the data frame types.
# The name of each type, as usable as a parameter to the TXFRAME host
# command, is given as the first value in a tuple.  In addition to the
# "E" form listed, there is also a corresponding frame type where the
# final "E" is replaced with an "O".
#
# The remaining values in each tuple are (number of carriers, data bytes
# per carrier, RS bytes per carrier).  Total data bytes and total RS bytes
# for the frame is the number of carriers times the quantity per carrier.
# The maximum possible Reed Solomon (RS) corrections per carrier is half
# of the RS bytes per carrier.
#
# Frame types are listed in order of increasing bandwidth, and then in order
# of assumed decreasing robustness.  However, 600 baud 4FSK modes intended only
# for use with FM rather than SSB modulation are listed after all others.
DATAFRAMES = [
    ("4FSK.200.50S.E", 1, 16, 4),  # 0x48, 0x49
    ("4PSK.200.100S.E", 1, 16, 8),  # 0x42, 0x43
    ("4PSK.200.100.E", 1, 64, 32),  # 0x40, 0x41
    ("8PSK.200.100.E", 1, 108, 36),  # 0x44, 0x45
    ("16QAM.200.100.E", 1, 128, 64),  # 0x46, 0x47

    ("4FSK.500.100S.E", 1, 32, 8),  # 0x4C, 0x4D
    ("4FSK.500.100.E", 1, 64, 16),  # 0x4A, 0x4B
    ("4PSK.500.100.E", 2, 64, 32),  # 0x50, 0x51
    ("8PSK.500.100.E", 2, 108, 36),  # 0x52, 0x53
    ("16QAM.500.100.E", 2, 128, 64),  # 0x54, 0x55

    ("4PSK.1000.100.E", 4, 64, 32),  # 0x60, 0x61
    ("8PSK.1000.100.E", 4, 108, 36),  # 0x62, 0x63
    ("16QAM.1000.100.E", 4, 128, 64),  # 0x64, 0x65

    ("4PSK.2000.100.E", 8, 64, 32),  # 0x70, 0x71
    ("8PSK.2000.100.E", 8, 108, 36),  # 0x72, 0x73
    ("16QAM.2000.100.E", 8, 128, 64),  # 0x74, 0x75

    ("4FSK.2000.600S.E", 1, 200, 50),  # 0x7C, 0x7D
    ("4FSK.2000.600.E", 1, 600, 150),  # 0x7A, 0x7B
]
