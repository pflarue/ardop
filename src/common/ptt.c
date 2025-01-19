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
#include "common/ptt.h"
#include "common/log.h"

#define PTTRTS 1
#define PTTDTR 2
#define PTTCAT 4
#define PTTCM108 8
#define PTTGPIO 16
#define PTTTCPCAT 32

#define MAXCATLEN 64

// The default port number used by Hamlib/rigctld as a string.
// This is used in response to --ptt RIGCTLD.
char rigctlddefault[5] = "4532";

int PTTmode = 0;  // PTT Control Flags.

// port names are for comparison with each other to indicate that an device
// handle should be used.  If the actual port name is longer than will fit, then
// anything which matches for this length will be assumed to match.
char CATportstr[80] = "";  // CAT port str
char RTSportstr[80] = "";  // RTS port str
char DTRportstr[80] = "";  // DTR port str
char CM108str[80] = "";  // CM108 device str
char tcpCATportstr[80] = "";  // tcpCAT port (port or address:port) str

HANDLE hCATdevice = 0;  // HANDLE/file descriptor for CAT device
HANDLE hRTSdevice = 0;  // HANDLE/file descriptor for PTT by RTS device
HANDLE hDTRdevice = 0;  // HANDLE/file descriptor for PTT by DTR device
HANDLE hCM108device = 0;  // HANDLE/file descriptor for PTT by CM108 device
int tcpCATport = 0;  // TCP port (usually on 127.0.0.1) for CAT


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

// Set GPIOpin for transmit and clear GPIOpin for recieve.  Except that if
// GPIOinvert is true, then do the opposite.  (ARM only).
int GPIOpin = 0;  // GPIO pin number to be used for PTT control
bool GPIOinvert = false;

// required external functions
char * strlop(char * buf, char delim);
int wg_send_pttled(int cnum, bool isOn);
void gpioWrite(unsigned gpio, unsigned level);
int gpioInitialise();
void SetupGPIOPTT(int pin, bool invert);
void SendCommandToHostQuiet(char * strText);
void SetLED(int LED, int State);
int hex2bytes(char *ptr, unsigned int len, unsigned char *output);
int bytes2hex(char *outputStr, size_t count, unsigned char *data,
	size_t datalen, bool spaces);

// If state is true, then switch to transmit.  If state is false, then switch
// to recieve.  If none of the methods for locally controlling PTT are set, then
// send commands to the host program which it may use to control PTT.
// If more than one method for locally controlling PTT are set, then all of them
// will be used.
//
// TODO: Should success of the various PTT state changes be checked, and
// something (blnClosing? close device and unset PTTmode?) be done if this fails?
// Among other things, a failure could indicate that the device has been
// (unintentionaly?) disconnected or otherwise failed.
void KeyPTT(bool state) {
	if (PTTmode == 0x00) {
		// No local PTT control
		if (state)
			SendCommandToHostQuiet("PTT TRUE");
		else
			SendCommandToHostQuiet("PTT FALSE");
	}
	if (PTTmode & PTTRTS) {
		if (state)
			COMSetRTS(hRTSdevice);
		else
			COMClearRTS(hRTSdevice);
	}
	if (PTTmode & PTTDTR) {
		if (state)
			COMSetDTR(hDTRdevice);
		else
			COMClearDTR(hDTRdevice);
	}
	if (PTTmode & PTTCAT) {
		if (state) {
			if (!WriteCOMBlock(hCATdevice, ptt_on_cmd, ptt_on_cmd_len)) {
				// TODO: Close hCATdevice? Remove PTTCAT from PTTmode?
				ZF_LOGE("Error writing to CAT on %s in KeyPTT(true).", CATportstr);
			}
		} else {
			if (!WriteCOMBlock(hCATdevice, ptt_off_cmd, ptt_off_cmd_len)) {
				// TODO: Close hCATdevice? Remove PTTCAT from PTTmode?
				ZF_LOGE("Error writing to CAT on %s in KeyPTT(false).", CATportstr);
			}
		}
	}
	if (PTTmode & PTTTCPCAT) {
		if (state) {
			if (tcpsend(tcpCATport, ptt_on_cmd, ptt_on_cmd_len) != 0) {
				// TODO: Close tcpCATport? Remove PTTTCPCAT from PTTmode?
				ZF_LOGE("Error writing to TCP CAT at %s in KeyPTT(true).", tcpCATportstr);
			}
		} else {
			if (tcpsend(tcpCATport, ptt_off_cmd, ptt_off_cmd_len) != 0) {
				// TODO: Close tcpCATport? Remove PTTTCPCAT from PTTmode?
				ZF_LOGE("Error writing to TCP CAT at %s in KeyPTT(false).", tcpCATportstr);
			}
		}
	}
	if (PTTmode & PTTCM108) {
		if (CM108_set_ptt(hCM108device, state) != 0) {
			// TODO: Close tcpCATport? Remove PTTTCPCAT from PTTmode?
			ZF_LOGE("Error setting CM108 device %s in KeyPTT(false).", CM108str);
		}
	}
#ifdef __ARM_ARCH
	if (PTTmode & PTTGPIO) {
		gpioWrite(GPIOpin, (GPIOinvert ? !state : state));
	}
#endif

	ZF_LOGD("[Main.KeyPTT]  PTT-%s", state ? "TRUE" : "FALSE");
	SetLED(0, state);
	wg_send_pttled(0, state);
}

