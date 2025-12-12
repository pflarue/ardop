# ARDOP Overview (Converted from PDF)

Overview of the Amateur Radio Digital Open Protocol (ARDOP)
Prepared by Rick Muething, KN6KB
Revision 0.6 July 1, 2015

The ARDOP project is a joint amateur development effort seeking to provide a specification and
implementation (software or hardware) for a modern versatile open digital protocol suitable for both
HF and VHF/UHF.

## Project Target Objectives

1. Open Design: Document and build a modern amateur protocol that can be used on a number
of common OS, computers/tablets and DSP devices and is compatible with both HF and VHF
transmission. The protocol should be easily extended. Software implementations will be open
sourced. A conformance specification and compatibility test insures compatibility between
ARDOP implementations.
2. Flexible Implementation: It is anticipated there will be several implementations compatible
with a number of software and hardware platforms. These include:
Software implementations (virtual TNC with “sound card”) on Windows, Linux, Apple and
Android OS
Hardware implementations using low cost dedicated DSP CPU chips and integrated “sound
cards”
3. Bandwidth Options: The initial ARDOP protocol is intended to operate in one of four audio
bandwidths, 200 Hz, 500 Hz, 1000 Hz, and 2000 Hz. The bandwidth can be forced by server,
forced by client or negotiated by the server and client.
4. Channel Adaptability: The protocol is intended to be able to operate over a wide range of
data rate and robustness levels by automatically adapting to propagation and channel
conditions seeking the best modulation and bandwidth to maximize net error-free throughput.
5. Support both FEC and ARQ operation: ARQ (connected) operation insures error free data
delivery between two connected stations. FEC may be used for broadcast (multicast)
applications. The bandwidth, modulation mode and repeat level for FEC (multicast) operation
is selectable to allow the sender to tradeoff robustness and net throughput. Receiver reception
requires no setup. Both FEC and ARQ transmission may be monitored by listening parties.
6. Compliance with US FCC Symbol rate rule: The maximum symbol rate on any carrier shall be
300 baud or less for all SSB modes. The protocol shall allow modification extensions to symbol
rates > 300 baud if and when the FCC rules are changed.
7. Strong Resistance to Multipath propagation: The protocol shall use modern techniques (low
symbol rates, OFDM carriers, cyclic prefix, FSK modulation, path compensation, strong FEC etc.)
to optimize performance under poor multipath conditions (path delay variation up to 5 ms).
8. Minimize Interference. The protocol shall minimize the chance of interference with other
existing connections on a frequency using modern busy channel detectors and listen before
transmit.
9. Flexible Operating Modes and Radios: The protocol may be used on both HF (SSB mode) and
VHF/UHF (SSB or FM mode). Timing parameters are adjusted automatically for ARQ modes to
accommodate various transmitter keying options, SDR type radios (e.g. Flex) and the use of
carrier or sub-tone operated VHF/UHF repeaters.
10. Compatible with multi-language usage: Although the protocol requires ASCII compliant call
signs (7 characters plus optional SSID of -1 to -15 or -A through -Z) all data is transferred in pure
binary insuring protocol compatibility with multi-language multi-byte character sets like UTF-8.
Available Documents:
11. Amateur Radio Digital Open Protocol (ARDOP) Specification (includes detailed specification
of all components, bandwidths and operating modes of the protocol)
12. Interface Specification for ARDOP TNC (includes commands and data interface details
between host program and ARDOP TNC)
13. ARDOP Frame Info (.xls) includes details on frame timing, coding and FEC and ARQ
throughput.
