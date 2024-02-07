/*
Copyright 2001-2015 John Wiseman G8BPQ

This file is part of LinBPQ/BPQ32.

LinBPQ/BPQ32 is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

LinBPQ/BPQ32 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with LinBPQ/BPQ32.  If not, see http://www.gnu.org/licenses
*/	

// Level 2 Code for ARDOP-Packet

#ifdef WIN32
#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>
#include <winioctl.h>
#else
#define HANDLE int
#endif

#include "ARDOPC.h"

#ifndef WIN32

#define strtok_s strtok_r
#define _strupr strupr
#define SOCKET int
#include <sys/types.h>
#ifndef TEENSY
#include <sys/socket.h>
#endif
#endif


#define	PFBIT 0x10		// POLL/FINAL BIT IN CONTROL BYTE

#define	REJSENT	1		// SET WHEN FIRST REJ IS SENT IN REPLY
						// TO AN I(P)
#define	RNRSET	2		// RNR RECEIVED FROM OTHER END
#define DISCPENDING	8		// SEND DISC WHEN ALL DATA ACK'ED
#define	RNRSENT	0x10	// WE HAVE SEND RNR
#define	POLLSENT 0x20	// POLL BIT OUTSTANDING

#define L2TICK 10		// Timer called every 1/10 sec
	
#define	TENSECS	10 * L2TICK
#define	THREESECS 3 * L2TICK

#define MAXHOSTCHANNELS 10

#define	UI	3
#define	SABM 0x2F
#define	DISC 0x43
#define	DM	0x0F
#define	UA	0x63
#define	FRMR 0x87
#define	RR	1
#define	RNR	5
#define	REJ	9

// V2.2 Types

#define	SREJ 0x0D
#define SABME 0x6F
#define XID 0xAF
#define TEST 0xE3

#define SUPPORT2point2 1

// Reason Equates

#define NORMALCLOSE 0
#define RETRIEDOUT 1
#define SETUPFAILED 2
#define LINKLOST 3
#define LINKSTUCK 4
#define LINKIDLE 5

// XID Optional Functions

#define OPMustHave 0x02A080		// Sync TEST 16 bit FCS Extended Address
#define OPSREJ 4
#define OPSREJMult 0x200000
#define OPREJ 2
#define OPMod8 0x400
#define OPMod128 0x800

#define MAXBUFFS 50

extern int KRXPutPtr;
extern int KRXGetPtr;

extern UCHAR PacketMon[360];
extern int PacketMonMore;
extern int PacketMonLength;

typedef struct _MESSAGE
{
//	BASIC LINK LEVEL MESSAGE BUFFER LAYOUT

//	struct _LINKTABLE * Linkptr;
	int DataLen;

	UCHAR * DEST;
	UCHAR * ORIGIN;
	UCHAR * DIGIS;

	UCHAR	CTL;
	UCHAR	PID; 
	
	UCHAR * L2DATA;

}MESSAGE, *PMESSAGE;

typedef struct _DATABUFFER
{
//	Data content of a packet to send or receive;

	int DataLen;
	UCHAR * Data;
} DATABUFFER;

typedef struct _LINKTABLE
{
//;
//;	LEVEL 2 LINK CONTROL TABLE
//;
	struct _LINKTABLE * Next;

	UCHAR	LINKCALL[7];	// CALLSIGN OF STATION
	UCHAR	OURCALL[7];		// CALLSIGN OF OUR END
	UCHAR	DIGIS[56];		// LEVEL 2 DIGIS IN PATH

	UCHAR	LINKTYPE;		// 1 = UP, 2= DOWN, 3 = INTERNODE

	UCHAR	LINKNR;
	UCHAR	LINKNS;			// LEV 2 SEQUENCE COUNTS
	UCHAR	LINKWS;			// WINDOW START
	UCHAR	LINKOWS;		// OLD (LAST ACKED) WINDOW START
	UCHAR	LINKWINDOW;		// LEVEL 2 WINDOW SIZE
	int		PACLEN;

	UCHAR	L2FLAGS;		// CONTROL BITS
  
	UCHAR *	TXBuffer;		// Bytes to send (Malloced)
	int TXCount;			// Count of bytes to send
	int TXBuffersize;		// Currently allocated size

	UCHAR *	RXBuffer;		// Bytes to send (Malloced)
	int RXCount;			// COunt of nytes to send
	int RXBuffersize;		// CUrrently allocated size

	struct _DATABUFFER FRAMES[8];		// FRAMES WAITING ACK
	struct _DATABUFFER RXFRAMES[8];		// Frames received out of sequence

	UCHAR	L2STATE;		// PROCESSING STATE
	UCHAR	UseSREJ;		// Set if running 2.2
	unsigned short	L2TIMER;		// FRAME RETRY TIMER
	UCHAR	L2TIME;			// RETRY TIMER INITIAL VALUE
	unsigned short	L2SLOTIM;		// DELAY FOR LINK VALIDATION POLL
	UCHAR	REJTIMER;		// TO TIME OUT REJ  IN VERSION 1
	unsigned short	LAST_F_TIME;	// TIME LAST R(F) SENT
	UCHAR	SDREJF;			// FLAGS FOR FRMR
	UCHAR	SDRBYTE;		// SAVED CONTROL BYTE FOR FRMR

	UCHAR	SDTSLOT	;		// POINTER TO NEXT TXSLOT TO USE

	UCHAR	L2RETRIES;		// RETRY COUNTER

	UCHAR	SESSACTIVE;		// SET WHEN WE ARE SURE SESSION IS UP

	unsigned short  KILLTIMER;		// TIME TO KILL IDLE LINK

	VOID *	L2FRAG_Q;		// DEFRAGMENTATION QUEUE

	int	HostChannel;		// DED Channel
	int	ReportFlags;		// Inicate need to report event to host

#define ConFailed 1
#define ConBusy 2
#define ConOK	4
#define DiscRpt	8
#define Incomming 16
#define ReportQueue 32

	// Stats for dynamic frame type, window and paclen

	int pktMode;

	int L2FRAMESFORUS;
	int L2FRAMESSENT;
	int L2TIMEOUTS;
	int BytesSent;
	int BytesReceived;
	int PacketsSent;
	int PacketsReceived;
	int L2FRMRRX;
	int L2FRMRTX;
//	int RXERRORS;			// RECEIVE ERRORS
	int L2REJCOUNT;			// REJ FRAMES RECEIVED
	int L2OUTOFSEQ;			// FRAMES RECEIVED OUT OF SEQUENCE
	int L2RESEQ;			// FRAMES RESEQUENCED

} LINKTABLE;

UCHAR MYCALL[7] = ""; //	 NODE CALLSIGN (BIT SHIFTED)

extern char Callsign[10];
extern char AuxCalls[10][10];
extern int AuxCallsLength;

char AuxAX[10][7];				// Aux Calls in ax.25 format


unsigned short SENDING;			// LINK STATUS BITS
unsigned short ACTIVE;

UCHAR AVSENDING;			// LAST MINUTE
UCHAR AVACTIVE;

UCHAR PORTWINDOW = 4;	// L2 WINDOW FOR THIS PORT
int PORTTXDELAY;		// TX DELAY FOR THIS PORT
UCHAR PORTPERSISTANCE;	// PERSISTANCE VALUE FOR THIS PORT
int PORTSLOTTIME;		// SLOT TIME
int PORTT1 = 4 * L2TICK;// L2 TIMEOUT
int PORTN2 = 6;			// RETRIES
int PORTPACLEN;			// DEFAULT PACLEN FOR INCOMING SESSIONS

int PORTMAXDIGIS = 3;
int T3 = 180 * L2TICK;
int L2KILLTIME = 600 * L2TICK;


VOID * PORTTX_Q;
VOID * FREE_Q;

int initMode = 1;
int SABMMode = 1;

// Modes now

//	"4PSK/200",
//	"4FSK/500", "4PSK/500", "8PSK/500", "16QAM/500",
//	"4FSK/1000", "4PSK/1000", "8PSK/1000", "16QAM/1000",
//	"4FSK/2000", "4PSK/2000", "8PSK/2000", "16QAM/2000"

int CtlMode[16]	= {			// Control frames are sent in faster modes but not full speed as they are short
			1,
			1, 2, 2, 2,
			5, 6, 6, 6,
			9, 9, 9 ,9};

int initMaxFrame = 7;	// Will be overriden depending on Mode
int initPacLen = 0;

const int defaultPacLen[16] = {
			32,
			32, 64, 64, 64,
			64, 128, 128, 128,
			128, 256, 256, 256};

// Paclen 256 is fine with 8 car 4PSK and just about ok with 4 car 4PSK.
// Also just about ok with 2 car QAM (4 Secs packet time)
// 2 Car 4PSK best limited to 128, 1 CAR 64




struct _LINKTABLE * LINKS = NULL;

int QCOUNT = 0;
int MINBUFFCOUNT = 0;

BOOL PacketHost = 0;	// Set if TCP Packet connection is in Host Mode not KISS Mode
						
VOID L2SENDCOMMAND();
VOID SETUPL2MESSAGE(MESSAGE * Buffer, struct _LINKTABLE * LINK, UCHAR CMD);
VOID SendSupervisCmd(struct _LINKTABLE * LINK);
void SEND_RR_RESP(struct _LINKTABLE * LINK, UCHAR PF);
VOID L2SENDRESPONSE(struct _LINKTABLE * LINK, int CMD);
VOID L2SENDCOMMAND(struct _LINKTABLE * LINK, int CMD);
VOID ACKMSG(struct _LINKTABLE * LINK);
VOID InformPartner(struct _LINKTABLE * LINK, int Reason);
unsigned int RR_OR_RNR(struct _LINKTABLE * LINK);
VOID L2TIMEOUT(struct _LINKTABLE * LINK);
VOID CLEAROUTLINK(struct _LINKTABLE * LINK);
VOID SENDFRMR(struct _LINKTABLE * LINK);
VOID SDFRMR(struct _LINKTABLE * LINK);
VOID SDNRCHK(struct _LINKTABLE * LINK, UCHAR CTL);
VOID RESETNS(struct _LINKTABLE * LINK, UCHAR NS);
VOID PROC_I_FRAME(struct _LINKTABLE * LINK, UCHAR * Data, int DataLen);
VOID RESET2X(struct _LINKTABLE * LINK);
VOID RESET2(struct _LINKTABLE * LINK);
VOID CONNECTREFUSED(struct _LINKTABLE * LINK);
VOID SDUFRM(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR CTL);
VOID SFRAME(struct _LINKTABLE * LINK, UCHAR CTL, UCHAR MSGFLAG);
VOID SDIFRM(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR CTL, UCHAR MSGFLAG);
VOID SENDCONNECTREPLY(struct _LINKTABLE * LINK);
VOID SETUPNEWL2SESSION(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR MSGFLAG);
VOID L2SABM(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR MSGFLAG);
VOID L2SENDUA(struct _LINKTABLE * LINK, MESSAGE * Buffer);
VOID L2SENDDM(struct _LINKTABLE * LINK, MESSAGE * Buffer);
VOID L2SENDRESP(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR CTL);
int COUNTLINKS(int Port);
VOID L2_PROCESS(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR CTL, UCHAR MSGFLAG);
VOID PUT_ON_PORT_Q(struct _LINKTABLE * LINK, MESSAGE * Buffer);
VOID L2SWAPADDRESSES(MESSAGE * Buffer);
struct _LINKTABLE * FindLink(UCHAR * LinkCall, UCHAR * OurCall);
VOID SENDSABM(struct _LINKTABLE * LINK);
VOID L2SENDXID(struct _LINKTABLE * LINK);
VOID L2LINKACTIVE(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR CTL, UCHAR MSGFLAG);
BOOL CompareAliases(UCHAR * c1, UCHAR * c2);
VOID L2FORUS(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR CTL, UCHAR MSGFLAG);
VOID MHPROC(MESSAGE * Buffer);
VOID L2SENDINVALIDCTRL(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR CTL);
UCHAR * SETUPADDRESSES(struct _LINKTABLE * LINK, PMESSAGE Msg);
VOID ProcessXIDCommand(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR CTL, UCHAR MSGFLAG);
UCHAR * PacketSessionPoll(UCHAR * NextChan);
VOID PktSend(UCHAR * Msg, int Len);
VOID ProcessKISSByte(UCHAR c);
VOID PutString(UCHAR * Msg);
int PutChar(UCHAR c);
int SerialSendData(UCHAR * Message,int MsgLen);
VOID EmCRCStuffAndSend(UCHAR * Msg, int Len);
VOID DisplaySessionStats(struct _LINKTABLE * LINK, int Exitcode);


int REALTIMETICKS = 0;

//	MSGFLAG contains CMD/RESPONSE BITS

#define	CMDBIT	4		// CURRENT MESSAGE IS A COMMAND
#define	RESP 2		// CURRENT MSG IS RESPONSE

//	FRMR REJECT FLAGS

#define	SDINVC 1		// INVALID COMMAND
#define	SDNRER 8		// INVALID N(R)



UCHAR NO_CTEXT = 0;

UCHAR MSGFLAG = 0;
extern UCHAR * ALIASPTR;

UCHAR QSTCALL[7] = {'Q'+'Q','S'+'S','T'+'T',0x40,0x40,0x40,0xe0};	// QST IN AX25
UCHAR NODECALL[7] = {0x9C, 0x9E, 0x88, 0x8A, 0xA6, 0x40, 0xE0};		// 'NODES' IN AX25 FORMAT


VOID * GetBuff()
{
	MESSAGE * Buffer = malloc(sizeof(MESSAGE));

	return Buffer;
}

unsigned int ReleaseBuffer(VOID *pBUFF)
{
	free(pBUFF);
	return 0;
}