// Return true if reading from CAT is enabled.
bool rxCAT() {
	return CATrx;
}

// Parse a hex string intended to be sent to the CAT device or TCP CAT port.
//
// If an error occurs, then write an appropriate msg to the log and return 0.
//
// descstr describes the source of hexstr, and is only used if an error must be
// logged.
//
// Return the length of the cmd (or 0 if an error occured)
static unsigned int parse_cmdhex(char *hexstr, unsigned char *cmd, char *descstr) {
	if (strlen(hexstr) > 2 * MAXCATLEN) {
		ZF_LOGE(
			"ERROR: Hex string for %s may not exceed %i characters (to describe"
			" a %d byte key sequence).  The provided string, \"%s\" has a"
			" length of %u characters.",
			descstr, 2 * MAXCATLEN, MAXCATLEN, hexstr,
			(unsigned int) strlen(hexstr));
		return 0;
	}

	if (strlen(hexstr) % 2 != 0) {
		ZF_LOGE(
			"ERROR: Hex string for %s must contain an even number of"
			" hexidecimal [0-9A-F] characters, but \"%s\" has an odd number"
			" (%u).",
			descstr, hexstr, (unsigned int) strlen(hexstr));
		return 0;
	}

	if (hex2bytes(hexstr, strlen(hexstr) / 2, cmd) != 0) {
		ZF_LOGE(
			"ERROR: Invalid string for %s.  Expected a hexidecimal string with"
			" an even number of [0-9A-F] characters but found \"%s\".",
			descstr, hexstr);
		return 0;
	}
	return strlen(hexstr) / 2;
}

// Write to hexstr up to length bytes (including a terminating NULL) of the hex
// representation of the CAT command used to switch from receive to transmit.
// On success, return 0.
// On failure (length insuficient to hold entire command string), return -1.
// If no command has been set, return 0 with hexstr set to a zero length string.
int get_ptt_on_cmd_hex(char *hexstr, int length) {
	int ret = bytes2hex(hexstr, length, ptt_on_cmd, ptt_on_cmd_len, false);
	if (ret < 0 || ret + 1 <= length)
		return 0;
	return (-1);
}

// Set the CAT command for switching from receive to transmit to a hex string.
// On success, return 0.  If a CAT port and the CAT command for switching from
// transmit to receive are already set, then also enable CAT control of PTT.
// On failure, leave the existing command unchanged, write an error to the log,
// and return -1.  descstr indicates the source of hexstr, to be used in the log
// message if an error occurs.
// A zero length hexstr erases the existing command, and returns 0 for success.
int set_ptt_on_cmd(char *hexstr, char *descstr) {
	if (hexstr[0] == 0x00) {
		ptt_on_cmd_len = 0;
		ZF_LOGI("PTT ON CMD set to None.");
		PTTmode &= (0xFF - PTTCAT);  // Disable PTTCAT
		PTTmode &= (0xFF - PTTTCPCAT);  // Disable PTTTCPCAT
	}
	unsigned char tmp_cmd[MAXCATLEN];
	unsigned int tmp_len = parse_cmdhex(hexstr, tmp_cmd, descstr);
	if (tmp_len == 0)
		return (-1);  // Error alreadly logged
	memcpy(ptt_on_cmd, tmp_cmd, tmp_len);
	ptt_on_cmd_len = tmp_len;
	ZF_LOGI("PTT ON CMD set to %s.", hexstr);
	if (hCATdevice != 0 && ptt_off_cmd_len > 0) {
		PTTmode |= PTTCAT;
		ZF_LOGI("PTT using CAT Port: %s", CATportstr);
	}
	if (tcpCATport != 0 && ptt_off_cmd_len > 0) {
		PTTmode |= PTTTCPCAT;
		ZF_LOGI("PTT using TCP CAT port: %s", tcpCATportstr);
	}
	return 0;
}

