#! /usr/bin/python
"""
This module implements a simple set of tests in which a recording is created for
each frame type used by ardopcf.  Then those recordings are decoded using the
--decodewav option of ardopcf.  This is done for both short control frames and
for the various data frames.  For data frames, the encoded data is compared to
the decoded data.  The frame type and paramters are listed for each frame
successfully decoded.  Additional information is listed about each failure.
When finished, a summary of failures is printed.
"""

from random import randbytes
import re
import os
import subprocess

# ardop_parameters.py contains constants required for these tests
from ardop_parameters import *


def test_contol_wav_io(quiet=False):
    """
    This function tests all short control frame types.  These frame types mostly
    carry little or no data.  So, no effort is made to validate any carried data.
    Thus, it could be enhanced to check that callsigns, quality values, etc. are
    correctly decoded.
    """
    faillist = []
    if not os.path.exists(TMPPATH):
        os.mkdir(TMPPATH)

    for frametype, paramstr in CONTROLFRAMES:
        res = subprocess.run(
            [
                APATH,
                # --logdir sets the location of the debug log and the
                # WAV files to be written.
                "--logdir",
                TMPPATH,
                "--writetxwav",
                # Using special audio device name "NOSOUND" tells
                # ardopcf to not use recording or playback sound
                # devices and to not sleep when simulating TX.
                # Whenever audio devices are specified, host port
                # number must also be specified.
                "8515", "NOSOUND", "NOSOUND",
                # LOGLEVEL 0 (LOGEMERGENCY) should prevent most writes
                # to the log file, which is not used for this testing.
                # CONSOLELOG 7 (LOGDEBUG) ensures that filename of
                # the WAV is written to stdout so that it can be
                # parsed from res.stdout.
                # DRIVELEVEL 30 reduces the volume of the signal in the
                # WAV file.  This is not expected to impact the ability
                # of ardopcf to demodulate/decode this WAV file, but it
                # may be useful if a later stage of this testing uses
                # noise added to the recording.
                "--hostcommands",
                f'LOGLEVEL 0;CONSOLELOG 7;DRIVELEVEL 30;TXFRAME'
                f' {frametype} {paramstr};CLOSE',
            ],
            capture_output=True,
            check=True,
        )
        fail = False
        # Parse captured stdout for WAV filename
        m = re.search(
            r"Opening WAV file for writing: ([^\s]+)\s",
            res.stdout.decode("iso-8859-1")
        )
        if m is None:
            if not quiet:
                print("ERROR parsing stdout from ardopcf.")
                print("stdout:\n", res.stdout.decode("iso-8859-1"))
                print("stderr:\n", res.stdout.decode("iso-8859-1"))
            faillist.append(f"ERROR parsing stdout from encoding {frametype}")
            fail = True
            continue
        wavpath = m.group(1)
        for sdftstr in ["", "--sdft"]:
            # Decode the WAV file.
            try:
                res = subprocess.run(
                    [
                        APATH,
                        # --logdir sets the location of the debug log and the
                        # WAV files to be written.
                        "--logdir",
                        TMPPATH,
                        "--decodewav",
                        wavpath,
                        # When sdftstr is "", the standard demodulators
                        # are used.  When it is "--sdft", the
                        # experimental SDFT demodulator is used.
                        sdftstr,
                        "--hostcommands",
                        # LOGLEVEL 0 (LOGEMERGENCY) should prevent most writes
                        # to the log file, which is not used for this testing.
                        # CONSOLELOG 7 (LOGDEBUG) ensures that [DecodeFrame] and
                        # [Frame Type Decode Fail] are written to stdout as well
                        # as the hex representation of the decoded data.
                        'LOGLEVEL 0;CONSOLELOG 7',
                    ],
                    capture_output=True,
                    check=True,
                )
            except subprocess.CalledProcessError as err:
                if not quiet:
                    print("Error decoding", err)
                    print(
                        f"The WAV file, {wavpath}, has not been erased.  So it"
                        f" may be used to repeat the failed attempt to decode.")
                    if sdftstr != "":
                        print(
                            "This error occured when the experiemental SDFT"
                            " demodulator was used.")
                faillist.append(
                    f"Error decoding {frametype} from {wavpath} {sdftstr}")
                fail = True
                continue

            # Parse stdout for results of success
            m = re.search(
                    r"\[DecodeFrame\] Frame: ([^ ]+)",
                    res.stdout.decode("iso-8859-1")
            )
            if m is None:
                if not quiet:
                    print(f"FAILURE TO DECODE {frametype}")
                    print(
                        f"The WAV file, {wavpath}, has not been erased.  So it"
                        f" may be used to repeat the failed attempt to decode.")
                    if sdftstr != "":
                        print(
                            "This error occured when the experiemental SDFT"
                            " demodulator was used.")
                faillist.append(
                    "Failure to decode {frametype} from {wavpath} {sdtfstr}")
                fail = True
                continue
            if not quiet:
                print(
                    f"FrameType='{m.group(1)}' decoded. {sdftstr}"
                )
        # only delete the wav file if it was successfully decoded.
        if not fail:
            os.remove(wavpath)
    if __name__ != '__main__':
        if faillist:
            print(f"{len(faillist)} failures occured in test_contol_wav_io().")
            for failnum, failstr in enumerate(faillist):
                print(f"{failnum + 1}.  {failstr}")
        else:
            print("No failures occured in test_contol_wav_io().")
    return faillist


