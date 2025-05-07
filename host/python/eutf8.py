"""
Function to convert arbitrary byte data to eutf8 (escaped utf-8) encoding for
display purposes.
"""

# See https://GitHub.com/pflarue/eutf8 for a full description of eutf8 (escaped
# UTF-8) encoding, along with reference implementations of to_eutf() and
# from_eutf() in Python, Javascript, and c.

eutf8_version = "0.9.0"

def to_eutf8(data, escape_tab=False, escape_lf=False, escape_cr=False):
    """Return a eutf8 encoded bytes object created from any bytes object"""
    output = b""
    index = 0
    while len(data) > index:
        if 0x80 <= data[index] <= 0xC1 or 0xF5 <= data[index]:
            # Not UTF-8
            # Escape 1 byte
            output += f"\\{data[index]:02X}".encode()
            index += 1
            continue

        if data[index] <= 0x7F:
            # Valid 1-byte UTF-8
            if (
                data[index] <= 0x08
                or (data[index] == 0x09 and escape_tab is True)
                or (data[index] == 0x0A and escape_lf is True)
                or 0x0B <= data[index] <= 0x0C
                or (data[index] == 0x0D and escape_cr is True)
                or 0x0E <= data[index] <= 0x1F
                or data[index] == 0x7F
            ):
                # This is a C0 control code, so escape anyways
                output += f"\\{data[index]:02X}".encode()
                index += 1
                continue
            if data[index] == 0x5C:  # backslash
                # Insert a Zero Width Space after the backslash only if it is
                # followed by zero or more additional Zero Width Spaces and then
                # two uppercase hex digits.
                # While parsing multiple additional characters, check for end
                # of data.
                zws_cnt = 0  # Number of existing Zero Width Spaces found
                while len(data) > index + 3 * zws_cnt + 3 and (
                    data[index + 3 * zws_cnt + 1 : index + 3 * zws_cnt + 4]
                    == b"\xE2\x80\x8B"
                ):
                    zws_cnt += 1

                if (
                    len(data) > index + 3 * zws_cnt + 2
                    and data[index + 3 * zws_cnt + 1] in b"0123456789ABCDEF"
                    and data[index + 3 * zws_cnt + 2] in b"0123456789ABCDEF"
                ):
                    # Add a Unicode Zero Width Space (U+200b) after the
                    # backslash '\' (0x5C).  This prevents from_eutf8() from
                    # mistaking this for an escape sequence or from removing the
                    # zws_cnt Zero Width Spaces that are a part of the source
                    # data.  The latter is probably unlikely, but is possible.
                    # UTF-8 encoding of U+200b is the three byte sequence
                    # 0xE2, 0x80, 0x8B.
                    output += (
                        b"\\\xE2\x80\x8B" + data[index + 1 : index + 3 * zws_cnt + 3]
                    )
                    index += 3 * zws_cnt + 3
                    continue
            # copy 1-byte output.  This might be a backslash.
            output += data[index : index + 1]
            index += 1
            continue

        if len(data) <= index + 1:
            # data[index] may be the first byte of a multi-byte UTF-8 sequence,
            # but the required number of additional bytes are not available.
            # Escape 1 byte
            output += f"\\{data[index]:02X}".encode()
            index += 1
            continue

        if 0xC2 <= data[index] <= 0xDF:
            if 0x80 <= data[index + 1] <= 0xBF:
                # Valid 2-byte UTF-8
                if (data[index] == 0xC2) and (data[index + 1] <= 0x9F):
                    # This is a C1 control code, so escape anyways
                    # When valid UTF-8 is escaped, escape all bytes since
                    # following bytes are not valid as first byte of UTF-8.
                    output += f"\\{data[index]:02X}\\{data[index + 1]:02X}".encode()
                    index += 2
                    continue
                # copy 2-bytes to output
                output += data[index : index + 2]
                index += 2
                continue
            # Not UTF-8.  Escape 1 byte
            output += f"\\{data[index]:02X}".encode()
            index += 1
            continue

        if len(data) <= index + 2:
            # data[index] may be the first byte of a multi-byte UTF-8 sequence,
            # but the required number of additional bytes are not available.
            # Escape 1 byte
            output += f"\\{data[index]:02X}".encode()
            index += 1
            continue

        if data[index] == 0xE0:
            if (0xA0 <= data[index + 1] <= 0xBF) and (0x80 <= data[index + 2] <= 0xBF):
                # Valid 3-byte UTF-8
                # copy 3-bytes to output
                output += data[index : index + 3]
                index += 3
                continue
            # Not UTF-8.  Escape 1 byte
            output += f"\\{data[index]:02X}".encode()
            index += 1
            continue

        if 0xE1 <= data[index] <= 0xEC:
            if (0x80 <= data[index + 1] <= 0xBF) and (0x80 <= data[index + 2] <= 0xBF):
                # Valid 3-byte UTF-8
                # copy 3-bytes to output
                output += data[index : index + 3]
                index += 3
                continue
            # Not UTF-8.  Escape 1 byte
            output += f"\\{data[index]:02X}".encode()
            index += 1
            continue

        if data[index] == 0xED:
            if (0x80 <= data[index + 1] <= 0x9F) and (0x80 <= data[index + 2] <= 0xBF):
                # Valid 3-byte UTF-8
                # copy 3-bytes to output
                output += data[index : index + 3]
                index += 3
                continue
            # Not UTF-8.  Escape 1 byte
            output += f"\\{data[index]:02X}".encode()
            index += 1
            continue

        if 0xEE <= data[index] <= 0xEF:
            if (0x80 <= data[index + 1] <= 0xBF) and (0x80 <= data[index + 2] <= 0xBF):
                # Valid 3-byte UTF-8
                if data[index + 1] <= 0xA3:
                    # This is a private use character, so escape anyways.
                    # When valid UTF-8 is escaped, escape all bytes since
                    # following bytes are not valid as first byte of UTF-8.
                    output += (
                        f"\\{data[index]:02X}\\{data[index + 1]:02X}"
                        f"\\{data[index + 2]:02X}"
                    ).encode()
                    index += 3
                    continue
                if (data[index] == 0xEF) and (
                    ((data[index + 1] == 0xB7) and (0x90 <= data[index + 2] <= 0xAF))
                    or ((data[index + 1] == 0xBF) and (0xBE <= data[index + 2]))
                ):
                    # This is a noncharacter, so escape it anyways.
                    # When valid UTF-8 is escaped, escape all bytes since
                    # following bytes are not valid as first byte of UTF-8.
                    output += (
                        f"\\{data[index]:02X}\\{data[index + 1]:02X}"
                        f"\\{data[index + 2]:02X}"
                    ).encode()
                    index += 3
                    continue
                # Valid 3-byte UTF-8
                # copy 3-bytes to output
                output += data[index : index + 3]
                index += 3
                continue
            # Not UTF-8.  Escape 1 byte
            output += f"\\{data[index]:02X}".encode()
            index += 1
            continue

        if len(data) <= index + 3:
            # data[index] may be the first byte of a multi-byte UTF-8 sequence,
            # but the required number of additional bytes are not available.
            # Escape 1 byte
            output += f"\\{data[index]:02X}".encode()
            index += 1
            continue

        if data[index] == 0xF0:
            if (
                (0x90 <= data[index + 1] <= 0xBF)
                and (0x80 <= data[index + 2] <= 0xBF)
                and (0x80 <= data[index + 3] <= 0xBF)
            ):
                # Valid 4-byte UTF-8
                if (
                    (data[index + 1] & 0x0F == 0x0F)
                    and (data[index + 2] == 0xBF)
                    and (data[index + 3] >= 0xBE)
                ):
                    # This is a noncharacter, so escape it anyways.
                    # When valid UTF-8 is escaped, escape all bytes since
                    # following bytes are not valid as first byte of UTF-8.
                    output += (
                        f"\\{data[index]:02X}\\{data[index + 1]:02X}"
                        f"\\{data[index + 2]:02X}\\{data[index + 3]:02X}"
                    ).encode()
                    index += 4
                    continue
                # Valid 4-byte UTF-8
                # copy 4-bytes to output
                output += data[index : index + 4]
                index += 4
                continue
            # Not UTF-8.  Escape 1 byte
            output += f"\\{data[index]:02X}".encode()
            index += 1
            continue

        if 0xF1 <= data[index] <= 0xF3:
            if (
                (0x80 <= data[index + 1] <= 0xBF)
                and (0x80 <= data[index + 2] <= 0xBF)
                and (0x80 <= data[index + 3] <= 0xBF)
            ):
                # Valid 4-byte UTF-8
                if (
                    (data[index + 1] & 0x0F == 0x0F)
                    and (data[index + 2] == 0xBF)
                    and (data[index + 3] >= 0xBE)
                ):
                    # This is a noncharacter, so escape it anyways.
                    # When valid UTF-8 is escaped, escape all bytes since
                    # following bytes are not valid as first byte of UTF-8.
                    output += (
                        f"\\{data[index]:02X}\\{data[index + 1]:02X}"
                        f"\\{data[index + 2]:02X}\\{data[index + 3]:02X}"
                    ).encode()
                    index += 4
                    continue
                if (
                    (data[index] == 0xF3)
                    and (0xB0 <= data[index + 1])
                    and (data[index + 3] <= 0xBD)
                ):
                    # This is a private use character, so escape anyways.
                    # When valid UTF-8 is escaped, escape all bytes since
                    # following bytes are not valid as first byte of UTF-8.
                    output += (
                        f"\\{data[index]:02X}\\{data[index + 1]:02X}"
                        f"\\{data[index + 2]:02X}\\{data[index + 3]:02X}"
                    ).encode()
                    index += 4
                    continue
                # Valid 4-byte UTF-8
                # copy 4-bytes to output
                output += data[index : index + 4]
                index += 4
                continue
            # Not UTF-8.  Escape 1 byte
            output += f"\\{data[index]:02X}".encode()
            index += 1
            continue

        if data[index] == 0xF4:
            if (
                (0x80 <= data[index + 1] <= 0x8F)
                and (0x80 <= data[index + 2] <= 0xBF)
                and (0x80 <= data[index + 3] <= 0xBF)
            ):
                # Valid 4-byte UTF-8
                if (
                    (data[index + 1] == 0x8F)
                    and (data[index + 2] == 0xBF)
                    and (data[index + 3] >= 0xBE)
                ):
                    # This is a noncharacter, so escape it anyways.
                    # When valid UTF-8 is escaped, escape all bytes since
                    # following bytes are not valid as first byte of UTF-8.
                    output += (
                        f"\\{data[index]:02X}\\{data[index + 1]:02X}"
                        f"\\{data[index + 2]:02X}\\{data[index + 3]:02X}"
                    ).encode()
                    index += 4
                    continue
                if data[index + 3] <= 0xBD:
                    # This is a private use character, so escape anyways.
                    # When valid UTF-8 is escaped, escape all bytes since
                    # following bytes are not valid as first byte of UTF-8.
                    output += (
                        f"\\{data[index]:02X}\\{data[index + 1]:02X}"
                        f"\\{data[index + 2]:02X}\\{data[index + 3]:02X}"
                    ).encode()
                    index += 4
                    continue
                # Valid 4-byte UTF-8
                # copy 4-bytes to output
                output += data[index : index + 4]
                index += 4
                continue
            # Not UTF-8.  Escape 1 byte
            output += f"\\{data[index]:02X}".encode()
            index += 1
            continue
        # shouldn't get here
        print(f"WARNING: Logic error in to_eutf8() for data[{index}]={data[index]}")
        # Escape 1 byte
        output += f"\\{data[index]:02X}".encode()
        index += 1
    return output
