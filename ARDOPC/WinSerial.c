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

VOID ProcessSCSPacket(UCHAR * rxbuffer, int Length);
BOOL WriteCOMBlock(HANDLE fd, char * Block, int BytesToWrite);
int ReadCOMBlock(HANDLE fd, char * Block, int MaxLength);
VOID ProcessKISSBytes(UCHAR * RXBuffer, int Read);
void KISSTCPPoll();
SOCKET OpenSocket4(int port);

extern int port;
extern BOOL UseKISS;			// Enable Packet (KISS) interface
int Speed;
int PollDelay;

extern SOCKET PktSock;
extern SOCKET PktListenSock;

extern BOOL PKTCONNECTED;


BOOL NewVCOM;

BOOL PortEnabled;
HANDLE hDevice;

BOOL VCOM;

char PORTNAME[80];

int CTS, DTR, RTS;


extern volatile int RXBPtr;

/*

#define IOCTL_SERIAL_SET_BAUD_RATE      CTL_CODE(FILE_DEVICE_SERIAL_PORT, 1,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_QUEUE_SIZE     CTL_CODE(FILE_DEVICE_SERIAL_PORT, 2,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_LINE_CONTROL   CTL_CODE(FILE_DEVICE_SERIAL_PORT, 3,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_BREAK_ON       CTL_CODE(FILE_DEVICE_SERIAL_PORT, 4,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_BREAK_OFF      CTL_CODE(FILE_DEVICE_SERIAL_PORT, 5,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_IMMEDIATE_CHAR     CTL_CODE(FILE_DEVICE_SERIAL_PORT, 6,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_TIMEOUTS       CTL_CODE(FILE_DEVICE_SERIAL_PORT, 7,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_TIMEOUTS       CTL_CODE(FILE_DEVICE_SERIAL_PORT, 8,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_DTR            CTL_CODE(FILE_DEVICE_SERIAL_PORT, 9,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_CLR_DTR            CTL_CODE(FILE_DEVICE_SERIAL_PORT,10,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_RESET_DEVICE       CTL_CODE(FILE_DEVICE_SERIAL_PORT,11,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_RTS            CTL_CODE(FILE_DEVICE_SERIAL_PORT,12,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_CLR_RTS            CTL_CODE(FILE_DEVICE_SERIAL_PORT,13,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_XOFF           CTL_CODE(FILE_DEVICE_SERIAL_PORT,14,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_XON            CTL_CODE(FILE_DEVICE_SERIAL_PORT,15,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_WAIT_MASK      CTL_CODE(FILE_DEVICE_SERIAL_PORT,16,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_WAIT_MASK      CTL_CODE(FILE_DEVICE_SERIAL_PORT,17,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_WAIT_ON_MASK       CTL_CODE(FILE_DEVICE_SERIAL_PORT,18,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_PURGE              CTL_CODE(FILE_DEVICE_SERIAL_PORT,19,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_BAUD_RATE      CTL_CODE(FILE_DEVICE_SERIAL_PORT,20,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_LINE_CONTROL   CTL_CODE(FILE_DEVICE_SERIAL_PORT,21,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_CHARS          CTL_CODE(FILE_DEVICE_SERIAL_PORT,22,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_CHARS          CTL_CODE(FILE_DEVICE_SERIAL_PORT,23,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_HANDFLOW       CTL_CODE(FILE_DEVICE_SERIAL_PORT,24,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_HANDFLOW       CTL_CODE(FILE_DEVICE_SERIAL_PORT,25,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_MODEMSTATUS    CTL_CODE(FILE_DEVICE_SERIAL_PORT,26,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_COMMSTATUS     CTL_CODE(FILE_DEVICE_SERIAL_PORT,27,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_XOFF_COUNTER       CTL_CODE(FILE_DEVICE_SERIAL_PORT,28,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_PROPERTIES     CTL_CODE(FILE_DEVICE_SERIAL_PORT,29,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GET_DTRRTS         CTL_CODE(FILE_DEVICE_SERIAL_PORT,30,METHOD_BUFFERED,FILE_ANY_ACCESS)


#define IOCTL_SERIAL_IS_COM_OPEN CTL_CODE(FILE_DEVICE_SERIAL_PORT,0x800,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_GETDATA     CTL_CODE(FILE_DEVICE_SERIAL_PORT,0x801,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SETDATA     CTL_CODE(FILE_DEVICE_SERIAL_PORT,0x802,METHOD_BUFFERED,FILE_ANY_ACCESS)

#define IOCTL_SERIAL_SET_CTS     CTL_CODE(FILE_DEVICE_SERIAL_PORT,0x803,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_DSR     CTL_CODE(FILE_DEVICE_SERIAL_PORT,0x804,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_SET_DCD     CTL_CODE(FILE_DEVICE_SERIAL_PORT,0x805,METHOD_BUFFERED,FILE_ANY_ACCESS)

#define IOCTL_SERIAL_CLR_CTS     CTL_CODE(FILE_DEVICE_SERIAL_PORT,0x806,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_CLR_DSR     CTL_CODE(FILE_DEVICE_SERIAL_PORT,0x807,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_SERIAL_CLR_DCD     CTL_CODE(FILE_DEVICE_SERIAL_PORT,0x808,METHOD_BUFFERED,FILE_ANY_ACCESS)

#define IOCTL_BPQ_ADD_DEVICE     CTL_CODE(FILE_DEVICE_SERIAL_PORT,0x809,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_BPQ_DELETE_DEVICE  CTL_CODE(FILE_DEVICE_SERIAL_PORT,0x80a,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define IOCTL_BPQ_LIST_DEVICES   CTL_CODE(FILE_DEVICE_SERIAL_PORT,0x80b,METHOD_BUFFERED,FILE_ANY_ACCESS)

#define	IOCTL_BPQ_SET_POLLDELAY	 CTL_CODE(FILE_DEVICE_SERIAL_PORT,0x80c,METHOD_BUFFERED,FILE_ANY_ACCESS)
#define	IOCTL_BPQ_SET_DEBUGMASK	 CTL_CODE(FILE_DEVICE_SERIAL_PORT,0x80d,METHOD_BUFFERED,FILE_ANY_ACCESS)

#define W98_SERIAL_IS_COM_OPEN 0x800
#define W98_SERIAL_GETDATA     0x801
#define W98_SERIAL_SETDATA     0x802

#define W98_SERIAL_SET_CTS     0x803
#define W98_SERIAL_SET_DSR     0x804
#define W98_SERIAL_SET_DCD     0x805

#define W98_SERIAL_CLR_CTS     0x806
#define W98_SERIAL_CLR_DSR     0x807
#define W98_SERIAL_CLR_DCD     0x808

#define W98_BPQ_ADD_DEVICE     0x809
#define W98_BPQ_DELETE_DEVICE  0x80a
#define W98_BPQ_LIST_DEVICES   0x80b

#define	W98_BPQ_SET_POLLDELAY	 0x80c
#define	W98_BPQ_SET_DEBUGMASK	 0x80d

#define W98_SERIAL_GET_COMMSTATUS    27
#define W98_SERIAL_GET_DTRRTS        30

#define DebugModemStatus 1
#define DebugCOMStatus 2
#define DebugWaitCompletion 4
#define DebugReadCompletion 8
*/