// Write to hexstr up to length bytes (including a terminating NULL) of the hex
// representation of the CAT command used to switch from transmit to receive.
// On success, return 0.
// On failure (length insuficient to hold entire command string), return -1.
// If no command has been set, return 0 with hexstr set to a zero length string.
int get_ptt_off_cmd_hex(char *hexstr, int length) {
	int ret = bytes2hex(hexstr, length, ptt_off_cmd, ptt_off_cmd_len, false);
	if (ret < 0 || ret + 1 <= length)
		return 0;
	return (-1);
}

// Set the CAT command for switching from transmit to receive to a hex string.
// On success, return 0.  If a CAT port or tcpCATport and the CAT command for
// switching from receive to transmit are already set, then also enable CAT
// control of PTT.
// On failure, leave the existing command unchanged, write an error to the log,
// and return -1.  descstr indicates the source of hexstr, to be used in the log
// message if an error occurs.
// A zero length hexstr erases the existing command, and returns 0 for success.
int set_ptt_off_cmd(char *hexstr, char *descstr) {
	if (hexstr[0] == 0x00) {
		ptt_off_cmd_len = 0;
		ZF_LOGI("PTT OFF CMD set to None.");
		PTTmode &= (0xFF - PTTCAT);  // Disable PTTCAT
		PTTmode &= (0xFF - PTTTCPCAT);  // Disable PTTTCPCAT
	}
	unsigned char tmp_cmd[MAXCATLEN];
	unsigned int tmp_len = parse_cmdhex(hexstr, tmp_cmd, descstr);
	if (tmp_len == 0)
		return (-1);  // Error alreadly logged
	memcpy(ptt_off_cmd, tmp_cmd, tmp_len);
	ptt_off_cmd_len = tmp_len;
	ZF_LOGI("PTT OFF CMD set to %s.", hexstr);
	if (hCATdevice != 0 && ptt_on_cmd_len > 0) {
		PTTmode |= PTTCAT;
		ZF_LOGI("PTT using CAT Port: %s", CATportstr);
	}
	if (tcpCATport != 0 && ptt_on_cmd_len > 0) {
		PTTmode |= PTTTCPCAT;
		ZF_LOGI("PTT using TCP CAT port: %s", tcpCATportstr);
	}
	return 0;
}

// Return the number of bytes read, or -1 if an error occurs.
// TCP CAT port is read if hCATdevice is not open or if reading from
// hCATdevice returns 0;
// If neither CAT device nor TCP CAT port is open, or if no data is
// available from either of them, return 0
int readCAT(unsigned char *data, size_t len) {
	int ret = 0;
	if (hCATdevice == 0 && tcpCATport == 0) {
		return ret;
	}
	if (hCATdevice != 0) {
		ret = ReadCOMBlock(hCATdevice, data, len);
		if (ret != 0)
			return ret; // either error or some data was read;
	}
	if (tcpCATport != 0) {
		// unlike recv(), nbrecv() returns 0 rather than -1 for nothing to read
		if ((ret = nbrecv(tcpCATport, (char *) data, len)) == -1)
			ZF_LOGE("Error reading from TCP CAT"); // TODO: close tcpCATport?
		return ret;
	}
	return 0;  // No data was available from hCATdevice and tcpCATport == 0
}

// Send data represented by a string of hexidecimal digits to the CAT device.
// Return -1 and log an error if no CAT device is available or an error occurs.
// Return 0 on success.
int sendCAThex(char *hexstr) {
	if (hCATdevice == 0 && tcpCATport == 0) {
		ZF_LOGW("No CAT device or TCP CAT port.  sendCAThex() failed.");
		return (-1);
	}
	unsigned char tmp_cmd[MAXCATLEN];
	unsigned int tmp_len = parse_cmdhex(hexstr, tmp_cmd, "RADIOHEX host comand");
	if (tmp_len == 0)
		return (-1);  // error already logged.
	if (hCATdevice != 0) {
		if (!WriteCOMBlock(hCATdevice, tmp_cmd, tmp_len)) {
			// TODO: Close hCATdevice?
			ZF_LOGE("Error writing %s to CAT on %s.", hexstr, CATportstr);
			return (-1);
		}
		CATrx = true;
	}
	if (tcpCATport != 0) {
		if (tcpsend(tcpCATport, tmp_cmd, tmp_len) != 0) {
			// TODO: Close tcpCATport?
			ZF_LOGE("Error writing %s to TCP CAT at %s.", hexstr, tcpCATportstr);
			return (-1);
		}
		CATrx = true;
	}
	return 0;
}

