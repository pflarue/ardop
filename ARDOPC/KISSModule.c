//
// KISS Code for ARDOPC. 
//
//	Allows Packet modes and ARDOP to coexist in same program
//	Mainly for Teensy Version.
//
//	Teensy will probably only support KISS over i2c,
//	but for testing Windows version uses a real com port

//	New idea is to support via SCS Host Channel 250, but will
//	probably leave serial/i2c support in

//	Now supports KISS over SCS Channel 250 or a KISS over TCP Connection



#ifdef WIN32
#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>
#include <winioctl.h>
#else
#define HANDLE int
#define SOCKET int
#include <sys/types.h>
#ifndef TEENSY
#include <sys/socket.h>
#endif
#endif

#include "ARDOPC.h"

#define FEND 0xC0 
#define FESC 0xDB
#define TFEND 0xDC
#define TFESC 0xDD


HANDLE OpenCOMPort(VOID * pPort, int speed, BOOL SetDTR, BOOL SetRTS, BOOL Quiet, int Stopbits);
int ReadCOMBlock(HANDLE fd, char * Block, int MaxLength);
VOID ProcessKISSBlock(UCHAR * KISSBUFFER, int Len);
VOID EncodePacket(UCHAR * Data, int Len);
VOID ProcessKISSBytes(UCHAR * RXBUFFER, int Read);
void PacketStartTX();
VOID EmCRCStuffAndSend(UCHAR * Msg, int Len);
VOID ProcessKISSControlFrame();
VOID ptkSessionBG();


char KISSPORTNAME[80] = "";  // for now just support over Host Interface;

HANDLE hControl;

typedef struct _SERIAL_STATUS {
    unsigned long Errors;
    unsigned long HoldReasons;
    unsigned long AmountInInQueue;
    unsigned long AmountInOutQueue;
    BOOL EofReceived;
    BOOL WaitForImmediate;
} SERIAL_STATUS,*PSERIAL_STATUS;

// Buffers for KISS frames to and from the host. Uses cyclic buffers
// Size must be modulo 2 so we can AND with mask to make cyclic

#define KISSBUFFERSIZE 4096		
#define KISSBUFFERMASK 4095	


//	KISS bytes from a serial, i2c or Host Mode link are placed in
//	the cyclic TX buffer as received. As we can only transmit complete
//	packets, we need some indicator of how many packets there are
//	in the buffer. Or maybe just how many FENDS, especially if
//	we remove any extra ones (just leave one at end of frame)


UCHAR KISSRXBUFF[KISSBUFFERSIZE];		// Host to RF
int KRXPutPtr = 0;
int KRXGetPtr = 0;
int FENDCount = 0;


UCHAR KISSTXBUFF[KISSBUFFERSIZE];		// RF to Host
int KTXPutPtr = 0;
int KTXGetPtr = 0;


UCHAR KISSBUFFER[500]; // Long enough for stuffed KISS frame
UCHAR * RXMPTR = &KISSBUFFER[0];
int KISSLength = 0;

BOOL ESCFlag = FALSE;

HANDLE KISSHandle = 0;

extern BOOL PacketHost;

int TXDelay = 500;	

extern SOCKET PktSock;
extern BOOL PKTCONNECTED;

BOOL KISSInit()
{
//	char * Baud = strlop(KISSPORTNAME, ',');

#ifdef WIN32

	if (KISSPORTNAME[0])
		KISSHandle = OpenCOMPort(KISSPORTNAME, 19200, FALSE, FALSE, FALSE, 0);

	if (KISSHandle)
		WriteDebugLog(LOGALERT, "KISS interface Using port %s", KISSPORTNAME); 
#endif

	return TRUE;
}

