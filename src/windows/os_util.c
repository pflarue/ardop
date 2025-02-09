#define _WIN32_WINNT 0x0600

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <timeapi.h>
#include <stdlib.h>

#include <hidsdi.h>
#include <cfgmgr32.h>

#define TARGET_RESOLUTION 1  // 1-millisecond target resolution

#include "common/os_util.h"
#include "common/log.h"
#include "windows/Waveform.h"

extern char DecodeWav[5][256];
extern int WavNow;  // Time since start of WAV file being decoded
extern bool blnClosing;


BOOL CtrlHandler(DWORD fdwCtrlType) {
	switch( fdwCtrlType ) {
	// Handle the CTRL-C signal.
	case CTRL_C_EVENT:
		printf( "Ctrl-C event\n\n" );
		blnClosing = true;
		Beep( 750, 300 );
		Sleep(1000);
		return( true );

	// CTRL-CLOSE: confirm that the user wants to exit.

	case CTRL_CLOSE_EVENT:

		blnClosing = true;
		printf( "Ctrl-Close event\n\n" );
		Sleep(20000);
		Beep( 750, 300 );
		return( true );

	// Pass other signals to the next handler.
	case CTRL_BREAK_EVENT:
		Beep( 900, 200 );
		printf( "Ctrl-Break event\n\n" );
		blnClosing = true;
		Beep( 750, 300 );
		return false;

	case CTRL_LOGOFF_EVENT:
		Beep( 1000, 200 );
		printf( "Ctrl-Logoff event\n\n" );
		return false;

	case CTRL_SHUTDOWN_EVENT:
		Beep( 750, 500 );
		printf( "Ctrl-Shutdown event\n\n" );
		blnClosing = true;
		Beep( 750, 300 );
	return false;

	default:
		return false;
	}
}


int platform_init() {
	TIMECAPS tc;
	unsigned int wTimerRes;

	SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, true);

	if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) != TIMERR_NOERROR) {
		// Error; application can't continue.
		// TODO: Should this return -1?
	}

	wTimerRes = min(max(tc.wPeriodMin, TARGET_RESOLUTION), tc.wPeriodMax);
	timeBeginPeriod(wTimerRes);
	if(SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS) == 0)
		ZF_LOGW("Failed to set High Priority (%lu)", GetLastError());

	return (0);
}


unsigned int getNow() {
	// When decoding a WAV file, return WavNow, a measure of the offset
	// in ms from the start of the WAV file.
	if (DecodeWav[0][0])
		return WavNow;
	// TODO: timeGetTime() returns the time in ms since windows was started.  As
	// a 32-bit value, it resets 2^32 ms, which is about 50 days.  While this is
	// unlikely to occur, something should be implemented to keep this
	// from causing a problem.  See DecodeCompleteTime and other uses.
	return timeGetTime();
}


