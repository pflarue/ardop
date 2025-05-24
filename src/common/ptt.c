#ifndef WIN32
// cross platform compatibility for device HANDLE/fd
#define HANDLE int
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/os_util.h"
#include "common/ardopcommon.h"
#include "common/ptt.h"
#include "common/log.h"
#include "common/Webgui.h"
#include "common/txframe.h"

#define PTTNONCATMASK 0x0F
#define PTTCATMASK 0x30

#define PTTRTS 1
#define PTTDTR 2
#define PTTCM108 4
#define PTTGPIO 8
#define PTTCAT 16
#define PTTTCPCAT 32

#define MAXCATLEN 64

// The default port number used by Hamlib/rigctld as a string.
// This is used in response to --ptt RIGCTLD.
char rigctlddefault[5] = "4532";

int PTTmode = 0;  // PTT Control Flags.

#define PORTSTRSZ 200
// port names are for comparison with each other to indicate that a device
// handle should be reused.
// CAT port str, possibly with TCP: prefix  If a baudrate is used when
// selecting the PTT port, this is also included in PTTstr (and
// LastGoodPTTstr).  Assume that if a baudrate is required/used to select
// a CAT device and that same device is also used for RTS/DTR PTT, that
// the same baudrate will be specified when selecting it.
char CATstr[PORTSTRSZ] = "";

// PTTstr does not include an RTS: or DTR: prefix, but may include a CM108: or
// GPIO: prefix.  If it has no prefix, use PTTmode to determine whether it is
// used for RTS or DTR.  If a baudrate is used when selecting the PTT port,
// this is also included in PTTstr (and LastGoodPTTstr).  Assume that if a
// baudrate is required/used to select a PTT device and that same device is
// also used for CAT, that the same baudrate will be specified when selecting
// it.
char PTTstr[PORTSTRSZ] = "";

// Use LastGoodCATstr and LastGoodPTTstr to store the names of that last good
// values of CATstr and PTTstr.  These are used when parse_catstr() and
// parse_pttstr() when they get "RESTORE" as an input in response to the
// RADIOCTRLPORT RESTORE or RADIOPTT RESTORE host commands.
// See also LastGoodCaptureDevice and LastGoodPlaybackDevice used in
// ALSA.c and Waveform.c.
char LastGoodCATstr[PORTSTRSZ] = "";
// Unlike PTTstr, LastGoodPTTstr includes DTR: or RTS: prefix
char LastGoodPTTstr[PORTSTRSZ] = "";
// PTT can be controlled via CAT or from a PTT device.  It is possible that
// since ardopcf was started, both CAT and PTT devices have been successfully
// used, such that LastGoodCATstr and LastGoodPTTstr both contain possibly
// useful values.  The TXENABLED TRUE host command should try parse_catstr()
// first if LastGoodControlWasCAT is true.  Otherwise, it should try
// parse_pttstr() first.  In either case, if the first fails, it should try
// the other.
bool LastGoodControlWasCAT = false;  // If neither, value is unimportant

// All of the following are set to 0 when inactive.

// Only one of hCATdevice or tmpCATport will be active at any time.
// The "name" used to establish either of these is stored in CATstr.
HANDLE hCATdevice = 0;  // HANDLE/file descriptor for CAT device
int tcpCATport = 0;  // TCP port (usually on 127.0.0.1) for CAT

// Only one of hPTTdevice, hCM108device, or GPIOpin will be active at any time.
// However, one of the following may be active in addition to one of the CAT
//   devices (which may or may not use the same physical port).  For example,
//   PTT may be done via RTS/DTR while CAT provides the ability to control other
//   settings (via the RADIOHEX host command).
// The "name" sed to establish any of these is stored in PTTstr, though RTS: and
//   DTR: prefixes are not included in PTTStr (but CM108: and GPIO: prefixes are).
HANDLE hPTTdevice = 0;  // HANDLE/file descriptor for PTT device for RTS or DTR
HANDLE hCM108device = 0;  // HANDLE/file descriptor for PTT by CM108 device
// Set GPIOpin for transmit and clear GPIOpin for recieve.  Except that if
// GPIOinvert is true, then do the opposite.  (ARM only).
int GPIOpin = 0;  // (ARM only) GPIO pin number to be used for PTT control
bool GPIOinvert = false;

// ptt_on_cmd contains ptt_on_cmd_len bytes to send to the CAT device (if
// available) for each transition from receive to transmit.
unsigned char ptt_on_cmd[MAXCATLEN];
unsigned int ptt_on_cmd_len = 0;
// ptt_off_cmd contains ptt_off_cmd_len bytes to send to the CAT device (if
// available) for each transition from transmit to receive.
unsigned char ptt_off_cmd[MAXCATLEN];
unsigned int ptt_off_cmd_len = 0;

// If anything other than PTT commands are sent to a CAT device or tcpCATport
// then CATrx is set to true.  This indicates that data read from the CAT
// device and/or tcpCATport should be passed to the host program as a
// RADIOHEX command response.
bool CATrx = false;

// required external functions
char * strlop(char * buf, char delim);
void gpioWrite(unsigned gpio, unsigned level);
int gpioInitialise();
void SetupGPIOPTT(int pin, bool invert);
void SendCommandToHostQuiet(char * strText);
void SendCommandToHost(char * strText);
void SetLED(int LED, int State);

// Return true if reading from CAT is enabled.
bool rxCAT() {
	return CATrx;
}

// Parse a string intended to be sent to the CAT device or TCP CAT port.
// If the string contains only an even number of valid hex characters (upper or
// lower case with no whitespace), then it is intrepreted as hex.  Otherwise, if
// it contains only printable ASCII characters 0x20-0x7E, it is interpreted as
// ASCII text with substition for "\\n" and "\\r".  A prefix of "ASCII:" may be
// used to force the string to be interpreted as ASCII text, and this is
// required for ASCII text that contains only an even number of valid hex
// characters.
//
// This is backward compatible with the prior implementation that accepted only
// hex input, except that what would have been invalid hex, may now be accepted
// as valid ASCII.
//
// If an error occurs, including for an empty string or any string that contains
// other than printable ASCII characters, then write an appropriate msg to the
// log and return 0.
//
// descstr describes the source of str, and is only used if an error must be
// logged.
//
// Return the length of the cmd (or 0 if an error occured)
static unsigned int parse_cmd(char *str, unsigned char *cmd, char *descstr) {
	int ret;
	if (strncmp(str, "ASCII:", strlen("ASCII:")) == 0) {
		if ((ret = parseeol((char *)cmd, MAXCATLEN, str + strlen("ASCII:")))
			== -1
		) {
			ZF_LOGE("ERROR: Failure to parse command string for %s with an"
				" ASCII: prefix.", descstr);
			return 0;
		}
		return ret;
	}
	if (strlen(str) % 2 != 0) {
		if ((ret = parseeol((char *)cmd, MAXCATLEN, str)) == -1) {
			ZF_LOGE("ERROR: Failure to parse command string for %s as ASCII"
				" text (because it did not contain an even number of hex"
				" characters)", descstr);
			return 0;
		}
		return ret;
	}
	if (strlen(str) > 2 * MAXCATLEN) {
		ZF_LOGE("ERROR: Command string for %s may not exceed %i hex characters"
			" or an ASCII string of %i characters (to describe a %i byte"
			" sequence).  The provided string, \"%s\" has a length of %zu"
			" characters.",
			descstr, 2 * MAXCATLEN, MAXCATLEN, MAXCATLEN, str, strlen(str));
		return 0;
	}

	if (hex2bytes(str, strlen(str) / 2, cmd) == 0)
		return strlen(str) / 2;

	// hex2bytes() failed, so parse as ASCII
	if ((ret = parseeol((char *)cmd, MAXCATLEN, str)) == -1) {
		ZF_LOGE("ERROR: Failure to parse command string for %s as ASCII"
			" text (because it did not contain an even number of hex"
			" characters)", descstr);
		return 0;
	}
	return ret;
}