/*
// win64 Compiler warning in this function.
// Since it doesn't appear to be used anywhere, comment it out
// rather than debug it.
int C_Q_ADD(VOID *PQ, VOID *PBUFF)
{
	unsigned int * Q;
	unsigned int * BUFF = (unsigned int *)PBUFF;
	unsigned int * next;
	int n = 0;

//	PQ may not be word aligned, so copy as bytes (for ARM5)

	Q = (unsigned int *) PQ;

	BUFF[0]=0;							// Clear chain in new buffer

	if (Q[0] == 0)						// Empty
	{
		Q[0]=(unsigned int)BUFF;				// New one on front
		return(0);
	}

	next = (unsigned int *)Q[0];

	while (next[0]!=0)
	{
		next=(unsigned int *)next[0];			// Chain to end of queue
	}
	next[0]=(unsigned int)BUFF;					// New one on end

	return(0);
}
*/

BOOL ConvToAX25(char * callsign, unsigned char * ax25call)
{
	int i;

	memset(ax25call,0x40,6);		// in case short
	ax25call[6]=0x60;				// default SSID

	for (i=0;i<7;i++)
	{
		if (callsign[i] == '-')
		{
			//
			//	process ssid and return
			//
			i = atoi(&callsign[i+1]);

			if (i < 16)
			{
				ax25call[6] |= i<<1;
				return (TRUE);
			}
			return (FALSE);
		}

		if (callsign[i] == 0 || callsign[i] == 13 || callsign[i] == ' ' || callsign[i] == ',')
		{
			//
			//	End of call - no ssid
			//
			return (TRUE);
		}

		ax25call[i] = callsign[i] << 1;
	}

	//
	//	Too many chars
	//

	return (FALSE);
}


int ConvFromAX25(unsigned char * incall, char * outcall)
{
	int in,out=0;
	unsigned char chr;

	memset(outcall,0x20,10);

	for (in=0;in<6;in++)
	{
		chr=incall[in];
		if (chr == 0x40)
			break;
		chr >>= 1;
		outcall[out++]=chr;
	}

	chr=incall[6];				// ssid

	if (chr == 0x42)
	{
		outcall[out++]='-';
		outcall[out++]='T';
		return out;
	}

	if (chr == 0x44)
	{
		outcall[out++]='-';
		outcall[out++]='R';
		return out;
	}

	chr >>= 1;
	chr	&= 15;

	if (chr > 0)
	{
		outcall[out++]='-';
		if (chr > 9)
		{
			chr-=10;
			outcall[out++]='1';
		}
		chr+=48;
		outcall[out++]=chr;
	}
	return (out);

}

VOID ConvertCallstoAX25()
{
	int n;

	ConvToAX25(Callsign, MYCALL);

	for (n = 0; n < AuxCallsLength; n++)
		ConvToAX25(&AuxCalls[n][0], &AuxAX[n][0]);

}



BOOL CompareCalls(UCHAR * c1, UCHAR * c2)
{
	//	COMPARE AX25 CALLSIGNS IGNORING EXTRA BITS IN SSID

	if (memcmp(c1, c2, 6))
		return FALSE;			// No Match

	if ((c1[6] & 0x1e) == (c2[6] & 0x1e))
		return TRUE;

	return FALSE;
}

BOOL CompareAliases(UCHAR * c1, UCHAR * c2)
{
	//	COMPARE first 6 chars of AX25 CALLSIGNS

	if (memcmp(c1, c2, 6))
		return FALSE;			// No Match

	return TRUE;
}

struct _LINKTABLE * NewLink()
{
	struct _LINKTABLE * LINK = malloc(sizeof(struct _LINKTABLE));
	int i;

	if (LINK == NULL)
		return NULL;

	memset(LINK, 0, sizeof(struct _LINKTABLE));

	// Allocate TX and RX Buffers
	
	LINK->TXBuffer = malloc(512);	//	Initial TX size. May Realloc if more needed

	if (LINK->TXBuffer == NULL)
	{
		free(LINK);
		return NULL;
	}

	LINK->RXBuffer = malloc(512);	//	Initial TX size. May Realloc if more needed

	if (LINK->RXBuffer == NULL)
	{
		free (LINK->TXBuffer);
		free(LINK);
		return NULL;
	}

	LINK->TXBuffersize = 512;	// Currently allocated size
	LINK->RXBuffersize = 512;	// Currently allocated size

	LINK->pktMode = initMode;			// Set by PAC commands

	// Get Default PACLEN for Mode

	// Paclen 256 is fine with 8 car 4PSK and just about ok with 4 car 4PSK.
	// Also just about ok with 2 car QAM (4 Secs packet time)
	// 2 Car 4PSK best limited to 128

	if (initPacLen == 0)		// Use configured if set
		LINK->PACLEN = defaultPacLen[initMode];	// Starting Point
	else
		LINK->PACLEN = initPacLen;			// Starting Point


	initMaxFrame = 7;

	LINK->LINKWINDOW = initMaxFrame;		// Starting Point

	LINK->UseSREJ = TRUE;

	// Allocate Frame buffers

	for (i = 0; i < 8; i++)
	{
		LINK->FRAMES[i].Data = malloc(LINK->PACLEN);	// paclen
	}

	if (LINK->FRAMES[7].Data == NULL)
	{
		// No Memory - release everything

		for (i = 0; i < 8; i++)
		{
			if (LINK->FRAMES[i].Data)
				free(LINK->FRAMES[i].Data);
		}
		free (LINK->TXBuffer);
		free (LINK->RXBuffer);
		free(LINK);
		return NULL;
	}

	// Add to chain

	if (LINKS == NULL)
		LINKS = LINK;
	else
	{
		LINK->Next = LINKS;
		LINKS = LINK;
	}
	return LINK;
}

struct _LINKTABLE * FindLink(UCHAR * LinkCall, UCHAR * OurCall)
{
	struct _LINKTABLE * LINK = LINKS;

	while (LINK)
	{
		if (CompareCalls(LINK->LINKCALL, LinkCall) && CompareCalls(LINK->OURCALL, OurCall))
			return LINK;
	
		LINK = LINK->Next;
	}

	return NULL;
}

int FindFreeHostChannel()
{
	struct _LINKTABLE * LINK = LINKS;
	int Channels[32] = {0};
	int i;

	while (LINK)
	{
		Channels[LINK->HostChannel] = 1;	// Mark as in use
		LINK = LINK->Next;
	}

	// Return first free

	for(i = 1; i < MAXHOSTCHANNELS; i++)
	{
		if (Channels[i] == 0)
			return i;
	}

	return 0;
}



VOID L2Routine(UCHAR * Packet, int Length, int FrameQuality, int totalRSErrors, int NumCar, int pktRXMode)
{
	//	LEVEL 2 PROCESSING

	// Packet is passed as received from Soundcard.c. It might be quite long
	// if using high speed modes (not sure of max size yet)

	// Note we must have finished with the data in the buffer before returning.
	// If we need to queue anything will have to make a copy


	MESSAGE Buffer = {0};
	struct _LINKTABLE * LINK;
	UCHAR * pktptr = Packet;
	
	UCHAR * ptr;
	int n;
	UCHAR CTL;
	UCHAR c;

	//	Check for invalid length (< 22 7Header + 7Addr + 7Addr + CTL

	if (Length < 15)
	{
		return;
	}

	// Trace it

	
	if (PacketMonLength == 0)	// Ingore if one queued
	{
		PacketMon[0] = 0;		// RX Flag
		memcpy(&PacketMon[1], Packet, Length);
		PacketMonLength = Length + 1;
	}

	// Separate out Header fields.

	Buffer.DEST = pktptr;
	pktptr += 7;
	Buffer.ORIGIN = pktptr;
	pktptr += 7;
	
	// Check for corrupt callsign

	//	Check for Corrupted Callsign in Origin (to keep MH list clean)

	ptr = Buffer.ORIGIN;
	n = 6;

	while(n--)
	{
		// Try a bit harder to detect corruption

		c = *(ptr++);

		if (c & 1)
		{
			return;
		}

		c = c >> 1;
		
		if (!isalnum(c) && !(c == '#') && !(c == ' '))
		{
			return;
		}
	}

		//	Check Digis if present

	Buffer.DIGIS = NULL;

	if ((Buffer.ORIGIN[6] & 1) == 0)	// Digis
	{
		ptr = pktptr;
		Buffer.DIGIS = ptr;

		n = 6;

		while(n--)
		{
			c = *(ptr++);

			if (c & 1)
			{
				return;
			}

			c = c >> 1;
		
			if (!isalnum(c) && !(c == '#') && !(c == ' '))
			{
				return;
			}
		}
	}


	//	Check Digis if present

	//  For the moment ARDOP Packet doesn't support digipeating

	//	CHECK THAT ALL DIGIS HAVE BEEN ACTIONED,

	n = 8;						// MAX DIGIS
	ptr = &Buffer.ORIGIN[6];	// End of Address bit

	while ((*ptr & 1) == 0)
	{
		//	MORE TO COME
	
		ptr += 7;

		if ((*ptr & 0x80) == 0)				// Digi'd bit
		{
			return;							// not complete and not for us
		}
		n--;

		if (n == 0)
		{
			return;						// Corrupt - no end of address bit
		}
	}

	// Reached End of digis, and all actioned, so can process it

	MSGFLAG = 0;					// CMD/RESP UNDEFINED

	ptr++;				// now points to CTL

	Buffer.CTL = *(ptr++);
	Buffer.PID = *(ptr++);

	Buffer.L2DATA = ptr;

	Buffer.DataLen = Length - (ptr - Packet);


	//	GET CMD/RESP BITS

	if (Buffer.DEST[6] & 0x80)
	{
		MSGFLAG |= CMDBIT;
	}
	else
	{
		if (Buffer.ORIGIN[6] & 0x80)			//	Only Dest Set
			MSGFLAG |= RESP;
	}

	//	SEE IF FOR AN ACTIVE LINK SESSION

	CTL = Buffer.CTL;

	// IF A UI, THERE IS NO SESSION

	LINK = FindLink(Buffer.ORIGIN, Buffer.DEST);

	if (LINK)
	{
		L2LINKACTIVE(LINK, &Buffer, CTL, MSGFLAG);
		return;
	}

	if (CompareCalls(Buffer.DEST, MYCALL))
		goto FORUS;


	for (n = 0; n < AuxCallsLength; n++)
	{
		if (CompareCalls(Buffer.DEST, &AuxAX[n][0]))
			goto FORUS;
	}

	// Not addresses to us
	return;

FORUS:

	// if a UI frame and UIHook Specified, call it

	L2FORUS(LINK, &Buffer, CTL, MSGFLAG);
}
	

VOID L2FORUS(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR CTL, UCHAR MSGFLAG)
{
	//	MESSAGE ADDRESSED TO OUR CALL OR ALIAS, BUT NOT FOR AN ACTIVE SESSION

	// LINK points to an empty link table entry

	int CTLlessPF = CTL & ~PFBIT;
	
	NO_CTEXT = 0;

	//	ONLY SABM or UI  ALLOWED IF NO SESSION 
	//	Plus XID/TEST/SABME if V2.2 support enabled

	if (CTLlessPF == 3)			// UI
	{
		//	A UI ADDRESSED TO US - SHOULD ONLY BE FOR IP, or possibly addressed NODES

		switch(Buffer->PID)
		{
		case 0xcf:				// Netrom
		
			break;

		case 0xcc:				// TCP
		case 0xcd:				// ARP
		case 0x08:				// NOS FRAGMENTED AX25 TCP/IP
	
			return;
		}
		return;
	}

	if (CTLlessPF == SABME)
	{
		// Although some say V2.2 requires SABME I don't agree!

		// Reject until we support Mod 128

		L2SENDINVALIDCTRL(LINK, Buffer, CTL);
		return;
	}

	if (CTLlessPF == SREJ)		// Used to see if other end supports SREJ on 2.0
	{
		// We do, so send DM 

		L2SENDRESP(LINK, Buffer, DM);
		return;
	}

	if (CTLlessPF == XID)
	{
		// Send FRMR if we only support V 2.0
		
		if (SUPPORT2point2 == FALSE)
		{
			L2SENDINVALIDCTRL(LINK, Buffer, CTL);		
			return;
		}
		// if Support 2.2 drop through
	}

	if (CTLlessPF == TEST)
	{
		// I can't see amy harm in replying to TEST

		L2SENDRESP(LINK, Buffer, TEST);
		return;
	}


//	if (CTLlessPF != SABM && CTLlessPF != SABME)
	if (CTLlessPF != SABM && CTLlessPF != XID)
	{
		if ((MSGFLAG & CMDBIT) && (CTL & PFBIT))	// Command with P?
			L2SENDDM(LINK, Buffer);

		return;
	}

	// Exclude and limit tests are done for XID and SABM
#ifdef	EXCLUDEBITS

	//	CHECK ExcludeList

	if (CheckExcludeList(Buffer->ORIGIN) == 0)
	{
		ReleaseBuffer(Buffer);
		return;
	}
#endif

	// OK to accept SABM or XID

	LINK = NewLink();

	if (LINK == NULL)
	{
		L2SENDDM(LINK, Buffer);	// No memory
		return;
	}

	// Find Free Host Session

	LINK->HostChannel = FindFreeHostChannel();

	if (LINK->HostChannel == 0)
	{
		L2SENDDM(LINK, Buffer);		// Busy
		return;
	}


	if (CTLlessPF == XID)
	{
		ProcessXIDCommand(LINK, Buffer, CTL, MSGFLAG);
		return;
	}

	// Not XID, so must be SABM

	L2SABM(LINK, Buffer, MSGFLAG);			// Process the SABM
}