VOID KISSPoll()
{
#ifdef WIN32

	unsigned long Read;
	unsigned char RXBuffer[512];

	if (KISSHandle == NULL)
		return;

	Read = ReadCOMBlock(KISSHandle, RXBuffer, 512);

	if (Read == 0)
		return;

	ProcessKISSBytes(RXBuffer, Read);
#endif
}
VOID ProcessPacketBytes(UCHAR * Buffer, int Read)
{
	// Called when frame received on TCP Packet Connection. Could be for
	// KISS or ARDOP Packet Session mode

	// Assumes that Complete KISS or Host packet will be received (pretty safe
	// with TCP (I hope!)

	// But could be more than one in buffer

	if (Buffer[0] == 192)
		ProcessKISSBytes(Buffer, Read);
	else
	{
		while (Read > 0)
		{
			int Used = Buffer[2] + 4;
			ProcessPacketHostBytes(Buffer, Read);
			Read -= Used;

			if (Read > 0)
			{
				memmove(Buffer, &Buffer[Used], Read);
			}
		}
	}
}

VOID ProcessKISSBytes(UCHAR * RXBuffer, int Read)
{
	// Store in cyclic buffer, counting FENDS so we know if we
	// have a full frame in the buffer

	UCHAR c;

	WriteDebugLog(LOGALERT, "Queuing %d Packet Bytes", Read); 


	while (Read--)
	{
		c = *(RXBuffer++);

		if (c == FEND)
			FENDCount++;

		KISSRXBUFF[KRXPutPtr++] = c;
		KRXPutPtr &= KISSBUFFERMASK;

		if (KRXPutPtr == KRXGetPtr)			// should never happen, but nasty if it does
			FENDCount = 0;		// Buffer is now empty
	}
}


VOID ProcessKISSByte(UCHAR c)
{
	// Store in cyclic buffer, counting FENDS so we know if we
	// have a full frame in the buffer

	if (c == FEND)
		FENDCount++;

	KISSRXBUFF[KRXPutPtr++] = c;
	KRXPutPtr &= KISSBUFFERMASK;

	if (KRXPutPtr == KRXGetPtr)			// should never happen, but nasty if it does
		FENDCount = 0;		// Buffer is now empty
}

	
BOOL GetNextKISSFrame()
{
	// Called to get a frame to send, either before starting or
	// when current frame has been sent (for back to back sends)

	unsigned char c;
	UCHAR * RXMPTR;

	ptkSessionBG();			// See if any session events to process

	if (KRXPutPtr == KRXGetPtr)	// Nothing to send
	{
		ptkSessionBG();			// See if any session events to process
		
		if (KRXPutPtr == KRXGetPtr)	// Still nothing to send
			return FALSE;			// Buffer empty

	}
	if (FENDCount < 2)
		return FALSE;				// Not a complete KISS frame

	if (KISSRXBUFF[KRXGetPtr++] != FEND)
	{
		// First char should always be FEND. If not Buffer has
		// wrapped. Remove the partial frame and discard

		while (KRXPutPtr != KRXGetPtr)
		{
			if (KISSRXBUFF[KRXGetPtr++] == FEND)
			{
				// Found a FEND. 
		
				KRXGetPtr &= KISSBUFFERMASK;
				FENDCount --;

				// Next should also be a FEND, but can check next time round

				// As this shouldn't happen often, just exit and get frame next time

				return FALSE;
			}
		}
	
		// no FENDS in buffer!!!

		FENDCount = 0;		// Buffer is now empty
		return FALSE;
	}

	// First char is a FEND, and get pointer points to next char

	RXMPTR = &KISSBUFFER[0];		// Initialise buffer pointer

	KRXGetPtr &= KISSBUFFERMASK;
	FENDCount --;
	
	while (KRXPutPtr != KRXGetPtr)
	{
		c = KISSRXBUFF[KRXGetPtr++];
		KRXGetPtr &= KISSBUFFERMASK;

		if (ESCFlag)
		{
			//
			//	FESC received - next should be TFESC or TFEND

			ESCFlag = FALSE;

			if (c == TFESC)
				c = FESC;
	
			if (c == TFEND)
				c = FEND;
		}
		else
		{
			switch (c)
			{
			case FEND:		
	
				//
				//	Either start of message or message complete
				//
				
				if (RXMPTR == &KISSBUFFER[0])
				{
					// Start of Message. Shouldn't Happen
					FENDCount--;					
					continue;
				}

				FENDCount--;
				KISSLength = RXMPTR - &KISSBUFFER[0];

				// Process Control Frames here

				if (KISSBUFFER[0] != 0 && KISSBUFFER[0] != 6 && KISSBUFFER[0] != 12)
				{
					ProcessKISSControlFrame();
					return FALSE;
				}

				return TRUE;	// Got complete frame in KISSBUFFER

			case FESC:
		
				ESCFlag = TRUE;
				continue;

			}
		}
		
		//
		//	Ok, a normal char
		//

		*(RXMPTR++) = c;

		if (RXMPTR == &KISSBUFFER[499])
			RXMPTR--;	// Protect Buffer
	}

	// We shouldnt get here, as it means FENDCOUNT is wrong. Reset it

	FENDCount = 0;
 	return FALSE;
}