// Set the device to be used for CAT control.
// If port is a zero length string, then disable CAT control.
// On failure, log an error message and return -1;
// On success, return 0.  If both PTT CAT commands are already set, then also
// enable CAT control of PTT.
int set_CATport(char *portstr) {
	if (strcmp(CATportstr, portstr) == 0)
		return 0;  // no change
	strncpy(CATportstr, portstr, sizeof(CATportstr));

	// close existing port if open
	if (hCATdevice != 0 && hCATdevice != hRTSdevice && hCATdevice != hDTRdevice)
		CloseCOMPort(hCATdevice);
	if (tcpCATport != 0)
		CATrx = false;  // Don't pass data from CAT to host until tx other than PTT

	if (portstr[0] == 0x00) {
		if (PTTmode & PTTCAT)
			ZF_LOGI("CAT PTT disabled");
		PTTmode &= (0xFF - PTTCAT);  // Disable PTTCAT
		return 0; // An empty string, do nothing but close CAT port if open.
	}
	if (strcmp(RTSportstr, portstr) == 0)
		hCATdevice = hRTSdevice;  // Reuse port already open for RTS
	else if (strcmp(DTRportstr, portstr) == 0)
		hCATdevice = hDTRdevice;  // Reuse port already open for DTR
	else {
		char *baudstr = strlop(portstr, ':');
		int baud = baudstr == NULL ? 19200 : atoi(baudstr);
		if ((hCATdevice = OpenCOMPort(portstr, baud)) == 0) {
			CATportstr[0] = 0x00;  // clear invalid CATportstr
			if (PTTmode & PTTCAT)
				ZF_LOGI("CAT PTT disabled");
			PTTmode &= (0xFF - PTTCAT);  // Disable PTTCAT
			return (-1);  // Error msg already logged.
		}
	}
	ZF_LOGI("CAT Control on port %s", CATportstr);
	if (ptt_on_cmd_len > 0 && ptt_off_cmd_len > 0) {
		PTTmode |= PTTCAT;
		ZF_LOGI("PTT using CAT Port: %s", CATportstr);
	}
}

void close_tcpCAT() {
	if (tcpCATport == 0)
		return;  // Nothing to close
	tcpclose(tcpCATport);
	tcpCATport = 0;
	if (hCATdevice != 0)
		CATrx = false;  // Don't pass data from tcpCAT to host until tx other than PTT
}

// portstr is usually a positive integer to indicate a TCP port on the
// local machine (127.0.0.1), but may also have the form
// ddd.ddd.ddd.ddd:port for a remote network port.
int set_tcpCAT(char *portstr) {
	if (strcmp(tcpCATportstr, portstr) == 0)
		return 0;  // no change
	strncpy(tcpCATportstr, portstr, sizeof(tcpCATportstr));

	// close existing connection if open
	if (tcpCATport != 0)
		close_tcpCAT();

	if (portstr[0] == 0x00) {
		if (PTTmode & PTTTCPCAT)
			ZF_LOGI("TCP CAT PTT disabled");
		PTTmode &= (0xFF - PTTTCPCAT);  // Disable PTTTCPCAT
		return 0; // An empty string, do nothing but close tcpCAT connection if open.
	}

	char errformat[] = "Invalid TCP CAT port.  Expected either a positive"
		" integer for a local TCP port number or ddd.ddd.ddd.ddd:port (where each"
		" ddd is an integer in the range of 0 to 255 and port is a positive"
		" integer) for a remote network TCP port, but found \"%s\".";
	char address[16] = "127.0.0.1";  // default (local) address
	int port = 0;
	char *remoteportstr = strlop(portstr, ':');
	if (remoteportstr != NULL) {
		// portstr is address:port
		if (strlen(portstr) > sizeof(address)) {
			ZF_LOGE(errformat, tcpCATport);
			tcpCATportstr[0] = 0x00;
			return (-1);
		}
		strcpy(address, portstr);
		port = atoi(remoteportstr);
	} else
		port = atoi(portstr);

	if (port < 1) {
		ZF_LOGE(errformat, tcpCATport);
		tcpCATportstr[0] = 0x00;
		return (-1);
	}

	// tcpconnect() may take a while to fail if address is unreachable.
	// So, write to log that we are attempting this connection to explain
	// the possible pause in execution.
	ZF_LOGI("Attempting to connect to %s for TCP CAT.", tcpCATportstr);
	if ((tcpCATport = tcpconnect(address, port)) == -1) {
		ZF_LOGI("Unable to connect to %s for TCP CAT.", tcpCATportstr);
		tcpCATportstr[0] = 0x00;
		tcpCATport = 0;
		return (-1);
	}

	ZF_LOGI("TCP CAT Control at %s", tcpCATportstr);
	if (ptt_on_cmd_len > 0 && ptt_off_cmd_len > 0) {
		PTTmode |= PTTTCPCAT;
		ZF_LOGI("PTT using TCP CAT port: %s", tcpCATportstr);
	}
	return 0;
}