VOID ProcessXIDCommand(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR CTL, UCHAR MSGFLAG)
{
		// I think it is fairly safe to accept XID as soon as we
		// can process SREJ, but only accept Mod 8 and 256 Byte frames

		// I think the only way to run 2.2 Mod 8 is to preceed a 
		//	SABM with XID, but others don't seem to agree!

		//	Run through XID fields, changing any we don't like,
		//	then return an XID response

		// Decode and process XID

		UCHAR * ptr = &Buffer->PID;
		UCHAR * ptr1, * ptr2;
		UCHAR TEMPDIGI[57];
		int n;

		if (*ptr++ == 0x82 && *ptr++ == 0x80)
		{
			int Type;
			int Len;
			unsigned int value;
			int xidlen = *(ptr++) << 8;	
			xidlen += *ptr++;
		
			// XID is set of Type, Len, Value n-tuples

			while (xidlen > 0)
			{
				Type = *ptr++;
				Len = *ptr++;
	
				value = 0;
				xidlen -= (Len + 2);

				while (Len--)
				{
					value <<=8;
					value += *ptr++;
				}
				switch(Type)
				{
				case 2:				//Bin fields

					break;

				case 3:

					if ((value & OPMustHave) != OPMustHave)
						goto BadXID;

					if ((value & OPMod8) == 0)
						goto BadXID;

					if ((value & OPSREJMult) == 0)
						goto BadXID;


					//	Reply Mod 8 SREJMULTI

					value = OPMustHave | OPSREJMult | OPMod8;
					ptr -=3;
					*ptr++ = value >> 16;
					*ptr++ = value >> 8;
					*ptr++ = value;


					break;

				case 6:				//RX Size

					break;

				case 8:				//RX Window

					break;
				}
			}

			// Send back as XID response

			LINK->L2STATE = 1;			// XID received
			LINK->UseSREJ = TRUE;	// Must support 2.2 if sent XID
			LINK->L2TIME = PORTT1;
			
			// save calls so we can match up SABM when it comes

			memcpy(LINK->LINKCALL, Buffer->ORIGIN, 7);
			LINK->LINKCALL[6] &= 0x1e;		// Mask SSID

			memcpy(LINK->OURCALL, Buffer->DEST, 7);
	
			LINK->OURCALL[6] &= 0x1e;		// Mask SSID

			memset(LINK->DIGIS, 0, 56);		// CLEAR DIGI FIELD IN CASE RECONNECT

			if ((Buffer->ORIGIN[6] & 1) == 0)	// End of Address
			{
				//	THERE ARE DIGIS TO PROCESS - COPY TO WORK AREA reversed, THEN COPY BACK
	
				memset(TEMPDIGI, 0, 57);		// CLEAR DIGI FIELD IN CASE RECONNECT

				ptr1 = &Buffer->ORIGIN[6];		// End of add 
				ptr2 = &TEMPDIGI[7 * 7];		// Last Temp Digi

				while((*ptr1 & 1) == 0)			// End of address bit
				{
					ptr1++;
					memcpy(ptr2, ptr1, 7);
					ptr2[6] &= 0x1e;			// Mask Repeated and Last bits
					ptr2 -= 7;
					ptr1 += 6;
				}
		
				//	LIST OF DIGI CALLS COMPLETE - COPY TO LINK CONTROL ENTRY

				n = PORTMAXDIGIS;

				ptr1 = ptr2 + 7;				// First in TEMPDIGIS
				ptr2 = &LINK->DIGIS[0];

				while (*ptr1)
				{
					if (n == 0)
					{
						// Too many for us
				
						CLEAROUTLINK(LINK);
						return;
					}

					memcpy(ptr2, ptr1, 7);
					ptr1 += 7;
					ptr2 += 7;
					n--;
				}
			}

			Buffer->CTL = CTL | PFBIT;

// 			Buffer->LENGTH = (UCHAR *)ADJBUFFER - (UCHAR *)Buffer + MSGHDDRLEN + 15;	// SET UP BYTE COUNT

			L2SWAPADDRESSES(Buffer);			// SWAP ADDRESSES AND SET RESP BITS

			PUT_ON_PORT_Q(LINK, Buffer);
			return;
		}
BadXID:
		L2SENDINVALIDCTRL(LINK, Buffer, CTL);
		return;
}


VOID L2LINKACTIVE(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR CTL, UCHAR MSGFLAG)
{
	//	MESSAGE ON AN ACTIVE LINK 

	int CTLlessPF = CTL & ~PFBIT;
	

	//	ONLY SABM or UI  ALLOWED IF NO SESSION 

	if (CTLlessPF == 3)			// UI
	{
		//	A UI ADDRESSED TO US - SHOULD ONLY BE FOR IP, or possibly addressed NODES

		switch(Buffer->PID)
		{
		case 0xcf:				// Netrom	
			break;

		case 0xcc:				// TCP
		case 0xcd:				// ARP
		case 0x08:				// NOS FRAGMENTED AX25 TCP/IP
	
			return;
		}
		return;
	}

	if (CTLlessPF == DISC)
	{
		InformPartner(LINK, NORMALCLOSE);		// SEND DISC TO OTHER END
		DisplaySessionStats(LINK, NORMALCLOSE);
		CLEAROUTLINK(LINK);

		L2SENDUA(LINK, Buffer);
		return;
	}


	if (LINK->L2STATE == 1)
	{
		// XID State. Should be XID response if 2.2 ok or DM/FRMR if not

		if (MSGFLAG & RESP)
		{
			if (CTLlessPF == DM || CTLlessPF == FRMR)
			{
				// Doesn't support XID - Send SABM

				LINK->L2STATE = 2;
				LINK->UseSREJ = FALSE;
				LINK->L2TIMER = 1;		// USe retry to send SABM
			}
			else if (CTLlessPF == XID)
			{
				// Process response to make sure ok, Send SABM or DISC

				LINK->L2STATE = 2;
				LINK->UseSREJ = TRUE;// Must support 2.2 if responded to XID
				LINK->L2TIMER = 1;		// USe retry to send SABM
			}
			return;
		}
	
		// Command on existing session. Could be due to other end missing
		// the XID response, so if XID just resend response

	}

	if (CTLlessPF == XID && (MSGFLAG & CMDBIT))
	{
		// XID Command on active session. Other end may be restarting. Send Response

		ProcessXIDCommand(LINK, Buffer, CTL, MSGFLAG);
		return;
	}


	if (CTLlessPF == SABM)
	{
		//	SABM ON EXISTING SESSION - IF DISCONNECTING, REJECT

		if (LINK->L2STATE == 1)			// Sent XID?
		{
			L2SABM(LINK, Buffer, MSGFLAG);			// Process the SABM
			return;
		}

		if (LINK->L2STATE == 4)			// DISCONNECTING?
		{
			L2SENDDM(LINK, Buffer);
			return;
		}

		//	THIS IS A SABM ON AN EXISTING SESSION

		//	THERE ARE SEVERAL POSSIBILITIES:

		//	1. RECONNECT COMMAND TO TNC
		//	2. OTHER END THINKS LINK HAS DIED
		//	3. RECOVERY FROM FRMR CONDITION
		//	4. REPEAT OF ORIGINAL SABM COS OTHER END MISSED UA

		//	FOR 1-3 IT IS REASONABLE TO FULLY RESET THE CIRCUIT, BUT IN 4
		//	SUCH ACTION WILL LOSE THE INITIAL SIGNON MSG IF CONNECTING TO A
		//	BBS. THE PROBLEM IS TELLING THE DIFFERENCE. I'M GOING TO SET A FLAG 
		//	WHEN FIRST INFO RECEIVED - IF SABM REPEATED BEFORE THIS, I'LL ASSUME
		//	CONDITION 4, AND JUST RESEND THE UA


		if (LINK->SESSACTIVE == 0)			// RESET OF ACTIVE CIRCUIT?
		{
			L2SENDUA(LINK, Buffer);			// No, so repeat UA
			return;
		}
		
		InformPartner(LINK, NORMALCLOSE);	// SEND DISC TO OTHER END

		L2SABM(LINK, Buffer, MSGFLAG);			// Process the SABM
		return;

	}

	L2_PROCESS(LINK, Buffer, CTL, MSGFLAG);
}


VOID L2SABM(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR MSGFLAG)
{
	//	SET UP NEW SESSION (OR RESET EXISTING ONE)

	if (LINK == 0)			// NO LINK ENTRIES - SEND DM RESPONSE
	{
		L2SENDDM(LINK, Buffer);
		return;
	}

	SETUPNEWL2SESSION(LINK, Buffer, MSGFLAG);

	if (LINK->L2STATE != 5)			// Setup OK?
	{
		L2SENDDM(LINK, Buffer);		// Failed
		return;
	}

	L2SENDUA(LINK, Buffer);
	return;
}

VOID SETUPNEWL2SESSION(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR MSGFLAG)
{
	//	COPY ADDRESS INFO TO LINK TABLE

	UCHAR * ptr1, * ptr2;
	UCHAR TEMPDIGI[57];
	int n;

	memcpy(LINK->LINKCALL, Buffer->ORIGIN, 7);
	LINK->LINKCALL[6] &= 0x1e;		// Mask SSID

	memcpy(LINK->OURCALL, Buffer->DEST, 7);
	LINK->OURCALL[6] &= 0x1e;		// Mask SSID

	memset(LINK->DIGIS, 0, 56);		// CLEAR DIGI FIELD IN CASE RECONNECT

	LINK->L2TIME = PORTT1;	// Set timeout for no digis

	if ((Buffer->ORIGIN[6] & 1) == 0)	// End of Address
	{
		//	THERE ARE DIGIS TO PROCESS - COPY TO WORK AREA reversed, THEN COPY BACK
	
		memset(TEMPDIGI, 0, 57);		// CLEAR DIGI FIELD IN CASE RECONNECT

		ptr1 = &Buffer->ORIGIN[6];		// End of add 
		ptr2 = &TEMPDIGI[7 * 7];		// Last Temp Digi

		while((*ptr1 & 1) == 0)			// End of address bit
		{
			ptr1++;
			memcpy(ptr2, ptr1, 7);
			ptr2[6] &= 0x1e;			// Mask Repeated and Last bits
			ptr2 -= 7;
			ptr1 += 6;
		}
		
		//	LIST OF DIGI CALLS COMPLETE - COPY TO LINK CONTROL ENTRY

		n = PORTMAXDIGIS;

		ptr1 = ptr2 + 7;				// First in TEMPDIGIS
		ptr2 = &LINK->DIGIS[0];

		while (*ptr1)
		{
			if (n == 0)
			{
				// Too many for us
				
				CLEAROUTLINK(LINK);
				return;
			}

			memcpy(ptr2, ptr1, 7);
			ptr1 += 7;
			ptr2 += 7;
			n--;

			LINK->L2TIME += PORTT1;	// Adjust timeout for digis
		}
	}

	//	THIS MAY BE RESETTING A LINK - BEWARE OF CONVERTING A CROSSLINK TO 
	//	AN UPLINK AND CONFUSING EVERYTHING


	if (LINK->LINKTYPE == 0)
		LINK->LINKTYPE = 1;			// Uplink

	LINK->L2TIMER = 0;				// CANCEL TIMER

	LINK->L2SLOTIM = T3;			// SET FRAME SENT RECENTLY

	LINK->LINKWINDOW = initMaxFrame;

	RESET2(LINK);					// RESET ALL FLAGS

	LINK->L2STATE = 5;

	LINK->ReportFlags |= Incomming;

}

VOID L2SENDUA(struct _LINKTABLE * LINK, MESSAGE * Buffer)
{
	L2SENDRESP(LINK, Buffer, UA);
}

VOID L2SENDDM(struct _LINKTABLE * LINK, MESSAGE * Buffer)
{
	L2SENDRESP(LINK, Buffer, DM);
}

VOID L2SENDRESP(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR CTL)
{
	//	QUEUE RESPONSE TO PORT CONTROL - MAY NOT HAVE A LINK ENTRY 

	int timeSinceDecoded = Now - DecodeCompleteTime;

	WriteDebugLog(LOGDEBUG, "Time since received = %d", timeSinceDecoded);

	//	SET APPROPRIATE P/F BIT 

	Buffer->CTL = CTL | PFBIT;

	L2SWAPADDRESSES(Buffer);			// SWAP ADDRESSES AND SET RESP BITS

// We should only send RR(RF) but in any case
// should delay a bit for link turnround,

// ?? is this best done by extending header or waiting ??

	if (timeSinceDecoded < 250)
		txSleep(250 - timeSinceDecoded);

	PUT_ON_PORT_Q(LINK, Buffer);

	return;
}


VOID L2SENDINVALIDCTRL(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR CTL)
{
	// Send FRMR Invalid Control field
	
	//	QUEUE RESPONSE TO PORT CONTROL - MAY NOT HAVE A LINK ENTRY 

	//	SET APPROPRIATE P/F BIT 

	UCHAR * ptr;

	Buffer->CTL = FRMR | PFBIT;
	
	ptr = &Buffer->PID;

	*(ptr++) = CTL;	// MOVE REJECT C-BYTE
	*(ptr++) = 0;
	*(ptr++) = SDINVC;			// MOVE REJECT FLAGS

 	Buffer->DataLen = 2;

	L2SWAPADDRESSES(Buffer);			// SWAP ADDRESSES AND SET RESP BITS

	PUT_ON_PORT_Q(LINK, Buffer);

	return;
}

VOID L2SWAPADDRESSES(MESSAGE * Buffer)
{
	//	EXCHANGE ORIGIN AND DEST, AND REVERSE DIGIS (IF PRESENT)

	UCHAR * ptr1, * ptr2;
	UCHAR TEMPDIGI[57];
	UCHAR * TEMP;
	TEMP = Buffer->ORIGIN;
	Buffer->ORIGIN = Buffer->DEST;
	Buffer->DEST = TEMP;

	Buffer->ORIGIN[6] &= 0x1e;			// Mask SSID
	Buffer->ORIGIN[6] |= 0xe0;			// Reserved and Response

	Buffer->DEST[6] &= 0x1e;			// Mask SSID
	Buffer->DEST[6] |= 0x60;			// Reserved 

	if (Buffer->DIGIS)
	{
		//	THERE ARE DIGIS TO PROCESS - COPY TO WORK AREA reversed, THEN COPY BACK
	
		memset(TEMPDIGI, 0, 57);		// CLEAR DIGI FIELD IN CASE RECONNECT

		ptr1 = &Buffer->ORIGIN[6];		// End of add 
		ptr2 = &TEMPDIGI[7 * 7];		// Last Temp Digi

		while((*ptr1 & 1) == 0)			// End of address bit
		{
			ptr1++;
			memcpy(ptr2, ptr1, 7);
			ptr2[6] &= 0x1e;			// Mask Repeated and Last bits
			ptr2 -= 7;
			ptr1 += 6;
		}
		
		//	LIST OF DIGI CALLS COMPLETE - copy back

		ptr1 = ptr2 + 7;				// First in TEMPDIGIS
		ptr2 = &Buffer->CTL;

		while (*ptr1)
		{
			memcpy(ptr2, ptr1, 7);
			ptr1 += 7;
			ptr2 += 7;
		}

		*(ptr2 - 1) |= 1;				// End of addresses
	}
	else
	{
		Buffer->ORIGIN[6] |= 1;			// End of address
	}
}

