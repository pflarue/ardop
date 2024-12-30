// ARDOP TNC Host Interface

#define HANDLE int

#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "common/log.h"

void Debugprintf(const char * format, ...);
int WriteCOMBlock(HANDLE fd, char * Block, int BytesToWrite);

extern HANDLE hCATDevice;  // port for Rig Control
extern char HostPort[80];

int ReadCOMBlock(HANDLE fd, char * Block, int MaxLength)
{
	int Length;

	Length = read(fd, Block, MaxLength);

	if (Length < 0)
	{
		return 0;
	}

	return Length;
}

void CloseCOMPort(HANDLE fd)
{
	close(fd);
}

void COMSetDTR(HANDLE fd)
{
	int status;

	if (ioctl(fd, TIOCMGET, &status) == -1)
		ZF_LOGE("ARDOP COMSetDTR TIOCMGET: %s", strerror(errno));
	status |= TIOCM_DTR;
	if (ioctl(fd, TIOCMSET, &status) == -1)
		ZF_LOGE("ARDOP COMSetDTR TIOCMSET: %s", strerror(errno));
}

void COMClearDTR(HANDLE fd)
{
	int status;

	if (ioctl(fd, TIOCMGET, &status) == -1)
		ZF_LOGE("ARDOP COMClearDTR TIOCMGET: %s", strerror(errno));
	status &= ~TIOCM_DTR;
	if (ioctl(fd, TIOCMSET, &status) == -1)
		ZF_LOGE("ARDOP COMClearDTR TIOCMSET: %s", strerror(errno));
}

void COMSetRTS(HANDLE fd)
{
	int status;

	if (ioctl(fd, TIOCMGET, &status) == -1)
		ZF_LOGE("ARDOP COMSetRTS TIOCMGET: %s", strerror(errno));
	status |= TIOCM_RTS;
	if (ioctl(fd, TIOCMSET, &status) == -1)
		ZF_LOGE("ARDOP COMSetRTS TIOCMSET: %s", strerror(errno));
}

void COMClearRTS(HANDLE fd)
{
	int status;

	if (ioctl(fd, TIOCMGET, &status) == -1)
		ZF_LOGE("ARDOP COMClearRTS TIOCMGET: %s", strerror(errno));
	status &= ~TIOCM_RTS;
	if (ioctl(fd, TIOCMSET, &status) == -1)
		ZF_LOGE("ARDOP COMClearRTS TIOCMSET: %s", strerror(errno));
}

static struct speed_struct
{
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


HANDLE OpenCOMPort(void * Port, int speed, int SetDTR, int SetRTS, int Quiet, int Stopbits)
{;
	char buf[100];

	// Linux Version.

	int fd;
	int hwflag = 0;
	u_long param=1;
	struct termios term;
	struct speed_struct *s;

	if ((fd = open(Port, O_RDWR | O_NDELAY)) == -1)
	{
		ZF_LOGE("Com Open failed: %s could not be opened", (const char*)Port);
		return 0;
	}

	// Validate Speed Param

	for (s = speed_table; s->user_speed != -1; s++)
		if (s->user_speed == speed)
			break;

	if (s->user_speed == -1)
	{
		ZF_LOGE("Invalid baud rate (%i) specified for com port (%s).", speed, Port);
		return 0;
	}

	if (tcgetattr(fd, &term) == -1)
	{
		ZF_LOGE("ERROR: Unable to get attributes of %s.", Port);
		return 0;
	}

	cfmakeraw(&term);
	cfsetispeed(&term, s->termios_speed);
	cfsetospeed(&term, s->termios_speed);

	if (tcsetattr(fd, TCSANOW, &term) == -1)
	{
		ZF_LOGE("Error setting baud rate for %s to %i.", Port, speed);
		return 0;
	}

	ioctl(fd, FIONBIO, &param);

	if (SetRTS)
		COMSetRTS(fd);
	else
		COMClearRTS(fd);

	if (SetDTR)
		COMSetDTR(fd);
	else
		COMClearDTR(fd);

	ZF_LOGI("Port %s opened as fd %d", (const char*)Port, fd);

	return fd;
}

int WriteCOMBlock(HANDLE fd, char * Block, int BytesToWrite)
{
	// Some systems seem to have a very small max write size

	int ToSend = BytesToWrite;
	int Sent = 0, ret;

	while (ToSend)
	{
		ret = write(fd, &Block[Sent], ToSend);

		if (ret >= ToSend)
			return 1;

		if (ret == -1)
		{
			if (errno != 11 && errno != 35)  // Would Block
				return 0;

			usleep(10000);
			ret = 0;
		}

		Sent += ret;
		ToSend -= ret;
	}
	return 1;
}
