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

import argparse
from random import randbytes
import re
import os
import subprocess

# ardop_parameters.py contains constants required for these tests
from ardop_parameters import *
from eutf8 import from_eutf8

def test_contol_wav_io(verbose=1):
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
                # --nologfile prevents the creation of a log file which
                # is not needed for this testing.
                "--nologfile",
                # --logdir sets the location of the WAV files written.
                "--logdir",
                TMPPATH,
                "--writetxwav",
                # The options -i -1 -o -1 tells ardopcf to not use
                # recording or playback sound devices and to not sleep
                # when simulating TX.
                "-i", "-1", "-o", "-1",
                # CONSOLELOG 2 ensures that the filename of the WAV is
                # written to stdout so that it can be parsed from
                # res.stdout.
                # MYCALL N0CALL sets the station callsign.  Like all
                # host commands that initiate transmitting, TXFRAME
                # will respond with a fault message if MYCALL is not
                # set first.  This is done to ensure that an IDFrame
                # can be sent when required.  Since the frames generated
                # by this script will not actaully be transmitted, it
                # is OK to use a fake callsign.
                # DRIVELEVEL 30 reduces the volume of the signal in the
                # WAV file.  This is not expected to impact the ability
                # of ardopcf to demodulate/decode this WAV file, but it
                # may be useful if a later stage of this testing uses
                # noise added to the recording.
                "--hostcommands",
                f'CONSOLELOG 2;MYCALL N0CALL;DRIVELEVEL 30;TXFRAME'
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
            if verbose > 0:
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
                        # --nologfile prevents the creation of a log
                        # file which is not needed for this testing.
                        "--nologfile",
                        "--decodewav",
                        wavpath,
                        # When sdftstr is "", the standard demodulators
                        # are used.  When it is "--sdft", the
                        # experimental SDFT demodulator is used.
                        # To prevent the empty string "" from being interpreted
                        # as an invalid host port, use "-y".  This left channel
                        # TX option has no effect when used with --decodewav.
                        sdftstr if sdftstr else "-y",
                        # CONSOLELOG 2 ensures that [DecodeFrame] and
                        # [Frame Type Decode Fail] are written to stdout as well
                        # as the eutf8 representation of the decoded data.
                        "--hostcommands",
                        'CONSOLELOG 2',
                        # No port number or sound devices are required
                        # when using the --decodewav option.
                    ],
                    capture_output=True,
                    check=True,
                )
            except subprocess.CalledProcessError as err:
                if verbose > 0:
                    print("Error decoding", err)
                    print(
                        f"The WAV file, {wavpath}, has not been erased.  So it"
                        f" may be used to repeat the failed attempt to decode.")
                    if sdftstr != "":
                        print(
                            "This error occured when the experiemental SDFT"
                            " demodulator was used.")
                faillist.append(
                    f"Error decoding {frametype} from {wavpath} {sdftstr}."
                    f" This should only occur when a serious error was"
                    f" encountered.  Re-attempting to decode this wav file may"
                    f" provide additional details.")
                fail = True
                continue

            # Parse stdout for results of success
            m = re.search(
                    r"\[DecodeFrame\] Frame: ([^ ]+)",
                    res.stdout.decode("iso-8859-1")
            )
            if m is None:
                if verbose > 0:
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
            if verbose > 0:
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


def parse_ber_results(logstr, cars, print_bermap=False):
    """For each carrier, print the Bit Error Rate data"""
    # Parse Bit Error results
    for car in range(cars):
        m = re.search(
            f"(Carrier\[{car}\] [0-9]+ raw bytes\. CER[^\n\[]+)[^\n]*",
            logstr
        )
        if m is None:
            # ardopcf cannot calculate CER and BER if there are more errors than
            # could by corrected.  However, with verbose logging (CONSOLELOG 1),
            # the uncorrectable raw data for each carrier is written to the log.
            # Given the data that was intended to be encoded in this frame, it
            # should be possible to reconstruct what each carrier's raw data
            # should have contained, and then compare that to the uncorrectable
            # raw data to calulate CER, BER, and the bit error map.  This should
            # be relateively easy for the actual data bytes, but would require
            # use of rs_append() from rrs.c (or creation of an equivalent python
            # function) to also calculate the RS bytes that would have been
            # encoded into this frame.
            # TODO: Implement CER/BER calculation for carriers whose contents
            # could not be corrected?
            print(f"Unable to parse CER and BER for Carrier[{car+1}].")
            print(logstr)
            raise ValueError
        if m is not None and "BER=0.0%" not in m.group(0):
            if print_bermap:
                print("  ", m.group(0))
            else:
                print("  ", m.group(1))



