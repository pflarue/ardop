//
//	Code Common to all versions of ARDOP. 
//

const char ProductVersion[] = "2.0.3.2-pflarue-3";

//	2.0.3.1 November 2023

//	Determine end of TX time from frame length


//	2.0.3.2 November 2023

//	Add Craig KM6LYW's snd_pcm_hw_params_set_period_size_near patch
//	Set default TrailerLength to 20 (ms)
//	Add --trailerlength command line parameter

// 2.0.3.2-pflarue-3  December 2023

// Modify SoundFlush() and OpenSoundCapture() to reduce delay decoding
// received audio after transmitting.


#ifdef WIN32
#define _CRT_SECURE_NO_DEPRECATE
#define _USE_32BIT_TIME_T

#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#else
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#ifndef TEENSY
#include <sys/socket.h>
#endif
#define SOCKET int
#define closesocket close
#define HANDLE int
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "ardopcommon.h"
#include "getopt.h"

extern int gotGPIO;
extern int useGPIO;

extern int pttGPIOPin;
extern int pttGPIOInvert;

extern HANDLE hCATDevice;		// port for Rig Control
extern char CATPort[80];
extern int CATBAUD;
extern int EnableHostCATRX;

extern HANDLE hPTTDevice;			// port for PTT
extern char PTTPort[80];			// Port for Hardware PTT - may be same as control port.
extern int PTTBAUD;

extern unsigned char PTTOnCmd[];
extern unsigned char PTTOnCmdLen;

extern unsigned char PTTOffCmd[];
extern unsigned char PTTOffCmdLen;

extern int PTTMode;				// PTT Control Flags.
extern char HostPort[80];
extern char CaptureDevice[80];
extern char PlaybackDevice[80];

int extraDelay = 0;				// Used for long delay paths eg Satellite
int	intARQDefaultDlyMs = 240;
int TrailerLength = 20;
BOOL InitRXO = FALSE;
BOOL WriteRxWav = FALSE;
BOOL TwoToneAndExit = FALSE;

int PTTMode = PTTRTS;				// PTT Control Flags.

#ifndef TEENSY
struct sockaddr HamlibAddr;		// Dest for above
#endif
int useHamLib = 0;


extern int LeaderLength;

unsigned const short CRCTAB[256] = {
0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf, 
0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7, 
0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e, 
0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876, 
0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd, 
0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5, 
0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c, 
0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974, 
0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb, 
0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3, 
0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a, 
0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72, 
0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9, 
0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1, 
0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738, 
0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70, 
0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7, 
0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff, 
0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036, 
0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e, 
0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5, 
0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd, 
0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134, 
0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c, 
0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3, 
0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb, 
0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232, 
0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a, 
0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1, 
0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9, 
0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330, 
0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78 
}; 


unsigned short int compute_crc(unsigned char *buf,int len)
{
	unsigned short fcs = 0xffff; 
	int i;

	for(i = 0; i < len; i++) 
		fcs = (fcs >>8 ) ^ CRCTAB[(fcs ^ buf[i]) & 0xff]; 

	return fcs;
}

#ifndef TEENSY

extern BOOL UseLeftRX;
extern BOOL UseRightRX;

extern BOOL UseLeftTX;
extern BOOL UseRightTX;

extern char LogDir[256];

static struct option long_options[] =
{
	{"logdir",  required_argument, 0 , 'l'},
	{"ptt",  required_argument, 0 , 'p'},
	{"cat",  required_argument, 0 , 'c'},
	{"keystring",  required_argument, 0 , 'k'},
	{"unkeystring",  required_argument, 0 , 'u'},
	{"extradelay",  required_argument, 0 , 'e'},
	{"leaderlength",  required_argument, 0 , 'x'},
	{"trailerlength",  required_argument, 0 , 't'},
	{"receive", no_argument, 0, 'r'},
	{"writewav",  no_argument, 0, 'w'},
	{"twotone", no_argument, 0, 'n'},
	{"help",  no_argument, 0 , 'h'},
	{ NULL , no_argument , NULL , no_argument }
};

