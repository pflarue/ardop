# The Ardop Protocol

The Amateur Radio Digital Open Protocol (Ardop), is a digital communication protocol created by Rick Muething KN6KB.  It provides ARQ and FEC protocols for the exchange of digital data encoded as audio and transmitted over amateur radio.

The ARDOP protocol [specification](refs/ARDOP_Specification_20171127.pdf) includes information about the different modes of operation, how stations should automatically excahnge information in an ARQ session, general modem behavior, and a list of the different frame types.

# Ardop implementations:

### [ARDOP_Win]

**ARDOP_Win** by Rick Muething KN6KB, is the original implentation of Ardop that runs natively only on Windows computers.  It is distributed by the Winlink Development team as a component of [Winlink Express](https://winlink.org/WinlinkExpress).

### [ardopc/piardopc/ARDOPC.exe](https://www.cantab.net/users/john.wiseman/Documents/ARDOPC.html)

**ardopc**, **piardopc**, and **ARDOPC.exe** are each created from the ARDOPC code base developed by John Wiseman G8BPQ.  This is written in the C programming language to make it usable on multiple platforms including Windows, Linux, and microcontrollers such as Teensy.  This began as a translation of ARDOP_Win into C.

### [ardopcf](https://github.com/pflarue/ardop)

**ardopcf** is a fork of **ardopc** by Peter LaRue AI7YN (formerly KG4JJA).  It is usable on Windows and Linux computers.  While **ardopcf** is useful and being used, there is still plenty of room for improvement.  Development is actively continuing.  The goals for this continuing work include improved stability and better over the air performance, while maintaining compatability with the other Ardop implementations and the ARDOP specification.  Ongoing efforts to reorganize the code base to make it easier to understand, debug, and maintain also help support these goals.

### Other ARDOP-related protocols/implementations

John Wiseman G8BPQ, in addition to creating **ardopc**, has created several additional variants.  While they all have "ardop" in their names, they are often not fully compatible with ARDOP_WIN, ardopc, or ardopcf.  Source code, binaries, and documentations for these variants are available at https://github.com/g8bpq/ardop and at https://www.cantab.net/users/john.wiseman/Downloads/Beta.

Some of these variants are reported to provide significant performance improvements over ardopc and ardopcf.  While these improvements are interesting, and should be explored further when considering development of future protocols, they are not being adopted into **ardopcf** because they would break conformance to the Ardop specification and over the air compatibility with ARDOP_Win and ardopc.