def test_data_wav_io(verbose=1, sessionid=0xFF):
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
                rdata = randbytes(payload_used)
                res = subprocess.run(
                    [
                        APATH,
                        # --nologfile prevents the creation of a log
                        # file which is not needed for this testing.
                        "--nologfile",
                        # --logdir sets the location of the WAV files written.
                        "--logdir",
                        TMPPATH,
                        "--writetxwav",
                        # CONSOLELOG 2 ensures that the filename of the WAV is
                        # written to stdout so that it can be parsed from
                        # res.stdout.
                        # MYCALL N0CALL sets the station callsign.  Like all
                        # host commands that initiate transmitting, TXFRAME
                        # will respond with a fault message if MYCALL is not
                        # set first.  This is done to ensure that an IDFrame
                        # can be sent when required.  Since the frames generated
                        # by this script will not actaully be transmitted, it
                        # is OK to use a fake callsign.
                        # DRIVELEVEL 30 reduces the volume of the signal in the
                        # WAV file.  This is not expected to impact the ability
                        # of ardopcf to demodulate/decode this WAV file, but it
                        # may be useful if a later stage of this testing uses
                        # noise added to the recording.
                        "--hostcommands",
                        f'CONSOLELOG 2;MYCALL N0CALL;DRIVELEVEL 30;TXFRAME'
                        f' {frametype[:-1]}{suffix} {rdata.hex()}'
                        f' 0x{hex(sessionid)[2:]:>02};CLOSE',
                        # The options -i -1 -o -1 tells ardopcf to not use
                        # recording or playback sound devices and to not sleep
                        # when simulating TX.
                        "-i", "-1", "-o", "-1",
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
                    if verbose > 0:
                        print("ERROR parsing stdout from ardopcf.")
                        print("stdout:\n", res.stdout.decode("iso-8859-1"))
                        print("stderr:\n", res.stdout.decode("iso-8859-1"))
                    faillist.append(
                        f"ERROR parsing stdout from encoding {frametype[:-1]}"
                        f"{suffix} ({payload_used}/{payload_capacity} bytes)")
                    fail = True
                    continue
                wavpath = m.group(1)
                for sdftstr in ["", "--sdft"]:
                    # Decode the WAV file.
                    try:
                        res = subprocess.run(
                            [
                                APATH,
                                # --nologfile prevents the creation of a log
                                # file which is not needed for this testing.
                                "--nologfile",
                                "--decodewav",
                                wavpath,
                                # When sdftstr is "", the standard demodulators
                                # are used.  When it is "--sdft", the
                                # experimental SDFT demodulator is used for 4FSK
                                # frame types, as well as for demodulating the
                                # frame type header for all data frames.
                                # To prevent the empty string "" from being
                                # interpreted as an invalid host port, use "-y".
                                # This left channel TX option has no effect when
                                # used with --decodewav.
                                sdftstr if sdftstr else "-y",
                                "--hostcommands",
                                # CONSOLELOG 1 ensures that
                                # [DecodeFrame] and [Frame Type Decode Fail],
                                # the eutf8 representation of the decoded data,
                                # and Bit Error results are written to stdout so
                                # that they can be parsed from res.stdout.
                                'CONSOLELOG 1',
                                # No port number or sound devices are required
                                # when using the --decodewav option.
                            ],
                            capture_output=True,
                            check=True,
                        )
                    except subprocess.CalledProcessError as err:
                        if verbose > 0:
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
                                    " SDFT demodulator was used.")
                        faillist.append(
                            f"ERROR decoding {frametype[:-1]}{suffix}"
                            f" ({payload_used}/{payload_capacity} bytes) from"
                            f" {wavpath} {sdftstr}.  This should only occur when"
                            f" a serious error was encountered.  Re-attempting"
                            f" to decode this wav file may provide additional"
                            f" details.")
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
                        if verbose > 0:
                            print(
                                f"FAILURE TO DECODE {frametype[:-1]}{suffix}"
                                f" carrying {payload_used}"
                                f" (of {payload_capacity} max) bytes of data."
                                f" {rdata.hex()} 0x{hex(sessionid)[2:]:>02}"
                            )
                            print(
                                f"The WAV file, {wavpath}, has not been erased."
                                f"  So it may be used to repeat the failed"
                                f" attempt to decode.")
                            if sdftstr != "":
                                print(
                                    "This error occured when the experiemental"
                                    " SDFT demodulator was used.")
                            if verbose > 1:
                                parse_ber_results(
                                    res.stdout.decode("iso-8859-1"),
                                    cars,
                                    verbose > 2
                                )
                        faillist.append(
                            f"Failure to decode {frametype[:-1]}{suffix}"
                            f" ({payload_used}/{payload_capacity} bytes) from"
                            f" {wavpath} {sdftstr}")
                        fail = True
                        continue
                    # The frame was reportedly successfully decoded.
                    if verbose > 0:
                        print(
                            f"FrameType='{m.group(1)}' carrying {payload_used}"
                            f" (of {payload_capacity} max) bytes of data."
                            f" Quality={int(m.group(3))} RS corrected"
                            f" {m.group(4)} of {m.group(5)} possible errors."
                            f"  {sdftstr}"
                        )
                        if verbose > 1:
                            if ".600." in frametype:
                                additional_pseudocarriers = 2
                            else:
                                additional_pseudocarriers = 0
                            parse_ber_results(
                                res.stdout.decode("iso-8859-1"),
                                cars + additional_pseudocarriers,
                                verbose > 2
                            )
                    # Further parse stdout for decoded data as eutf8 encoded
                    # data so that it can be compared to the encoded data.
                    m = re.search(
                        r"\[RXO ([0-9A-F][0-9A-F])\] ([0-9]+) bytes of data"
                        r" \(eutf8\):\n+([^\n]+)\n",
                        res.stdout.decode("utf-8")
                    )
                    if m is None:
                        if verbose > 0:
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
                                f" is\n{rdata.hex()}"
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
                    decodeddata = from_eutf8(m.group(3).encode("utf-8"));
                    if decodeddata != rdata:
                        if verbose > 0:
                            print(
                                f"While ardopcf reported that the data frame was"
                                f" correctly decoded, the decoded data does not"
                                f" match the encoded data.  The WAV file,"
                                f" {wavpath}, has not been erased so that it may"
                                f" be used to repeat the failed attempt to"
                                f" decode for debugging purposes."
                                f"\nEncoded data: {rdata}"
                                f"\nDecoded data: {decodeddata}"
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
        else:
            print("No failures occured in test_data_wav_io().")
    return faillist


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-v",
        "--verbose",
        nargs='?',
        const=2,
        type=int,
        help="Change output verbosity"
    )
    args = parser.parse_args()
    # By default, use verbose=1. so -v 1 has no effect.
    # Using -v (which is eqivalent to -v 2), or -v 3 increase the verbosity
    # Using -v 0 decreases the verbosity.
    if args.verbose is None:
        args.verbose = 1

    full_faillist = []
    full_faillist.extend(test_contol_wav_io(verbose=args.verbose))
    full_faillist.extend(test_data_wav_io(verbose=args.verbose))
    if full_faillist:
        print(f"\n{len(full_faillist)} failures occured in test_wav_io.py.")
        for failnum, failstr in enumerate(full_faillist):
            print(f"{failnum + 1}.  {failstr}")
    else:
        print("\nNo failures occured in test_wav_io.py.")