VOID L2_PROCESS(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR CTL, UCHAR MSGFLAG)
{
	//	PROCESS LEVEL 2 PROTOCOL STUFF

	//	SEE IF COMMAND OR RESPONSE

	if ((MSGFLAG & CMDBIT) == 0)
	{
		// RESPONSE

		//	IF RETRYING, MUST ONLY ACCEPT RESPONSES WITH F SET

		if (CTL & PFBIT)
		{
			//	F SET or CAN CANCEL TIMER

			LINK->L2TIMER = 0;			// CANCEL LINK TIMER
		}
	}

	if (LINK->L2STATE == 3)
	{
	
	//	FRMR STATE - IF C(P) SEND FRMR, ELSE IGNORE

		if (CTL & PFBIT)
		{
			if (CTL == (FRMR | PFBIT))	//	if both ends in FRMR state, reset link
			{
				RESET2(LINK);

				LINK->L2STATE = 2;				// INITIALISING
				LINK->L2RETRIES = 0;			// ALLOW FULL RETRY COUNT FOR SABM

				L2SENDCOMMAND(LINK, SABM | PFBIT);
			}
		}

		if (MSGFLAG & CMDBIT)
		{
			//	SEND FRMR AGAIN

			SENDFRMR(LINK);
		}
		return;
	}

	if (LINK->L2STATE >= 5)
	{
		//	LINK IN STATE 5 OR ABOVE - LINK RUNNING

		if ((CTL & 1) == 0)			// I frame
		{
			SDIFRM(LINK, Buffer, CTL, MSGFLAG);			// consumes buffer
			return;
		}
	
		if ((CTL & 2))			// U frame
		{
			SDUFRM(LINK, Buffer, CTL);					//consumes buffer
			return;
		}

		// ELSE SUPERVISORY, MASK OFF N(R) AND P-BIT
	
		switch (CTL & 0x0f)
		{
			// is there any harm in accepoting SREJ even if we don't
			// otherwise support 2.2?

		case REJ:
		case SREJ:
			
			LINK->L2REJCOUNT++;

		case RR:
		case RNR:
				
			SFRAME(LINK, CTL, MSGFLAG);
			break;
		
		default:
			
			//	UNRECOGNISABLE COMMAND

			LINK->SDRBYTE = CTL;			// SAVE FOR FRMR RESPONSE
 			LINK->SDREJF |= SDINVC;			// SET INVALID COMMAND REJECT
			SDFRMR(LINK);				// PROCESS FRAME REJECT CONDITION
		}
		
		return;
	}

	//	NORMAL DISCONNECT MODE

	//	COULD BE UA, DM - SABM AND DISC HANDLED ABOVE

	switch (CTL & ~PFBIT)
	{
	case UA:

		//	UA RECEIVED
			
		if (LINK->L2STATE == 2)
		{
			//	RESPONSE TO SABM - SET LINK  UP

			RESET2X(LINK);			// LEAVE QUEUED STUFF

			LINK->L2STATE = 5;
			LINK->L2TIMER = 0;		// CANCEL TIMER
			LINK->L2RETRIES = 0;
			LINK->L2SLOTIM = T3;		// SET FRAME SENT RECENTLY

			//	TELL PARTNER CONNECTION IS ESTABLISHED
			
			SENDCONNECTREPLY(LINK);
			return;
		}

		if (LINK->L2STATE == 4)				// DISCONNECTING?
		{
			DisplaySessionStats(LINK, NORMALCLOSE);
			InformPartner(LINK, NORMALCLOSE);	// SEND DISC TO OTHER END
			CLEAROUTLINK(LINK);
		}

		//	UA, BUT NOT IN STATE 2 OR 4 - IGNORE

		return;
	
	case DM:
			
		//	DM RESPONSE - IF TO SABM, SEND BUSY MSG

		if (LINK->L2STATE == 2)
		{
			CONNECTREFUSED(LINK);	// SEND MESSAGE IF DOWNLINK
			return;
		}
			
		//	DM ESP TO DISC RECEIVED - OTHER END HAS LOST SESSION

		//	CLEAR OUT TABLE ENTRY - IF INTERNAL TNC, SHOULD SEND *** DISCONNECTED

		DisplaySessionStats(LINK, LINKLOST);
		InformPartner(LINK, LINKLOST);		// SEND DISC TO OTHER END
		CLEAROUTLINK(LINK);
		return;

	case FRMR:

	//	FRAME REJECT RECEIVED - LOG IT AND RESET LINK

		RESET2(LINK);
	
		LINK->L2STATE = 2;				// INITIALISING
		LINK->L2RETRIES = 0;			// ALLOW FULL RETRY COUNT FOR SABM

		LINK->L2FRMRRX++;

		L2SENDCOMMAND(LINK, SABM | PFBIT);
		return;

	default:
			
		//	ANY OTHER - IGNORE

		return;
	}
}
			
VOID SDUFRM(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR CTL)
{
	//	PROCESS AN UNSEQUENCED COMMAND (IN LINK UP STATES)

	switch (CTL & ~PFBIT)
	{
	case UA:

		// DISCARD - PROBABLY REPEAT OF  ACK OF SABM

		break;

	case FRMR:

		//	FRAME REJECT RECEIVED - LOG IT AND RESET LINK

		RESET2(LINK);

		LINK->L2STATE = 2;				// INITIALISING
		LINK->L2RETRIES = 0;			// ALLOW FULL RETRY COUNT FOR SABM

		LINK->L2FRMRRX++;

		L2SENDCOMMAND(LINK, SABM | PFBIT);
		break;

	case DM:

		// DM RESPONSE - SESSION MUST HAVE GONE

		//	SEE IF CROSSLINK ACTIVE

		InformPartner(LINK, LINKLOST);		// SEND DISC TO OTHER END
		CLEAROUTLINK(LINK);	
		break;

	default:

		//	UNDEFINED COMMAND

		LINK->SDRBYTE = CTL;			// SAVE FOR FRMR RESPONSE
		LINK->SDREJF |= SDINVC;
		SDFRMR(LINK);		// PROCESS FRAME REJECT CONDITION

	}
}
	

VOID SFRAME(struct _LINKTABLE * LINK, UCHAR CTL, UCHAR MSGFLAG)
{
	//	CHECK COUNTS, AND IF RNR INDICATE _BUFFER SHORTAGE AT OTHER END

	if (LINK->SDREJF)			// ARE ANY REJECT FLAGS SET?
	{
		SDFRMR(LINK);		// PROCESS FRAME REJECT CONDITION
		return;
	}

	SDNRCHK(LINK, CTL);			// CHECK RECEIVED N(R)

	if (LINK->SDREJF)			// ARE ANY REJECT FLAGS SET NOW?
	{
		SDFRMR(LINK);		// PROCESS FRAME REJECT CONDITION
		return;
	}

	if ((CTL & 0xf) == SREJ)
	{
		// Probably safer to handle SREJ completely separately

		// Can we get SREJ Command with P??(Yes)

		// Can we just resend missing frame ?? (Think so!)

		// We support MultiSREJ (can gave additional missing frame
		// numbers in the Info field

		// I don't see the point of Multi unless we wait fot an F bit,
		// bur maybe not safe to assume others do the same

		// So if I get SREJ(F) I can send missing frame(s)

		if (MSGFLAG & RESP)
		{
			// SREJ Response

			if (CTL & PFBIT)
			{
				// SREJ(F). Send Frames()

				UCHAR NS = (CTL >> 5) & 7;		// Frame to resend
				UCHAR CTL;
				MESSAGE Buffer;
				UCHAR Data[256];
				Buffer.L2DATA = Data;

				WriteDebugLog(LOGINFO, "Got SREJ, resending Frame Retries = %d", LINK->L2RETRIES);
		
				if (LINK->FRAMES[NS].DataLen == 0)	// is frame available?
					return;				// Wot!!

				// send the frame

				SETUPADDRESSES(LINK, &Buffer);	// copy addresses

				// ptr2  NOW POINTS TO COMMAND BYTE

				//	GOING TO SEND I FRAME - WILL ACK ANY RECEIVED FRAMES

				LINK->L2SLOTIM = T3 + rand() % 255;		// SET FRAME SENT RECENTLY
				LINK->KILLTIMER = 0;		// RESET IDLE CIRCUIT TIMER

				CTL = LINK->LINKNR << 5;	// GET CURRENT N(R), SHIFT IT TO TOP 3 BITS			
				CTL |= NS << 1;	// BITS 1-3 OF CONTROL BYTE

				//	SET P BIT IF NO MORE TO SEND (only more if Multi SREJ)

				CTL |= PFBIT;
				LINK->L2FLAGS |= POLLSENT;
				LINK->L2TIMER = LINK->L2TIME;	// (RE)SET TIMER
		
				Buffer.CTL = CTL;		// TO DATA (STARTING WITH PID)
				Buffer.PID = 0xf0;

				Buffer.DataLen = LINK->FRAMES[NS].DataLen;
	
				memcpy(Data, LINK->FRAMES[NS].Data, LINK->FRAMES[NS].DataLen);

				Buffer.DEST[6] |= 0x80;		// SET COMMAND
				Buffer.ORIGIN[6] &= 0x7F;	// SET COMMAND

				LINK->L2TIMER = LINK->L2TIME;	// (RE)SET TIMER

				PUT_ON_PORT_Q(LINK, &Buffer);
			}
		}

		return;
	}

	//	VALID RR/RNR RECEIVED

	LINK->L2FLAGS &= ~RNRSET;		//CLEAR RNR

	if ((CTL & 0xf) == RNR)
		LINK->L2FLAGS |= RNRSET;	//Set RNR

	if (MSGFLAG & CMDBIT)
	{
		//	ALWAYS REPLY TO RR/RNR/REJ COMMAND (even if no P bit ??)

		//	FIRST PROCESS RESEQ QUEUE

		//;	CALL	PROCESS_RESEQ

		// IGNORE IF AN 'F' HAS BEEN SENT RECENTLY

		if (LINK->LAST_F_TIME + 150U > getTicks())
			return;			// DISCARD

		CTL = RR_OR_RNR(LINK);

		CTL |= LINK->LINKNR << 5;	// SHIFT N(R) TO TOP 3 BITS
		CTL |= PFBIT;

		L2SENDRESPONSE(LINK, CTL);

		LINK->L2SLOTIM = T3 + rand() % 255;	// SET FRAME SENT RECENTLY	
	
		//	SAVE TIME IF 'F' SENT'

		LINK->LAST_F_TIME = getTicks();

		return;
	}

	// Response

	if ((CTL & PFBIT) == 0)
	{
		//	RESPONSE WITHOUT P/F DONT RESET N(S) (UNLESS V1)
	
		return;
	}

	//	RESPONSE WITH P/F - MUST BE REPLY TO POLL FOLLOWING TIMEOUT OR I(P)

	LINK->L2FLAGS &= ~POLLSENT;				// CLEAR I(P) SET

	//	THERE IS A PROBLEM WITH REPEATED RR(F), SAY CAUSED BY DELAY AT L1

	//	AS FAR AS I CAN SEE, WE SHOULD ONLY RESET N(S) IF AN RR(F) FOLLOWS
	//	AN RR(P) AFTER A TIMEOUT - AN RR(F) FOLLOWING AN I(P) CANT POSSIBLY
	//	INDICATE A LOST FRAME. ON THE OTHER HAND, A REJ(F) MUST INDICATE
	//	A LOST FRAME. So dont reset NS if not retrying, unless REJ

	if ((CTL & 0xf) == REJ || LINK->L2RETRIES)
	{
		if (LINK->LINKNS == ((CTL >> 5) & 7))	// All Acked
			LINK->L2RETRIES = 0;

		RESETNS(LINK, (CTL >> 5) & 7);	// RESET N(S) AND COUNT RETRIED FRAMES
		LINK->L2TIMER = 0;			// WILL RESTART TIMER WHEN RETRY SENT
	}

	if ((CTL & 0xf) == RNR)
	{
		//	Dont Clear timer on receipt of RNR(F), spec says should poll for clearing of busy,
		//	and loss of subsequent RR will cause hang. Perhaps should set slightly longer time??
		//	Timer may have been cleared earlier, so restart it

		LINK->L2TIMER = LINK->L2TIME;
	}
}

//***	PROCESS AN INFORMATION FRAME


