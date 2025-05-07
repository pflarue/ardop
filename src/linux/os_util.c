#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>

#include "common/os_util.h"
#include "common/log.h"


struct timespec time_start;  // reference used for getNow() and Now;

extern char PlaybackDevice[80];
extern char DecodeWav[5][256];
extern int WavNow;  // Time since start of WAV file being decoded
extern bool blnClosing;
extern int closedByPosixSignal;


void Sleep(long unsigned int mS) {
	if (strcmp(PlaybackDevice, "NOSOUND") != 0)
		usleep(mS * 1000);
	return;
}


// Write UTC date and time of the form YYYYMMDD_hhmmss (15 characters) to out
void get_utctimestr(char *out) {
	struct tm * tm;
	time_t T;
	struct timespec tp;
	int ss, hh, mm;

	T = time(NULL);
	tm = gmtime(&T);
	clock_gettime(CLOCK_REALTIME, &tp);
	ss = tp.tv_sec % 86400;  // Seconds in a day
	hh = ss / 3600;
	mm = (ss - (hh * 3600)) / 60;
	ss = ss % 60;

	sprintf(out, "%04d%02d%02d_%02d%02d%02d",
		tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, hh, mm, ss);
}


// Return a ms resolution time value
unsigned int getNow() {
	struct timespec tp;

	// When decoding a WAV file, return WavNow, a measure of the offset
	// in ms from the start of the WAV file.
	if (DecodeWav[0][0])
		return WavNow;

	// Otherwise, return a measure of clock time (also measured in ms).
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return (unsigned int) ((tp.tv_sec - time_start.tv_sec) * 1000
		+ (tp.tv_nsec - time_start.tv_nsec) / 1000000);
}


static void signal_handler_trigger_shutdown(int sig) {
	blnClosing = true;
	closedByPosixSignal = sig;
}


int platform_init() {
	struct sigaction act;

	// Set time_start, which is used by getNow() for Nowe
	clock_gettime(CLOCK_MONOTONIC, &time_start);

	// Trap signals
	memset (&act, '\0', sizeof(act));

	act.sa_handler = &signal_handler_trigger_shutdown;
	if (sigaction(SIGINT, &act, NULL) < 0)
		perror ("SIGINT");

	act.sa_handler = &signal_handler_trigger_shutdown;
	if (sigaction(SIGTERM, &act, NULL) < 0)
		perror ("SIGTERM");

	act.sa_handler = SIG_IGN;

	if (sigaction(SIGHUP, &act, NULL) < 0)
		perror ("SIGHUP");

	if (sigaction(SIGPIPE, &act, NULL) < 0)
		perror ("SIGPIPE");

	return (0);
}


/*
 * Lists common signal abbreviations
 *
 * This method is a portable version of glibc's sigabbrev_np().
 * It only supports a handful of signal names that ARDOP
 * currently catches and/or ignores. Unlike the glibc function,
 * the return value is always guaranteed to be non-NULL.
 */
const char* PlatformSignalAbbreviation(int signal) {
	switch (signal) {
		case SIGABRT:
			return "SIGABRT";
		case SIGINT:
			return "SIGINT";
		case SIGHUP:
			return "SIGHUP";
		case SIGPIPE:
			return "SIGPIPE";
		case SIGTERM:
			return "SIGTERM";
		default:
			return "Unknown";
	}
}

static struct speed_struct {
	int	user_speed;
	speed_t termios_speed;
} speed_table[] = {
	{300, B300},
	{600, B600},
	{1200, B1200},
	{2400, B2400},
	{4800, B4800},
	{9600, B9600},
	{19200, B19200},
	{38400, B38400},
	{57600, B57600},
	{115200, B115200},
	{-1, B0}
};


// Return file descriptor on success.  On failure, log an error
// and return 0;
HANDLE OpenCOMPort(void * Port, int speed) {
	int fd;
	struct termios term;
	struct speed_struct *s;

	if ((fd = open(Port, O_RDWR | O_NONBLOCK)) == -1) {
		ZF_LOGE("Com Open failed: %s could not be opened", (const char*)Port);
		return 0;
	}

	// Validate Speed Param
	for (s = speed_table; s->user_speed != -1; s++)
		if (s->user_speed == speed)
			break;

	if (s->user_speed == -1) {
		ZF_LOGE("Invalid baud rate (%i) specified for com port (%s).", speed, (char *) Port);
		return 0;
	}
	if (tcgetattr(fd, &term) == -1) {
		ZF_LOGE("ERROR: Unable to get attributes of %s.", (char *) Port);
		return 0;
	}

	cfmakeraw(&term);
	cfsetispeed(&term, s->termios_speed);
	cfsetospeed(&term, s->termios_speed);

	if (tcsetattr(fd, TCSANOW, &term) == -1) {
		ZF_LOGE("Error setting baud rate for %s to %i.", (char *) Port, speed);
		return 0;
	}

	// This COM port is opened for CAT control or PTT control via RTS or DTR.
	// For all of these cases, Clearing RTS and DTR shouldn't hurt and
	// may help (to avoid accidentally keying PTT);
	COMClearRTS(fd);
	COMClearDTR(fd);

	ZF_LOGI("Port %s opened as fd %d", (const char*)Port, fd);
	return fd;
}

