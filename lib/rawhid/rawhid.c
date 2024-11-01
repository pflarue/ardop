#ifdef WIN32
#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#else
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#define SOCKET int
#define closesocket close
#define HANDLE int
#endif

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "hid/hidapi.h"

#ifdef WIN32
#include "common/ardopcommon.h"

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




//	rawhid_recv - receive a packet
//		Inputs:
//	num = device to receive from (zero based)
//	buf = buffer to receive packet
//	len = buffer's size
//	timeout = time to wait, in milliseconds
//		Output:
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

//	rawhid_send - send a packet
//		Inputs:
//	num = device to transmit to (zero based)
//	buf = buffer containing packet to send
//	len = number of bytes to transmit
//	timeout = time to wait, in milliseconds
//		Output:
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


//	rawhid_close - close a device
//
//		Inputs:
//	num = device to close (zero based)
//		 Output
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
	ZF_LOGE("err %ld: %s\n", err, buf);
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
			printf("HID Read %d\n", Len);
			return Len;
		}
	}
	return 0;
}

void rawhid_close(int num);

int HID_Write_Block()
{
	int n = HIDTXLen;
	unsigned char * ptr = HIDTXBuffer;
	unsigned char Msg[64] = "";
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
			ZF_LOGE("Rigcontrol HID Write Failed %d", errno);
			rawhid_close(0);
			CM108Handle = NULL;
			return 0;
		}
#else
		ret = write(CM108Handle, Msg, 64);

		if (ret != 64)
		{
			printf ("Write to %s failed, n=%d, errno=%d\n", HIDDevice, ret, errno);
			close (CM108Handle);
			CM108Handle = 0;
			return 0;
		}

//		printf("HID Write %d\n", i);
#endif
	}
	return 1;
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
		ZF_LOGE("Unable to write(): %ls", hid_error(handle));
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
				ZF_LOGI("HID Device %s VID %04x PID %04x %s", product, cur_dev->vendor_id, cur_dev->product_id, cur_dev->path);
			else
				ZF_LOGI("HID Device %s VID %04x PID %04x %s", "Missing Product", cur_dev->vendor_id, cur_dev->product_id, cur_dev->path);

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
				ZF_LOGI("Using CM108 device %04x:%04x for PTT", VID, PID);
			else
				ZF_LOGI("Using CM108 device %s for PTT", CM108Device);
		}
		else
		{
			if (VID || PID)
				ZF_LOGE("Unable to open CM108 device %x %x Error %d", VID, PID, GetLastError());
			else
				ZF_LOGE("Unable to open CM108 device %s Error %d", CM108Device, GetLastError());
		}
		free(path_to_open);
	}
#else

	// Linux - Param is HID Device, eg /dev/hidraw0

	CM108Device = strdup(ptr);
#endif
}