VOID SDIFRM(struct _LINKTABLE * LINK, MESSAGE * Buffer, UCHAR CTL, UCHAR MSGFLAG)
{
	int NS;
	
	if (LINK->SDREJF)			// ARE ANY REJECT FLAGS SET?
	{
		SDFRMR(LINK);		// PROCESS FRAME REJECT CONDITION
		return;
	}

	SDNRCHK(LINK, CTL);				// CHECK RECEIVED N(R)

	if (LINK->SDREJF)			// ARE ANY REJECT FLAGS SET NOW?
	{
		SDFRMR(LINK);		// PROCESS FRAME REJECT CONDITION
		return;
	}

	LINK->SESSACTIVE = 1;		// SESSION IS DEFINITELY SET UP

	NS = (CTL >> 1) & 7;			// ISOLATE RECEIVED N(S)
	
CheckNSLoop:

	if (NS != LINK->LINKNR)		// EQUAL TO OUR N(R)?
	{
		// There is a frame missing.
		// if we have just sent a REJ we have at least one out
		// of sequence frame in RXFRAMES

		// so if we have frame LINK->LINKNR we can process it
		// and remove it from RXFRAMES. If we are then back 
		// in sequence we just carry on.

		if (LINK->RXFRAMES[LINK->LINKNR].DataLen)
		{
			// We have the first missing frame. Process it.
	
			struct _DATABUFFER * OldBuffer = &LINK->RXFRAMES[LINK->LINKNR];
			WriteDebugLog(LOGDEBUG, "L2 process saved Frame %d", LINK->LINKNR);
			PROC_I_FRAME(LINK, OldBuffer->Data, OldBuffer->DataLen);

			OldBuffer->DataLen = 0;
			free(OldBuffer->Data);

			// NR has been updated.

			goto CheckNSLoop;		// See if OK or we have another saved frame
		}
		
		//	BAD FRAME, SEND SREJ nexr time polled
		//	SAVE THE FRAME 


		LINK->L2OUTOFSEQ++;
		LINK->L2STATE = 6;

		//	IF RUNNING VER1, AND OTHER END MISSES THIS REJ, LINK WILL FAIL
		//	SO TIME OUT REJ SENT STATE (MUST KEEP IT FOR A WHILE TO AVOID
		//	'MULTIPLE REJ' PROBLEM)
	
		if (LINK->RXFRAMES[NS].DataLen)
		{
			// Already have a copy, so discard this one
			
			WriteDebugLog(LOGDEBUG, "Frame %d out of seq but already have copy - release", NS);
		}
		else
		{
			WriteDebugLog(LOGDEBUG, "Frame %d out of seq - save", NS);
			LINK->RXFRAMES[NS].DataLen = Buffer->DataLen;
			LINK->RXFRAMES[NS].Data = malloc(Buffer->DataLen);
			memcpy(LINK->RXFRAMES[NS].Data, Buffer->L2DATA, Buffer->DataLen);
		}
		goto CheckPF;
	}

	// IN SEQUENCE FRAME 
	
	// Remove any stored frame with this seq

	if (LINK->RXFRAMES[NS].DataLen)
	{
		free(LINK->RXFRAMES[NS].Data);
		LINK->RXFRAMES[NS].DataLen = 0;
	}
	if (LINK->L2STATE == 6)				// REJ?
	{
		// If using REJ we can cancel REJ state.
		// If using SREJ we only cancel REJ if we have no stored frames
	
		if (LINK->UseSREJ)
		{
			// see if any frames saved. 

			int i;

			for (i = 0; i < 8; i++)
			{
				if (LINK->RXFRAMES[i].DataLen)
					goto stayinREJ;
			}
			// Drop through if no stored frames
		}

		// CANCEL REJ

		LINK->L2STATE = 5;
		LINK->L2FLAGS &= ~REJSENT;
	}

stayinREJ:

	PROC_I_FRAME(LINK, Buffer->L2DATA, Buffer->DataLen);		// Passes on  or releases Buffer


CheckPF:

	if (LINK->UseSREJ == 0)			// Unless using SREJ
		if (LINK->L2FLAGS & REJSENT)
			return;						// DONT SEND ANOTHER TILL REJ IS CANCELLED

	if (CTL & PFBIT)
	{
		if (LINK->L2STATE == 6)
			LINK->L2FLAGS |= REJSENT;	// Set "REJ Sent"

		SEND_RR_RESP(LINK, PFBIT);
	
		//	RECORD TIME

		LINK->LAST_F_TIME = getTicks();
	}
}


VOID PROC_I_FRAME(struct _LINKTABLE * LINK, UCHAR * Data, int DataLen)
{

	//	ATTACH I FRAMES TO LINK TABLE RX QUEUE - ONLY DATA IS ADDED (NOT ADDRESSES)

	//	IF DISC PENDING SET, IGNORE FRAME

	if (LINK->L2FLAGS & DISCPENDING)
	{
		LINK->LINKNR++;				// INCREMENT OUR N(R)
		LINK->LINKNR &= 7;			//  MODULO 8
		return;
	}

/*
	switch(PID)
	{
	case 0xcc:
	case 0xcd:
		
		// IP Message

		if (n < 8)			// If digis, move data back down buffer
		{
			memmove(&Buffer->PID, &EOA[2], Length);
			Buffer->LENGTH -= (&EOA[2] - &Buffer->PID);
		}

		break;

	case 8:

		// NOS FRAGMENTED IP

		if (n < 8)			// If digis, move data back down buffer
		{
			memmove(&Buffer->PID, &EOA[2], Length);
			Buffer->LENGTH -= (&EOA[2] - &Buffer->PID);
		}

		C_Q_ADD(&LINK->L2FRAG_Q, Buffer);

		if (Buffer->L2DATA[0] == 0)
		{
			//	THERE IS A WHOLE MESSAGE ON FRAG_Q - PASS TO IP

			while(LINK->L2FRAG_Q)
			{
				Buffer = Q_REM(&LINK->L2FRAG_Q);
			}
		}
		break;

	default:

		if (Length < 1 || Length > 257)
		{
			return;
		}

		// Copy Data back over

		memmove(&Msg->PID, Info, Length);

		Buffer->LENGTH = Length + MSGHDDRLEN;
 
		C_Q_ADD(&LINK->RX_Q, Buffer);
	}
	*/

	// Copy Data to RXBuffer, extending if necessary

	if (LINK->RXCount + DataLen > LINK->RXBuffersize)
	{
		// Extend

		LINK->RXBuffersize += 512;
		
		LINK->RXBuffer = realloc(LINK->RXBuffer, LINK->RXBuffersize);

		if (LINK->RXBuffer == NULL)
		{
			// Not sure - should we just ignore the frame and try to allocate when retry happens?

			LINK->RXBuffersize = 0;
			return;
		}
	}

	memcpy(&LINK->RXBuffer[LINK->RXCount], Data, DataLen);
	LINK->RXCount += DataLen;

	LINK->BytesReceived += DataLen;
	LINK->PacketsReceived ++;

	LINK->LINKNR++;				// INCREMENT OUR N(R)
	LINK->LINKNR &= 7;			//  MODULO 8

	LINK->KILLTIMER = 0;		// RESET IDLE LINK TIMER
}

//***	CHECK RECEIVED N(R) COUNT

VOID SDNRCHK(struct _LINKTABLE * LINK, UCHAR CTL)
{
	UCHAR NR = (CTL >> 5) & 7;

	if (NR >= LINK->LINKWS)			// N(R) >= WINDOW START?
	{
		//	N(R) ABOVE OR EQUAL TO WINDOW START - OK IF NOT ABOVE N(S), OR N(S) BELOW WS

		if (NR > LINK->LINKNS)			// N(R) <= WINDOW END?
		{
			//	N(R) ABOVE N(S) - DOES COUNT WRAP?

			if (LINK->LINKNS >= LINK->LINKWS)		// Doesnt wrap
				goto BadNR;
		}

GoodNR:
		
		if ((CTL & 0x0f) == SREJ)
			if ((CTL & PFBIT) == 0)
				return;				// SREJ without F doesn't ACK anything

		LINK->LINKWS = NR;		// NEW WINDOW START = RECEIVED N(R)
		ACKMSG(LINK);			// Remove any acked messages
		return;
	}

	//	N(R) LESS THAN WINDOW START - ONLY OK IF WINDOW	WRAPS

	if (NR <= LINK->LINKNS)				// N(R) <= WINDOW END?
		goto GoodNR;

BadNR:

	//	RECEIVED N(R) IS INVALID
	
	LINK->SDREJF |= SDNRER;			// FLAG A REJECT CONDITION
	LINK->SDRBYTE = CTL;			// SAVE FOR FRMR RESPONSE
}

VOID RESETNS(struct _LINKTABLE * LINK, UCHAR NS)
{
	int Resent = (LINK->LINKNS - NS) & 7;	// FRAMES TO RESEND

	LINK->LINKNS = NS;			// RESET N(S)
}

int COUNT_AT_L2(struct _LINKTABLE * LINK);

//***	RESET HDLC AND PURGE ALL QUEUES ETC.

VOID RESET2X(struct _LINKTABLE * LINK)
{
	LINK->SDREJF = 0;			// CLEAR FRAME REJECT FLAGS
	LINK->LINKWS = 0;			// CLEAR WINDOW POINTERS
	LINK->LINKOWS = 0;
	LINK->LINKNR = 0;			// CLEAR N(R)
	LINK->LINKNS = 0;			// CLEAR N(S)
	LINK->SDTSLOT= 0;
	LINK->L2STATE = 5;			// RESET STATE
	LINK->L2FLAGS = 0;
}


VOID CLEARL2QUEUES(struct _LINKTABLE * LINK)
{
	//	GET RID OF ALL FRAMES THAT ARE QUEUED

	int n = 0;

	while (n < 8)
	{
		if (LINK->FRAMES[n].DataLen)
		{
			LINK->FRAMES[n].DataLen = 0;
		}
		if (LINK->RXFRAMES[n].DataLen)
		{
			free(LINK->RXFRAMES[n].Data);
			LINK->RXFRAMES[n].DataLen = 0;
		}

		n++;
	}

	//	GET RID OF ALL FRAMES THAT ARE
	//	QUEUED ON THE TX HOLDING QUEUE, RX QUEUE AND LEVEL 3 QUEUE


	LINK->TXCount = 0;
	LINK->RXCount = 0;

}

VOID RESET2(struct _LINKTABLE * LINK)
{
	CLEARL2QUEUES(LINK);
	RESET2X(LINK);
}

VOID SENDSABM(struct _LINKTABLE * LINK)
{
	L2SENDCOMMAND(LINK, SABM | PFBIT);
}


#define FEND 0xC0 
#define FESC 0xDB
#define TFEND 0xDC
#define TFESC 0xDD


VOID PutKISS(UCHAR c)
{
	if (c == FEND)
	{
		ProcessKISSByte(FESC);
		ProcessKISSByte(TFEND);
	}
	else if (c == FESC)
	{
		ProcessKISSByte(FESC);
		ProcessKISSByte(TFESC);
	} 
	else
		ProcessKISSByte(c);
}


VOID PUT_ON_PORT_Q(struct _LINKTABLE * LINK, MESSAGE * Buffer)
{
	// Copy Message to cyclic TX Buffer. Code expects to see KISS frames in buffer,
	// so KISS Encode

	UCHAR * ptr;
	int n;
	int timeSinceDecoded = Now - DecodeCompleteTime;

	// Allow for link turnround before responding

	if (timeSinceDecoded < 250)
	{
		txSleep(250 - timeSinceDecoded);
		DecodeCompleteTime = Now;			// so we dont add extra delay for back to back
	}

	// Send Set Mode Packet then the message

	if (LINK)
	{
		ProcessKISSByte(FEND);
		ProcessKISSByte(6);
		if (Buffer->DataLen > 0)
			ProcessKISSByte(LINK->pktMode);
		else
		{
			if (Buffer->CTL == 0x3f || Buffer->CTL == 0x73)		// SABM or 
				ProcessKISSByte(SABMMode);	
			else
				ProcessKISSByte(CtlMode[LINK->pktMode]);
		}

		ProcessKISSByte(FEND);
	}

	ProcessKISSByte(FEND);
	ProcessKISSByte(0);

	ptr = Buffer->DEST;

	for (n = 0; n < 7; n++)
		PutKISS(*ptr++);
			
	ptr = Buffer->ORIGIN;
	for (n = 0; n < 7; n++)
		PutKISS(*ptr++);

	PutKISS(Buffer->CTL);

	if (Buffer->DataLen > 0)
	{
		PutKISS(Buffer->PID);

		ptr = Buffer->L2DATA;
		for (n = 0; n < Buffer->DataLen; n++)
			PutKISS(*ptr++);
	}
	ProcessKISSByte(FEND);
}

UCHAR * SETUPADDRESSES(struct _LINKTABLE * LINK, PMESSAGE Msg)
{
	//	COPY ADDRESSES FROM LINK TABLE TO MESSAGE _BUFFER

	UCHAR * ptr1 = &LINK->DIGIS[0];
	UCHAR * ptr2 = &Msg->CTL;
	int Digis  = 8;

	Msg->DEST = &LINK->LINKCALL[0]; // COPY DEST AND ORIGIN
	Msg->ORIGIN = &LINK->OURCALL[0]; 

	Msg->DEST[6] |= 0x60;
	Msg->ORIGIN[6] |= 0x60;

	Msg->DIGIS = 0;

/*	while (Digis)
	{
		if (*(ptr1))			// any more to copy?
		{
			memcpy(ptr2, ptr1, 7);
			ptr1 += 7;
			ptr2 += 7;
			Digis--;
		}
		else
			break;
	}
*/
	Msg->ORIGIN[6] |= 1;

	return ptr2;			// Pointer to CTL
}


VOID SDETX(struct _LINKTABLE * LINK)
{
	// Start sending frsmes if possible

	int Outstanding;
	UCHAR CTL;
	MESSAGE Buffer;
	UCHAR Data[256];

	Buffer.L2DATA = Data;

	//	DONT SEND IF RESEQUENCING RECEIVED FRAMES - CAN CAUSE FRMR PROBLEMS

//	if (LINK->L2RESEQ_Q)
//		return;
	
	Outstanding = LINK->LINKWS - LINK->LINKOWS;

	if (Outstanding < 0)
		Outstanding += 8;		// allow fro wrap

	if (Outstanding >= LINK->LINKWINDOW)		// LIMIT
		return;

	//	See if we can load any more frames into the frame holding q

	while (LINK->TXCount && LINK->FRAMES[LINK->SDTSLOT].DataLen == 0)
	{
		int Len = LINK->TXCount;
		
		if (Len > LINK->PACLEN)
			Len = LINK->PACLEN;
		
		LINK->FRAMES[LINK->SDTSLOT].DataLen = Len;
		memcpy(LINK->FRAMES[LINK->SDTSLOT].Data, LINK->TXBuffer, Len);
		LINK->TXCount -= Len;

		if (LINK->TXCount)
			memmove(LINK->TXBuffer, &LINK->TXBuffer[Len], LINK->TXCount);

		LINK->SDTSLOT ++;
		LINK->SDTSLOT &= 7;

		LINK->BytesSent += Len;
		LINK->PacketsSent++;
	}
	
	// dont send while poll outstanding

	while ((LINK->L2FLAGS & POLLSENT) == 0)
	{
		if (LINK->FRAMES[LINK->LINKNS].DataLen == 0)	// is next frame available?
			return;

		// send the frame


		SETUPADDRESSES(LINK, &Buffer);	// copy addresses

		//	GOING TO SEND I FRAME - WILL ACK ANY RECEIVED FRAMES

		LINK->L2SLOTIM = T3 + rand() % 255;		// SET FRAME SENT RECENTLY
		LINK->KILLTIMER = 0;		// RESET IDLE CIRCUIT TIMER

		CTL = LINK->LINKNR << 5;	// GET CURRENT N(R), SHIFT IT TO TOP 3 BITS			
		CTL |= LINK->LINKNS << 1;	// BITS 1-3 OF CONTROL BYTE

		Buffer.DataLen = LINK->FRAMES[LINK->LINKNS].DataLen;
		memcpy(Data, LINK->FRAMES[LINK->LINKNS].Data, Buffer.DataLen);

		LINK->LINKNS++;				// INCREMENT NS
		LINK->LINKNS &= 7;			// mod 8

		//	SET P BIT IF END OF WINDOW OR NO MORE TO SEND

		Outstanding = LINK->LINKNS - LINK->LINKOWS;

		if (Outstanding < 0)
			Outstanding += 8;		// allow for wrap

		// if at limit, or no more to send, set P)
	
		if (Outstanding >= LINK->LINKWINDOW || LINK->FRAMES[LINK->LINKNS].DataLen == 0)
		{
			CTL |= PFBIT;
			LINK->L2FLAGS |= POLLSENT;
			LINK->L2TIMER = LINK->L2TIME;	// (RE)SET TIMER
		}
	
		Buffer.CTL = CTL;		// TO DATA (STARTING WITH PID)

		Buffer.DEST[6] |= 0x80;		// SET COMMAND
		Buffer.ORIGIN[6] &= 0x7F;	// SET COMMAND
		Buffer.PID = 0xF0;

		LINK->L2TIMER = LINK->L2TIME;	// (RE)SET TIMER

		PUT_ON_PORT_Q(LINK, &Buffer);

	}
}