char HelpScreen[] =
	"Usage:\n"
	"%s port [capture device playbackdevice] [Options]\n"
	"defaults are port = 8515, capture device ARDOP playback device ARDOP\n"
	"If you need to specify capture and playback devices you must specify port\n"
	"\n"
	"For TCP Host connection, port is TCP Port Number\n"
	"For Serial Host Connection port must start with \"COM\" or \"com\"\n"
	"  On Windows use the name of the BPQ Virtual COM Port, eg COM4\n"
	"  On Linux the program will create a pty and symlink it to the specified name.\n"
	"\n"
	"Optional Paramters\n"
	"-l path or --logdir path          Path for log files\n"
	"-c device or --cat device         Device to use for CAT Control\n"
	"-p device or --ptt device         Device to use for PTT control using RTS\n"
#ifdef LINBPQ
	"                                  or CM108-like Device to use for PTT\n"
#else
	"                                  or VID:PID of CM108-like Device to use for PTT\n"
#endif
	"-g [Pin]                          GPIO pin to use for PTT (ARM Only)\n"
	"                                  Default 17. use -Pin to invert PTT state\n"
	"-k string or --keystring string   String (In HEX) to send to the radio to key PTT\n"
	"-u string or --unkeystring string String (In HEX) to send to the radio to unkeykey PTT\n"
	"-L use Left Channel of Soundcard for receive in stereo mode\n"
	"-R use Right Channel of Soundcard for receive in stereo mode\n"
	"-e val or --extradelay val        Extend no response timeot for use on paths with long delay\n"
	"--leaderlength val                Sets Leader Length (mS)\n"
	"--trailerlength val               Sets Trailer Length (mS)\n"
	"-r or --receiveonly               Start in RXO (receive only) mode.\n"
	"-w or --writewav                  Write WAV files of received audio for debugging.\n"
	"-n or --twotone                   Send a 5 second two tone signal and exit.\n"
	"\n"
	" CAT and RTS PTT can share the same port.\n"
	" See the ardop documentation for more information on cat and ptt options\n"
	"  including when you need to use -k and -u\n\n";

void processargs(int argc, char * argv[])
{
	int val;
	UCHAR * ptr1;
	UCHAR * ptr2;
	int c;

	while (1)
	{		
		int option_index = 0;

		c = getopt_long(argc, argv, "l:c:p:g::k:u:e:hLRytrzwn", long_options, &option_index);

		// Check for end of operation or error
		if (c == -1)
			break;

		// Handle options
		switch (c)
		{
		case 'h':
	
			printf("%s Version %s\n", ProductName, ProductVersion);
			printf(HelpScreen, ProductName);
			exit (0);

		case 'l':
			strcpy(LogDir, optarg);
			break;

			
		case 'g':
			if (optarg)
				pttGPIOPin = atoi(optarg);
			else
				pttGPIOPin = 17;
			break;

		case 'k':

			ptr1 = optarg;
			ptr2 = PTTOnCmd;
		
			if (ptr1 == NULL)
			{
				printf("RADIOPTTON command string missing\r");
				break;
			}

			while (c = *(ptr1++))
			{
				val = c - 0x30;
				if (val > 15) val -= 7;
				val <<= 4;
				c = *(ptr1++) - 0x30;
				if (c > 15) c -= 7;
				val |= c;
				*(ptr2++) = val;
			}

			PTTOnCmdLen = ptr2 - PTTOnCmd;
			PTTMode = PTTCI_V;

			printf ("PTTOnString %s len %d\n", optarg, PTTOnCmdLen);
			break;

		case 'u':

			ptr1 = optarg;
			ptr2 = PTTOffCmd;

			if (ptr1 == NULL)
			{
				printf("RADIOPTTOFF command string missing\r");
				break;
			}

			while (c = *(ptr1++))
			{
				val = c - 0x30;
				if (val > 15) val -= 7;
				val <<= 4;
				c = *(ptr1++) - 0x30;
				if (c > 15) c -= 7;
				val |= c;
				*(ptr2++) = val;
			}

			PTTOffCmdLen = ptr2 - PTTOffCmd;
			PTTMode = PTTCI_V;

			printf ("PTTOffString %s len %d\n", optarg, PTTOffCmdLen);
			break;

		case 'p':
			strcpy(PTTPort, optarg);
			break;

		case 'c':
			strcpy(CATPort, optarg);
			break;

		case 'L':
//			UseLeftTX = UseLeftRX = 1;
//			UseRightTX = UseRightRX = 0;
			UseLeftRX = 1;
			UseRightRX = 0;
			break;

		case 'R':
//			UseLeftTX = UseLeftRX = 0;
//			UseRightTX = UseRightRX = 1;
			UseLeftRX = 0;
			UseRightRX = 1;
			break;

		case 'y':
			UseLeftTX = 1;
			UseRightTX = 0;
			break;

		case 'z':
			UseLeftTX = 0;
			UseRightTX = 1;
			break;

		case 'e':
			extraDelay = atoi(optarg);
			break;

		case 'x':
			intARQDefaultDlyMs = LeaderLength = atoi(optarg);
			break;

		case 't':
			TrailerLength = atoi(optarg);
			break;

		case 'r':
			InitRXO = TRUE;
			break;

		case 'w':
			WriteRxWav = TRUE;
			break;

		case 'n':
			TwoToneAndExit = TRUE;
			break;

		case '?':
			/* getopt_long already printed an error message. */
			break;

		default:
			abort();
		}
	}


	if (argc > optind)
	{
		strcpy(HostPort, argv[optind]);
	}

	if (argc > optind + 2)
	{
		strcpy(CaptureDevice, argv[optind + 1]);
		strcpy(PlaybackDevice, argv[optind + 2]);
	}

	if (argc > optind + 3)
	{
		printf("%s Version %s\n", ProductName, ProductVersion);
		printf("Only three positional parameters allowed\n");
		printf ("%s", HelpScreen);
		exit(0);
	}
}

