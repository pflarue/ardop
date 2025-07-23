//
// Serial/PTT interface for macOS
//
// This handles serial port communications and PTT control on macOS
// Based on LinSerial.c - POSIX serial interface works on both Linux and macOS
//

#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define HANDLE int

#include "common/ardopcommon.h"
#include "common/log.h"

// Forward declarations
void Debugprintf(const char *format, ...);
int WriteCOMBlock(HANDLE fd, char *Block, int BytesToWrite);
void COMSetDTR(HANDLE fd);
void COMClearDTR(HANDLE fd);
void COMSetRTS(HANDLE fd);
void COMClearRTS(HANDLE fd);

// External variables (defined in common code)
extern HANDLE hCATDevice; // port for Rig Control
extern char HostPort[80];

// Speed conversion table for POSIX termios
static struct speed_struct
{
	int user_speed;
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
		{-1, B0}};

// Read data from serial port
int ReadCOMBlock(HANDLE fd, char *Block, int MaxLength)
{
	int Length;

	Length = read(fd, Block, MaxLength);

	if (Length < 0)
	{
		return 0;
	}

	return Length;
}

// Open serial port - compatible with macOS /dev/cu.* devices
HANDLE OpenCOMPort(void *Port, int speed, int SetDTR, int SetRTS, int Quiet, int Stopbits)
{
	char buf[100];

	// macOS Version - works with /dev/cu.* devices

	int fd;
	int hwflag = 0;
	u_long param = 1;
	struct termios term;
	struct speed_struct *s;

	// macOS serial ports are typically /dev/cu.usbserial-*, /dev/cu.Bluetooth-*, etc.
	// Open with non-blocking I/O
	if ((fd = open(Port, O_RDWR | O_NDELAY)) == -1)
	{
		if (!Quiet)
			ZF_LOGE("Com Open failed: %s could not be opened", (const char *)Port);
		return 0;
	}

	// Validate Speed Parameter
	for (s = speed_table; s->user_speed != -1; s++)
		if (s->user_speed == speed)
			break;

	if (s->user_speed == -1)
	{
		if (!Quiet)
			fprintf(stderr, "tty_speed: invalid speed %d", speed);
		close(fd);
		return 0;
	}

	// Get current terminal attributes
	if (tcgetattr(fd, &term) == -1)
	{
		if (!Quiet)
			perror("tty_speed: tcgetattr");
		close(fd);
		return 0;
	}

	// Configure terminal for raw mode
	cfmakeraw(&term);
	cfsetispeed(&term, s->termios_speed);
	cfsetospeed(&term, s->termios_speed);

	// Set stop bits if specified (default is 1)
	if (Stopbits == 2)
	{
		term.c_cflag |= CSTOPB;
	}
	else
	{
		term.c_cflag &= ~CSTOPB;
	}

	// Apply terminal settings
	if (tcsetattr(fd, TCSANOW, &term) == -1)
	{
		if (!Quiet)
			perror("tty_speed: tcsetattr");
		close(fd);
		return 0;
	}

	// Set non-blocking I/O
	ioctl(fd, FIONBIO, &param);

	// Set initial DTR/RTS states if requested
	if (SetDTR)
		COMSetDTR(fd);
	else
		COMClearDTR(fd);

	if (SetRTS)
		COMSetRTS(fd);
	else
		COMClearRTS(fd);

	if (!Quiet)
		ZF_LOGI("macOS Serial Port %s fd %d opened successfully", (const char *)Port, fd);

	return fd;
}

// Set DTR (Data Terminal Ready) line high
void COMSetDTR(HANDLE fd)
{
	int status;

	if (fd <= 0)
		return;

	ioctl(fd, TIOCMGET, &status);
	status |= TIOCM_DTR;
	ioctl(fd, TIOCMSET, &status);
}

// Clear DTR (Data Terminal Ready) line (set low)
void COMClearDTR(HANDLE fd)
{
	int status;

	if (fd <= 0)
		return;

	ioctl(fd, TIOCMGET, &status);
	status &= ~TIOCM_DTR;
	ioctl(fd, TIOCMSET, &status);
}

// Set RTS (Request To Send) line high - commonly used for PTT
void COMSetRTS(HANDLE fd)
{
	int status;

	if (fd <= 0)
		return;

	if (ioctl(fd, TIOCMGET, &status) == -1)
		perror("ARDOP PTT TIOCMGET");
	status |= TIOCM_RTS;
	if (ioctl(fd, TIOCMSET, &status) == -1)
		perror("ARDOP PTT TIOCMSET");
}

// Clear RTS (Request To Send) line (set low) - commonly used for PTT
void COMClearRTS(HANDLE fd)
{
	int status;

	if (fd <= 0)
		return;

	if (ioctl(fd, TIOCMGET, &status) == -1)
		perror("ARDOP PTT TIOCMGET");
	status &= ~TIOCM_RTS;
	if (ioctl(fd, TIOCMSET, &status) == -1)
		perror("ARDOP PTT TIOCMSET");
}

// Write data to serial port with retry logic
int WriteCOMBlock(HANDLE fd, char *Block, int BytesToWrite)
{
	// Some systems have a small max write size, so we loop until all data is sent

	int ToSend = BytesToWrite;
	int Sent = 0, ret;

	if (fd <= 0)
		return 0;

	while (ToSend)
	{
		ret = write(fd, &Block[Sent], ToSend);

		if (ret >= ToSend)
			return 1; // All data sent successfully

		if (ret == -1)
		{
			if (errno != EAGAIN && errno != EWOULDBLOCK) // Would Block
				return 0;																	 // Real error

			usleep(10000); // Wait 10ms and retry
			ret = 0;
		}

		Sent += ret;
		ToSend -= ret;
	}
	return 1;
}

// Close serial port
void CloseCOMPort(HANDLE fd)
{
	if (fd > 0)
		close(fd);
}

// Additional macOS-specific utility functions

// Get list of available serial devices on macOS
void EnumerateSerialDevices()
{
	// macOS serial devices are typically in /dev/cu.*
	// Common patterns:
	// /dev/cu.usbserial-*     - USB-to-serial adapters
	// /dev/cu.Bluetooth-*     - Bluetooth serial devices
	// /dev/cu.usbmodem*       - USB CDC devices
	// /dev/cu.serial*         - Built-in serial ports (rare on modern Macs)

	printf("macOS Serial Device Patterns:\n");
	printf("  /dev/cu.usbserial-*  - USB-to-serial adapters\n");
	printf("  /dev/cu.Bluetooth-*  - Bluetooth serial devices\n");
	printf("  /dev/cu.usbmodem*    - USB CDC/ACM devices\n");
	printf("Use 'ls /dev/cu.*' to see available devices\n");

	// Also show CM108 HID devices if available
	// Note: CM108 enumeration is implemented in CoreAudioSound.c
	printf("\\nFor CM108 HID PTT devices, use: -p 0d8c:0008\\n");
}