VOID L2TimerProc()
{
	struct _LINKTABLE * LINK = LINKS;
	struct _LINKTABLE * THISLINK;

	while (LINK)
	{
		//	CHECK FOR TIMER EXPIRY OR BUSY CLEARED  

		if (LINK->L2STATE == 0)		// Shouldnt happen
		{
			LINK = LINK->Next;
			continue;
		}
	
		if (LINK->L2TIMER)
		{
			LINK->L2TIMER--;
			if (LINK->L2TIMER == 0)
			{
				THISLINK = LINK;		// Timer could remove link rom chain
				LINK = LINK->Next;
				L2TIMEOUT(THISLINK);
				continue;
			}		
		}
		else
		{
			// TIMER NOT RUNNING - MAKE SURE STATE NOT BELOW 5 - IF
			// IT IS, SOMETHING HAS GONE WRONG, AND LINK WILL HANG FOREVER

			if (LINK->L2STATE < 5 && LINK->L2STATE != 2 && LINK->L2STATE != 1)	// 2 = CONNECT - PROBABLY TO CQ
				LINK->L2TIMER = 2;							// ARBITRARY VALUE
		}

		//	TEST FOR RNR SENT, AND NOT STILL BUSY

		if (LINK->L2FLAGS & RNRSENT)
		{
			//	Was busy

			if (RR_OR_RNR(LINK) != RNR)		//  SEE IF STILL BUSY
			{
				// Not still busy - tell other end

				//	Just sending RR will hause a hang of RR is missed, and other end does not poll on Busy
				//	Try sending RR CP, so we will retry if not acked
	
				if (LINK->L2RETRIES == 0)			// IF RR(P) OUTSTANDING WILl REPORT ANYWAY
				{
					SendSupervisCmd(LINK);
					LINK = LINK->Next;
					continue;
				}
			}
		}
		else
		{
			// NOT	BUSY

		}

		//	CHECK FOR REJ TIMEOUT

		if (LINK->REJTIMER)
		{
			LINK->REJTIMER--;
			if (LINK->REJTIMER == 0)			// {REJ HAS TIMED OUT (THIS MUST BE A VERSION 1 SESSION)
			{
				// CANCEL REJ STATE

				if (LINK->L2STATE == 6)				// REJ?
					LINK->L2STATE = 5;				// CLEAR REJ 
			}
		}

		// See if time for link validation poll

		if (LINK->L2SLOTIM)
		{
			LINK->L2SLOTIM--;
			if (LINK->L2SLOTIM == 0)			// Time to poll
			{
				SendSupervisCmd(LINK);
				LINK = LINK->Next;
				continue;
			}
		}

		// See if idle too long

		LINK->KILLTIMER++;

		if (L2KILLTIME && LINK->KILLTIMER > L2KILLTIME)
		{
			// CIRCUIT HAS BEEN IDLE TOO LONG - SHUT IT DOWN

			LINK->KILLTIMER = 0;
			LINK->L2TIMER = 1;		// TO FORCE DISC
			LINK->L2STATE = 4;		// DISCONNECTING

			//	TELL OTHER LEVELS

			DisplaySessionStats(LINK, LINKIDLE);
			InformPartner(LINK, NORMALCLOSE);
		}
		LINK = LINK->Next;
	}
}

VOID SendSupervisCmd(struct _LINKTABLE * LINK)
{
	// Send Super Command RR/RNR/REJ(P)

	UCHAR CTL;
	
	//	SEND RR COMMAND - EITHER AS LINK VALIDATION POLL OR FOLLOWING TIMEOUT

	CTL = RR_OR_RNR(LINK);

//	MOV	L2STATE[EBX],5			; CANCEL REJ - ACTUALLY GOING TO 'PENDING ACK'

	CTL |= LINK->LINKNR << 5;	// SHIFT N(R) TO TOP 3 BITS
	CTL |= PFBIT;

	LINK->L2FLAGS |= POLLSENT;

	L2SENDCOMMAND(LINK, CTL);

	LINK->L2SLOTIM = T3 + rand() % 255;		// SET FRAME SENT RECENTLY	
}

void SEND_RR_RESP(struct _LINKTABLE * LINK, UCHAR PF)
{
	UCHAR CTL;
	
	CTL = RR_OR_RNR(LINK);

//	MOV	L2STATE[EBX],5			; CANCEL REJ - ACTUALLY GOING TO 'PENDING ACK'

	CTL |= LINK->LINKNR << 5;	// SHIFT N(R) TO TOP 3 BITS
	CTL |= PF;

	L2SENDRESPONSE(LINK, CTL);
	
	ACKMSG(LINK);		// SEE IF STILL WAITING FOR ACK
}

VOID ACKMSG(struct _LINKTABLE * LINK)
{
	//	RELEASE ANY ACKNOWLEDGED FRAMES

	while (LINK->LINKOWS != LINK->LINKWS)	// is OLD WINDOW START EQUAL TO NEW WINDOW START?
	{
		// No, so frames to ack

		if (LINK->FRAMES[LINK->LINKOWS].DataLen)
		{
			LINK->FRAMES[LINK->LINKOWS].DataLen = 0;
			LINK->ReportFlags |= ReportQueue;
		}
		else
		{
//			char Call1[12], Call2[12];

//			Call1[ConvFromAX25(LINK->LINKCALL, Call1)] = 0;
//			Call2[ConvFromAX25(LINK->OURCALL, Call2)] = 0;

//			Debugprintf("Missing frame to ack Seq %d Calls %s %s", LINK->LINKOWS, Call1, Call2);
		}

		LINK->LINKOWS++;			// INCREMENT OLD WINDOW START
		LINK->LINKOWS &= 7;			// MODULO 8

		//	SOMETHING HAS BEEN ACKED - RESET RETRY COUNTER

		if (LINK->L2RETRIES)
			LINK->L2RETRIES = 1;	// MUSTN'T SET TO ZERO - COULD CAUSE PREMATURE RETRANSMIT

	}

	if (LINK->LINKWS != LINK->LINKNS)		// IS N(S) = NEW WINDOW START?
	{
		//	NOT ALL I-FRAMES HAVE BEEN ACK'ED - RESTART TIMER

		LINK->L2TIMER = LINK->L2TIME;
		return;
	}

	//	ALL FRAMES HAVE BEEN ACKED - CANCEL TIMER UNLESS RETRYING
	//	   IF RETRYING, MUST ONLY CANCEL WHEN RR(F) RECEIVED

	if (LINK->L2RETRIES == 0)	 // STOP TIMER IF LEVEL 1 or not retrying
	{
		LINK->L2TIMER = 0;
		LINK->L2FLAGS &= ~POLLSENT;	// CLEAR I(P) SET (IN CASE TALKING TO OLD BPQ!)
	}

	//	IF DISCONNECT REQUEST OUTSTANDING, AND NO FRAMES ON TX QUEUE,  SEND DISC

	if (LINK->L2FLAGS & DISCPENDING && LINK->TXCount == 0)
	{
		LINK->L2FLAGS &=  ~DISCPENDING;

		LINK->L2TIMER = 1;		// USE TIMER TO SEND DISC
		LINK->L2STATE = 4;		// DISCONNECTING
	}
}

VOID CONNECTFAILED();
	
VOID L2TIMEOUT(struct _LINKTABLE * LINK)
{
	//	TIMER EXPIRED

	//	IF LINK UP (STATE 5 OR ABOVE) SEND RR/RNR AS REQUIRED
	//	IF S2, REPEAT SABM
	//  IF S3, REPEAT FRMR
	//  IF S4, REPEAT DISC


	LINK->L2TIMEOUTS++;			// FOR STATS	

	if (LINK->L2STATE == 0) 
		return;

	if (LINK->L2STATE == 1) 
	{
		//	XID

		LINK->L2RETRIES++;
		if (LINK->L2RETRIES >= PORTN2)
		{
			//	RETRIED N2 TIMES - Give up

			CONNECTFAILED(LINK);		// TELL LEVEL 4 IT FAILED
			CLEAROUTLINK(LINK);
			return;
		}

		L2SENDXID(LINK);
		return;
	}


	if (LINK->L2STATE == 2)
	{
		//	CONNECTING

		LINK->L2RETRIES++;
		if (LINK->L2RETRIES >= PORTN2)
		{
			//	RETRIED N2 TIMES - Give up

			CONNECTFAILED(LINK);		// TELL LEVEL 4 IT FAILED
			CLEAROUTLINK(LINK);
			return;
		}

		SENDSABM(LINK);
		return;
	}

	if (LINK->L2STATE == 4)
	{
		//	DISCONNECTING

		LINK->L2RETRIES++;
		if (LINK->L2RETRIES >= PORTN2)
		{
			//	RETRIED N2 TIMES - JUST CLEAR OUT LINK
		
			CLEAROUTLINK(LINK);
			return;
		}

		L2SENDCOMMAND(LINK, DISC | PFBIT);
		return;
	}
	
	if (LINK->L2STATE == 3)
	{
		//	FRMR

		LINK->L2RETRIES++;
		if (LINK->L2RETRIES >= PORTN2)
		{
			//	RETRIED N2 TIMES - RESET LINK

			LINK->L2RETRIES = 0;
			LINK->L2STATE = 2;
			SENDSABM(LINK);
			return;
		}
	}

	//	STATE 5 OR ABOVE

	//	SEND RR(P) UP TO N2 TIMES

	LINK->L2RETRIES++;
	if (LINK->L2RETRIES >= PORTN2)
	{
		//	RETRIED N TIMES SEND A COUPLE OF DISCS AND THEN CLOSE

		DisplaySessionStats(LINK, RETRIEDOUT);
		InformPartner(LINK, RETRIEDOUT);	// TELL OTHER END ITS GONE

		LINK->L2RETRIES =-2;
		LINK->L2STATE = 4;			// CLOSING

		L2SENDCOMMAND(LINK, DISC | PFBIT);
		return;
	}
	
	SendSupervisCmd(LINK);
}

VOID SDFRMR(struct _LINKTABLE * LINK)
{
	LINK->L2FRMRTX++;

	LINK->L2STATE = 3;				// ENTER FRMR STATE

	LINK->L2TIMER = LINK->L2TIME;	//SET TIMER

	SENDFRMR(LINK);
}

VOID SENDFRMR(struct _LINKTABLE * LINK)
{
	//	RESEND FRMR

	MESSAGE Buffer;
	UCHAR Data[4];

	Buffer.L2DATA = Data;

	SETUPL2MESSAGE(&Buffer, LINK, FRMR);

	Buffer.ORIGIN[6] |= 0x80;		// SET RESPONSE
	Buffer.DEST[6] &= 0x7F;	// SET COMMAND

	Buffer.PID = LINK->SDRBYTE;	// First Byte to PID, Rest to Data

	Data[0] = LINK->LINKNR << 5 | LINK->LINKNS << 1;

	Data[1] = LINK->SDREJF;			// MOVE REJECT FLAGS

	Buffer.DataLen = 2;

	PUT_ON_PORT_Q(LINK, &Buffer);

	return;
}

VOID CLEAROUTLINK(struct _LINKTABLE * OLDLINK)
{
	struct _LINKTABLE * LINK = LINKS;
	int i;

	CLEARL2QUEUES(OLDLINK);				// TO RELEASE ANY BUFFERS

	OLDLINK->L2STATE = 0;

	if (OLDLINK->ReportFlags)
		return;							// Keep Link until Report sent

	// Remove from Chain

	if (OLDLINK == LINK)
	{
		// First in chain

		LINKS = OLDLINK->Next;
	}
	else
	{
		// Not first, so find in chain

		while (LINK && LINK->Next != OLDLINK)
		{
			LINK = LINK->Next;
		}

		if (!LINK)
			return;				// not found ?????

		// LINK is now the one before ours

		LINK->Next = OLDLINK->Next;
	}

	// Release malloc'ed structures

	if (OLDLINK->TXBuffer)
		free(OLDLINK->TXBuffer);

	if (OLDLINK->RXBuffer)
		free(OLDLINK->RXBuffer);

	for (i = 0; i < 8; i++)
	{
		if (OLDLINK->FRAMES[i].Data)
			free(OLDLINK->FRAMES[i].Data);
	}

	for (i = 0; i < 8; i++)
	{
		if (OLDLINK->RXFRAMES[i].DataLen)
			free(OLDLINK->RXFRAMES[i].Data);
	}

	free(OLDLINK);
}