extern enum _ARDOPState ProtocolState;

extern int blnARQDisconnect;

void ClosePacketSessions();

void LostHost()
{
	// Called if Link to host is lost

	// Close any sessions

	if (ProtocolState == IDLE || ProtocolState == IRS || ProtocolState == ISS || ProtocolState == IRStoISS)
		blnARQDisconnect = 1;

	ClosePacketSessions();
}


#include "hidapi.h"

#ifdef WIN32

/* Simple Raw HID functions for Windows - for use with Teensy RawHID example
 * http://www.pjrc.com/teensy/rawhid.html
 * Copyright (c) 2009 PJRC.COM, LLC
 *
 *  rawhid_open - open 1 or more devices
 *  rawhid_recv - receive a packet
 *  rawhid_send - send a packet
 *  rawhid_close - close a device
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above description, website URL and copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Version 1.0: Initial Release
 */

#include <stdio.h>
#include <stdlib.h>
//#include <stdint.h>
//#include <windows.h>
#include <setupapi.h>
//#include <ddk/hidsdi.h>
//#include <ddk/hidclass.h>

typedef USHORT USAGE;


typedef struct _HIDD_CONFIGURATION {
	PVOID cookie;
 	ULONG size;
	ULONG RingBufferSize;
} HIDD_CONFIGURATION, *PHIDD_CONFIGURATION;

typedef struct _HIDD_ATTRIBUTES {
	ULONG Size;
	USHORT VendorID;
	USHORT ProductID;
	USHORT VersionNumber;
} HIDD_ATTRIBUTES, *PHIDD_ATTRIBUTES;


typedef struct _HIDP_CAPS {
  USAGE  Usage;
  USAGE  UsagePage;
  USHORT  InputReportByteLength;
  USHORT  OutputReportByteLength;
  USHORT  FeatureReportByteLength;
  USHORT  Reserved[17];
  USHORT  NumberLinkCollectionNodes;
  USHORT  NumberInputButtonCaps;
  USHORT  NumberInputValueCaps;
  USHORT  NumberInputDataIndices;
  USHORT  NumberOutputButtonCaps;
  USHORT  NumberOutputValueCaps;
  USHORT  NumberOutputDataIndices;
  USHORT  NumberFeatureButtonCaps;
  USHORT  NumberFeatureValueCaps;
  USHORT  NumberFeatureDataIndices;
} HIDP_CAPS, *PHIDP_CAPS;


typedef struct _HIDP_PREPARSED_DATA * PHIDP_PREPARSED_DATA;



// a list of all opened HID devices, so the caller can
// simply refer to them by number
typedef struct hid_struct hid_t;
static hid_t *first_hid = NULL;
static hid_t *last_hid = NULL;
struct hid_struct {
	HANDLE handle;
	int open;
	struct hid_struct *prev;
	struct hid_struct *next;
};
static HANDLE rx_event=NULL;
static HANDLE tx_event=NULL;
static CRITICAL_SECTION rx_mutex;
static CRITICAL_SECTION tx_mutex;


