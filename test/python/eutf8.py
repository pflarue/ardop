"""
Function to convert data from eutf8 (escaped utf-8) encoding to the arbitrary
bytes object that it was created from (using to_euft8())
"""

# See https://GitHub.com/pflarue/eutf8 for a full description of eutf8 (escaped
# UTF-8) encoding, along with reference implementations of to_eutf() and
# from_eutf() in Python, Javascript, and c.

eutf8_version = "0.9.0"

def from_eutf8(data):
    """Return a copy of the raw bytes object used to create a eutf8 object"""

    output = b""
    index = 0
    while (offset := data.find(b"\\", index)) != -1:
        output += data[index:offset]
        if len(data) > offset + 5 and data[offset + 1 : offset + 4] == b"\xE2\x80\x8B":
            # This Zero Width Space was probabaly inserted after the backslash
            # by to_eutf8(), but only if it is followed by zero or more
            # additional Zero Width Spaces and then two uppercase hex digits.
            zws_cnt = 1
            while len(data) > offset + 3 * (zws_cnt + 1) + 2 and (
                data[offset + 3 * zws_cnt + 1 : offset + 3 * zws_cnt + 4]
                == b"\xE2\x80\x8B"
            ):
                zws_cnt += 1
            if (
                data[offset + 3 * zws_cnt + 1] in b"0123456789ABCDEF"
                and data[offset + 3 * zws_cnt + 2] in b"0123456789ABCDEF"
            ):
                # Remove the first Unicode Zero Width Space after the backslash
                output += data[offset : offset + 1]
                index = offset + 4
            else:
                # The Zero Width Space after the backslash was not inserted by
                # to_eutf8(), so ignore it.
                output += data[offset : offset + 1]
                index = offset + 1
        elif (
            len(data) > offset + 2
            and data[offset + 1] in b"0123456789ABCDEF"
            and data[offset + 2] in b"0123456789ABCDEF"
        ):
            # restore escaped byte
            output += bytes.fromhex(data[offset + 1 : offset + 3].decode())
            index = offset + 3
        else:
            # This backslash is not part of an escape and did not require a Zero
            # Width Space to be added after it to prevent it from being
            # misinterpreted as part of an escape.
            output += data[offset : offset + 1]
            index = offset + 1
    output += data[index:]
    return output