// Write to str up to length bytes (including a terminating NULL)
// representing the CAT command used to switch from receive to transmit.
// If this command can be written using only printable ASCII 0x20-0x7F with
// "\\r" and "\\n", then return this string with a prefix of "ASCII:".
// Otherwise, return a hex string.
// On success, return 0.
// On failure (length insuficient to hold entire command string), return -1.
// If no command has been set, return 0 with str set to a zero length string.
int get_ptt_on_cmd(char *str, int length) {
	// Allow for "ASCII:" prefix and terminating NULL
	char tmpcmd[MAXCATLEN + 7];
	int ret;
	if (length < (int) ptt_on_cmd_len + 1)
		return -1;
	if (ptt_on_cmd_len == 0) {
		str[0] = 0x00;  // zero length string
		return 0;
	}
	if (snprintf(tmpcmd, sizeof(tmpcmd), "ASCII:%.*s",
		ptt_on_cmd_len, ptt_on_cmd) < (int) sizeof(tmpcmd)
		&& (ret = escapeeol(str, length, tmpcmd)) != -1
	) {
		return 0;
	}
	// ptt_on_cmd could not be expressed as ASCII text.  So, return hex
	ret = bytes2hex(str, length, ptt_on_cmd, ptt_on_cmd_len, false);
	if (ret < 0 || ret + 1 <= length)
		return 0;
	return (-1);
}

// Set the CAT command for switching from receive to transmit to a hex or
// ASCII string (see parse_cmd() for requirements).
// On success, return 0.  If a CAT port and the CAT command for switching from
// transmit to receive are already set, then also enable CAT control of PTT.
// On failure, leave the existing command value unchanged, write an error to the
// log, and return -1.  descstr indicates the source of str, to be used in the
// log message if an error occurs.
// A zero length str erases the existing command value, and returns 0 for
// success.
int set_ptt_on_cmd(char *str, char *descstr) {
	if (str[0] == 0x00) {
		ptt_on_cmd_len = 0;
		wg_send_ptton(0, NULL);
		ZF_LOGI("PTT ON CMD set to None.");
		PTTmode &= PTTNONCATMASK;  // Disable all CAT related PTT
		wg_send_pttenabled(0, !!PTTmode);
	}
	unsigned char tmp_cmd[MAXCATLEN];
	unsigned int tmp_len = parse_cmd(str, tmp_cmd, descstr);
	if (tmp_len != 0) {
		memcpy(ptt_on_cmd, tmp_cmd, tmp_len);
		ptt_on_cmd_len = tmp_len;
	}
	// update WebGui
	char tmpstr[MAXCATLEN * 2 + 1];
	get_ptt_on_cmd(tmpstr, sizeof(tmpstr));
	wg_send_ptton(0, tmpstr);
	if (tmp_len == 0) {
		// error in parse_cmd.  PTT ON CMD unchanged
		return (-1);  // Error alreadly logged
	}
	ZF_LOGI("PTT ON CMD set to %s.", tmpstr);
	if (hCATdevice != 0 && ptt_off_cmd_len > 0) {
		PTTmode = (PTTmode & PTTNONCATMASK) + PTTCAT;
		wg_send_pttenabled(0, !!PTTmode);
		ZF_LOGI("PTT using CAT Port: %s", CATstr);
		LastGoodControlWasCAT = true;
	}
	if (tcpCATport != 0 && ptt_off_cmd_len > 0) {
		PTTmode = (PTTmode & PTTNONCATMASK) + PTTTCPCAT;
		wg_send_pttenabled(0, !!PTTmode);
		ZF_LOGI("PTT using TCP CAT port: %s", CATstr);
		LastGoodControlWasCAT = true;
	}
	return 0;
}

// Write to str up to length bytes (including a terminating NULL)
// representing the CAT command used to switch from transmit to receive.
// If this command can be written using only printable ASCII 0x20-0x7F with
// "\\r" and "\\n", then return this string with a prefix of "ASCII:".
// Otherwise, return a hex string.
// On success, return 0.
// On failure (length insuficient to hold entire command string), return -1.
// If no command has been set, return 0 with str set to a zero length string.
int get_ptt_off_cmd(char *str, int length) {
	// Allow for "ASCII:" prefix and terminating NULL
	char tmpcmd[MAXCATLEN + 7];
	int ret;
	if (length < (int) ptt_off_cmd_len + 1)
		return -1;
	if (ptt_off_cmd_len == 0) {
		str[0] = 0x00;  // zero length string
		return 0;
	}
	if (snprintf(tmpcmd, sizeof(tmpcmd), "ASCII:%.*s",
		ptt_off_cmd_len, ptt_off_cmd) < (int) sizeof(tmpcmd)
		&& (ret = escapeeol(str, length, tmpcmd)) != -1
	) {
		return 0;
	}
	// ptt_on_cmd could not be expressed as ASCII text.  So, return hex
	ret = bytes2hex(str, length, ptt_off_cmd, ptt_off_cmd_len, false);
	if (ret < 0 || ret + 1 <= length)
		return 0;
	return (-1);
}

// Set the CAT command for switching from transmit to receive to a hex or
// ASCII string (see parse_cmd() for requirements).
// On success, return 0.  If a CAT port or tcpCATport and the CAT command for
// switching from receive to transmit are already set, then also enable CAT
// control of PTT.
// On failure, leave the existing command value unchanged, write an error to the
// log, and return -1.  descstr indicates the source of str, to be used in the
// log message if an error occurs.
// A zero length str erases the existing command value, and returns 0 for
// success.
int set_ptt_off_cmd(char *str, char *descstr) {
	if (str[0] == 0x00) {
		ptt_off_cmd_len = 0;
		wg_send_pttoff(0, NULL);
		ZF_LOGI("PTT OFF CMD set to None.");
		PTTmode &= PTTNONCATMASK;  // Disable all CAT related PTT
		wg_send_pttenabled(0, !!PTTmode);
	}
	unsigned char tmp_cmd[MAXCATLEN];
	unsigned int tmp_len = parse_cmd(str, tmp_cmd, descstr);
	if (tmp_len != 0) {
		memcpy(ptt_off_cmd, tmp_cmd, tmp_len);
		ptt_off_cmd_len = tmp_len;
	}
	// update WebGui
	char tmpstr[MAXCATLEN * 2 + 1];
	get_ptt_off_cmd(tmpstr, sizeof(tmpstr));
	wg_send_pttoff(0, tmpstr);
	if (tmp_len == 0) {
		// error in parse_cmd.  PTT OFF CMD unchanged
		return (-1);  // Error alreadly logged
	}
	ZF_LOGI("PTT OFF CMD set to %s.", tmpstr);
	if (hCATdevice != 0 && ptt_on_cmd_len > 0) {
		PTTmode = (PTTmode & PTTNONCATMASK) + PTTCAT;
		wg_send_pttenabled(0, !!PTTmode);
		ZF_LOGI("PTT using CAT Port: %s", CATstr);
		LastGoodControlWasCAT = true;
	}
	if (tcpCATport != 0 && ptt_on_cmd_len > 0) {
		PTTmode = (PTTmode & PTTNONCATMASK) + PTTTCPCAT;
		wg_send_pttenabled(0, !!PTTmode);
		ZF_LOGI("PTT using TCP CAT port: %s", CATstr);
		LastGoodControlWasCAT = true;
	}
	return 0;
}

