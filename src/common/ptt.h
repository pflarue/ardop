#include <stdbool.h>
#include <stddef.h>

// MAXCATLEN is the maximum length (in bytes) of commands that may be sent to
// the CAT device.  A hex string representing such a command may have a string
// length of 2 * MAXCATLEN, and thus should be stored in a character array of
// length 2 * MAXCATLEN + 1 (to allow for the terminating NULL)
#define MAXCATLEN 64

// host command.
// The string provided with the -k, --keystring, -u, --unkeystring command line
// option or with the RADIOPTTON or RADIOPTTOFF host commands may be twice this
// number of characters since two hex characters are used to define each CAT
// command byte.

// TODO: Add documentation for these functions
void KeyPTT(bool state);
bool rxCAT();
int set_ptt_on_cmd(char *hexstr, char *descstr);
int set_ptt_off_cmd(char *hexstr, char *descstr);
int get_ptt_on_cmd_hex(char *hexstr, int length);
int get_ptt_off_cmd_hex(char *hexstr, int length);
int sendCAThex(char *hexstr);
int readCAT(unsigned char *data, size_t len);
int parse_catstr(char *catstr);
// Write up to size bytes of the CAT string as provided to parse_catstr to
// catstr.  If no CAT port is open, write nothing.but a terminating null.
// Return the number of bytes written (excluding the terminating NULL)
int get_catstr(char *catstr, int size);

int parse_pttstr(char *pttstr);
// Write up to size bytes of the PTT string as provided to parse_pttstr to
// pttstr.  If no PTT port/device is active, write nothing.but a terminating
// null.
// Return the number of bytes written (excluding the terminating NULL)
int get_pttstr(char *pttstr, int size);

// Does nothing if no PTT device/port is open.
// If statusmsg && ZF_LOG_ON_VERBOSE && !isPTTmodeEnabled() after closing,
// then STATUS PTTENABLED FALSE.  So, use statusmsg=false when responding to
// a PTTENABLED FALSE host command to avoid redundancy.
void close_PTT(bool statusmsg);

// Does nothing if no CATport or tcpCATport is open.
// If statusmsg && ZF_LOG_ON_VERBOSE && !isPTTmodeEnabled() after closing,
// then STATUS PTTENABLED FALSE.  So, use statusmsg=false when responding to
// a PTTENABLED FALSE host command to avoid redundancy.
void close_CAT(bool statusmsg);

// Return true if any PTT mode is enabled, else false.  If false, this means
// that either the host program must do PTT control or the radio must be
// configured to use VOX, which is not recommended.
bool isPTTmodeEnabled();

// Return true PTT control CAT via CAT was usable more recently than via a
// non-CAT control device.
bool wasLastGoodControlCAT();

// Encode a list of devices to the format required by wg_send_devices().  Return
// the number of bytes written to dst.
// On return, dst is NOT a null terminated string.
// Include found serial and CM108 devices, as well as RIGCTLD and TCP:4532
// (without attepting to verify that they are available).  If LastGoodCATstr or
// LastGoodPTTstr is not an empty string and does not match any value in the
// encoded device list, then also include them.
size_t EncodeDeviceStrlist(char *dst, int dstsize, char **ss, char **cs);