// private functions, not intended to be used from outside this file
static void add_hid(hid_t *h);
static hid_t * get_hid(int num);
static void free_all_hid(void);
void print_win32_err(void);




//  rawhid_recv - receive a packet
//    Inputs:
//	num = device to receive from (zero based)
//	buf = buffer to receive packet
//	len = buffer's size
//	timeout = time to wait, in milliseconds
//    Output:
//	number of bytes received, or -1 on error
//
int rawhid_recv(int num, void *buf, int len, int timeout)
{
	hid_t *hid;
	unsigned char tmpbuf[516];
	OVERLAPPED ov;
	DWORD r;
	long unsigned int n;

	if (sizeof(tmpbuf) < len + 1) return -1;
	hid = get_hid(num);
	if (!hid || !hid->open) return -1;
	EnterCriticalSection(&rx_mutex);
	ResetEvent(&rx_event);
	memset(&ov, 0, sizeof(ov));
	ov.hEvent = rx_event;
	if (!ReadFile(hid->handle, tmpbuf, len + 1, NULL, &ov)) {
		if (GetLastError() != ERROR_IO_PENDING) goto return_error;
		r = WaitForSingleObject(rx_event, timeout);
		if (r == WAIT_TIMEOUT) goto return_timeout;
		if (r != WAIT_OBJECT_0) goto return_error;
	}
	if (!GetOverlappedResult(hid->handle, &ov, &n, FALSE)) goto return_error;
	LeaveCriticalSection(&rx_mutex);
	if (n <= 0) return -1;
	n--;
	if (n > len) n = len;
	memcpy(buf, tmpbuf + 1, n);
	return n;
return_timeout:
	CancelIo(hid->handle);
	LeaveCriticalSection(&rx_mutex);
	return 0;
return_error:
	print_win32_err();
	LeaveCriticalSection(&rx_mutex);
	return -1;
}

//  rawhid_send - send a packet
//    Inputs:
//	num = device to transmit to (zero based)
//	buf = buffer containing packet to send
//	len = number of bytes to transmit
//	timeout = time to wait, in milliseconds
//    Output:
//	number of bytes sent, or -1 on error
//
int rawhid_send(int num, void *buf, int len, int timeout)
{
	hid_t *hid;
	unsigned char tmpbuf[516];
	OVERLAPPED ov;
	DWORD n, r;

	if (sizeof(tmpbuf) < len + 1) return -1;
	hid = get_hid(num);
	if (!hid || !hid->open) return -1;
	EnterCriticalSection(&tx_mutex);
	ResetEvent(&tx_event);
	memset(&ov, 0, sizeof(ov));
	ov.hEvent = tx_event;
	tmpbuf[0] = 0;
	memcpy(tmpbuf + 1, buf, len);
	if (!WriteFile(hid->handle, tmpbuf, len + 1, NULL, &ov)) {
		if (GetLastError() != ERROR_IO_PENDING) goto return_error;
		r = WaitForSingleObject(tx_event, timeout);
		if (r == WAIT_TIMEOUT) goto return_timeout;
		if (r != WAIT_OBJECT_0) goto return_error;
	}
	if (!GetOverlappedResult(hid->handle, &ov, &n, FALSE)) goto return_error;
	LeaveCriticalSection(&tx_mutex);
	if (n <= 0) return -1;
	return n - 1;
return_timeout:
	CancelIo(hid->handle);
	LeaveCriticalSection(&tx_mutex);
	return 0;
return_error:
	print_win32_err();
	LeaveCriticalSection(&tx_mutex);
	return -1;
}