// Does nothing if no CATport or tcpCATport is open.
// If statusmsg && ZF_LOG_ON_VERBOSE && !isPTTmodeEnabled() after closing,
// then STATUS PTTENABLED FALSE.  So, use statusmsg=false when responding to
// a PTTENABLED FALSE host command to avoid redundancy.
void close_CAT(bool statusmsg) {
	if (PTTmode & PTTCATMASK)
		LastGoodControlWasCAT = !(PTTmode & PTTNONCATMASK);

	CATstr[0] = 0x00;  // empty string
	CATrx = false;  // Don't pass data from tcpCAT to host until tx other than PTT
	if (tcpCATport != 0) {
		tcpclose(&tcpCATport);
		tcpCATport = 0;
		if (PTTmode & PTTTCPCAT)
			ZF_LOGI("TCP CAT PTT disabled");
	}
	if (hCATdevice != 0) {
		if (hCATdevice != hPTTdevice) {
			// If this port is also being used for ptt (RTS/DTR), do not
			// actually close the port, just stop using it for CAT.
			CloseCOMPort(&hCATdevice);
		}
		hCATdevice = 0;
		if (PTTmode & PTTCAT)
			ZF_LOGI("CAT PTT disabled");
	}
	if (PTTmode & PTTCATMASK) {
		PTTmode &= PTTNONCATMASK;  // Disable any CAT related PTT
		wg_send_pttenabled(0, !!PTTmode);
		if (statusmsg && ZF_LOG_ON_VERBOSE && !isPTTmodeEnabled()) {
			// For testing with testhost.py
			ZF_LOGV("STATUS PTTENABLED FALSE");
			SendCommandToHost("STATUS PTTENABLED FALSE");
		}
	}
	updateWebGuiNonAudioConfig();
}

// Return the number of bytes read, or -1 if an error occurs.
// TCP CAT port is read if hCATdevice is not open or if reading from
// hCATdevice returns 0;
// If neither CAT device nor TCP CAT port is open, or if no data is
// available from either of them, return 0
// TODO: Close connection if an error occurs?
int readCAT(unsigned char *data, size_t len) {
	int ret = 0;
	if (hCATdevice != 0) {
		if ((ret = ReadCOMBlock(hCATdevice, data, len)) == -1) {
			ZF_LOGE("Error reading from CAT");
			close_CAT(true);
		}
	}
	if (tcpCATport != 0) {
		if ((ret = nbrecv(tcpCATport, (char *) data, len)) == -1) {
			ZF_LOGE("Error reading from TCP CAT");
			close_CAT(true);
		}
	}
	return ret;
}

// Send data represented by a hex or ASCII string to the CAT device.
// See parse_cmd() for str requirements
// Return -1 and log an error if no CAT device is available or an error occurs.
// Return 0 on success.
// TODO: Close connection if an error occurs?
int sendCAT(char *str) {
	if (hCATdevice == 0 && tcpCATport == 0) {
		ZF_LOGW("No CAT device or TCP CAT port.  sendCAT() failed.");
		return (-1);
	}
	unsigned char tmp_cmd[MAXCATLEN];
	unsigned int tmp_len = parse_cmd(str, tmp_cmd, "RADIOHEX host comand");
	if (tmp_len == 0)
		return (-1);  // error already logged.
	if (hCATdevice != 0) {
		if (!WriteCOMBlock(hCATdevice, tmp_cmd, tmp_len)) {
			ZF_LOGE("Error writing %s to CAT on %s.", str, CATstr);
			close_CAT(true);
			return (-1);
		}
		CATrx = true;
	}
	if (tcpCATport != 0) {
		if (tcpsend(tcpCATport, tmp_cmd, tmp_len) != 0) {
			// TODO: Close tcpCATport?
			ZF_LOGE("Error writing %s to TCP CAT on %s.", str, CATstr);
			close_CAT(true);
			return (-1);
		}
		CATrx = true;
	}
	return 0;
}

// Write up to size bytes of CATstr to catstr
// If no CATstr is set, write nothing.but a terminating null.
// return the number of bytes written (excluding the terminating NULL)
int get_catstr(char *catstr, int size) {
	if (CATstr[0] != 0x00)
		return snprintf(catstr, size, "%s", CATstr);
	catstr[0] = 0x00;  // empty string
	return 0;
}


// Set the device to be used for CAT control.
// If port is a zero length string, then disable CAT control.
// On failure, log an error message and return -1;
// On success, return 0.  If both PTT CAT commands are already set, then also
// enable CAT control of PTT.
int set_CATport(char *portstr) {
	char tmpstr[PORTSTRSZ];
	// Compare only the first PORTSTRSZ - 1 bytes (exclude terminating NUlL)
	if (strncmp(CATstr, portstr, PORTSTRSZ - 1) == 0)
		return 0;  // no change

	close_CAT(true); // close existing CATport and tcpCATport if open

	if (portstr[0] == 0x00)
		return 0; // An empty string, do nothing but close CAT port if open.

	// Create a temporary copy of portstr.  This will be copied to tcpCATportstr
	// if successful.  portstr may be modified before then as it is parsed.
	snprintf(tmpstr, PORTSTRSZ, "%s", portstr);

	// Compare only the first PORTSTRSZ - 1 bytes (exclude terminating NUlL)
	if (strncmp(PTTstr, portstr, PORTSTRSZ - 1) == 0)
		hCATdevice = hPTTdevice;  // Reuse port already open for RTS/DTR PTT
	else {
		char *baudstr = strlop(portstr, ':');
		int baud = baudstr == NULL ? 19200 : atoi(baudstr);
		if ((hCATdevice = OpenCOMPort(portstr, baud)) == 0)
			return (-1);  // Error msg already logged.
	}
	strcpy(CATstr, tmpstr);
	strcpy(LastGoodCATstr, CATstr);
	ZF_LOGI("CAT Control on port %s", CATstr);
	if (ptt_on_cmd_len > 0 && ptt_off_cmd_len > 0) {
		PTTmode = (PTTmode & PTTNONCATMASK) + PTTCAT;
		wg_send_pttenabled(0, !!PTTmode);
		ZF_LOGI("PTT CAT on %s", CATstr);
		LastGoodControlWasCAT = true;
	}
	updateWebGuiNonAudioConfig();
	return 0;
}

