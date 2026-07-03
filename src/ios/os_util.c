// iOS-specific OS utility functions.
//
// The timing, signal handling, serial (COM) port, and TCP helpers shared with
// the Linux and macOS backends live in src/unix/os_util.c.  This file holds
// only the parts that differ on iOS: CM108 HID PTT and serial device
// enumeration, neither of which is available to a sandboxed iOS application.
//
// On iOS there is no user access to raw HID devices or serial ports, so PTT
// control via CM108, RTS/DTR, or GPIO is not supported.  Keying is expected to
// be handled out of band (VOX, or an external/Bluetooth keyer driven by the
// application in response to the PTT events delivered through ardop_lib).

#include <stdbool.h>

#include "common/os_util.h"
#include "common/ardopcommon.h"
#include "common/log.h"

// CM108 GPIO PTT over raw HID is not supported on iOS.
HANDLE OpenCM108(char *devstr) {
	(void)devstr;
	ZF_LOGE("CM108 PTT is not supported on iOS.");
	return 0;
}

// CM108 GPIO PTT is not supported on iOS.  Log an error and return -1.
int CM108_set_ptt(HANDLE fd, bool State) {
	(void)fd;
	(void)State;
	ZF_LOGE("CM108 PTT is not supported on iOS.");
	return (-1);
}

// No CM108 devices are discoverable on iOS.  Always return NULL.
char** GetCM108Strlist() {
	return NULL;
}

// No serial devices are accessible to a sandboxed iOS application.  Always
// return NULL.
char** GetSerialStrlist() {
	return NULL;
}