HANDLE rawhid_open(char * Device)
{
	DWORD index=0;
	HANDLE h;
	hid_t *hid;
	int count=0;

	if (first_hid) free_all_hid();

	if (!rx_event)
	{
		rx_event = CreateEvent(NULL, TRUE, TRUE, NULL);
		tx_event = CreateEvent(NULL, TRUE, TRUE, NULL);
		InitializeCriticalSection(&rx_mutex);
		InitializeCriticalSection(&tx_mutex);
	}
	h = CreateFile(Device, GENERIC_READ|GENERIC_WRITE,
		FILE_SHARE_READ|FILE_SHARE_WRITE, NULL,
		OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

	if (h == INVALID_HANDLE_VALUE) 
		return 0;

	hid = (struct hid_struct *)malloc(sizeof(struct hid_struct));
	if (!hid)
	{
		CloseHandle(h);
		return 0;
	}
	hid->handle = h;
	hid->open = 1;
	add_hid(hid);

	return h;
}


//  rawhid_close - close a device
//
//    Inputs:
//	num = device to close (zero based)
//    Output
//	(nothing)
//
void rawhid_close(int num)
{
	hid_t *hid;

	hid = get_hid(num);
	if (!hid || !hid->open) return;

	CloseHandle(hid->handle);
	hid->handle = NULL;
	hid->open = FALSE;
}




static void add_hid(hid_t *h)
{
	if (!first_hid || !last_hid) {
		first_hid = last_hid = h;
		h->next = h->prev = NULL;
		return;
	}
	last_hid->next = h;
	h->prev = last_hid;
	h->next = NULL;
	last_hid = h;
}


static hid_t * get_hid(int num)
{
	hid_t *p;
	for (p = first_hid; p && num > 0; p = p->next, num--) ;
	return p;
}


static void free_all_hid(void)
{
	hid_t *p, *q;

	for (p = first_hid; p; p = p->next)
	{
		CloseHandle(p->handle);
		p->handle = NULL;
		p->open = FALSE;
	}
	p = first_hid;
	while (p) {
		q = p;
		p = p->next;
		free(q);
	}
	first_hid = last_hid = NULL;
}



void print_win32_err(void)
{
	char buf[256];
	DWORD err;

	err = GetLastError();
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err,
		0, buf, sizeof(buf), NULL);
	Debugprintf("err %ld: %s\n", err, buf);
}

#endif



char * HIDDevice = 0;
hid_device * CM108Handle = 0;
unsigned char HIDRXBuffer[100];
int HIDRXLen = 0;
unsigned char HIDTXBuffer[100];
int HIDTXLen = 0;
char * CM108Device = NULL;

int HID_Read_Block()
{
	int Len;
	unsigned char Msg[65] = "";

	if (HIDRXLen > 400)
		HIDRXLen = 0;

	// Don't try to read more than 64

#ifdef WIN32
	Len = rawhid_recv(0, Msg, 64, 100);
#else
	Len = read(CM108Handle, Msg, 64);
#endif

	if (Len <= 0)
		return 0;

	// First byte is actual length

	Len = Msg[0];

	if (Len > 0)
	{
		if (Len < 64)		// Max in HID Packet
		{
			memcpy(&HIDRXBuffer[HIDRXLen], Msg + 1, Len);
			Debugprintf("HID Read %d\n", Len);
			return Len;
		}
	}
	return 0;
}

void rawhid_close(int num);

BOOL HID_Write_Block()
{
	int n = HIDTXLen;
	UCHAR * ptr = HIDTXBuffer;
	UCHAR Msg[64] = "";
	int ret, i;

	while (n)
	{
		i = n;
		if (i > 63)
			i = 63;

		Msg[0] = i;		// Length on front
		memcpy(&Msg[1], ptr, i);
		ptr += i;
		n -= i;
		//	n = hid_write(CM108Handle, PORT->TXBuffer, PORT->TXLen);
#ifdef WIN32
		ret = rawhid_send(0, Msg, 64, 100);		// Always send 64

		if (ret < 0)
		{
			Debugprintf("Rigcontrol HID Write Failed %d", errno);
			rawhid_close(0);
			CM108Handle = NULL;
			return FALSE;
		}
#else
		ret = write(CM108Handle, Msg, 64);

		if (ret != 64)
		{
			printf ("Write to %s failed, n=%d, errno=%d\n", HIDDevice, ret, errno);
			close (CM108Handle);
			CM108Handle = 0;
			return FALSE;
		}

//		printf("HID Write %d\n", i);
#endif
	}
	return TRUE;
}