// portstr is usually a positive integer to indicate a TCP port on the
// local machine (127.0.0.1), but may also have the form
// ddd.ddd.ddd.ddd:port for a remote network port.
int set_tcpCAT(char *portstr) {
	char prefix[] = "TCP:";
	char tmpstr[PORTSTRSZ];
	// Compare only the first PORTSTRSZ - 1 bytes (exclude terminating NUlL)
	if (strncmp(CATstr, prefix, strlen(prefix)) == 0
		&& strncmp(CATstr + strlen(prefix), portstr,
			PORTSTRSZ - 1 - strlen(prefix)) == 0
	)
		return 0;  // no change

	close_CAT(true); // close existing CATport and tcpCATport if open

	if (portstr[0] == 0x00)
		return 0; // An empty string, do nothing but close all CAT connections.

	// Create a temporary copy of portstr.  This will be copied to CATstr
	// if successful.  portstr may be modified before then as it is parsed.
	strcpy(tmpstr, prefix);
	snprintf(tmpstr + strlen(tmpstr), PORTSTRSZ - strlen(tmpstr), "%s", portstr);

	char errformat[] = "Invalid TCP CAT port.  Expected either a positive"
		" integer for a local TCP port number or ddd.ddd.ddd.ddd:port (where each"
		" ddd is an integer in the range of 0 to 255 and port is a positive"
		" integer) for a remote network TCP port, but found \"%s\".";
	char address[16] = "127.0.0.1";  // default (local) address
	int port = 0;
	char *remoteportstr = strlop(portstr, ':');
	if (remoteportstr != NULL) {
		// portstr is address:port
		if (strlen(portstr) > sizeof(address) - 1) {
			ZF_LOGE(errformat, tmpstr + strlen(prefix));
			return (-1);
		}
		strcpy(address, portstr);  // size checked above
		port = atoi(remoteportstr);
	} else
		port = atoi(portstr);

	if (port < 1) {
		ZF_LOGE(errformat, tmpstr + strlen(prefix));
		return (-1);
	}

	// tcpconnect() may take a while to fail if address is unreachable.
	// So, write to log that we are attempting this connection to explain
	// the possible pause in execution.
	// TODO: Examine what happens to rx audio if this introduces a long delay.
	ZF_LOGI("Attempting to connect to %s for CAT.", tmpstr);
	if ((tcpCATport = tcpconnect(address, port, false)) == -1) {
		ZF_LOGI("Unable to connect to %s for CAT.", tmpstr);
		tcpCATport = 0;
		return (-1);
	}
	strcpy(CATstr, tmpstr);
	strcpy(LastGoodCATstr, CATstr);
	ZF_LOGI("CAT Control at %s", CATstr);
	if (ptt_on_cmd_len > 0 && ptt_off_cmd_len > 0) {
		PTTmode = (PTTmode & PTTNONCATMASK) + PTTTCPCAT;
		wg_send_pttenabled(0, !!PTTmode);
		ZF_LOGI("PTT using CAT on %s", CATstr);
		LastGoodControlWasCAT = true;
	}
	updateWebGuiNonAudioConfig();
	return 0;
}


// Write up to size bytes of PTTstr to pttstr
// If no PTTstr is set, write nothing.but a terminating null.
// PTTstr includes prefixes for CM108: and GPIO: but not RTS: or DTR.
// If PTTmode is PTTRTS or PTTDTR, write a RTS: or DTR: prefix to pttstr
// before PTTstr
// return the number of bytes written (excluding the terminating NULL)
int get_pttstr(char *pttstr, int size) {
	if (PTTstr[0] != 0x00)
		return snprintf(pttstr, size, "%s%s%s",
			PTTmode & PTTRTS ? "RTS:" : "",
			PTTmode & PTTDTR ? "DTR:" : "",
			PTTstr);
	pttstr[0] = 0x00;  // empty string
	return 0;
}

// Does nothing if no PTT device/port is open.
// If statusmsg && ZF_LOG_ON_VERBOSE && !isPTTmodeEnabled() after closing,
// then STATUS PTTENABLED FALSE.  So, use statusmsg=false when responding to
// a PTTENABLED FALSE host command to avoid redundancy.
void close_PTT(bool statusmsg) {
	if (PTTmode & PTTNONCATMASK)
		LastGoodControlWasCAT = PTTmode & PTTCATMASK;

	PTTstr[0] = 0x00;  // empty string
	if (hPTTdevice != 0) {
		if (hPTTdevice != hCATdevice) {
			// If this port is also being used for CAT, do not actually close
			// the port, just stop using it for RTS/DTR PTT.
			CloseCOMPort(&hPTTdevice);
		}
		hPTTdevice = 0;
		if (PTTmode & PTTRTS) {
			ZF_LOGI("RTS PTT disabled");
		}
		if (PTTmode & PTTDTR) {
			ZF_LOGI("DTR PTT disabled");
		}
	}
	if (hCM108device != 0) {
		CloseCM108(&hCM108device);
		ZF_LOGI("CM108 PTT disabled");
	}
	if (GPIOpin != 0) {
		GPIOpin = 0;  // nothing to explicityly close
		ZF_LOGI("GPIO PTT disabled");
	}
	if (PTTmode & PTTNONCATMASK) {
		PTTmode &= PTTCATMASK;  // Disable any non-CAT related PTT
		wg_send_pttenabled(0, !!PTTmode);
		if (statusmsg && ZF_LOG_ON_VERBOSE && !isPTTmodeEnabled()) {
			// For testing with testhost.py
			ZF_LOGV("STATUS PTTENABLED FALSE");
			SendCommandToHost("STATUS PTTENABLED FALSE");
		}
	}
	updateWebGuiNonAudioConfig();
}

// Set/update PTTmode associated with RTS/DTR.  This is NOT for other pttModes
void set_PTT_RTSDTR(bool useRTS) {
	if (useRTS) {
		if (PTTmode & PTTRTS)
			return;  // no change
		PTTmode = (PTTmode & PTTCATMASK) + PTTRTS;
		wg_send_pttenabled(0, !!PTTmode);
		ZF_LOGI("Using RTS on port %s for PTT", PTTstr);
		LastGoodControlWasCAT = false;
	} else {
		if (PTTmode & PTTDTR)
			return;  // no change
		PTTmode = (PTTmode & PTTCATMASK) + PTTDTR;
		wg_send_pttenabled(0, !!PTTmode);
		ZF_LOGI("Using DTR on port %s for PTT", PTTstr);
		LastGoodControlWasCAT = false;
	}
}