typedef struct _SERIAL_STATUS {
    unsigned long Errors;
    unsigned long HoldReasons;
    unsigned long AmountInInQueue;
    unsigned long AmountInOutQueue;
    BOOL EofReceived;
    BOOL WaitForImmediate;
} SERIAL_STATUS,*PSERIAL_STATUS;



HANDLE BPQOpenSerialPort(DWORD * lasterror)
{
	// Open a Virtual COM Port

	char szPort[80];
	HANDLE hDevice;
	int Err;

	*lasterror=0;

	// Only support New Style VCOM 

	WriteDebugLog(LOGDEBUG, "ARDOPC opening port com%d", port);

	sprintf( szPort, "\\\\.\\pipe\\BPQCOM%d", port ) ;

	hDevice = CreateFile( szPort, GENERIC_READ | GENERIC_WRITE,
                  0,                    // exclusive access
                  NULL,                 // no security attrs
                  OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL, 
                  NULL );

	Err = GetLastError();

	if (hDevice != (HANDLE) -1)
	{
		NewVCOM = TRUE;
		PortEnabled = TRUE;
		Err = GetFileType(hDevice);
	}
		  
	if (hDevice == (HANDLE) -1 )
	{
		*lasterror=GetLastError();
	}

   return hDevice;
}

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