// Set the device to be used for PTT control by setting RTS.
// If port is a zero length string, then disable PTT control by setting RTS.
// On failure, log an error message and return -1;
// On success, return 0.
int set_RTSport(char *portstr) {
	if (strcmp(RTSportstr, portstr) == 0)
		return 0;  // no change
	strncpy(RTSportstr, portstr, sizeof(RTSportstr));

	// close existing port if open
	if (hRTSdevice != 0 && hRTSdevice != hCATdevice && hRTSdevice != hDTRdevice)
		CloseCOMPort(hRTSdevice);

	if (portstr[0] == 0x00) {
		PTTmode &= (0xFF - PTTRTS);  // Disable PTTRTS
		ZF_LOGI("RTS PTT disabled");
		return 0; // An empty string, do nothing but close RTS port if open.
	}
	if (strcmp(CATportstr, portstr) == 0)
		hRTSdevice = hCATdevice;  // Reuse port already open for CAT
	else if (strcmp(DTRportstr, portstr) == 0)
		hRTSdevice = hDTRdevice;  // Reuse port already open for DTR
	else {
		char *baudstr = strlop(portstr, ':');
		int baud = baudstr == NULL ? 19200 : atoi(baudstr);
		if ((hRTSdevice = OpenCOMPort(portstr, baud)) == 0) {
			RTSportstr[0] = 0x00;  // clear invalid RTSportstr
			PTTmode &= (0xFF - PTTRTS);  // Disable PTTRTS
			ZF_LOGI("RTS PTT disabled");
			return (-1);  // Error msg already logged.
		}
	}
	// OpenCOMPort always clears RTS and DTR, so PTT is not ON due to RTS.
	PTTmode |= PTTRTS;  // Enable PTTRTS
	ZF_LOGI("Using RTS on port %s for PTT", RTSportstr);
	return 0;
}

// Set the device to be used for PTT control by setting DTR.
// If port is a zero length string, then disable PTT control by setting DTR.
// On failure, log an error message and return -1;
// On success, return 0.
int set_DTRport(char *portstr) {
	if (strcmp(DTRportstr, portstr) == 0)
		return 0;  // no change
	strncpy(DTRportstr, portstr, sizeof(RTSportstr));

	// close existing port if open
	if (hDTRdevice != 0 && hDTRdevice != hCATdevice && hDTRdevice != hRTSdevice)
		CloseCOMPort(hDTRdevice);

	if (portstr[0] == 0x00) {
		PTTmode &= (0xFF - PTTDTR);  // Disable PTTDTR
		ZF_LOGI("DTR PTT disabled");
		return 0; // An empty string, do nothing but close DTR port if open.
	}
	if (strcmp(CATportstr, portstr) == 0)
		hDTRdevice = hCATdevice;  // Reuse port already open for CAT
	else if (strcmp(RTSportstr, portstr) == 0)
		hDTRdevice = hRTSdevice;  // Reuse port already open for RTS
	else {
		char *baudstr = strlop(portstr, ':');
		int baud = baudstr == NULL ? 19200 : atoi(baudstr);
		if ((hDTRdevice = OpenCOMPort(portstr, baud)) == 0) {
			DTRportstr[0] = 0x00;  // clear invalid RTSportstr
			PTTmode &= (0xFF - PTTDTR);  // Disable PTTDTR
			ZF_LOGI("DTR PTT disabled");
			return (-1);  // Error msg already logged.
		}
	}
	// OpenCOMPort always clears RTS and DTR, so PTT is not ON due to DTR.
	PTTmode |= PTTDTR;  // Enable PTTDTR
	ZF_LOGI("Using DTR on port %s for PTT", DTRportstr);
	return 0;
}