VOID L2SENDXID(struct _LINKTABLE * LINK)
{
	//	Set up and send XID

	UCHAR * ptr;
	unsigned int xidval;
	MESSAGE Buffer;
	UCHAR Data[32];

	Buffer.L2DATA = Data;

	SETUPL2MESSAGE(&Buffer, LINK, XID | PFBIT);

	Buffer.DEST[6] |= 0x80;		// SET COMMAND
	Buffer.ORIGIN[6] &= 0x7F;	// SET COMMAND

	ptr = &Data[0];

	// Set up default XID Mod 8 

	*ptr++ = 0x82;			// FI
	*ptr++ = 0x80;			// GI
	*ptr++ = 0x0;
	*ptr++ = 0x10;			// Length 16

	*ptr++ = 0x02;			// Classes of Procedures
	*ptr++ = 0x02;			// Length
	*ptr++ = 0x00;			// 
	*ptr++ = 0x21;			// ABM Half Duplex

	// We offer REJ, SREJ and SREJ Multiframe

	*ptr++ = 0x03;			// Optional Functions
	*ptr++ = 0x03;			// Len

	// Sync TX, SREJ Multiframe 16 bit FCS, Mod 8, TEST,
	// Extended Addressing, REJ, SREJ

	xidval = OPMustHave | OPSREJ | OPSREJMult | OPREJ | OPMod8;
	*ptr++ = xidval >> 16;
	*ptr++ = xidval >> 8;
	*ptr++ = xidval;

	
	*ptr++ = 0x06;			// RX Packet Len
	*ptr++ = 0x02;			// Len
	*ptr++ = 0x08;			// 
	*ptr++ = 0x00;			// 2K bits (256) Bytes

	*ptr++ = 0x08;			// RX Window
	*ptr++ = 0x01;			// Len
	*ptr++ = 0x07;			// 7

	Buffer.DataLen = ptr - Data;	// SET LENGTH

	LINK->L2TIMER = LINK->L2TIME;	// (RE)SET TIMER
	PUT_ON_PORT_Q(LINK, &Buffer);
	
}






VOID L2SENDCOMMAND(struct _LINKTABLE * LINK, int CMD)
{
	//	SEND COMMAND IN CMD

	MESSAGE Buffer;

	SETUPL2MESSAGE(&Buffer, LINK, CMD);

	Buffer.DEST[6] |= 0x80;		// SET COMMAND
	Buffer.ORIGIN[6] &= 0x7F;	// SET COMMAND

	if (CMD & PFBIT)				// RESPONSE EXPECTED?
	{
		LINK->L2TIMER = LINK->L2TIME;	// (RE)SET TIMER
	}

	PUT_ON_PORT_Q(LINK, &Buffer);

}






VOID L2SENDRESPONSE(struct _LINKTABLE * LINK, int CMD)
{
	//	SEND Response IN CMD

	MESSAGE Buffer;
	int timeSinceDecoded = Now - DecodeCompleteTime;

	WriteDebugLog(LOGDEBUG, "Time since received = %d", timeSinceDecoded);

// Should delay a bit for link turnround,

// ?? is this best done by extending header or waiting ??

	if (timeSinceDecoded < 250)
		txSleep(250 - timeSinceDecoded);

	SETUPL2MESSAGE(&Buffer, LINK, CMD);

	Buffer.ORIGIN[6] |= 0x80;		// SET RESPONSE
	Buffer.DEST[6] &= 0x7F;	// SET COMMAND

	LINK->L2SLOTIM = T3 + rand() % 255;	// SET FRAME SENT RECENTLY

	PUT_ON_PORT_Q(LINK, &Buffer);

}


VOID SETUPL2MESSAGE(MESSAGE * Buffer, struct _LINKTABLE * LINK, UCHAR CMD)
{
	SETUPADDRESSES(LINK, Buffer);	// copy addresses

	// ptr  NOW POINTS TO COMMAND BYTE

	Buffer->DataLen = 0;
	Buffer->CTL = CMD;
	return;
}


VOID L3LINKCLOSED(struct _LINKTABLE * LINK, int Reason);

VOID InformPartner(struct _LINKTABLE * LINK, int Reason)
{
	//	LINK IS DISCONNECTING - IF THERE IS A CROSSLINK, SEND DISC TO IT

	LINK->ReportFlags |= DiscRpt;
}


unsigned int RR_OR_RNR(struct _LINKTABLE * LINK)
{
	LINK->L2FLAGS &= ~RNRSENT;

	//	we shoudl send RNR if host session is blocked or we are running
	//  out of buffer space

	if (LINK->RXCount > 512)
		goto SENDRNR;			// NOT ENOUGH

	//	SEND REJ IF IN REJ STATE

	if (LINK->L2STATE == 6)
	{

	// We may have the needed frame in RXFRAMES

CheckNSLoop2:

		if (LINK->RXFRAMES[LINK->LINKNR].DataLen)
		{
			// We have the first missing frame. Process it.
	
			struct _DATABUFFER * OldBuffer = &LINK->RXFRAMES[LINK->LINKNR];
			WriteDebugLog(LOGDEBUG, "L2 process saved Frame %d", LINK->LINKNR);
			PROC_I_FRAME(LINK, OldBuffer->Data, OldBuffer->DataLen);

			OldBuffer->DataLen = 0;
			free(OldBuffer->Data);
		

			// NR has been updated.

			// CLear REJ if we have no more saved
			
			if (LINK->UseSREJ)			// Using SREJ?
			{
				// see if any frames saved. 

				int i;

				for (i = 0; i < 8; i++)
				{
					if (LINK->RXFRAMES[i].DataLen)
						goto stayinREJ2;
				}
				// Drop through if no stored frames
			}

			LINK->L2STATE = 5;
			LINK->L2FLAGS &= ~REJSENT;
stayinREJ2:

			goto CheckNSLoop2;		// See if OK or we have another saved frame
		}
		if (LINK->L2STATE == 6)

		// if we support SREJ send that instesd or REJ

		if (LINK->UseSREJ)		// We only allow 2.2 with SREJ Multi
			return SREJ;
		else
			return REJ;
	}
	return RR;

SENDRNR:

	LINK->L2FLAGS |= RNRSENT;		// REMEMBER

	return RNR;
}


VOID ConnectFailedOrRefused(struct _LINKTABLE * LINK, char * Msg);

VOID CONNECTFAILED(struct _LINKTABLE * LINK)
{
	LINK->ReportFlags |= ConFailed;
}
VOID CONNECTREFUSED(struct _LINKTABLE * LINK)
{
	LINK->ReportFlags |= ConBusy;
}

VOID L3CONNECTFAILED();

VOID ConnectFailedOrRefused(struct _LINKTABLE * LINK, char * Msg)
{
	//	IF DOWNLINK, TELL PARTNER
	//	IF CROSSLINK, TELL ROUTE CONTROL
}

VOID SENDCONNECTREPLY(struct _LINKTABLE * LINK)
{
	//	LINK SETUP COMPLETE

	LINK->ReportFlags |= ConOK;
}

int LastL2Tick = 0;

VOID ptkSessionBG()
{
	// Only called when link is idle and there is nothing to senf
	// so we wont run frack timer when still sending

	int time = Now;
	struct _LINKTABLE * LINK = LINKS;

	while (LINK)
	{
		//	CHECK FOR Stuff to send

		if (LINK->L2STATE >= 5)
			SDETX(LINK);

		if (KRXPutPtr != KRXGetPtr)	// Something to send
			return;

		LINK = LINK->Next;
	}
	
	// Check for timer expiry.

	// Only run if at least tick interval has passed


	if (time - LastL2Tick < 100)
		return;

	LastL2Tick = time;

	L2TimerProc();

}

struct _LINKTABLE * FindLinkForChannel(int Channel)
{
	struct _LINKTABLE * LINK = LINKS;

	while (LINK)
	{
		if (LINK->HostChannel == Channel)
			return LINK;
		
		LINK = LINK->Next;
	}
	return NULL;
}

extern SOCKET PktSock;
extern BOOL PKTCONNECTED;


VOID PktSend(UCHAR * Msg, int Len)
{
#ifndef TEENSY
	if (PKTCONNECTED)
	{
		// Send over TCP
	
		Msg[0] = 255;
		Msg[1] = 255;			// Dummy Header

		send(PktSock, Msg, Len, 0);
	}
	else
#endif
		EmCRCStuffAndSend(Msg, Len + 5);
}

BOOL ProcessPktCommand(int Channel, char *Buffer, int Len)
{
	// ARDOP Packet Command. Probably only connect or Disconnect

	UCHAR SCSReply[256 + 10];		// Host Mode reply buffer
	struct _LINKTABLE * LINK;
	int ReplyLen;
	UCHAR ORIGIN[7];
	UCHAR DEST[7];

	Buffer[Len] = 0;

	if (memcmp(Buffer, "PKTCALL ", 7) == 0)
	{
		char * port, * from, *to, *Context;

		port = strtok_s(&Buffer[8], " ", &Context);
		to = strtok_s(NULL, " ", &Context);
		from = strtok_s(NULL, " ", &Context);

		if (from  == NULL)
		{
			// Port may not be present

			from = to;
			to = port;
		}			
	
		if (!to || !from)
		{			
			SCSReply[2] = Channel;
			SCSReply[3] = 2;
			ReplyLen = sprintf(&SCSReply[4], "Bad PKTCall Command");
			PktSend(SCSReply, ReplyLen + 5);
			return TRUE;
		}

		if (ConvToAX25(from, ORIGIN) == FALSE || ConvToAX25(to, DEST) == FALSE)
		{
			SCSReply[2] = Channel;
			SCSReply[3] = 2;
			ReplyLen = sprintf(&SCSReply[4], "Invalid Callsign");
			PktSend(SCSReply, ReplyLen + 5);
			return TRUE;
		}

		// Check if already have session for these calls

		LINK = FindLink(DEST, ORIGIN);

		if (LINK)
		{
			SCSReply[2] = Channel;
			SCSReply[3] = 2;
			ReplyLen = sprintf(&SCSReply[4], "There is already a session between %s and %s", from, to);
			PktSend(SCSReply, ReplyLen + 5);
			return TRUE;
		}

		LINK = NewLink();

		if (LINK == NULL)
		{
			SCSReply[2] = Channel;
			SCSReply[3] = 2;
			ReplyLen = sprintf(&SCSReply[4], "Not enough memory to set up ssssion");
			PktSend(SCSReply, ReplyLen + 5);
			return TRUE;
		}

		// Set up New Session

		LINK->HostChannel = Channel;

		memcpy(LINK->LINKCALL, DEST, 7);
		LINK->LINKCALL[6] &= 0x1e;		// Mask SSID

		memcpy(LINK->OURCALL, ORIGIN, 7);
		LINK->OURCALL[6] &= 0x1e;		// Mask SSID

		LINK->L2TIME = PORTT1;		// Set timeout for no digis

		LINK->LINKTYPE = 2;			// Downlink

		LINK->L2SLOTIM = T3 + rand() % 255;		// SET FRAME SENT RECENTLY

		LINK->LINKWINDOW = initMaxFrame;

		RESET2(LINK);				// RESET ALL FLAGS	

		LINK->L2STATE = 2;
		LINK->L2TIMER = 1;			// Use retry to send SABM
		LINK->L2TIMEOUTS--;			// Keep Stats right

		SCSReply[2] = Channel;
		SCSReply[3] = 0;
		PktSend(SCSReply, 4);

		return TRUE;
	}
	
	if (strcmp(Buffer, "DISCONNECT") == 0)
	{
		struct _LINKTABLE * LINK = LINKS;

		LINK = FindLinkForChannel(Channel);
		
		if (LINK)
		{
			LINK->L2STATE = 4;			// CLOSING
			LINK->L2TIMER = 1;			// Use retry to send DISC
			LINK->L2TIMEOUTS--;			// Keep stats right

			SCSReply[2] = Channel;
			SCSReply[3] = 0;
			PktSend(SCSReply, 4);
			return TRUE;
		}
	
		// Ignore if not found
	}

	SCSReply[2] = Channel;
	SCSReply[3] = 0;
	PktSend(SCSReply, 4);

	return TRUE;
}


UCHAR * PacketSessionPoll(UCHAR * NextChan)
{
	// Look for anything to send 

	struct _LINKTABLE * LINK = LINKS;

	while (LINK)
	{ 
		if (LINK->ReportFlags || LINK->RXCount)
			*(NextChan++) = LINK->HostChannel + 1;
		
		LINK = LINK->Next;
	}

	return NextChan;
}

BOOL CheckForPktMon()
{
	UCHAR SCSReply[256 + 10];		// Host Mode reply buffer
	
	if (PacketMonLength)
	{
		// If length is less than 257, return whole 
		// lot in a Type 4 message

		// if not, first 256 as a type 5 Message
		// Next poll return rest in type 6

		int Length = PacketMonLength;
		
		if (Length > 256)
			Length = 256;
	
		if (PacketMonMore)
			memcpy(&SCSReply[5], &PacketMon[256], Length);
		else
			memcpy(&SCSReply[5], PacketMon, Length);
				
		SCSReply[2] = 0;		// Channel 0

		if (PacketMonLength > 256)
		{
			PacketMonMore = 1;
			PacketMonLength -= 256;
			SCSReply[3] = 5;
		}
		else
		{
			if (PacketMonMore)
				SCSReply[3] = 6;
			else
				SCSReply[3] = 4;

			PacketMonLength = PacketMonMore = 0;
		}

		SCSReply[4] = Length - 1;
		PktSend(SCSReply, Length + 5);
		return 1;
	}
	return 0;
}