// Called by SCS Host Interface

BOOL CheckKISS(UCHAR * SCSReply)
{
	int Length = KTXPutPtr - KTXGetPtr;
	int n;
	int get = KTXGetPtr;

	if (Length == 0)
		return FALSE;

	if (Length < 0)
		Length += KISSBUFFERSIZE;

	// Return up to 256 chars

	if (Length > 256)
		Length = 256;

	n = 0;
	
	while (n < Length)
	{
		SCSReply[n++ + 5] = KISSTXBUFF[get++];
		get &= KISSBUFFERMASK;
	}

	KTXGetPtr = get;
	
	SCSReply[2] = 250;
	SCSReply[3] = 7;
	SCSReply[4] = Length - 1;

	EmCRCStuffAndSend(SCSReply, Length + 5);
	return TRUE;
}


VOID ProcessKISSControlFrame()
{
}

VOID SendAckModeAck()
{
	KISSTXBUFF[KTXPutPtr++] = FEND;
	KTXPutPtr &= KISSBUFFERMASK;
	KISSTXBUFF[KTXPutPtr++] = 12;			// AckMode opcode
	KTXPutPtr &= KISSBUFFERMASK;
	KISSTXBUFF[KTXPutPtr++] = KISSBUFFER[1];
	KTXPutPtr &= KISSBUFFERMASK;
	KISSTXBUFF[KTXPutPtr++] = KISSBUFFER[2];
	KTXPutPtr &= KISSBUFFERMASK;
	KISSTXBUFF[KTXPutPtr++] = FEND;

	// If using KISS over TCP, send it

#ifndef TEENSY	
	
	// If Using TCP, send it

	if (pktport)
	{
		if (PKTCONNECTED)
			send(PktSock, KISSTXBUFF, KTXPutPtr, 0);
	
		KTXPutPtr = 0;
	}

#endif

}

void SendFrametoHost(unsigned char *data, unsigned dlen)
{
	KISSTXBUFF[KTXPutPtr++] = FEND;
	KTXPutPtr &= KISSBUFFERMASK;

	KISSTXBUFF[KTXPutPtr++] = 0;		// Data
	KTXPutPtr &= KISSBUFFERMASK;

	for (; dlen > 0; dlen--, data++)
	{
		if (*data == FEND)
		{
			KISSTXBUFF[KTXPutPtr++] = FESC;
			KTXPutPtr &= KISSBUFFERMASK;
			KISSTXBUFF[KTXPutPtr++] = TFEND;
			KTXPutPtr &= KISSBUFFERMASK;
		}
		else if (*data == FESC)
		{
			KISSTXBUFF[KTXPutPtr++] = FESC;
			KTXPutPtr &= KISSBUFFERMASK;
			KISSTXBUFF[KTXPutPtr++] = TFESC;
			KTXPutPtr &= KISSBUFFERMASK;
		} 
		else 
		{
			KISSTXBUFF[KTXPutPtr++] = *data;
			KTXPutPtr &= KISSBUFFERMASK;
		}
	}

	KISSTXBUFF[KTXPutPtr++] = FEND;
	KTXPutPtr &= KISSBUFFERMASK;

#ifndef TEENSY	
	
	// If Using TCP, send it

	if (pktport)
	{
		if (PKTCONNECTED)
			send(PktSock, KISSTXBUFF, KTXPutPtr, 0);
	
		KTXPutPtr = 0;
	}

#endif
}
 