// Write UTC date and time of the form YYYYMMDD_hhmmss (15 characters) to out
void get_utctimestr(char *out) {
	SYSTEMTIME st;
	GetSystemTime(&st);
	sprintf(out, "%04d%02d%02d_%02d%02d%02d",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}


const char* PlatformSignalAbbreviation(int signal) {
	(void)signal;
	return "Unused";
}


// Return file descriptor on success.  On failure, log an error
// and return 0;
HANDLE OpenCOMPort(VOID * pPort, int speed) {
	char szPort[256];
	bool fRetVal;
	COMMTIMEOUTS CommTimeOuts;
	HANDLE fd;
	DCB dcb;

	// if Port Name starts COM, convert to \\.\COM or ports above 10 wont work
	if (atoi(pPort) != 0)  // just a com port number
		sprintf(szPort, "\\\\.\\COM%d", atoi(pPort));

	else if (_memicmp(pPort, "COM", 3) == 0) {
		char * pp = (char *)pPort;
		int p = atoi(&pp[3]);
		sprintf( szPort, "\\\\.\\COM%d", p);
	}
	else
		strcpy(szPort, pPort);

	// open COMM device
	fd = CreateFile( szPort, GENERIC_READ | GENERIC_WRITE,
		0,  // exclusive access
		NULL,  // no security attrs
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);
	if (fd == (HANDLE) -1) {
		if (atoi(pPort) != 0)
			ZF_LOGE("COM%d could not be opened", atoi(pPort));
		else
			ZF_LOGE("%s could not be opened", (char *) pPort);
		return 0;
	}

	// setup device buffers
	SetupComm(fd, 4096, 4096);
	// purge any information in the buffer
	PurgeComm(fd, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
	// set up for overlapped I/O
	CommTimeOuts.ReadIntervalTimeout = 0xFFFFFFFF;
	CommTimeOuts.ReadTotalTimeoutMultiplier = 0;
	CommTimeOuts.ReadTotalTimeoutConstant = 0;
	CommTimeOuts.WriteTotalTimeoutMultiplier = 0;
//	CommTimeOuts.WriteTotalTimeoutConstant = 0;
	CommTimeOuts.WriteTotalTimeoutConstant = 500;
	SetCommTimeouts(fd, &CommTimeOuts);
	dcb.DCBlength = sizeof(DCB);
	GetCommState(fd, &dcb);
	dcb.BaudRate = speed;
	dcb.ByteSize = 8;
	dcb.Parity = 0;
	dcb.StopBits = TWOSTOPBITS;
	dcb.StopBits = 0;
	// setup (No) hardware flow control
	dcb.fOutxDsrFlow = 0;
	dcb.fDtrControl = DTR_CONTROL_DISABLE;
	dcb.fOutxCtsFlow = 0;
	dcb.fRtsControl = RTS_CONTROL_DISABLE;
	// setup (No) software flow control
	dcb.fInX = dcb.fOutX = 0;
	dcb.XonChar = 0;
	dcb.XoffChar = 0;
	dcb.XonLim = 100;
	dcb.XoffLim = 100;
	// other various settings
	dcb.fBinary = true;
	dcb.fParity = false;

	fRetVal = SetCommState(fd, &dcb);
	if (!fRetVal) {
		if (atoi(pPort) != 0)
			ZF_LOGE("COM%d Setup Failed %lu ", atoi(pPort), GetLastError());
		else
			ZF_LOGE("%s Setup Failed %lu ", (char *) pPort, GetLastError());

		CloseHandle(fd);
		return 0;
	}

	// This COM port is opened for CAT control or PTT control via RTS or DTR.
	// For all of these cases, Clearing RTS and DTR shouldn't hurt and
	// may help (to avoid accidentally keying PTT);
	COMClearRTS(fd);
	COMClearDTR(fd);

	return fd;
}

// Return the number of bytes read, or -1 if an error occurs.
int ReadCOMBlock(HANDLE fd, unsigned char * Block, int MaxLength ) {
	COMSTAT ComStat;
	DWORD dwErrorFlags;
	DWORD dwLength;

	// only try to read number of bytes in queue
	ClearCommError(fd, &dwErrorFlags, &ComStat);
	dwLength = min((DWORD) MaxLength, ComStat.cbInQue);
	if (dwLength > 0) {
		if (ReadFile(fd, Block, dwLength, &dwLength, NULL))
			return dwLength;
		// failure
		ClearCommError(fd, &dwErrorFlags, &ComStat);
		return (-1);
	}
	return 0;  // Nothing read
}


bool WriteCOMBlock(HANDLE fd, unsigned char * Block, int BytesToWrite) {
	bool fWriteStat;
	DWORD BytesWritten;
	DWORD ErrorFlags;
	COMSTAT ComStat;

	fWriteStat = WriteFile(fd, Block, BytesToWrite, &BytesWritten, NULL);
	if ((!fWriteStat) || (BytesToWrite != (int) BytesWritten)) {
		ClearCommError(fd, &ErrorFlags, &ComStat);
		return false;
	}
	return true;
}


VOID COMSetDTR(HANDLE fd) {
	EscapeCommFunction(fd, SETDTR);
}

VOID COMClearDTR(HANDLE fd) {
	EscapeCommFunction(fd, CLRDTR);
}

VOID COMSetRTS(HANDLE fd) {
	EscapeCommFunction(fd, SETRTS);
}

VOID COMClearRTS(HANDLE fd) {
	EscapeCommFunction(fd, CLRRTS);
}


VOID CloseCOMPort(HANDLE fd) {
	SetCommMask(fd, 0);
	// drop DTR
	COMClearDTR(fd);  // TODO: Why is this done?  Is it appropriate?
	// purge any outstanding reads/writes and close device handle
	PurgeComm(fd, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
	CloseHandle(fd);
}


// Similar to recv() with no flags, but if no data available (EAGAIN or EWOULDBLOCK),
// return 0 rather than -1
int nbrecv(int sockfd, char *data, size_t len) {
	int ret = recv(sockfd, data, len, 0);
	if (ret == -1 && WSAGetLastError() == WSAEWOULDBLOCK)
		return 0;
	return ret;
}

// Provide tcpconnect() here rather than implementing it in ptt.c
// mostly to produce better error messages if something fails.
// Return a file descriptor on success.  If an error occurs, log the error
// and return -1;
int tcpconnect(char *address, int port) {
	static bool wsa_started = false;
	WSADATA wsaData;
	struct sockaddr_in addr;
	int fd = 0;

	if (!wsa_started) {
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
			// TODO: Show more specific error information?
			ZF_LOGE("WSAStartup failed while trying to connect to a TCP port at %s:%i",
				address, port);
			return (-1);
		}
	}
	wsa_started = true;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_pton(AF_INET, address, &addr.sin_addr) != 1) {
		ZF_LOGE("Error parsing %s:%i for TCP port.", address, port);
		return (-1);
	}
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		int err = WSAGetLastError();
		char *errstr = NULL;
		FormatMessageA(
			FORMAT_MESSAGE_ALLOCATE_BUFFER
			| FORMAT_MESSAGE_FROM_SYSTEM
			| FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPSTR)&errstr, 0, NULL);
		ZF_LOGE("Error creating socket for TCP port for %s:%i. %s",
			address, port, errstr);
		LocalFree(errstr);
		return (-1);
	}
	if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		int err = WSAGetLastError();
		char *errstr = NULL;
		FormatMessageA(
			FORMAT_MESSAGE_ALLOCATE_BUFFER
			| FORMAT_MESSAGE_FROM_SYSTEM
			| FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPSTR)&errstr, 0, NULL);
		ZF_LOGE("Error connecting to TCP port at %s:%i. %s",
			address, port, errstr);
		LocalFree(errstr);
		closesocket(fd);
		return (-1);
	}
	// set socket to be non-blocking
	unsigned long mode = 1;
	ioctlsocket(fd, FIONBIO, &mode);
	return fd;
}

