// macOS-specific OS utility functions.
//
// The timing, signal handling, serial (COM) port, and TCP helpers shared with
// the Linux backend live in src/unix/os_util.c.  This file holds only the parts
// that differ on macOS: CM108 HID PTT (not supported) and serial device
// enumeration (which scans /dev/cu.*, skipping Apple-internal debug/Bluetooth
// nodes that are present on every Mac but unusable for CAT/PTT).

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/os_util.h"
#include "common/ardopcommon.h"
#include "common/log.h"


// CM108 GPIO PTT over raw HID is not supported on macOS.  macOS does not
// provide /dev/hidraw* device nodes.  Supporting this would require using
// IOKit/IOHIDManager, which is out of scope here.
// Log an error and return 0 to indicate failure.
HANDLE OpenCM108(char *devstr) {
	(void)devstr;
	ZF_LOGE("CM108 PTT is not supported on macOS.");
	return 0;
}

// CM108 GPIO PTT is not supported on macOS.  Log an error and return -1.
int CM108_set_ptt(HANDLE fd, bool State) {
	(void)fd;
	(void)State;
	ZF_LOGE("CM108 PTT is not supported on macOS.");
	return (-1);
}

// Return an alternating list of the names and descriptions of available CM108
// devices, or NULL if an error occurs or none are found.  The name is suitable
// for passing to parse_pttstr() and thus must include the CM108: prefix.  The
// description is optional, and shall be an empty string ("") if no description
// is available, as is the case here.  The windows version of this function
// provides a description corresponding to each name.
// So, returned lists shall have an even number of non-null strings, followed
// by one or more null pointers.
// Unless the return value is NULL, use FreeStrlist() to free it when done.
//
// CM108 GPIO PTT over raw HID is not supported on macOS, so no devices are
// discoverable.  Always return NULL.
char** GetCM108Strlist() {
	return NULL;
}


// Return an alternating list of the names and descriptions of available serial
// devices, or NULL if an error occurs or none are found.  The name is suitable
// for passing to parse_catstr() or parse_pttstr() where an optional RTS: or
// DTR: prefix may be applied.  The description is optional, and shall be an
// empty string ("") if no description is available, as is the case here.  The
// windows version of this function provides a description corresponding to each
// name.  On macOS, serial ports appear as /dev/cu.* (callout) device nodes.
// The cu.* form is preferred over the tty.* form since it is non-blocking and
// does not wait for DCD.
// So, returned lists shall have an even number of non-null strings, followed
// by one or more null pointers.
// Unless the return value is NULL, use FreeStrlist() to free it when done.
char** GetSerialStrlist() {
	char **slist = NULL;
	int slistsize = 0;
	DIR *d;
	struct dirent *dir;
	int namesz;
	char pathstr[] = "/dev/";
	if ((d = opendir(pathstr)) == NULL) {
		ZF_LOGD("Directory of serial devices %s not found (%s).",
			pathstr, strerror(errno));
		return slist;
	}
	// Apple-internal /dev/cu.* nodes that are not usable for CAT/PTT.  These
	// are exposed by macOS system services (Bluetooth SPP listener, kernel
	// debug console, Wi-Fi driver debug channel) rather than by IOKit
	// IOSerialBSDClient, so they show up in a raw /dev scan but are not real
	// serial adapters.  cu.wlan-debug is Apple-silicon-only; the other two
	// are present on every modern Mac.
	static const char * const macos_internal_serial_devices[] = {
		"cu.Bluetooth-Incoming-Port",
		"cu.debug-console",
		"cu.wlan-debug",
		NULL
	};
	while ((dir = readdir(d)) != NULL) {
		if (dir->d_type == DT_DIR || dir->d_name[0] == '.')
			continue;
		if (strncmp("cu.", dir->d_name, strlen("cu.")) != 0)
			continue;  // not a callout serial port
		bool is_internal = false;
		for (int i = 0; macos_internal_serial_devices[i] != NULL; i++) {
			if (strcmp(dir->d_name, macos_internal_serial_devices[i]) == 0) {
				is_internal = true;
				break;
			}
		}
		if (is_internal) {
			ZF_LOGD("Skipping macOS-internal serial device %s%s",
				pathstr, dir->d_name);
			continue;
		}
		if (slist == NULL) {
			if ((slist = (char **) malloc(sizeof(char *))) == NULL) {
				ZF_LOGE("Error from malloc() in GetSerialStrlist() (%s)",
					strerror(errno));
				closedir(d);
				return slist;
			}
			slistsize = 1;
			slist[slistsize - 1] = NULL;  // Last pointer must always be null
		}
		slistsize += 2;
		char **tmp = (char **) realloc(slist, slistsize * sizeof(char *));
		if (tmp == NULL) {
			ZF_LOGE("Error from realloc() in GetSerialStrlist() (%s)",
				strerror(errno));
			FreeStrlist(&slist);
			closedir(d);
			return NULL;
		}
		slist = tmp;
		namesz = strlen(pathstr) + strlen(dir->d_name) + 1;
		if ((slist[slistsize - 3] = malloc((namesz) * sizeof(char *)))
			== NULL
		) {
			ZF_LOGE("Error from malloc() in GetSerialStrlist() (%s)",
				strerror(errno));
			closedir(d);
			return slist;
		}
		snprintf(slist[slistsize - 3], namesz, "%s%s", pathstr, dir->d_name);
		slist[slistsize - 2] = strdup("");  // no description provided
		slist[slistsize - 1] = NULL;  // Last pointer must always be null
	}
	closedir(d);
	return slist;
}