QAM2000_NOTE = (
    "NOTE: Using ardopcf v1.0.4.1.2+develop, current as of the initial"
    " use of this test script, one or more of the 16QAM.2000.100 frames"
    " usually fail to be decoded properly during this test.  These are"
    " the fastest and least robust of the ardop data frame types.  Even"
    " when decoded correctly, the large number of RS errors corrected is"
    " high, except when the fill level of the frame is low.  This"
    " indicates poor performance of the demodulator that must be"
    " compensated for by the large amount of forward error correction"
    " used.  It is unclear at this time whether this mode is simply too"
    " fragile for the algorithms and parameters being used, or whether"
    " there is an as yet unidentified bug in ardopcf, which will improve"
    " the usefulness of this frame type once it is fixed."
)


def test_data_wav_io(quiet=False, sessionid=0xFF):
    """
    For each data frame type, create a wav file of that frame with random data.
    Then read/decode that frame to confirm that the data encoded matches the
    data decoded.  Do this with both fully filled frames as well as partially
    filled frames.  During normal use of ardopcf, partially filled data frames
    are regularly sent whenever the amount of data available to be sent is less
    than the capacity of the data frame to be used.  Thus, even when sending
    a large amount of data, the last data frame sent is typically only partially
    filled.  Partially filled data frames are padded with zeros, which produces
    a noticibly different audio pattern than most other data to be transmitted.
    The demodulator algorithms used by ardopcf may react to this differently.
    So, it seems prudent to test both fully filled and partially filled frames.

    In addition to the demodulators that are used in other implementations of
    Ardop, ardopcf also provides an experimental Sliding DFT (SDFT) demodulator.
    So, both the standard and optional SDFT demodulators are tested.  The SDFT
    demodulator is used to demodulate the frame types for all frames.  It is
    also used to demodulate the data for 4FSK frames.  It is not used to
    demodulate the PSK/QAM data.
    """
    faillist = []
    if not os.path.exists(TMPPATH):
        os.mkdir(TMPPATH)

    # Test data frame types
    for frametype, cars, dbpc, _ in DATAFRAMES:
        for fill in [1.0, 0.8, 0.1]:
            # fill is the approximate fraction of full data capacity to be filled.
            for suffix in ["E", "O"]:
                # While TXFRAME has an option to automatically fill or partially
                # fill a data frame with random data, the data written to the
                # frame is unknown, and thus is not available to verify that
                # the decoded data matches the encoded data.  So, for this test
                # the random data will be generated here instead.
                payload_capacity = cars * dbpc
                payload_used = round(fill * payload_capacity)
                rdatahex = randbytes(payload_used).hex()
                res = subprocess.run(
                    [
                        APATH,
                        # --logdir sets the location of the debug log and the
                        # WAV files to be written.
                        "--logdir",
                        TMPPATH,
                        "--writetxwav",
                        # LOGLEVEL 0 (LOGEMERGENCY) should prevent most writes
                        # to the log file, which is not used for this testing.
                        # CONSOLELOG 7 (LOGDEBUG) ensures that filename of
                        # the WAV is written to stdout so that it can be
                        # parsed from res.stdout.
                        # DRIVELEVEL 30 reduces the volume of the signal in the
                        # WAV file.  This is not expected to impact the ability
                        # of ardopcf to demodulate/decode this WAV file, but it
                        # may be useful if a later stage of this testing uses
                        # noise added to the recording.
                        "--hostcommands",
                        f'LOGLEVEL 0;CONSOLELOG 7;DRIVELEVEL 30;TXFRAME'
                        f' {frametype[:-1]}{suffix} {rdatahex}'
                        f' 0x{hex(sessionid)[2:]:>02};CLOSE',
                        # Using special audio device name "NOSOUND" tells
                        # ardopcf to not use recording or playback sound
                        # devices and to not sleep when simulating TX.
                        # Whenever audio devices are specified, host port
                        # number must also be specified.
                        "8515", "NOSOUND", "NOSOUND",
                    ],
                    capture_output=True,
                    check=True,
                )
                fail = False
                # Parse captured stdout for WAV filename
                m = re.search(
                    r"Opening WAV file for writing: ([^\s]+)\s",
                    res.stdout.decode("iso-8859-1")
                )
                if m is None:
                    if not quiet:
                        print("ERROR parsing stdout from ardopcf.")
                        print("stdout:\n", res.stdout.decode("iso-8859-1"))
                        print("stderr:\n", res.stdout.decode("iso-8859-1"))
                    faillist.append(
                        f"ERROR parsing stdout from encoding {frametype[:-1]}"
                        f"{suffix} ({payload_used}/{payload_capacity} bytes)"
                        f" {sdftstr}")
                    fail = True
                    continue
                wavpath = m.group(1)
                for sdftstr in ["", "--sdft"]:
                    # Decode the WAV file.
                    try:
                        res = subprocess.run(
                            [
                                APATH,
                                # --logdir sets the location of the debug log and
                                # the WAV files to be written.
                                "--logdir",
                                TMPPATH,
                                "--decodewav",
                                wavpath,
                                # When sdftstr is "", the standard demodulators
                                # are used.  When it is "--sdft", the
                                # experimental SDFT demodulator is used for 4FSK
                                # frame types, as well as for demodulating the
                                # frame type header for all data frames.
                                sdftstr,
                                "--hostcommands",
                                # LOGLEVEL 0 (LOGEMERGENCY) should prevent most
                                # writes to the log file, which is not used for
                                # this testing.
                                # CONSOLELOG 7 (LOGDEBUG) ensures that
                                # [DecodeFrame] and [Frame Type Decode Fail] are
                                # written to stdout as well as the hex
                                # representation of the decoded data.
                                'LOGLEVEL 0;CONSOLELOG 7',
                                # No port number of sound devices are required
                                # when using the --decodewav option.
                            ],
                            capture_output=True,
                            check=True,
                        )
                    except subprocess.CalledProcessError as err:
                        if not quiet:
                            print(
                                f"{err}\n"
                                f"An error occured while attempting to decode"
                                f" {wavpath}, for a {frametype[:-1]}{suffix}"
                                f" frame.  The WAV file has not been erased, so"
                                f" that it may be used to repeat the failed"
                                f" attempt to decode for debugging purposes.")
                            if sdftstr != "":
                                print(
                                    "This error occured when the experiemental"
                                    f" SDFT demodulator was used.")
                        faillist.append(
                            f"ERROR decoding {frametype[:-1]}{suffix}"
                            f" ({payload_used}/{payload_capacity} bytes) from"
                            f" {wavpath} {sdftstr}")
                        fail = True
                        continue
                    # Parse stdout for results of success
                    m = re.search(
                        r"\[DecodeFrame\] Frame: ([^ ]+) Decode ([^ ]+),"
                        r"  Quality= ([0-9]+),"
                        r"  RS fixed ([0-9]+) \(of ([0-9]+) max\).",
                        res.stdout.decode("iso-8859-1")
                    )
                    if m is None:
                        if not quiet:
                            print(
                                f"FAILURE TO DECODE {frametype[:-1]}{suffix}"
                                f" {rdatahex} 0x{hex(sessionid)[2:]:>02}"
                            )
                            print(
                                f"The WAV file, {wavpath}, has not been erased."
                                f"  So it may be used to repeat the failed"
                                f" attempt to decode.")
                            if sdftstr != "":
                                print(
                                    "This error occured when the experiemental"
                                    f" SDFT demodulator was used.")

                            if frametype == "16QAM.2000.100.E":
                                print(
                                    "NOTE: This failure is not unexpected.  See"
                                    " note included in summary for more detail.")
                        faillist.append(
                            f"Failure to decode {frametype[:-1]}{suffix}"
                            f" ({payload_used}/{payload_capacity} bytes) from"
                            f" {wavpath} {sdftstr}")
                        fail = True
                        continue
                    # The frame was reportedly successfully decoded.
                    if not quiet:
                        print(
                            f"FrameType='{m.group(1)}' carrying {payload_used}"
                            f" (of {payload_capacity} max) bytes of data."
                            f" Quality={int(m.group(3))} RS corrected"
                            f" {m.group(4)} of {m.group(5)} possible errors."
                            f"  {sdftstr}"
                        )
                    # Further parse stdout for decoded data as space delimited
                    # hex string so that it can be compared to the encoded data.
                    m = re.search(
                        r"\[RXO ([0-9A-F][0-9A-F])\] ([0-9]+) bytes of data as"
                        r" hex values:\s([0-9A-F ]+)\s\s+",
                        res.stdout.decode("iso-8859-1")
                    )
                    if m is None:
                        if not quiet:
                            print(
                                f"While ardopcf reported that the data frame was"
                                f" correctly decoded, this test script failed to"
                                f" parse the decoded data so that it could be"
                                f" compared to the encoded data.  This is"
                                f" probably due to a testing error, not an error"
                                f" in ardopcf, but manual review is required to"
                                f" diagnose the problem.  The WAV file,"
                                f" {wavpath}, has not been erased since it may"
                                f" be useful for debugging purposes.  The data"
                                f" encoded in this WAV file (as a hex string)"
                                f" is\n{rdatahex}"
                            )
                            if sdftstr != "":
                                print(
                                    "This error occured when the experiemental"
                                    " SDFT demodulator was used.")
                            print("stdout:\n", res.stdout.decode("iso-8859-1"))
                            print("stderr:\n", res.stdout.decode("iso-8859-1"))
                        faillist.append(
                            f"Error parsing decoded data for {frametype[:-1]}"
                            f"{suffix} ({payload_used}/{payload_capacity} bytes)"
                            f" from {wavpath} {sdftstr}")
                        fail = True
                        continue
                    if m.group(3).replace(' ', '') != rdatahex.upper():
                        if not quiet:
                            print(
                                f"While ardopcf reported that the data frame was"
                                f" correctly decoded, the decoded data does not"
                                f" match the encoded data.  The WAV file,"
                                f" {wavpath}, has not been erased so that it may"
                                f" be used to repeat the failed attempt to"
                                f" decode for debugging purposes."
                                f"\nEncoded data: {rdatahex}"
                                f"\nDecoded data: {m.group(3).replace(' ', '')}"
                            )
                            if sdftstr != "":
                                print(
                                    "This error occured when the experiemental"
                                    " SDFT demodulator was used.")
                            print(
                                "IMPORTANT: For diagnostic purposes, a copy of"
                                " the input data listed above should be retained"
                                " along with the WAV file.")
                        faillist.append(
                            f"Decoded data does not match encoded data for"
                            f" {frametype[:-1]}{suffix} ({payload_used}/"
                            f"{payload_capacity} bytes) from {wavpath}"
                            f" {sdftstr}")
                        fail = True
                        continue
                    # The decoded data matches the encoded data.
                # Only delete the wav file if it was successfully decoded.
                if not fail:
                    os.remove(wavpath)
    if __name__ != '__main__':
        if faillist:
            print(f"{len(faillist)} failures occured in test_data_wav_io().")
            for failnum, failstr in enumerate(faillist):
                print(f"{failnum + 1}.  {failstr}")
            print(QAM2000_NOTE)
        else:
            print("No failures occured in test_data_wav_io().")
    return faillist


if __name__ == '__main__':
    full_faillist = []
    full_faillist.extend(test_contol_wav_io(quiet=False))
    full_faillist.extend(test_data_wav_io(quiet=False))
    if full_faillist:
        print(f"\n{len(full_faillist)} failures occured in test_wav_io.py.")
        for failnum, failstr in enumerate(full_faillist):
            print(f"{failnum + 1}.  {failstr}")
        print(QAM2000_NOTE)
    else:
        print("\nNo failures occured in test_wav_io.py.")