// Return 0 on success.  If an error occurs, log the error
// and return -1;
int tcpsend(int fd, unsigned char *data, size_t datalen) {
	if (send(fd, (char *) data, datalen, 0) != (int) datalen) {
		int err = WSAGetLastError();
		char *errstr = NULL;
		FormatMessageA(
			FORMAT_MESSAGE_ALLOCATE_BUFFER
			| FORMAT_MESSAGE_FROM_SYSTEM
			| FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPSTR)&errstr, 0, NULL);
		ZF_LOGE("Error sending data to TCP port. %s", errstr);
		LocalFree(errstr);
		return (-1);
	}
	return 0;
}

void tcpclose(int fd) {
	closesocket(fd);
}

// Known CM108 and compatible devices have VID=0x0D8C and one of the following
// PID values:
int CM108VID = 0x0D8C;  // CM108 vendor ID.
#define CM108PIDSLEN 13
int CM108PIDS[CM108PIDSLEN] = {  // CM108 (and compatible devices) product ids.
	0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F, 0x0012,
	0x0013, 0x0139, 0x013A, 0x013C
};
// https://github.com/skuep/AIOC/blob/master/aioc-fw/Src/usb_descriptors.h
int AIOCVID = 0x1209;
int AIOCPID = 0x7388;

int devices_logged = 0;

// Return true if OutputReportByteLength=5, as expected for a CM108
// compatible device, else return false.
bool verify_compat(HANDLE handle) {
	PHIDP_PREPARSED_DATA PreparsedData = NULL;
	HIDP_CAPS Capabilities;
	if (!HidD_GetPreparsedData(handle, &PreparsedData))
		return false;
	if(HidP_GetCaps(PreparsedData, &Capabilities) != HIDP_STATUS_SUCCESS)
		return false;
	if (Capabilities.OutputReportByteLength != 5)
		return false;
	return true;
}