// Set the device to be used for PTT control by setting RTS or DTR.
// If port is a zero length string, then disable non-cat PTT control.
// On failure, log an error message and return -1;
// On success, return 0.
int set_PTTport(char *portstr, bool useRTS) {
	char tmpstr[PORTSTRSZ];
	// Compare only the first PORTSTRSZ - 1 bytes (exclude terminating NUlL)
	if (portstr[0] != 0x00 && strncmp(PTTstr, portstr, PORTSTRSZ - 1) == 0) {
		set_PTT_RTSDTR(useRTS);  // port OK, but PTTmode may need to be updated
		updateWebGuiNonAudioConfig();
		return 0;  // no change
	}
	snprintf(tmpstr, PORTSTRSZ, "%s", portstr);

	close_PTT(true);  // close port used for RTS/DTR, CM108, or GPIO if open

	if (portstr[0] == 0x00)
		return 0; // An empty string, do nothing but close PTT port if open.

	// Compare only the first PORTSTRSZ - 1 bytes (exclude terminating NUlL)
	if (strncmp(CATstr, portstr, PORTSTRSZ - 1) == 0) {
		hPTTdevice = hCATdevice;  // Reuse port already open for CAT
	} else {
		char *baudstr = strlop(portstr, ':');
		int baud = baudstr == NULL ? 19200 : atoi(baudstr);
		if ((hPTTdevice = OpenCOMPort(portstr, baud)) == 0)
			return (-1);  // Error msg already logged.
	}
	// OpenCOMPort always clears RTS and DTR, so PTT is not ON due to RTS or DTR.
	strcpy(PTTstr, tmpstr);  // Do this before set_PTT_RTSDTR()
	set_PTT_RTSDTR(useRTS);  // updates PTTmode and logs change
	// Since LastGoodPTTstr includes a prefix while PTTstr does not, a long
	// PTTstr may be truncated when copied to LastGoodPTTstr
	snprintf(LastGoodPTTstr, PORTSTRSZ, "%s%s%s",
			PTTmode & PTTRTS ? "RTS:" : "",
			PTTmode & PTTDTR ? "DTR:" : "",
			PTTstr);
	updateWebGuiNonAudioConfig();
	return 0;
}

// Expect pinstr to have a GPIO: prefix
int set_GPIOpin(char *pinstr) {
#ifdef __ARM_ARCH
	// Compare only the first PORTSTRSZ - 1 bytes (exclude terminating NUlL)
	if (strncmp(PTTstr, pinstr, PORTSTRSZ - 1) == 0)
		return 0;  // no change

	close_PTT(true);  // close port used for RTS/DTR, CM108, or GPIO if open

	long pin;
	if (strncmp(pinstr, "GPIO:", 5) != 0 || !try_parse_long(pinstr + 5, &pin)) {
		ZF_LOGW("Error extracting a pin number from %s for GPIO.", pinstr);
		return (-1);
	}
	if (pin == 0)
		return 0; // Do nothing but close PTT port if open.
	if (pin < 0) {
		GPIOpin = (int) -pin;
		GPIOinvert = true;
	} else {
		GPIOpin = (int) pin;
		GPIOinvert = false;
	}
	if (GPIOpin > 26) {
		ZF_LOGW("Error: GPIO pin number (%i) may not exceed 26", GPIOpin);
		return (-1);
	}
	if (gpioInitialise() == 0) {
		SetupGPIOPTT(GPIOpin, GPIOinvert);
		PTTmode = (PTTmode & PTTCATMASK) + PTTGPIO;  // Enable PTTGPIO
		wg_send_pttenabled(0, !!PTTmode);
		ZF_LOGI("Using %sGPIO pin %i for PTT",
			GPIOinvert ? "inverted " : "", GPIOpin);
		snprintf(PTTstr, PORTSTRSZ, "%s", pinstr);
		strcpy(LastGoodPTTstr, PTTstr);
		LastGoodControlWasCAT = false;
		updateWebGuiNonAudioConfig();
		return 0;
	} else {
		ZF_LOGE("Couldn't initialise GPIO interface for PTT");
		return (-1);
	}
#else
	(void) pinstr;  // to avoid unused variable warning
	ZF_LOGE("GPIO interface for PTT not available on this platform");
	return (-1);
#endif
}

// Expect devstr to have a CM108: prefix
int set_cm108(char *devstr) {
	// Compare only the first PORTSTRSZ - 1 bytes (exclude terminating NUlL)
	if (strncmp(PTTstr, devstr, PORTSTRSZ - 1) == 0)
		return 0;  // no change

	close_PTT(true);  // close port used for RTS/DTR, CM108, or GPIO if open

	if (devstr[0] == 0x00)
		return 0; // An empty string, do nothing but disable CM108 PTT

	// skip CM108: prefix when passing to OpenCM108
	if ((hCM108device = OpenCM108(devstr + 6)) == 0) {
		// Invalid devstr.  On Windows, also when devstr is "?" or "??"
		return (-1);  // Error msg already logged.
	}
	PTTmode = (PTTmode & PTTCATMASK) + PTTCM108;  // Enable PTTCM108
	wg_send_pttenabled(0, !!PTTmode);
	snprintf(PTTstr, PORTSTRSZ, "%s", devstr);
	strcpy(LastGoodPTTstr, PTTstr);
	LastGoodControlWasCAT = false;
	ZF_LOGI("Using CM108 device %s for PTT", PTTstr);
	updateWebGuiNonAudioConfig();
	return 0;
}


// Take the argument to the -ptt or -p command line option or the RADIOPTT host
// command and pass it to the appropriate function for control of PTT by
// RTS/DTR, CM108, or GPIO.
// Return the result returned by the appropriate function, which should be 0 for
// success or -1 for failure
int parse_pttstr(char *pttstr) {
	if (pttstr[0] == 0x00) {
		// empty string, so just close.
		close_PTT(true);
		return 0;  // success
	}
	if (strcmp(pttstr, "RESTORE") == 0) {
		if (PTTstr[0] != 0x00) {
			ZF_LOGW("parse_pttstr(RESTORE) called, but PTTstr is %s (not"
				" empty).  So, do nothing",
				PTTstr);
			// Don't leave RESTORE shown for PTT Device in WebGUI
			updateWebGuiNonAudioConfig();
			return 0;  // this is success
		}
		if (LastGoodPTTstr[0] == 0x00) {
			ZF_LOGW("parse_pttstr(RESTORE) called, but LastGoodPTTstr is empty,"
				"  So, unable to restore.");
			// Don't leave RESTORE shown for PTT Device in WebGUI
			updateWebGuiNonAudioConfig();
			return -1;
		}
		char *tmpstr = strdup(LastGoodPTTstr);
		int ret = parse_pttstr(tmpstr);
		free(tmpstr);
		return ret;
	}
	// Notice that RTS: or DTR: prefix is stripped from pttstr when calling
	// set_PTTport(), but CM108: and GPIO: prefixes are not stripped.
	if (strncmp(pttstr, "CM108:", 6) == 0 && strlen(pttstr) > 6)
		return set_cm108(pttstr);
	if (strncmp(pttstr, "RTS:", 4) == 0 && strlen(pttstr) > 4)
		return set_PTTport(pttstr + 4, true);  // This strips the RTS: prefix
	if (strncmp(pttstr, "DTR:", 4) == 0 && strlen(pttstr) > 4)
		return set_PTTport(pttstr + 4, false);  // This strips the DTR: prefix
	if (strncmp(pttstr, "GPIO:", 5) == 0 && strlen(pttstr) > 5)
		return set_GPIOpin(pttstr);
	return set_PTTport(pttstr, true);  // Assume RTS if no prefix isprovided.
}