int BPQSerialGetData(UCHAR * Message, unsigned int BufLen, unsigned long * MsgLen)
{
	DWORD dwLength = 0;
	DWORD Available = 0;
	long unsigned int Length, RealLen = 0;
	
	int ret = PeekNamedPipe(hDevice, NULL, 0, NULL, &Available, NULL);

	if (ret == 0)
	{
		ret = GetLastError();

		if (ret == ERROR_BROKEN_PIPE)
		{
			CloseHandle(hDevice);
			hDevice = INVALID_HANDLE_VALUE;
			return 0;
		}
	}

	if (Available > BufLen)
		Available = BufLen;
		
	if (Available)
	{
		UCHAR * ptr1 = Message;
		UCHAR * ptr2 = Message;
		UCHAR c;
			
		ReadFile(hDevice, Message, Available, &Length, NULL);

		// Have to look for FF escape chars

		RealLen = Length;

		while (Length != 0)
		{
			c = *(ptr1++);
			Length--;

			if (c == 0xff)
			{
				c = c = *(ptr1++);
				Length--;
					
				if (c == 0xff)			// ff ff means ff
				{
					RealLen--;
				}
				else
				{
					// This is connection statua from other end

					RealLen -= 2;
//					PortEnabled = c;
					continue;
				}
			}
			*(ptr2++) = c;
		}
	}
	*MsgLen = RealLen;
	return 0;
}



VOID PutString(UCHAR * Msg)
{
//	WriteDebugLog(7, Msg);
	SerialSendData(Msg, strlen(Msg));
}

int PutChar(UCHAR c)
{
	SerialSendData(&c, 1);
	return 0;
}

BOOL SerialHostInit()
{
	WSADATA WsaData;			 // receives data from WSAStartup
	unsigned long OpenCount = 0;
	long unsigned int Errorval;
	char * Baud = strlop(PORTNAME, ',');

	VCOM = TRUE;

	hDevice = BPQOpenSerialPort(&Errorval);

	if (hDevice != (HANDLE) -1)
		WriteDebugLog(LOGALERT, "ARDOPC opened port com%d", port);
	else
	{
		WriteDebugLog(LOGALERT, "ARDOPC Open Failed for Port com%d", port);
		hDevice = 0;
	}

	if (UseKISS && pktport)
	{
		WSAStartup(MAKEWORD(2, 0), &WsaData);
		WriteDebugLog(LOGALERT, "ARDOPC listening for KISS frames on port %d", pktport);
		PktListenSock = OpenSocket4(pktport);
	}
	return TRUE;
}


UCHAR RXBUFFER[500]; // Long enough for stuffed Host Mode frame

VOID SerialHostPoll()
{
	unsigned int n;
	unsigned long Read = 0;

	if (VCOM)
		n = BPQSerialGetData(&RXBUFFER[RXBPtr], 499 - RXBPtr, &Read);

	if (Read)
	{		
		RXBPtr += Read;
		ProcessSCSPacket(RXBUFFER, RXBPtr);
	}
	n=0;
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