// if vid=0, return the handle if the device can be opened and
//    verify_compat() is true.  otherwise close it and return 0;
// If vid=-1, log info if vid:pid match a known CM-108 compatible device
// If vid=-2, log info if able to open and get VID:PID
// For both of these cases, always close the handle and return 0.
// For any other vid, pid, return the handle if VID:PID matches vid:pid and
//   verify_compat() is true, else close the handle and return 0;
HANDLE open_hid_by_devid(char *deviceid, int vid, int pid) {
	HANDLE handle = CreateFile(deviceid,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
		return 0;
	if (vid == 0) {
		if (verify_compat(handle))
			return handle;
		ZF_LOGE("Opened %s, but verify compatibility failed.", deviceid);
		CloseHandle(handle);
		return 0;
	}

	HIDD_ATTRIBUTES Attributes;
	if (!HidD_GetAttributes(handle, &Attributes)) {
		// Can't get VID:PID for
		CloseHandle(handle);
		return 0;
	}
	if (Attributes.VendorID == vid && Attributes.ProductID == pid) {
		if (verify_compat(handle))
			return handle;
		ZF_LOGE("Opened VID:PID=%04X:%04X, but verify compatibility failed.",
			vid, pid);
		CloseHandle(handle);
		return 0;
	}
	if (vid > 0) {
		// vid:pid specified, and this doesn't match
		CloseHandle(handle);
		return 0;
	}
	if (vid == -1) {
		// Return 0 without writing to the log if VID:PID don't match a known
		// CM108 compatible HID devices
		if (Attributes.VendorID == CM108VID) {
			int i;
			for (i = 0; i <= CM108PIDSLEN; ++i) {
				if (Attributes.ProductID == CM108PIDS[i])
					break;
			}
			if (i > CM108PIDSLEN) {
				// not a match
				CloseHandle(handle);
				return 0;
			}
		} else if (Attributes.VendorID != AIOCVID
			|| Attributes.ProductID != AIOCPID)
		{
			// not a match
			CloseHandle(handle);
			return 0;
		}
	}

	// Write info to log
	wchar_t Product[128];
	char ProductStr[256];
	ProductStr[0] = 0x00;  // empty str
	if (HidD_GetProductString(handle, Product, 128))
		wcstombs(ProductStr, Product, sizeof(ProductStr));
	// Manufacturer, Serial Number, etc. also available, but dont seem to
	// be useful in most cases
	ZF_LOGI("VID:PID=\"%04X:%04X\" %s %s",
		Attributes.VendorID, Attributes.ProductID, ProductStr, deviceid);
	CloseHandle(handle);
	devices_logged += 1;
	return 0;
}


// Return file descriptor on success.  On failure, log an error
// and return 0;
HANDLE OpenCM108(char *devstr) {
	// If devstr is ? or ?? write VID:PID, ProductString, and full Windows
	// device id for known CM108 HID devices or all HID devices that can be
	// opened and whose VID:PID can be determined respectively.  Then return 0.
	// Otherwise,
	// devstr is normally VID:PID, where VID and PID are both hex strings.
	// However, devstr may also be a full windows HID device id
	// beginning with \\?\HID.  Uss of the full HID device id (which can be
	// found using --ptt CM108:?) may be useful if two devices with the same
	// VID:PID are available.
	// For either of these cases, open the device, verify that
	// OuputReportByteLength is 5, as expected for a CM108 compatible HID
	// device, and return the HANDLE.
	if (strncmp(devstr, "\\\\?\\HID", strlen("\\\\?\\HID")) == 0)
		// Use vid=0 to return handle if valid, else return 0
		return open_hid_by_devid(devstr, 0, 0);

	devices_logged = 0;
	int vid;
	int pid;
	if (strcmp(devstr, "?") == 0)
		vid = -1;  // log all known CM108 compatible HID devices
	else if (strcmp(devstr, "??") == 0)
		vid = -2;  // log all available HID devices
	else {
		char * ptr = devstr;
		char * next;
		vid = strtol(ptr, &next, 16);
		if (next == ptr || vid < 1 || *next != ':') {
			ZF_LOGE("Expected a long HID device string starting with"
				" \"\\\\?\\HID\" or VID:PID for CM108 device, where VID and PID"
				" are both strings of hexidecimal digits but found \"%s\". "
				" Unable to parse.  Try --ptt CM108:? to list known CM108"
				" compatible HID devices or --ptt CM108:?? to list all HID"
				" devices",
				devstr);
			return 0;
		}
		ptr = next + 1;
		pid = strtol(ptr, &next, 16);
		if (next == ptr || pid < 1) {
			ZF_LOGE("Expected a long HID device string starting with"
				" \"\\\\?\\HID\" or VID:PID for CM108 device, where VID and PID"
				" are both strings of hexidecimal digits but found \"%s\". "
				" Unable to parse.  Try --ptt CM108:? to list known CM108"
				" compatible HID devices or --ptt CM108:?? to list all HID"
				" devices",
				devstr);
			return 0;
		}
	}

	GUID HidGuid;
	HidD_GetHidGuid(&HidGuid);
	CONFIGRET cr = CR_BUFFER_SMALL;
	char *DeviceInterfaceList = NULL;
	unsigned long DeviceInterfaceListLength = 0;
	HANDLE handle;
	while (cr == CR_BUFFER_SMALL) {
		// Adapted from example at
		// https://learn.microsoft.com/en-us/windows/win32/api/cfgmgr32/
		// nf-cfgmgr32-cm_get_device_interface_lista
		cr = CM_Get_Device_Interface_List_Size(&DeviceInterfaceListLength,
			(LPGUID)&HidGuid, NULL, CM_GET_DEVICE_INTERFACE_LIST_ALL_DEVICES);
		if (cr != CR_SUCCESS)
			break;
		if (DeviceInterfaceList != NULL)
			HeapFree(GetProcessHeap(), 0, DeviceInterfaceList);
		DeviceInterfaceList = (PSTR)HeapAlloc(GetProcessHeap(),
			HEAP_ZERO_MEMORY, DeviceInterfaceListLength * sizeof(WCHAR));
		if (DeviceInterfaceList == NULL) {
			cr = CR_OUT_OF_MEMORY;
			break;
		}
		cr = CM_Get_Device_Interface_List((LPGUID)&HidGuid,
			NULL, DeviceInterfaceList, DeviceInterfaceListLength,
			CM_GET_DEVICE_INTERFACE_LIST_ALL_DEVICES);
	}
	if (cr != CR_SUCCESS) {
		ZF_LOGE("Error in CM_Get_Device_Interface_List_SizeW()");
		return 0;
	}

	while (strlen(DeviceInterfaceList) != 0) {
		handle = open_hid_by_devid(DeviceInterfaceList, vid, pid);
		if (handle != 0)
			return handle;
		DeviceInterfaceList += strlen(DeviceInterfaceList) + 1;
	}
	if (strcmp(devstr, "?") == 0 && devices_logged == 0)
		ZF_LOGE("No known CM108 compatible HID devices found.  Try --ptt"
			" CM108:?? to list all available HID devices, especially if you"
			" are using a new or uncommon HID device that may not be in"
			" the list of known compatible devices.  Such devices may still"
			" be usable.");
	else if (strcmp(devstr, "??") == 0) {
		if (devices_logged == 0)
			ZF_LOGE("No HID devices found.  This is unexpected.");
		else
			ZF_LOGE("All available HID devices were listed, not only those"
			" known to be CM108 compatible.  This list may be useful if you are"
			" using a new or uncommon HID device that may not be in the list"
			" of known compatible devices.  Such devices may still be usable. "
			" To see a list of only known compatible devices, try --ptt CM108:?"
			" rather than --ptt CM108:??");
	}
	return 0;
}

// Return 0 on success.  If an error occurs, log the error
// and return -1;
int CM108_set_ptt(HANDLE fd, bool State) {
	// Iniitalize io[] for State = true
	char io[5] = {0, 0, 1 << (3 - 1), 1 << (3 - 1), 0};
	if (!State)
		io[2] = 0;  // adjust for State = false

	DWORD NumberOfBytesWritten;
	if (!WriteFile(fd, io, 5, &NumberOfBytesWritten, NULL)) {
		ZF_LOGE("ERROR: Failure of PTT via CM108.");
		return (-1);
	}
	if (NumberOfBytesWritten != 5) {
		ZF_LOGE("ERROR: Failure of PTT via CM108.");
		return (-1);
	}
	return 0;
}

void CloseCM108(HANDLE fd) {
	CloseHandle(fd);
}