// return 0 on success (including close if catstr is an empty string)
int parse_catstr(char *catstr) {
	if (catstr[0] == 0x00) {
		// empty string, so just close
		close_CAT(true);  // also does updateWebGuiNonAudioConfig()
		return 0;  // success
	}
	if (strcmp(catstr, "RESTORE") == 0) {
		if (CATstr[0] != 0x00) {
			ZF_LOGW("parse_catstr(RESTORE) called, but CATstr is %s (not"
				" empty).  So, do nothing",
				CATstr);
			// Don't leave RESTORE shown for CAT Device in WebGUI
			updateWebGuiNonAudioConfig();
			return 0;  // This is success
		}
		if (LastGoodCATstr[0] == 0x00) {
			ZF_LOGW("parse_catstr(RESTORE) called, but LastGoodCATstr is empty,"
				"  So, unable to restore.");
			// Don't leave RESTORE shown for CAT Device in WebGUI
			updateWebGuiNonAudioConfig();
			return -1;
		}
		char *tmpstr = strdup(LastGoodCATstr);
		int ret = parse_catstr(tmpstr);  // does updateWebGuiNonAudioConfig();
		free(tmpstr);
		return ret;
	}
	if (strcmp(catstr, "RIGCTLD") == 0) {
		// This is equivalent to
		// -c TCP:4532 -k 5420310A -u 5420300A
		// Set ptt commands first.  That way, if rigctld is being used on some
		// other port, then setting CAT to RIGCTLD (which will fail), and then
		// to the correct port will produce the desired result.
		int ret = set_ptt_on_cmd("5420310A",
			"command line option --ptt RIGCTLD");
		if (ret == 0)
			ret = set_ptt_off_cmd("5420300A",
				"command line option --ptt RIGCTLD");
		// to use hamlib/rigctld running on its default
		// TCP port 4532 for PTT.
		if (ret == 0)
			ret = set_tcpCAT(rigctlddefault);
		if (ret == 0)
			// LastGoodCATstr has been set to TCP:4532.  Change this to RIGCTLD
			// so that RADIOCTRLPORT RESTORE will ensure that both TCP:4532 is
			// opened and that the PTTON and PTTOFF strings are set.
			strcpy(LastGoodCATstr, "RIGCTLD");
		return ret;
	}
	if (strncmp(catstr, "TCP:", 4) == 0 && strlen(catstr) > 4)
		return set_tcpCAT(catstr + 4);
	return set_CATport(catstr);  // default without prefix
}

bool isPTTmodeEnabled() {
	return !!PTTmode;
}

bool wasLastGoodControlCAT() {
	return LastGoodControlWasCAT;
}

// Return true if TCP:4532 is either currently being used for TCP CAT control
// or if it can be opened.
bool isTCP4532available() {
	int testport;
	if (strcmp(CATstr, "TCP:4532") == 0)
		return true;
	if ((testport = tcpconnect("127.0.0.1", 4532, true)) == -1)
		return false;
	tcpclose(&testport);
	return true;
}

