//
//  Code for Packet using ARDOP like frames.
//	
//	This Module handles frame level stuff, and can be used
//	with a KISS interface. Module pktSession inplements an
//	ax.25 like Level 2, with dynamic parameter updating
//
// This uses Special Variable Length frames

// Packet has header of 6 bytes  sent in 4FSK.500.100. 
// Header is 6 bits Type 10 Bits Len 2 bytes CRC 2 bytes RS
// Once we have that we receive the rest of the packet in the 
// mode defined in the header.
// Uses Frame Type 0xC0, symbolic name PktFrameHeader


#ifdef WIN32
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>
#include <winioctl.h>
#else
#define HANDLE int
#endif

#include "ARDOPC.h"


extern UCHAR KISSBUFFER[500]; // Long enough for stuffed KISS frame
extern int KISSLength;


VOID EncodePacket(UCHAR * Data, int Len);
VOID AddTxByteDirect(UCHAR Byte);
VOID AddTxByteStuffed(UCHAR Byte);
unsigned short int compute_crc(unsigned char *buf,int len);
void PacketStartTX();
BOOL GetNextKISSFrame();
VOID SendAckModeAck();

extern unsigned char bytEncodedBytes[1800];		// I think the biggest is 600 bd 768 + overhead
extern int EncLen;

extern UCHAR PacketMon[360];
extern int PacketMonMore;
extern int PacketMonLength;


int pktBandwidth = 4;
int pktMaxBandwidth = 8;
int pktMaxFrame = 4;
int pktPacLen = 80;

int pktMode = 0;
int pktRXMode;		// Currently receiving mode

int pktDataLen;
int pktRSLen;

// Now use Mode number to encode type and bandwidth

const char pktMod[16][12] = {
	"4PSK/200",
	"4FSK/500", "4PSK/500", "8PSK/500", "16QAM/500",
	"4FSK/1000", "4PSK/1000", "8PSK/1000", "16QAM/1000",
	"4FSK/2000", "4PSK/2000", "8PSK/2000", "16QAM/2000",
};

// Note FSK modes, though identified as 200 500 or 1000 actually
// occupy 500, 1000 or 2000 BW

const int pktBW[16] = {200,
					500, 500, 500, 500,
					1000, 1000, 1000, 1000,
					2000, 2000, 2000, 2000};

const int pktCarriers[16] = {
					1,
					1, 2, 2, 2,
					2, 4, 4, 4,
					4, 8, 8, 8};

const BOOL pktFSK[16] = {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};

int pktModeLen = 13;

VOID PktARDOPEncode(UCHAR * Data, int Len)
{
	unsigned char DataToSend[4];
	int pktNumCar = pktCarriers[pktMode];

	// Header now sent as 4FSK.500.100
	// 6 Bits Mode, 10 Bits Length

	// 2 Bytes Header 2 Bytes CRC 2 Bytes RS

	if (Len > 1023)
		return;

	DataToSend[0] = (pktMode << 2)|(Len >> 8);
	DataToSend[1] = Len & 0xff;
	
	// Calc Data and RS Length

	pktDataLen = (Len + (pktNumCar - 1))/pktNumCar; // Round up

	pktRSLen = pktDataLen >> 2;			// Try 25% for now

	if (pktRSLen & 1)
		pktRSLen++;						// Odd RS bytes no use

	if (pktRSLen < 4)
		pktRSLen = 4;					// At least 4

	// Encode Header
	
	EncLen = EncodeFSKData(PktFrameHeader, DataToSend, 2, bytEncodedBytes);
	
	// Encode Data

	if (pktFSK[pktMode])
		EncodeFSKData(PktFrameData, Data, Len, &bytEncodedBytes[EncLen]);
	else
		EncodePSKData(PktFrameData, Data, Len, &bytEncodedBytes[EncLen]);

	// Header is FSK
	
	Mod4FSKDataAndPlay(PktFrameHeader, bytEncodedBytes, EncLen, intCalcLeader);  // Modulate Data frame 
}

// Called when link idle to see if any packet frames to send

void PktARDOPStartTX()
{
	if (GetNextKISSFrame() == FALSE)
		return;			// nothing to send
	
	while (TRUE)				// loop till we run out of packets
	{
		switch(KISSBUFFER[0])
		{
		case 0:			// Normal Data

			WriteDebugLog(LOGALERT, "Sending Packet Frame Len %d", KISSLength - 1); 

			PktARDOPEncode(KISSBUFFER + 1, KISSLength - 1);

			// Trace it

			if (PacketMonLength == 0)	// Ingore if one queued
			{
				PacketMon[0] = 0x80;		// TX Flag
				memcpy(&PacketMon[1], &KISSBUFFER[1], KISSLength);
	
				PacketMonLength = KISSLength;
			}

			break;

		case 6:			// HW Paramters. Set Mode and Bandwidth

			pktMode = KISSBUFFER[1]; 
			break;
		
		case 12:
		
		// Ackmode frame. Return ACK Bytes (first 2) to host when TX complete
		
			WriteDebugLog(LOGALERT, "Sending Packet Frame Len %d", KISSLength - 3); 
			PktARDOPEncode(KISSBUFFER + 3, KISSLength - 3);

			// Returns when Complete so can send ACK

			SendAckModeAck();
			break;
		}

		// See if any more

		if (GetNextKISSFrame() == FALSE)
			break;			// no more to send
	}
}