void CloseCOMPort(HANDLE *fd) {
	close(*fd);
	*fd = 0;
}

bool COMSetRTS(HANDLE fd) {
	int status;

	if (ioctl(fd, TIOCMGET, &status) == -1) {
		ZF_LOGE("ARDOP COMSetRTS TIOCMGET: %s", strerror(errno));
		return false;
	}
	status |= TIOCM_RTS;
	if (ioctl(fd, TIOCMSET, &status) == -1) {
		ZF_LOGE("ARDOP COMSetRTS TIOCMSET: %s", strerror(errno));
		return false;
	}
	return true;
}

bool COMClearRTS(HANDLE fd) {
	int status;

	if (ioctl(fd, TIOCMGET, &status) == -1) {
		ZF_LOGE("ARDOP COMClearRTS TIOCMGET: %s", strerror(errno));
		return false;
	}
	status &= ~TIOCM_RTS;
	if (ioctl(fd, TIOCMSET, &status) == -1) {
		ZF_LOGE("ARDOP COMClearRTS TIOCMSET: %s", strerror(errno));
		return false;
	}
	return true;
}

bool COMSetDTR(HANDLE fd) {
	int status;

	if (ioctl(fd, TIOCMGET, &status) == -1) {
		ZF_LOGE("ARDOP COMSetDTR TIOCMGET: %s", strerror(errno));
		return false;
	}
	status |= TIOCM_DTR;
	if (ioctl(fd, TIOCMSET, &status) == -1) {
		ZF_LOGE("ARDOP COMSetDTR TIOCMSET: %s", strerror(errno));
		return false;
	}
	return true;
}

bool COMClearDTR(HANDLE fd) {
	int status;

	if (ioctl(fd, TIOCMGET, &status) == -1) {
		ZF_LOGE("ARDOP COMClearDTR TIOCMGET: %s", strerror(errno));
		return false;
	}
	status &= ~TIOCM_DTR;
	if (ioctl(fd, TIOCMSET, &status) == -1) {
		ZF_LOGE("ARDOP COMClearDTR TIOCMSET: %s", strerror(errno));
		return false;
	}
	return true;
}


bool WriteCOMBlock(HANDLE fd, unsigned char * Block, int BytesToWrite) {
	int ToSend = BytesToWrite;
	int Sent = 0, ret;

	while (ToSend) {
		ret = write(fd, &Block[Sent], ToSend);
		if (ret >= ToSend)
			return true;
		if (ret == -1) {
			if (errno != 11 && errno != 35)  // Would Block
				return false;
			usleep(10000);
			ret = 0;
		}
		Sent += ret;
		ToSend -= ret;
	}
	return true;
}

// Return the number of bytes read, or -1 if an error occurs.
int ReadCOMBlock(HANDLE fd, unsigned char * Block, int MaxLength) {
	int ret = read(fd, Block, MaxLength);
	if (ret == -1 && (errno == EWOULDBLOCK || errno == EAGAIN))
		return 0;
	return ret;
}

// Similar to recv() with no flags, but if no data available (EAGAIN or EWOULDBLOCK),
// return 0 rather than -1
int nbrecv(int sockfd, char *data, size_t len) {
	int ret = recv(sockfd, data, len, 0);
	if (ret == -1 && (errno == EWOULDBLOCK || errno == EAGAIN))
		return 0;
	return ret;
}

// Provide tcpconnect() here rather than implementing it in ptt.c
// mostly to produce better error messages if something fails.
// Return a file descriptor on success.  If an error occurs, log the error
// and return -1;
int tcpconnect(char *address, int port) {
	struct sockaddr_in addr;
	int fd = 0;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_pton(AF_INET, address, &addr.sin_addr) != 1) {
		ZF_LOGE("Error parsing %s:%i for TCP port.", address, port);
		return (-1);
	}
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		ZF_LOGE("Error creating socket for TCP port for %s:%i. %s",
			address, port, strerror(errno));
		return (-1);
	}
	if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		ZF_LOGE("Error connecting to TCP port at %s:%i. %s",
			address, port, strerror(errno));
		close(fd);
		return (-1);
	}
	// set socket to be non-blocking
	fcntl(fd, F_SETFL, O_NONBLOCK);
	return fd;
}

// Return 0 on success.  If an error occurs, log the error
// and return -1;
int tcpsend(int fd, unsigned char *data, size_t datalen) {
	if (send(fd, (char *) data, datalen, 0) != (int) datalen) {
		ZF_LOGE("Error sending data to TCP port. %s", strerror(errno));
		return (-1);
	}
	return 0;
}

void tcpclose(int *fd) {
	close(*fd);
	*fd = 0;
}

void CloseCM108(HANDLE *fd) {
	close(*fd);
	*fd = 0;
}

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