BOOL CheckForPktData(int Channel)
{
	UCHAR SCSReply[500];		// Host Mode reply buffer
	struct _LINKTABLE * LINK = LINKS;
	int ReplyLen;

	while (LINK)
	{
		if (LINK->HostChannel == Channel || Channel == 0)
		{
			// Check status before data

			if (LINK->ReportFlags)
			{
				char NormCall[10];
				char OurCall[10];
	
				SCSReply[2] = LINK->HostChannel;
				SCSReply[3] = 3;		// Status

				NormCall[ConvFromAX25(LINK->LINKCALL, NormCall)] = 0;
				OurCall[ConvFromAX25(LINK->OURCALL, OurCall)] = 0;

				if (LINK->ReportFlags & ConFailed)
					ReplyLen = sprintf(&SCSReply[4], "Failure with %s", NormCall);
				else if (LINK->ReportFlags & ConBusy)
					ReplyLen = sprintf(&SCSReply[4], "Busy from %s", NormCall);
				else if (LINK->ReportFlags & ConOK)
					ReplyLen = sprintf(&SCSReply[4], "Connected to %s", NormCall);
				else if (LINK->ReportFlags & DiscRpt)
					ReplyLen = sprintf(&SCSReply[4], "Disconnected from %s", NormCall);
				else if (LINK->ReportFlags & Incomming)
					ReplyLen = sprintf(&SCSReply[4], "Incoming Call from %s to %s", NormCall, OurCall);
				else if (LINK->ReportFlags & ReportQueue)
				{
					// Queue Status Report

					int i, Count = LINK->TXCount;

					for (i = 0; i < 8; i++)
						Count += LINK->FRAMES[i].DataLen;	// Queued on holding Q
	
					ReplyLen = sprintf(&SCSReply[4], "Queued %d", Count);
					SCSReply[3] = 1;		// Success
				 }
		
				LINK->ReportFlags = 0;

				if (LINK->L2STATE == 0)		// Dead Link
					CLEAROUTLINK(LINK);		// Get rid of entry

				PktSend(SCSReply, ReplyLen + 5);		// Include NULL
				return TRUE;
			}
			else
			{
				if (LINK->RXCount)
				{
					int Count = LINK->RXCount;
					if (Count > 256)
						Count = 256;
					
					SCSReply[2] = LINK->HostChannel;
					SCSReply[3] = 7;		// Data
					SCSReply[4] = Count - 1;

					memcpy(&SCSReply[5], LINK->RXBuffer, Count);
					LINK->RXCount -= Count;

					if (LINK->RXCount)		// if any left move down buffer
						memmove(LINK->RXBuffer, &LINK->RXBuffer[Count], LINK->RXCount);

					PktSend(SCSReply, Count + 5);
					return TRUE;
				}
			}
		}
		LINK = LINK->Next;
	}
	return FALSE;
}

VOID ProcessPktData(int Channel, UCHAR * Buffer, int Len)
{
	UCHAR SCSReply[256 + 10];		// Host Mode reply buffer
	struct _LINKTABLE * LINK = LINKS;
	int i, Count = 0, ReplyLen;

	LINK = FindLinkForChannel(Channel);
		
	if (LINK)
	{
		// Copy Data to TX Buffer, extending if necessary

		if (LINK->TXCount + Len > LINK->TXBuffersize)
		{
			LINK->TXBuffersize += 512;
			LINK->TXBuffer = realloc(LINK->TXBuffer, LINK->TXBuffersize);
		}
		memcpy(&LINK->TXBuffer[LINK->TXCount], Buffer, Len);
		LINK->TXCount += Len;
		Count = LINK->TXCount;

		// Return Bytes Queued

		for (i = 0; i < 8; i++)
			Count += LINK->FRAMES[i].DataLen;	// Queued on holding Q
	}

	SCSReply[2] = Channel;
	SCSReply[3] = 1;		// Success

	ReplyLen = sprintf(&SCSReply[4], "Queued %d", Count);

	PktSend(SCSReply, ReplyLen + 5);
}

// DED Mode For RMS Express, Uses JOST1 not JHOST4

extern BOOL DEDMode;		// Used by RMS Express for Packet
extern BOOL PACMode;

extern BOOL HostMode;		// Host or Term

VOID ProcessDEDModeFrame(UCHAR * rxbuffer, unsigned int Length)
{
		int Chan = rxbuffer[0];
		int Cmd = rxbuffer[1];
		int Len = rxbuffer[2] + 1;
		struct _LINKTABLE * LINK = LINKS;
		UCHAR ORIGIN[7];
		UCHAR DEST[7];
		char Message[80] = "";

		LINK = FindLinkForChannel(Chan);

		if (Cmd == 0)				// Data
		{
			if (LINK)
			{
				// Copy Data to TX Buffer, extending if necessary

				if (LINK->TXCount + Len > LINK->TXBuffersize)
				{
					LINK->TXBuffersize += 512;
					LINK->TXBuffer = realloc(LINK->TXBuffer, LINK->TXBuffersize);
				}
				memcpy(&LINK->TXBuffer[LINK->TXCount], &rxbuffer[3], Len);
				LINK->TXCount += Len;
			}
			PutChar(Chan);
			PutChar(0);					// Success, nothing follows

			return;
		}

		
		switch (rxbuffer[3])		// Poll
		{		
		case 'G':

//			WriteDebugLog(LOGDEBUG, "DED Poll");

			if (LINK == NULL)
			{
				PutChar(Chan);			// REPLY ON SAME CHANNEL
				PutChar(0);				// NOTHING DOING
			
				break;
			}

			if (LINK->ReportFlags == ReportQueue)		// not use in ded mode
				LINK->ReportFlags = 0;

			if (LINK->ReportFlags)
			{
				char NormCall[10];
				char OurCall[10];
	
				PutChar(Chan);		// REPLY ON SAME CHANNEL
				PutChar(3);			// Status
				
				NormCall[ConvFromAX25(LINK->LINKCALL, NormCall)] = 0;
				OurCall[ConvFromAX25(LINK->OURCALL, OurCall)] = 0;

				if (LINK->ReportFlags & ConFailed)
					sprintf(Message, "XX NOT LINK FAILURE with %s \r", NormCall);
				else if (LINK->ReportFlags & ConBusy)
					sprintf(Message, "BUSY fm %s", NormCall);
				else if (LINK->ReportFlags & ConOK)
					sprintf(Message, "XX CONNECTED to %s ", NormCall);
				else if (LINK->ReportFlags & DiscRpt)
					sprintf(Message, "XX DISCONNECTED from %s ", NormCall);
				else if (LINK->ReportFlags & Incomming)
					sprintf(Message, "Incoming Call from %s to %s", NormCall, OurCall);
				else
					WriteDebugLog(7, "ReportFlags %d", LINK->ReportFlags);
				
				LINK->ReportFlags = 0;

				if (LINK->L2STATE == 0)		// Dead Link
					CLEAROUTLINK(LINK);		// Get rid of entry

				PutString(Message);
				PutChar(0);					// Null Terminate
				break;
			}

			if (LINK->RXCount)
			{
				int Count = LINK->RXCount;
				if (Count > 256)
				Count = 256;
					
				PutChar(Chan);		// REPLY ON SAME CHANNEL
				PutChar(7);			// Data
				PutChar(Count - 1);	// Length
				
				SerialSendData(LINK->RXBuffer, Count);

				LINK->RXCount -= Count;

				if (LINK->RXCount)		// if any left move down buffer
					memmove(LINK->RXBuffer, &LINK->RXBuffer[Count], LINK->RXCount);

				break;
			}
			
			PutChar(Chan);				// REPLY ON SAME CHANNEL
			PutChar(0);					// NOTHING DOING
			break;
	
		case 'J':

			WriteDebugLog(LOGDEBUG, "Exit Host");
			PutChar(Chan);				// REPLY ON SAME CHANNEL
			PutChar(0);					// NOTHING DOING
			HostMode = 0;
			DEDMode = 0;
			break;

		case 'L':

			if (LINK)
				sprintf(Message, "%d %d %d %d %d %d\r",
				(LINK->ReportFlags) ? 1 : 0, 
				0, 0, 0,
				LINK->L2RETRIES,
				(LINK->L2STATE) ? LINK->L2STATE-1 : 0);
			else
				sprintf(Message, "0 0 0 0 0 0\r");

//			WriteDebugLog(LOGDEBUG, "Status Reply %s", Message);

			PutChar(Chan);				// REPLY ON SAME CHANNEL
			PutChar(1);					// Success Null Terminted
			PutString(Message);
			PutChar(0);
			break;

/*

			Channel Status format
 ---------------------
 a b c d e f
 a = Number of link status messages not yet displayed
 b = Number of receive frames not yet displayed
 c = Number of send frames not yet transmitted
 d = Number of transmitted frames not yet acknowledged
 e = Number of tries on current operation
 f = Link state
     Possible link states are:
      0 = Disconnected
      1 = Link Setup
      2 = Frame Reject
      3 = Disconnect Request
      4 = Information Transfer
      5 = Reject Frame Sent
      6 = Waiting Acknowledgement
      7 = Device Busy
      8 = Remote Device Busy
      9 = Both Devices Busy
     10 = Waiting Acknowledgement and Device Busy
     11 = Waiting Acknowledgement and Remote Busy
     12 = Waiting Acknowledgement and Both Devices Busy
     13 = Reject Frame Sent and Device Busy
     14 = Reject Frame Sent and Remote Busy
     15 = Reject Frame Sent and Both Devices Busy
*/

		case 'D':

			WriteDebugLog(LOGDEBUG, "Disconnect Command");
		
			if (LINK)
			{
				LINK->L2STATE = 4;			// CLOSING
				LINK->L2TIMER = 1;			// Use retry to send DISC
			}
			PutChar(Chan);				// REPLY ON SAME CHANNEL
			PutChar(0);					// NOTHING DOING
			break;

		case 'V':

			WriteDebugLog(LOGDEBUG, "V Command");
			PutChar(Chan);				// REPLY ON SAME CHANNEL
			PutChar(1);					// Success Null Terminted
			PutString("DSPTNC Firmware V.1.3a, (C) 2005-2010 SCS GmbH & Co.");
			PutChar(0);
			break;

		case 'C':
				
			WriteDebugLog(LOGDEBUG, "DED Connect Request");

			if (LINK)
			{
				PutChar(Chan);				// REPLY ON SAME CHANNEL
				PutChar(2);					// NOTHING DOING
				PutString("CHANNEL ALREADY CONNECTED");
				PutChar(0);					// NOTHING DOING
				break;
			}

			rxbuffer[Length] = 0;

			if (ConvToAX25(Callsign, ORIGIN) == FALSE || ConvToAX25(&rxbuffer[5], DEST) == FALSE)
			{
				PutChar(Chan);				// REPLY ON SAME CHANNEL
				PutChar(2);					// NOTHING DOING
				PutString("INVALID CALLSIGN");
				PutChar(0);					// NOTHING DOING
				break;
			}

			// Check if already have session for these calls

			LINK = FindLink(DEST, ORIGIN);

			if (LINK)
			{
				PutChar(Chan);				// REPLY ON SAME CHANNEL
				PutChar(2);					// NOTHING DOING
				PutString("STATION ALREADY CONNECTED");
				PutChar(0);					// NOTHING DOING
				break;
			}

			LINK = NewLink();

			if (LINK == NULL)
			{
				PutChar(Chan);				// REPLY ON SAME CHANNEL
				PutChar(2);					// NOTHING DOING
				PutString("NO MEMORY");
				PutChar(0);					// NOTHING DOING
				break;
			}

			// Set up New Session

			LINK->HostChannel = Chan;

			memcpy(LINK->LINKCALL, DEST, 7);
			LINK->LINKCALL[6] &= 0x1e;		// Mask SSID

			memcpy(LINK->OURCALL, ORIGIN, 7);
			LINK->OURCALL[6] &= 0x1e;		// Mask SSID

			LINK->L2TIME = PORTT1;		// Set timeout for no digis

			LINK->LINKTYPE = 2;			// Downlink

			LINK->L2SLOTIM = T3 + rand() % 255;		// SET FRAME SENT RECENTLY

			LINK->LINKWINDOW = PORTWINDOW;

			RESET2(LINK);				// RESET ALL FLAGS	

			LINK->L2STATE = 2;
			LINK->L2TIMER = 1;			// Use retry to send SABM

			PutChar(Chan);				// REPLY ON SAME CHANNEL
			PutChar(0);					// Ok No Message
			break;

		default:

			WriteDebugLog(LOGDEBUG, "DED Command %c", rxbuffer[3]);
			break;
		}
	return;
}


VOID ProcessPacketHostBytes(UCHAR * RXBuffer, int Len)
{
	// Host Mode Frame - Starts Chan, Cmd/Data, Len - 1

	if (RXBuffer[1] == 1)		// Command
	{
		ProcessPktCommand(RXBuffer[0], &RXBuffer[3], RXBuffer[2] + 1);
		return;
	}

	if (RXBuffer[1])
		return;					// Not Data??

	 ProcessPktData(RXBuffer[0], &RXBuffer[3], RXBuffer[2] + 1);
}


VOID ClosePacketSessions()
{
	struct _LINKTABLE * LINK = LINKS;

	while (LINK)
	{
		if (LINK->L2STATE != 4)
		{
			LINK->L2STATE = 4;			// CLOSING
			LINK->L2TIMER = 1;			// Use retry to send DISC
		}
		LINK = LINK->Next;
	}
}

VOID DisplaySessionStats(struct _LINKTABLE * LINK, int Exitcode)
{
	char FarCall[10];
	char OurCall[10];
	
	FarCall[ConvFromAX25(LINK->LINKCALL, FarCall)] = 0;
	OurCall[ConvFromAX25(LINK->OURCALL, OurCall)] = 0;

	Statsprintf("****** Packet Session stats %s and %s  Last Mode %s ******", OurCall, FarCall, pktMod[LINK->pktMode]); 
	Statsprintf("");
	Statsprintf("Packets Received %d Bytes Received %d", LINK->PacketsReceived, LINK->BytesReceived);
	Statsprintf("Packets Sent %d Bytes Sent %d", LINK->PacketsSent, LINK->BytesSent);
	Statsprintf("");
	Statsprintf("Timeouts %d", LINK->L2TIMEOUTS);
	Statsprintf("Packets received out of Sequence %d", LINK->L2OUTOFSEQ);
	Statsprintf("SREJ Frames Sent %d", LINK->L2REJCOUNT);
	Statsprintf("");
	Statsprintf("**********************************************************");

	CloseStatsLog();
}
