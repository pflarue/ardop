// Linux-specific OS utility functions.
//
// The timing, signal handling, serial (COM) port, and TCP helpers shared with
// the macOS backend live in src/unix/os_util.c.  This file holds only the parts
// that are specific to Linux: CM108 HID PTT, serial device enumeration, and
// (on ARM) Raspberry Pi GPIO PTT.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>

#include "common/os_util.h"
#include "common/ardopcommon.h"
#include "common/log.h"


// Return file descriptor on success.  On failure, log an error
// and return 0;
HANDLE OpenCM108(char *devstr) {
	// devstr is HID Device, eg /dev/hidraw0
	int fd = open(devstr, O_WRONLY);
	if (fd == -1) {
		ZF_LOGE("Could not open CM108 device %s for write, errno=%d",
			devstr, errno);
		if (errno == EACCES)
			ZF_LOGE("The error appears to be related to access permissions. "
				" By default, HID devices are read-only.  Run 'sudo chmod 666"
				" %s' and then try again.  For a persistent fix, create a udev"
				" rule that does this whenever this device is plugged in.  For"
				" more information, see USAGE_linux.md in the ardopcf docs at"
				" https://github.com/pflarue/ardop.",
				devstr);
		return 0;
	}
	return fd;
}

// Return 0 on success.  If an error occurs, log the error
// and return -1;
int CM108_set_ptt(HANDLE fd, bool State) {
	// Iniitalize io[] for State = true
	char io[5] = {0, 0, 1 << (3 - 1), 1 << (3 - 1), 0};
	if (!State)
		io[2] = 0;  // adjust for State = false
	if (write(fd, io, 5) != 5) {
		ZF_LOGE("CM108 PTT write failed. %s", strerror(errno));
		return (-1);
	}
	return 0;
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
char** GetCM108Strlist() {
	char **slist = NULL;
	int slistsize = 0;
	DIR *d;
	struct dirent *dir;
	long devnum;
	int namesz;
	char pathstr[] = "/dev/";
	char prefix[] = "CM108:";
	if ((d = opendir(pathstr)) == NULL) {
		ZF_LOGD("Directory of devices %snot found (%s).", pathstr,
			strerror(errno));
		return slist;
	}
	while ((dir = readdir(d)) != NULL) {
		if (dir->d_type == DT_DIR || dir->d_name[0] == '.')
			continue;
		if (strncmp(dir->d_name, "hidraw", strlen("hidraw")) != 0)
			continue;  // not a CM108 device
		if (!try_parse_long(dir->d_name + strlen("hidraw"), &devnum))
			continue;  // not a valid CM108 device (missing/invalid number)
		if (slist == NULL) {
			if ((slist = (char **) malloc(sizeof(char *))) == NULL) {
				ZF_LOGE("Error from malloc() in GetCM108Strlist() (%s)",
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
			ZF_LOGE("Error from realloc() in GetCM108Strlist() (%s)",
				strerror(errno));
			FreeStrlist(&slist);
			closedir(d);
			return NULL;
		}
		slist = tmp;
		namesz = strlen(prefix) + strlen(pathstr) + strlen(dir->d_name) + 1;
		if ((slist[slistsize - 3] = malloc((namesz) * sizeof(char *)))
			== NULL
		) {
			ZF_LOGE("Error from malloc() in GetSerialStrlist() (%s)",
				strerror(errno));
			closedir(d);
			return slist;
		}
		snprintf(slist[slistsize - 3], namesz, "%s%s%s", prefix, pathstr,
			dir->d_name);
		slist[slistsize - 2] = strdup("");  // No description available
		slist[slistsize - 1] = NULL;  // Last pointer must always be null
	}
	closedir(d);
	return slist;
}


// Return an alternating list of the names and descriptions of available serial
// devices, or NULL if an error occurs or none are found.  The name is suitable
// for passing to parse_catstr() or parse_pttstr() where an optional RTS: or
// DTR: prefix may be applied.  The description is optional, and shall be an
// empty string ("") if no description is available, as is the case here.  The
// windows version of this function provides a description corresponding to each
// name. Include everything in /dev/serial/by_id/ and /dev/serial/by_path/, as
// well as /dev/ttyUSB* and /dev/ttyACM*.  The by-id values are better choices
// since they are usually more descriptive.  The ttyUSB and ttyACM and the
// by-path values are equally valid, and must be included to allow the select
// controls in the webgui to have something to point to if that is the current
// CATstr or PTTstr.
// So, returned lists shall have an even number of non-null strings, followed
// by one or more null pointers.
// Unless the return value is NULL, use FreeStrlist() to free it when done.
char** GetSerialStrlist() {
	char **slist = NULL;
	int slistsize = 0;
	DIR *d;
	struct dirent *dir;
	int namesz;
	char *pathstrs[] = {"/dev/serial/by-id/", "/dev/serial/by-path/", "/dev/"};
	for (int pnum = 0; pnum < 3; ++pnum) {
		if ((d = opendir(pathstrs[pnum])) == NULL) {
			ZF_LOGD("Directory of serial devices %s not found (%s).",
				pathstrs[pnum], strerror(errno));
			return slist;
		}
		while ((dir = readdir(d)) != NULL) {
			if (dir->d_type == DT_DIR || dir->d_name[0] == '.')
				continue;
			if (pnum == 2
				&& strncmp("ttyUSB", dir->d_name, strlen("ttyUSB")) != 0
				&& strncmp("ttyACM", dir->d_name, strlen("ttyACM")) != 0
			)
				continue;  // not a serial port
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
			namesz = strlen(pathstrs[pnum]) + strlen(dir->d_name) + 1;
			if ((slist[slistsize - 3] = malloc((namesz) * sizeof(char *)))
				== NULL
			) {
				ZF_LOGE("Error from malloc() in GetSerialStrlist() (%s)",
					strerror(errno));
				closedir(d);
				return slist;
			}
			snprintf(slist[slistsize - 3], namesz, "%s%s", pathstrs[pnum],
				dir->d_name);
			slist[slistsize - 2] = strdup("");  // no description provided
			slist[slistsize - 1] = NULL;  // Last pointer must always be null
		}
		closedir(d);
	}
	return slist;
}

#ifdef __ARM_ARCH
// ARM Linux specific stuff
// The following GPIO functions appear to be excerpted/adapted from
// https://github.com/joan2937/pigpio which is released to the the public domain
// (or equivalent) via unlicense (http://unlicense.org)
#define PI_OUTPUT 1
#define GPCLR0 10
#define GPSET0 7
#define PI_BANK (gpio>>5)
#define PI_BIT  (1<<(gpio&0x1F))

unsigned piModel;
unsigned piRev;
static volatile uint32_t  *gpioReg = MAP_FAILED;

unsigned gpioHardwareRevision(void) {
	static unsigned rev = 0;

	FILE * filp;
	char term;
	char buf[512];
	int chars=4;  // number of chars in revision string

	if (rev)
		return rev;

	piModel = 0;

	filp = fopen ("/proc/cpuinfo", "r");

	if (filp != NULL) {
		while (fgets(buf, sizeof(buf), filp) != NULL) {
			if (piModel == 0) {
				if (!strncasecmp("model name", buf, 10)) {
					if (strstr (buf, "ARMv6") != NULL) {
						piModel = 1;
						chars = 4;
					} else if (strstr (buf, "ARMv7") != NULL) {
						piModel = 2;
						chars = 6;
					} else if (strstr (buf, "ARMv8") != NULL) {
						piModel = 2;
						chars = 6;
					}
				}
			}

			if (!strncasecmp("revision", buf, 8)) {
				if (sscanf(buf+strlen(buf)-(chars+1),
					"%x%c", &rev, &term) == 2)
				{
					if (term != '\n')
						rev = 0;
				}
			}
		}

		fclose(filp);
	}
	return rev;
}

int gpioInitialise() {
	int fd;
	piRev = gpioHardwareRevision();  // sets piModel and piRev
	fd = open("/dev/gpiomem", O_RDWR | O_SYNC);
	if (fd < 0) {
		ZF_LOGE("Failed to open /dev/gpiomem");
		return (-1);
	}

	gpioReg = (uint32_t *)mmap(NULL, 0xB4, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (gpioReg == MAP_FAILED) {
		ZF_LOGE("Bad, mmap failed");
		return (-1);
	}
	return 0;
}

void gpioSetMode(unsigned gpio, unsigned mode) {
	int reg, shift;

	reg   =  gpio/10;
	shift = (gpio%10) * 3;
	gpioReg[reg] = (gpioReg[reg] & ~(7<<shift)) | (mode<<shift);
}

void gpioWrite(unsigned gpio, unsigned level) {
	if (level == 0)
		*(gpioReg + GPCLR0 + PI_BANK) = PI_BIT;
	else
		*(gpioReg + GPSET0 + PI_BANK) = PI_BIT;
}

void SetupGPIOPTT(int pin, bool invert) {
	gpioSetMode(pin, PI_OUTPUT);
	gpioWrite(pin, invert ? 1 : 0);
}

#endif  // end of ARM Linux