int set_GPIOpin(int pin) {
#ifdef __ARM_ARCH
	if (pin == 0) {
		PTTmode &= (0xFF - PTTGPIO);  // Disable GPIO PTT
		ZF_LOGI("GPIO PTT disabled");
		return 0;
	}
	if (pin < 0) {
		GPIOpin = -pin;
		GPIOinvert = true;
	} else {
		GPIOpin = pin;
		GPIOinvert = false;
	}
	if (gpioInitialise() == 0) {
		SetupGPIOPTT(GPIOpin, GPIOinvert);
		PTTmode |= PTTGPIO;  // Enable PTTGPIO
		ZF_LOGI("Using %sGPIO pin %i for PTT",
			GPIOinvert ? "inverted " : "", GPIOpin);
	} else {
		ZF_LOGE("Couldn't initialise GPIO interface for PTT");
		PTTmode &= (0xFF - PTTGPIO);  // disable GPIO PTT
		return (-1);
	}
#else
	(void) pin;  // to avoid unused variable warning
	ZF_LOGE("GPIO interface for PTT not available on this platform");
	PTTmode &= (0xFF - PTTGPIO);  // disable GPIO PTT
	return (-1);
#endif
}

int set_cm108(char *devstr) {
	if (strcmp(CM108str, devstr) == 0)
		return 0;  // no change
	strncpy(CM108str, devstr, sizeof(CM108str));

	// close existing device if open
	if (hCM108device != 0)
		CloseCM108(hCM108device);

	if (devstr[0] == 0x00) {
		PTTmode &= (0xFF - PTTCM108);  // disable CM108 PTT
		ZF_LOGI("CM108 Device PTT disabled");
		return 0; // An empty string, do nothing but disable CM108 PTT
	}

	if ((hCM108device = OpenCM108(devstr)) == 0) {
		// On Windows, if devstr is "?" or "??"
		CM108str[0] = 0x00;  // clear invalid CM108str
		PTTmode &= (0xFF - PTTCM108);  // Disable PTTCM108
		ZF_LOGI("CM108 PTT disabled");
		return (-1);  // Error msg already logged.
	}
	PTTmode |= PTTCM108;  // Enable PTTCM108
	ZF_LOGI("Using CM108 device %s for PTT", CM108str);
	return 0;
}


// Take the argument to the -ptt or -p command line option and pass it to the
// appropriate function for control of PTT by RTS, DTR, or CM108.
// Return the result returned by the appropriate function, which should be 0 for
// success or -1 for failure
int parse_pttstr(char *pttstr) {
	if (strcmp(pttstr, "RIGCTLD") == 0) {
		// This is equivalent to
		// -c TCP:4532 -k 5420310A -u 5420300A
		// to use hamlib/rigctld running on its default
		// TCP port 4532 for PTT.
		int ret = set_tcpCAT(rigctlddefault);
		if (ret == 0)
			ret = set_ptt_on_cmd("5420310A",
				"command line option --ptt RIGCTLD");
		if (ret == 0)
			ret = set_ptt_off_cmd("5420300A",
				"command line option --ptt RIGCTLD");
		return ret;
	}
	if (strncmp(pttstr, "CM108:", 6) == 0 && strlen(pttstr) > 6)
		return set_cm108(pttstr + 6);
	if (strncmp(pttstr, "RTS:", 4) == 0 && strlen(pttstr) > 4)
		return set_RTSport(pttstr + 4);
	if (strncmp(pttstr, "DTR:", 4) == 0 && strlen(pttstr) > 4)
		return set_DTRport(pttstr + 4);
	return set_RTSport(pttstr);  // default
}

int parse_catstr(char *catstr) {
	if (strncmp(catstr, "TCP:", 4) == 0 && strlen(catstr) > 4)
		return set_tcpCAT(catstr + 4);
	return set_CATport(catstr);  // default without prefix
}
