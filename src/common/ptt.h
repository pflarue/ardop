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
int parse_pttstr(char *pttstr);
int set_GPIOpin(int pin);
