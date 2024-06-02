// ARDOP TNC Host Interface
//
// Supports Serial Interface for ARDOP and TCP for ARDOP Packet

#ifdef WIN32
#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>
#include <winioctl.h>
#pragma comment(lib, "WS2_32.Lib")
#define ioctl ioctlsocket
#define MAX_PENDING_CONNECTS 4

#else
#define HANDLE int
#endif

#include "ARDOPC.h"

BOOL WriteCOMBlock(HANDLE fd, char * Block, int BytesToWrite);
int ReadCOMBlock(HANDLE fd, char * Block, int MaxLength);

HANDLE hDevice;

int SerialSendData(UCHAR * Message,int MsgLen)
{
	unsigned long bytesReturned;
	
	// Have to escape all oxff chars, as these are used to get status info 

	UCHAR NewMessage[2000];
	UCHAR * ptr1 = Message;
	UCHAR * ptr2 = NewMessage;
	UCHAR c;

	int Length = MsgLen;

	while (Length != 0)
	{
		c = *(ptr1++);
		*(ptr2++) = c;

		if (c == 0xff)
		{
			*(ptr2++) = c;
			MsgLen++;
		}
		Length--;
	}

	return WriteFile(hDevice, NewMessage, MsgLen, &bytesReturned, NULL);
}


int ReadCOMBlockEx(HANDLE fd, char * Block, int MaxLength, BOOL * Error)
{
	BOOL       fReadStat ;
	COMSTAT    ComStat ;
	DWORD      dwErrorFlags;
	DWORD      dwLength;
	BOOL	ret;

	// only try to read number of bytes in queue

	ret = ClearCommError(fd, &dwErrorFlags, &ComStat);

	if (ret == 0)
	{
		int Err = GetLastError();
		*Error = TRUE;
		return 0;
	}


	dwLength = min((DWORD) MaxLength, ComStat.cbInQue);

	if (dwLength > 0)
	{
		fReadStat = ReadFile(fd, Block, dwLength, &dwLength, NULL) ;

		if (!fReadStat)
		{
		    dwLength = 0 ;
			ClearCommError(fd, &dwErrorFlags, &ComStat ) ;
		}
	}

	*Error = FALSE;

   return dwLength;
}