/*
BOOL OpenHIDPort()
{
#ifdef WIN32

	if (HIDDevice == NULL)
		return FALSE;

	CM108Handle = rawhid_open(HIDDevice);

	if (CM108Handle)
		Debugprintf("Rigcontrol HID Device %s opened", HIDDevice);

//	handle = hid_open_path(HIDDevice);

//	if (handle)
//	hid_set_nonblocking(handle, 1);

//	CM108Handle = handle;
	=
#else
	int fd;
	unsigned int param = 1;

	if (HIDDevice== NULL)
		return FALSE;

	fd = open (HIDDevice, O_RDWR);
		
	if (fd == -1)
	{
          printf ("Could not open %s, errno=%d\n", HIDDevice, errno);
          return FALSE;
	}

	ioctl(fd, FIONBIO, &param);
	printf("Rigcontrol HID Device %s opened", HIDDevice);
	
	CM108Handle = fd;
#endif
	if (CM108Handle == 0)
		return (FALSE);

	return TRUE;
}
*/

void CM108_set_ptt(int PTTState)
{
	char io[5];
	hid_device *handle;
	int n;

	io[0] = 0;
	io[1] = 0;
	io[2] = 1 << (3 - 1);
	io[3] = PTTState << (3 - 1);
	io[4] = 0;

	if (CM108Device == NULL)
		return;

#ifdef WIN32
	handle = hid_open_path(CM108Device);

	if (!handle) {
		printf("unable to open device\n");
 		return;
	}

	n = hid_write(handle, io, 5);
	if (n < 0)
	{
		Debugprintf("Unable to write()\n");
		Debugprintf("Error: %ls\n", hid_error(handle));
	}
	
	hid_close(handle);

#else

	int fd;

	fd = open (CM108Device, O_WRONLY);
	
	if (fd == -1)
	{
          printf ("Could not open %s for write, errno=%d\n", CM108Device, errno);
          return;
	}
	
	io[0] = 0;
	io[1] = 0;
	io[2] = 1 << (3 - 1);
	io[3] = PTTState << (3 - 1);
	io[4] = 0;

	n = write (fd, io, 5);
	if (n != 5)
		printf ("Write to %s failed, n=%d, errno=%d\n", CM108Device, n, errno);
	
	close (fd);
#endif
	return;
}


#endif

#ifndef TEENSY

void DecodeCM108(char * ptr)
{
	// Called if Device Name or PTT = Param is CM108

#ifdef WIN32

	// Next Param is VID and PID - 0xd8c:0x8 or Full device name
	// On Windows device name is very long and difficult to find, so 
	//	easier to use VID/PID, but allow device in case more than one needed

	char * next;
	long VID = 0, PID = 0;
	char product[256];

	struct hid_device_info *devs, *cur_dev;
	char *path_to_open = NULL;
	hid_device *handle = NULL;

	if (strlen(ptr) > 16)
		path_to_open = _strdup(ptr);
	else
	{
		VID = strtol(ptr, &next, 0);
		if (next)
			PID = strtol(++next, &next, 0);

		// Look for Device
	
		devs = hid_enumerate(0,0); // so we list devices(USHORT)VID, (USHORT)PID);
		cur_dev = devs;
		while (cur_dev)
		{
			wcstombs(product, cur_dev->product_string, 255);

			if (product)
				Debugprintf("HID Device %s VID %04x PID %04x %s", product, cur_dev->vendor_id, cur_dev->product_id, cur_dev->path);
			else
				Debugprintf("HID Device %s VID %04x PID %04x %s", "Missing Product", cur_dev->vendor_id, cur_dev->product_id, cur_dev->path);
				
			if (cur_dev->vendor_id == VID && cur_dev->product_id == PID)
				path_to_open = _strdup(cur_dev->path);

			cur_dev = cur_dev->next;
		}
		hid_free_enumeration(devs);
	}
	
	if (path_to_open)
	{
		handle = hid_open_path(path_to_open);

		if (handle)
		{
			hid_close(handle);
			CM108Device = _strdup(path_to_open);
			PTTMode = PTTCM108;
			RadioControl = TRUE;
			if (VID || PID)
				Debugprintf ("Using CM108 device %04x:%04x for PTT", VID, PID);
			else
				Debugprintf ("Using CM108 device %s for PTT", CM108Device);
		}
		else
		{
			if (VID || PID)
				Debugprintf("Unable to open CM108 device %x %x Error %d", VID, PID, GetLastError());
			else
				Debugprintf("Unable to open CM108 device %s Error %d", CM108Device, GetLastError());
		}
		free(path_to_open);
	}
#else
		
	// Linux - Param is HID Device, eg /dev/hidraw0

	CM108Device = strdup(ptr);
#endif
}

#endif