// Encode a list of devices to the format required by wg_send_devices().
// Return the number of bytes written to dst.
// On return, dst is NOT a null terminated string.
// Include found serial and CM108 devices, as well as RIGCTLD and TCP:4532
// if available.  If CATstr or PTTstr is not an empty string and does not
// match any value in the encoded device list, then also include them.  This
// is required for the WebGui to correctly show the current device if it is
// not something that is detected, such as a new/unusual Windows CM108 device
// or a serial device selected with a specific baud rate.
// cs and ss should include an ever number of non-null strings (names and
// descriptions of devices) followed by one or more NULL pointers.  The
// strings with odd index numbers (descriptions) may be empty strings,
// indicating that no description is available.
size_t EncodeDeviceStrlist(char *dst, int dstsize, char **ss, char **cs) {
	// dst and dstsize are updated to always point to the end of the string
	// written so far, and the amount of remaining free space respectively
	char *lenptr = dst;
	int thislen;
	// If encodepttstr and/or encodecatstr is true after all known devices
	// have been encoded, then encode these values.
	bool encodepttstr = PTTstr[0] != 0x00;  // true if a PTT device is open
	bool encodecatstr = CATstr[0] != 0x00;  // true if a CAT device is open
	if (dstsize < 3)
		return 0;
	dst[0] = 0x00;  // placeholder for number of entries written (2 str per dev)
	// 0xFE for none but restorable.  0xFF for none and not restorable
	if (LastGoodCATstr[0] != 0x00)
		dst[1] = 0xFE;  // index of current CAT device
	else
		dst[1] = 0xFF;  // index of current CAT device
	if (LastGoodPTTstr[0] != 0x00)
		dst[2] = 0xFE;  // index of current PTT device
	else
		dst[2] = 0xFF;  // index of current PTT device
	dst += 3;
	dstsize -= 3;
	int index;  // ss[2 * index] is name, ss[2 * index + 1] is desc
	int dstcount = 0;  // number of name/desc pairs encoded

	if (isTCP4532available()) {
		if (dstsize < 12 + (int) (strlen("RIGCTLD")
			+ strlen("Hamlib/rigctld with PTTON and PTTOFF commands")
			+ strlen("TCP:4532") + strlen("default TCP port for Hamlib/rigctld"))
		) {
			ZF_LOGE("ERROR: Not including RIGCTLD or TCP:4532 in list of"
				" devices due to excess size");
			return dst - lenptr;
		}
		if ((thislen = encodeUvint(dst, 3, strlen("RIGCTLD"))) == -1) {
			ZF_LOGE("ERROR: Failure encoding length of device name"
				" \"RIGCTLD\" for wg_send_devices()");
			return dst - lenptr;
		}
		memcpy(dst + thislen, "RIGCTLD", strlen("RIGCTLD"));
		dst += thislen + strlen("RIGCTLD");
		dstsize -= thislen + strlen("RIGCTLD");
		// encode the description
		if ((thislen = encodeUvint(dst, 3,
			strlen("Hamlib/rigctld with PTTON and PTTOFF commands"))) == -1
		) {
			ZF_LOGE("ERROR: Failure encoding length of device description"
				" \"RIGCTLD\" for wg_send_devices()");
			// Don't send this name without the description
			return (dst - (thislen + strlen("RIGCTLD"))) - lenptr;
		}
		memcpy(dst + thislen, "Hamlib/rigctld with PTTON and PTTOFF commands",
			strlen("Hamlib/rigctld with PTTON and PTTOFF commands"));
		dst += thislen + strlen("Hamlib/rigctld with PTTON and PTTOFF commands");
		dstsize -= thislen
			+ strlen("Hamlib/rigctld with PTTON and PTTOFF commands");
		// name and description for an entry have been encoded
		lenptr[0] = ++dstcount;

		if ((thislen = encodeUvint(dst, 3, strlen("TCP:4532"))) == -1) {
			ZF_LOGE("ERROR: Failure encoding length of device name"
				" \"TCP:4532\" for wg_send_devices()");
			return dst - lenptr;
		}
		memcpy(dst + thislen, "TCP:4532", strlen("TCP:4532"));
		dst += thislen + strlen("TCP:4532");
		dstsize -= thislen + strlen("TCP:4532");
		// encode the description
		if ((thislen = encodeUvint(dst, 3,
			strlen("default TCP port for Hamlib/rigctld"))) == -1
		) {
			ZF_LOGE("ERROR: Failure encoding length of device description"
				" \"TCP:4532\" for wg_send_devices()");
			// Don't send this name without the description
			return (dst - (thislen + strlen("TCP:4532")))
				- lenptr;
		}
		memcpy(dst + thislen, "default TCP port for Hamlib/rigctld",
			strlen("default TCP port for Hamlib/rigctld"));
		dst += thislen + strlen("default TCP port for Hamlib/rigctld");
		dstsize -= thislen + strlen("default TCP port for Hamlib/rigctld");
		if (strcmp(CATstr, "TCP:4532") == 0) {
			encodecatstr = false;
			if (ptt_on_cmd_len == 4 && ptt_off_cmd_len == 4
				&& memcmp(ptt_on_cmd, "\x54\x20\x31\x0A", 4) == 0
				&& memcmp(ptt_off_cmd, "\x54\x20\x31\x0A", 4) == 0
			)
				lenptr[1] = dstcount - 1;  // RIGCTLD
			else
				lenptr[1] = dstcount;  // TCP:4532
		}
		// name and description for an entry have been encoded
		lenptr[0] = ++dstcount;
	}
	index = 0;
	while(cs != NULL && cs[2 * index] != NULL) {
		// A name/desc pairs will be added for each CM108 device.  If no
		// desciption is available as indicated by a zero length string in cs,
		// provide a Uvint of 0.
		int memreq = 4 + strlen(cs[2 * index]);
		if (cs[2 * index + 1][0] != 0x00) {
			// a description is available
			memreq += 2 + strlen(cs[2 * index + 1]);  // desc
		}
		if (dstsize < memreq) {
			ZF_LOGE("ERROR: Truncating list of devices due to excess size");
			return dst - lenptr;
		}
		if ((thislen = encodeUvint(dst, 3, strlen(cs[2 * index]))) == -1) {
			ZF_LOGE("ERROR: Failure encoding length of device name"
				" \"%s\" for wg_send_devices()", cs[2 * index]);
			return dst - lenptr;
		}
		memcpy(dst + thislen, cs[2 *  index], strlen(cs[2 * index]));
		dst += thislen + strlen(cs[2 * index]);
		dstsize -= thislen + strlen(cs[2 * index]);
		if (cs[2 * index + 1] == NULL) {
			ZF_LOGE("Invalid cs[] passed to EncodeDeviceStrlist.  A NULL pointer"
				" was found where a description string for %s was expected.",
				cs[2 * index]);
				// Don't send this name without the description
				return (dst - (thislen + strlen(cs[2 * index]))) - lenptr;
		}
		if (cs[2 * index + 1][0] == 0x00) {  // a zero length description string
			*dst = 0x00;  // Uvint value of 0
			++dst;
			--dstsize;
		} else {
			// encode the description
			if ((thislen = encodeUvint(dst, 3,
				strlen(cs[2 * index + 1]))) == -1
			) {
				ZF_LOGE("ERROR: Failure encoding length of device description"
					" \"%s\" for wg_send_devices()", cs[2 * index + 1]);
				// Don't send this name without the description
				return (dst - (thislen + strlen(cs[2 * index]))) - lenptr;
			}
			memcpy(dst + thislen, cs[2 * index + 1], strlen(cs[2 * index + 1]));
			dst += thislen + strlen(cs[2 * index + 1]);
			dstsize -= thislen + strlen(cs[2 * index + 1]);
		}
		if (strcmp(cs[2 * index], PTTstr) == 0) {
			lenptr[2] = dstcount;  // index of current PTT device
			encodepttstr = false;
		}
		// name and description for an entry have been encoded
		lenptr[0] = ++dstcount;
		++index;  // next name and description in cs
	}

	index = 0;
	while(ss != NULL && ss[2 * index] != NULL) {
		// Two name/desc pairs will be added for each serial device.  One with
		// no prefix suitable for CAT and RTS PTT.  A second one with a "DTR:"
		// prefix is suitable for DTR PTT.  If no desciption is available as
		// indicated by a zero length string in ss, provide a Uvint of 0.
		int memreq = 8 + strlen("DTR:") + 2 * strlen(ss[2 * index]);
		if (ss[2 * index + 1][0] != 0x00) {
			// a description is available
			memreq += 4 + 2 * strlen(ss[2 * index + 1]);  // desc
		}
		if (dstsize < memreq) {
			ZF_LOGE("ERROR: Truncating list of devices due to excess size");
			return dst - lenptr;
		}
		if ((thislen = encodeUvint(dst, 3, strlen(ss[2 * index]))) == -1) {
			ZF_LOGE("ERROR: Failure encoding length of device name"
				" \"%s\" for wg_send_devices()", ss[2 * index]);
			return dst - lenptr;
		}
		memcpy(dst + thislen, ss[2 *  index], strlen(ss[2 * index]));
		dst += thislen + strlen(ss[2 * index]);
		dstsize -= thislen + strlen(ss[2 * index]);
		if (ss[2 * index + 1] == NULL) {
			ZF_LOGE("Invalid ss[] passed to EncodeDeviceStrlist.  A NULL pointer"
				" was found where a description string for %s was expected.",
				ss[2 * index]);
				// Don't send this name without the description
				return (dst - (thislen + strlen(ss[2 * index]))) - lenptr;
		}
		if (ss[2 * index + 1][0] == 0x00) {  // a zero length description string
			*dst = 0x00;  // Uvint value of 0
			++dst;
			--dstsize;
		} else {
			// encode the description
			if ((thislen = encodeUvint(dst, 3,
				strlen(ss[2 * index + 1]))) == -1
			) {
				ZF_LOGE("ERROR: Failure encoding length of device description"
					" \"%s\" for wg_send_devices()", ss[2 * index + 1]);
				// Don't send this name without the description
				return (dst - (thislen + strlen(ss[2 * index]))) - lenptr;
			}
			memcpy(dst + thislen, ss[2 * index + 1], strlen(ss[2 * index + 1]));
			dst += thislen + strlen(ss[2 * index + 1]);
			dstsize -= thislen + strlen(ss[2 * index + 1]);
		}
		if (strcmp(ss[2 * index], CATstr) == 0) {
			lenptr[1] = dstcount;  // index of current CAT device
			encodecatstr = false;
		}
		if (strcmp(ss[2 * index], PTTstr) == 0 && (PTTmode & PTTRTS)) {
			lenptr[2] = dstcount;  // index of current PTT device
			encodepttstr = false;
		}
		// name and description for an entry have been encoded
		lenptr[0] = ++dstcount;

		if ((thislen = encodeUvint(dst, 3,
			strlen("DTR:") + strlen(ss[2 * index]))) == -1
		) {
			ZF_LOGE("ERROR: Failure encoding length of device name"
				" \"DTR:%s\" for wg_send_devices()", ss[2 * index]);
			return dst - lenptr;
		}
		memcpy(dst + thislen, "DTR:", strlen("DTR:"));
		memcpy(dst + thislen + strlen("DTR:"), ss[2 *  index],
			strlen(ss[2 * index]));
		dst += thislen + strlen("DTR:") + strlen(ss[2 * index]);
		dstsize -= thislen + strlen("DTR:")+ strlen(ss[2 * index]);
		if (ss[2 * index + 1][0] == 0x00) {  // a zero length description string
			*dst = 0x00;  // Uvint value of 0
			++dst;
			--dstsize;
		} else {
			// encode the description
			if ((thislen = encodeUvint(dst, 3,
				strlen(ss[2 * index + 1]))) == -1
			) {
				ZF_LOGE("ERROR: Failure encoding length of device description"
					" \"DTR:%s\" for wg_send_devices()", ss[2 * index + 1]);
				// Don't send this name without the description
				return (dst - (thislen+ strlen("DTR:") + strlen(ss[2 * index])))
					- lenptr;
			}
			memcpy(dst + thislen, ss[2 * index + 1], strlen(ss[2 * index + 1]));
			dst += thislen + strlen(ss[2 * index + 1]);
			dstsize -= thislen + strlen(ss[2 * index + 1]);
		}
		if (strcmp(ss[2 * index], PTTstr) == 0 && (PTTmode & PTTDTR)) {
			lenptr[2] = dstcount;  // index of current PTT device
			encodecatstr = false;
		}
		// name and description for an entry have been encoded
		lenptr[0] = ++dstcount;
		++index;  // next name and description in ss
	}

	if (encodecatstr) {
		// include contents of CATstr
		if (dstsize < 6 + (int) (strlen(CATstr) + strlen("current CAT device"))) {
			ZF_LOGE("ERROR: Not including %s in list of devices due to excess size",
				CATstr);
			return dst - lenptr;
		}
		if ((thislen = encodeUvint(dst, 3, strlen(CATstr))) == -1) {
			ZF_LOGE("ERROR: Failure encoding length of device name"
				" \"%s\" for wg_send_devices()", CATstr);
			return dst - lenptr;
		}
		memcpy(dst + thislen, CATstr, strlen(CATstr));
		dst += thislen + strlen(CATstr);
		dstsize -= thislen + strlen(CATstr);
		// encode a description
		if ((thislen = encodeUvint(dst, 3, strlen("current CAT device"))) == -1) {
			ZF_LOGE("ERROR: Failure encoding length of device description"
				" \"%s\" for wg_send_devices()", CATstr);
			// Don't send this name without the description
			return (dst - (thislen + strlen(CATstr))) - lenptr;
		}
		memcpy(dst + thislen, "current CAT device", strlen("current CAT device"));
		dst += thislen + strlen("current CAT device");
		dstsize -= thislen + strlen("current CAT device");
		lenptr[1] = dstcount;  // index of current CAT device
		if (strcmp(CATstr, PTTstr) == 0) {
			lenptr[2] = dstcount;  // index of current PTT device
			encodepttstr = false;  // just encoded, so re-encode for PTT
		}
		// name and description for an entry have been encoded
		lenptr[0] = ++dstcount;
	}

	if (encodepttstr) {
		// include contents of LastGoodPTTstr which is equal to PTTstr, but also
		// includes an RTS: or DTR: prefix if needed.
		if (dstsize < 6 + (int) (strlen(LastGoodPTTstr)
			+ strlen("current PTT device"))
		) {
			ZF_LOGE("ERROR: Not including %s in list of"
				" devices due to excess size", LastGoodPTTstr);
			return dst - lenptr;
		}
		if ((thislen = encodeUvint(dst, 3, strlen(LastGoodPTTstr))) == -1) {
			ZF_LOGE("ERROR: Failure encoding length of device name"
				" \"%s\" for wg_send_devices()", LastGoodPTTstr);
			return dst - lenptr;
		}
		memcpy(dst + thislen, LastGoodPTTstr, strlen(LastGoodPTTstr));
		dst += thislen + strlen(LastGoodPTTstr);
		dstsize -= thislen + strlen(LastGoodPTTstr);
		// encode a description
		if ((thislen = encodeUvint(dst, 3,
			strlen("current PTT device"))) == -1
		) {
			ZF_LOGE("ERROR: Failure encoding length of device description"
				" \"%s\" for wg_send_devices()", LastGoodPTTstr);
			// Don't send this name without the description
			return (dst - (thislen + strlen(LastGoodPTTstr)))
				- lenptr;
		}
		memcpy(dst + thislen, "current PTT device",
			strlen("current PTT device"));
		dst += thislen + strlen("current PTT device");
		dstsize -= thislen + strlen("current PTT device");
		lenptr[2] = dstcount;  // index of current PTT device
		// name and description for an entry have been encoded
		lenptr[0] = ++dstcount;
	}
	return dst - lenptr;
}

