// Defining HANDLE as int for non-Windows systems allows serial port
// related functions to match the signature of equivalent functions
// for Windows.
#ifdef WIN32
#include <windows.h>
#else
#define HANDLE int
#endif

#include <stdbool.h>
#include <stddef.h>

// Each version of os_util.c must implement the following unless it is
// already implemented by an os-specific library used.
// If a PTT method is not supported, then the corresponding Open*()
// command should log an error message indicating such, and return an
// appropriate value to indicate an error.

#ifndef WIN32
// Sleep is a std library function for Windows, but must be defined for Linux.
void Sleep(long unsigned int mS);
#endif

// Write UTC date and time of the form YYYYMMDD_hhmmss (15 characters) to out
void get_utctimestr(char *out);
int platform_init();
unsigned int getNow();
#define Now getNow()

const char* PlatformSignalAbbreviation(int signal);


// Return file descriptor on success.  On failure, log an error
// and return 0;
HANDLE OpenCOMPort(void * Port, int speed);

void CloseCOMPort(HANDLE *fd);
bool COMSetRTS(HANDLE fd);
bool COMClearRTS(HANDLE fd);
bool COMSetDTR(HANDLE fd);
bool COMClearDTR(HANDLE fd);
bool WriteCOMBlock(HANDLE fd, unsigned char * Block, int BytesToWrite);

// Return the number of bytes read, or -1 if an error occurs.
// If no data is available (EAGAIN or EWOULDBLOCK), return 0, not -1.
int ReadCOMBlock(HANDLE fd, unsigned char * Block, int MaxLength);

// Similar to recv() with no flags, but if no data available (EAGAIN or EWOULDBLOCK),
// return 0 rather than -1
int nbrecv(int sockfd, char *data, size_t len);

// Provide tcpconnect() in os_util.c rather than implementing it in ptt.c
// mostly to produce better error messages if something fails.
// Return a file descriptor on success.  If an error occurs, log the error
// and return -1;
// However, if testing is true, then do not log an error if unable to open.
int tcpconnect(char *address, int port, bool testing);
// Return 0 on success.  If an error occurs, log the error
// and return -1;
int tcpsend(int fd, unsigned char *data, size_t datalen);
void tcpclose(int *fd);

// Return file descriptor on success.  On failure, log an error
// and return 0;
HANDLE OpenCM108(char *devstr);
// Return 0 on success.  If an error occurs, log the error
// and return -1;
int CM108_set_ptt(HANDLE fd, bool State);
void CloseCM108(HANDLE *fd);

char** GetSerialStrlist();
char** GetCM108Strlist();

// Items below are platform specific
#ifdef WIN32
// Windows specific stuff
#else
// Linux specific stuff
#ifdef __ARM_ARCH
// ARM Linux specific stuff
// The following GPIO functions appear to be excerpted/adapted from
// https://github.com/joan2937/pigpio which is released to the the public domain
// (or equivalent) via unlicense (http://unlicense.org)
extern int pttGPIOPin;
extern bool pttGPIOInvert;

unsigned gpioHardwareRevision(void);
int gpioInitialise();
void gpioSetMode(unsigned gpio, unsigned mode);
void gpioWrite(unsigned gpio, unsigned level);
void SetupGPIOPTT(int pin, bool invert);

#endif  // end of ARM Linux
#endif  // end of Linux