// If state is true, then switch to transmit.  If state is false, then switch
// to recieve.  If none of the methods for locally controlling PTT are set, then
// send commands to the host program which it may use to control PTT.
void KeyPTT(bool state) {
	bool done = false;

	if (PTTmode & PTTRTS) {
		if (state) {
			if (COMSetRTS(hPTTdevice)) {
				done = true;
			} else {
				ZF_LOGE("Error setting RTS %s in KeyPTT(true).", PTTstr);
				close_PTT(true);
			}
		} else {
			if (COMClearRTS(hPTTdevice)) {
				done = true;
			} else {
				ZF_LOGE("Error clearing RTS %s in KeyPTT(true).", PTTstr);
				close_PTT(true);
			}
		}
	}
	if (PTTmode & PTTDTR) {
		if (state) {
			if (COMSetDTR(hPTTdevice)) {
				done = true;
			} else {
				ZF_LOGE("Error setting DTR %s in KeyPTT(true).", PTTstr);
				close_PTT(true);
			}
		} else {
			if (COMClearDTR(hPTTdevice)) {
				done = true;
			} else {
				ZF_LOGE("Error clearing DTR %s in KeyPTT(true).", PTTstr);
				close_PTT(true);
			}
		}
	}
	if (PTTmode & PTTCAT) {
		if (state) {
			if (WriteCOMBlock(hCATdevice, ptt_on_cmd, ptt_on_cmd_len)) {
				done = true;
			} else {
				ZF_LOGE("Error writing to CAT on %s in KeyPTT(true).", CATstr);
				close_CAT(true);
			}
		} else {
			if (WriteCOMBlock(hCATdevice, ptt_off_cmd, ptt_off_cmd_len)) {
				done = true;
			} else {
				ZF_LOGE("Error writing to CAT on %s in KeyPTT(false).", CATstr);
				close_CAT(true);
			}
		}
	}
	if (PTTmode & PTTTCPCAT) {
		if (state) {
			if (tcpsend(tcpCATport, ptt_on_cmd, ptt_on_cmd_len) == 0) {
				done = true;
			} else {
				ZF_LOGE("Error writing to CAT on %s in KeyPTT(true).", CATstr);
				close_CAT(true);
			}
		} else {
			if (tcpsend(tcpCATport, ptt_off_cmd, ptt_off_cmd_len) == 0) {
				done = true;
			} else {
				ZF_LOGE("Error writing to CAT on %s in KeyPTT(false).", CATstr);
				close_CAT(true);
			}
		}
	}
	if (PTTmode & PTTCM108) {
		if (CM108_set_ptt(hCM108device, state) == 0) {
			done = true;
		} else {
			ZF_LOGE("Error setting CM108 device %s in KeyPTT(%s).",
				PTTstr, state ? "true" : "false");
			close_PTT(true);
		}
	}
#ifdef __ARM_ARCH
	if (PTTmode & PTTGPIO) {
		gpioWrite(GPIOpin, (GPIOinvert ? !state : state));
		done = true;
	}
#endif

	if (!done) {
		// This handles PTTmode == 0x00 as well as failure of another mode
		// Require host or VOX to manage PTT
		if (state)
			SendCommandToHostQuiet("PTT TRUE");
		else
			SendCommandToHostQuiet("PTT FALSE");
	}
	ZF_LOGD("[Main.KeyPTT]  PTT-%s", state ? "TRUE" : "FALSE");
	SetLED(0, state);
	wg_send_pttled(0, state);
}
