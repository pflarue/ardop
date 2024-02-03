//	ARDOP Modem Decode Sound Samples

#ifdef WIN32
#define _CRT_SECURE_NO_DEPRECATE
#define _USE_32BIT_TIME_T
#include <windows.h>
#endif

#include "ARDOPC.h"
#include "RXO.h"

#pragma warning(disable : 4244)		// Code does lots of float to int

#ifndef TEENSY
#define MEMORYARQ
#endif

#undef PLOTWATERFALL

#ifdef PLOTWATERFALL
#define WHITE  0xffff
#define Tomato 0xffff
#define Orange 0xffff
#define Khaki 0xffff
#define Cyan 0xffff
#define DeepSkyBlue 0
#define RoyalBlue 0 
#define Navy 0
#define Black 0
#endif


#ifdef TEENSY
#define PKTLED LED3		// flash when packet received
extern unsigned int PKTLEDTimer;
#endif

void SendFrametoHost(unsigned char *data, unsigned dlen);

void CheckandAdjustRXLevel(int maxlevel, int minlevel, BOOL Force);
void clearDisplay();
void updateDisplay();
VOID L2Routine(UCHAR * Packet, int Length, int FrameQuality, int totalRSErrors, int NumCar, int pktRXMode);


void DrawAxes(int Qual, const char * FrameType, char * Mode);

extern int lastmax, lastmin;		// Sample Levels

char strRcvFrameTag[32];

BOOL blnLeaderFound = FALSE;

int intLeaderRcvdMs = 1000;		// Leader length??

extern int intLastRcvdFrameQuality;
extern int intReceivedLeaderLen;
extern UCHAR bytLastReceivedDataFrameType;
extern int NErrors;
extern BOOL blnBREAKCmd;
extern UCHAR bytLastACKedDataFrameType;
extern int intARQDefaultDlyMs;
extern unsigned int tmrFinalID;
extern BOOL PKTCONNECTED;
void DrawDecode(char * Decode);

extern int pktRXMode;

short intPriorMixedSamples[120];  // a buffer of 120 samples to hold the prior samples used in the filter
int	intPriorMixedSamplesLength = 120;  // size of Prior sample buffer

// While searching for leader we must save unprocessed samples
// We may have up to 720 left, so need 1920 

short rawSamples[2400];	// Get Frame Type need 2400 and we may add 1200
int rawSamplesLength = 0;

short intFilteredMixedSamples[5000];	// Get Frame Type need 2400 and we may add 1200
int intFilteredMixedSamplesLength = 0;

int intFrameType;				// Type we are decoding
int LastDataFrameType;			// Last data frame processed (for Memory ARQ, etc)

char strDecodeCapture[1024];

//	Frame type parameters

int intCenterFreq = 1500;
float intCarFreq;			//(was int)	// Are these the same ??
int intNumCar;
int intBaud;
int intDataLen;
int intRSLen;
int intSampleLen;
int DataRate = 0;				// For SCS Reporting
int intDataPtr;
int intSampPerSym;
int intDataBytesPerCar;
BOOL blnOdd;
char strType[18] = "";
char strMod[16] = "";
UCHAR bytMinQualThresh;
int intPSKMode;

#define MAX_RAW_LENGTH	256     // I think! Max length of an RS block
#define MAX_DATA_LENGTH	8 * 128 // I think! 16QAM.2000.100

// intToneMags should be an array with one row per carrier.
// and 16 * max bytes data (2 bits per symbol, 4 samples per symbol in 4FSK.

// but as 600 Baud frames are very long (750 bytes), but only one carrier
// may be better to store as scalar and calculate offsets into it for each carrier
// treat 600 as 3 * 200, but scalar still may be better

// Needs 64K if ints + another 64 for MEM ARQ. (maybe able to store as shorts)
// 48K would do if we use a scalar (600 baud, 750 bytes)
// Max is 4 carrier, 83 bytes or 1 carrier 762 (or treat as 3 * 253)

// Could just about do this on Teensy 3.6 or Nucleo F7

// looks like we have 4 samples for each 2 bits, which means 16 samples per byte.

int intToneMags[4][16 * MAX_RAW_LENGTH] = {0};	// Need one per carrier

int intToneMagsIndex[4];

// Same here

int intSumCounts[8];			// number in above arrays

int intToneMagsLength;

unsigned char goodCarriers = 0;	// Carriers we have already decoded

//	We always collect all phases for PSK and QAM so we can do phase correction

short intPhases[8][652] = {0};	// We will decode as soon as we have 4 or 8 depending on mode
								//	(but need one set per carrier)
								// 652 is 163 * 4 (4PSK 167 Baud)

//	We only use Mags for QAM, and max is 2 carriers 195 bytes
// ??????????????????
//short intMags[2][195 * 2] = {0};
short intMags[8][652] = {0};

#ifdef MEMORYARQ

// Enough RAM for memory ARQ so keep all samples for FSK and a copy of tones or phase/amplitude

int  intToneMagsAvg[4][332];	//???? FSK Tone averages

short intCarPhaseAvg[8][652];	// array to accumulate phases for averaging (Memory ARQ)
short intCarMagAvg[8][652];		// array to accumulate mags for averaging (Memory ARQ) 
 
#endif


//219 /3 * 8= 73 * 8 =  584
//163 * 4 = 652

//	If we do Mem ARQ we will need a fair amount of RAM

int intPhasesLen;

// Received Frame

UCHAR bytData[MAX_DATA_LENGTH];
int frameLen;

int totalRSErrors;

// We need one raw buffer per carrier

// This can be optimized quite a bit to save space
// We can probably overlay on bytData

UCHAR bytFrameData1[760];					// Received chars
UCHAR bytFrameData2[MAX_RAW_LENGTH];		// Received chars
UCHAR bytFrameData3[MAX_RAW_LENGTH];		// Received chars
UCHAR bytFrameData4[MAX_RAW_LENGTH];		// Received chars
UCHAR bytFrameData5[MAX_RAW_LENGTH];		// Received chars
UCHAR bytFrameData6[MAX_RAW_LENGTH];		// Received chars
UCHAR bytFrameData7[MAX_RAW_LENGTH];		// Received chars
UCHAR bytFrameData8[MAX_RAW_LENGTH];		// Received chars

UCHAR * bytFrameData[8] = {bytFrameData1, bytFrameData2,
		bytFrameData3, bytFrameData4, bytFrameData5,
		bytFrameData6, bytFrameData7, bytFrameData8};

char CarrierOk[8];			// RS OK Flags per carrier

int charIndex = 0;			// Index into received chars

int SymbolsLeft;			// number still to decode

int DummyCarrier = 0;	// pseudo carrier used for long 600 baud frames
UCHAR * Decode600Buffer = bytFrameData1;

BOOL PSKInitDone = FALSE;

BOOL blnSymbolSyncFound, blnFrameSyncFound;

extern UCHAR bytLastARQSessionID;
extern UCHAR bytCurrentFrameType;
extern int intShiftUpDn;
extern const char ARQSubStates[10][11];
extern int intLastARQDataFrameToHost;

// dont think I need it short intRcvdSamples[12000];		// 1 second. May need to optimise

float dblOffsetLastGoodDecode = 0;
int dttLastGoodFrameTypeDecode = -20000;

float dblOffsetHz = 0;;
int dttLastLeaderDetect;

extern int intRmtLeaderMeasure;

extern BOOL blnARQConnected;


extern BOOL blnPending;
extern UCHAR bytPendingSessionID;
extern UCHAR bytSessionID;

int dttLastGoodFrameTypeDecod;
int dttStartRmtLeaderMeasure;

char lastGoodID[11] = "";

int GotBitSyncTicks;

int intARQRTmeasuredMs;

float dbl2Pi = 2 * M_PI; 

float dblSNdBPwr;
float dblNCOFreq = 3000;	 // nominal NC) frequency
float dblNCOPhase = 0;
float dblNCOPhaseInc = 2 * M_PI * 3000 / 12000;  // was dblNCOFreq

int	intMFSReadPtr = 30;				// reset the MFSReadPtr offset 30 to accomodate the filter delay

int RcvdSamplesLen = 0;				// Samples in RX buffer


BOOL Acquire2ToneLeaderSymbolFraming();
BOOL SearchFor2ToneLeader3(short * intNewSamples, int Length, float * dblOffsetHz, int * intSN);
BOOL AcquireFrameSyncRSB();
int Acquire4FSKFrameType();

void DemodulateFrame(int intFrameType);
void Demod1Car4FSKChar(int Start, UCHAR * Decoded, int Carrier);
VOID Track1Car4FSK(short * intSamples, int * intPtr, int intSampPerSymbol, float intSearchFreq, int intBaud, UCHAR * bytSymHistory);
VOID Decode1CarPSK(UCHAR * Decoded, int Carrier);
int EnvelopeCorrelator();
BOOL DecodeFrame(int intFrameType, UCHAR * bytData);

void Update4FSKConstellation(int * intToneMags, int * intQuality);
void Update16FSKConstellation(int * intToneMags, int * intQuality);
void Update8FSKConstellation(int * intToneMags, int * intQuality);
void ProcessPingFrame(char * bytData);
int Compute4FSKSN();

void DemodPSK();
BOOL DemodQAM();

/*

const int SamplesToComplete[256] = {
//  lookup the number of samples (@12 KHz) needed to complete the frame after the Frame ID is detected 
// Value is increased by factor of 1.005 (5000 ppm)  to accomodate sample rate offsets in Transmitter and Receiver 


// Also used to validate frame type (len != -1)

//	Note these samples DO NOT include the PSK reference symbols which are accomodated in DemodPSK
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 00 - 0F    ACK and NAK
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 10 - 1F
	-1,-1,-1,0,-1,-1,0,-1,-1,0,-1,-1,0,0,0,-1,	// BREAK=23, IDLE=26, DISC=29, END=2C, ConRejBusy=2D, ConRejBW=2E

	(int)(1.005 * (6 + 6 + 2) * 4 * 240),	// 30 - 38 ID Frame Call sign + Grid Square, Connect request frames
	(int)(1.005 * (6 + 6 + 2) * 4 * 240),	// 30 - 38 ID Frame Call sign + Grid Square, Connect request frames
	(int)(1.005 * (6 + 6 + 2) * 4 * 240),	// 30 - 38 ID Frame Call sign + Grid Square, Connect request frames
	(int)(1.005 * (6 + 6 + 2) * 4 * 240),	// 30 - 38 ID Frame Call sign + Grid Square, Connect request frames
	(int)(1.005 * (6 + 6 + 2) * 4 * 240),	// 30 - 38 ID Frame Call sign + Grid Square, Connect request frames
	(int)(1.005 * (6 + 6 + 2) * 4 * 240),	// 30 - 38 ID Frame Call sign + Grid Square, Connect request frames
	(int)(1.005 * (6 + 6 + 2) * 4 * 240),	// 30 - 38 ID Frame Call sign + Grid Square, Connect request frames
	(int)(1.005 * (6 + 6 + 2) * 4 * 240),	// 30 - 38 ID Frame Call sign + Grid Square, Connect request frames
	(int)(1.005 * (6 + 6 + 2) * 4 * 240),	// 30, 38 ID Frame Call sign + Grid Square, Connect request frames
  
	(int)(1.005 * 240 * 3 * 4),	// 39 - 3c	 Con ACK with timing data 
	(int)(1.005 * 240 * 3 * 4),	// 39 - 3c	 Con ACK with timing data 
	(int)(1.005 * 240 * 3 * 4),	// 39 - 3c	 Con ACK with timing data 
	(int)(1.005 * 240 * 3 * 4),	// 39 - 3c	 Con ACK with timing data 
	-1,-1,-1,						// 3d - 3f			

	(int)(1.005 * (120 + (1 + 64 + 2 + 32) * 4 * 120)), // 40, 41 1 carrier 100 baud 4PSK
	(int)(1.005 * (120 + (1 + 64 + 2 + 32) * 4 * 120)), // 40, 41 1 carrier 100 baud 4PSK
	(int)(1.005 * (120 + (1 + 16 + 2 + 8) * 4 * 120)),	// 42, 43 1 carrier 100 baud 4PSK Short
	(int)(1.005 * (120 + (1 + 16 + 2 + 8) * 4 * 120)),	// 42, 43 1 carrier 100 baud 4PSK Short			
	(int)(1.005 * (120 + (120 * (8 * (1 + 108 + 2 + 36)) / 3))), // 44, 45 1 carrier 100 baud 8PSK
	(int)(1.005 * (120 + (120 * (8 * (1 + 108 + 2 + 36)) / 3))), // 44, 45 1 carrier 100 baud 8PSK	
	(int)(1.005 * (240 * 4 * (1 + 32 + 2 + 8))),	// 46, 47 1 carrier 50 baud 4FSK
	(int)(1.005 * (240 * 4 * (1 + 32 + 2 + 8))),	// 46, 47 1 carrier 50 baud 4FSK
	(int)(1.005 * (240 * 4 * (1 + 16 + 2 + 4))),	// 48, 49 ' 1 carrier 50 baud 4FSK short 
	(int)(1.005 * (240 * 4 * (1 + 16 + 2 + 4))),	// 48, 49 ' 1 carrier 50 baud 4FSK short 
	(int)(1.005 * (120 * 4 * (1 + 64 + 2 + 16))),	// 4A, 4B ' 1 carrier 100 baud 4FSK 
	(int)(1.005 * (120 * 4 * (1 + 64 + 2 + 16))),	// 4A, 4B ' 1 carrier 100 baud 4FSK 			
	(int)(1.005 * (120 * 4 * (1 + 32 + 2 + 8))),	// 4C, 4d ' 1 carrier 100 baud 4FSK short 
	(int)(1.005 * (120 * 4 * (1 + 32 + 2 + 8))),	// 4C, 4d ' 1 carrier 100 baud 4FSK short 
	(int)(1.005 * (480 * (8 * (1 + 24 + 2 + 6)) / 3)),	// 4E, 4F ' 1 carrier 25 baud 8FSK 
	(int)(1.005 * (480 * (8 * (1 + 24 + 2 + 6)) / 3)),	// 4E, 4F ' 1 carrier 25 baud 8FSK 

	(int)(1.005 * (120 + (1 + 64 + 2 + 32) * 4 * 120)),	// 50, 51 ' 2 carrier 100 baud 4PSK
	(int)(1.005 * (120 + (1 + 64 + 2 + 32) * 4 * 120)),	// 50, 51 ' 2 carrier 100 baud 4PSK
	(int)(1.005 * (120 + (120 * (8 * (1 + 108 + 2 + 36)) / 3))),	// 52, 53 ' 2 carrier 100 baud 8PSK
	(int)(1.005 * (120 + (120 * (8 * (1 + 108 + 2 + 36)) / 3))),	// 52, 53 ' 2 carrier 100 baud 8PSK
	(int)(1.005 * (72 + (1 + 120 + 2 + 40) * 4 * 72)),	// 54, 55 ' 2 carrier 167 baud 4PSK
	(int)(1.005 * (72 + (1 + 120 + 2 + 40) * 4 * 72)),	// 54, 55 ' 2 carrier 167 baud 4PSK
	(int)(1.005 * (72 + (72 * (8 * (1 + 159 + 2 + 60)) / 3))),	// 56, 57 ' 2 carrier 167 baud 8PSK
	(int)(1.005 * (72 + (72 * (8 * (1 + 159 + 2 + 60)) / 3))),	// 56, 57 ' 2 carrier 167 baud 8PSK
	(int)(1.005 * (480 * 2 * (1 + 32 + 2 + 8))),	// 58, 59 ' 1 carrier 25 baud 16FSK (in testing) 
	(int)(1.005 * (480 * 2 * (1 + 32 + 2 + 8))),	// 58, 59 ' 1 carrier 25 baud 16FSK (in testing) 
	(int)(1.005 * (480 * 2 * (1 + 16 + 2 + 4))),	// 5A, 5B ' 1 carrier 25 baud 16FSK Short (in testing) 
	(int)(1.005 * (480 * 2 * (1 + 16 + 2 + 4))),	// 5A, 5B ' 1 carrier 25 baud 16FSK Short (in testing) 
	-1,-1,-1,-1,			// 5C -5F

	(int)(1.005 * (120 + (1 + 64 + 2 + 32) * 4 * 120)),	// 60, 61 ' 4 carrier 100 baud 4PSK
	(int)(1.005 * (120 + (1 + 64 + 2 + 32) * 4 * 120)),	// 60, 61 ' 4 carrier 100 baud 4PSK
	(int)(1.005 * (120 + (120 * (8 * (1 + 108 + 2 + 36)) / 3))),	//62, 63 ' 4 carrier 100 baud 8PSK
	(int)(1.005 * (120 + (120 * (8 * (1 + 108 + 2 + 36)) / 3))),	//62, 63 ' 4 carrier 100 baud 8PSK
	(int)(1.005 * (72 + (1 + 120 + 2 + 40) * 4 * 72)),	// 64, 65 ' 4 carrier 167 baud 4PSK
	(int)(1.005 * (72 + (1 + 120 + 2 + 40) * 4 * 72)),	// 64, 65 ' 4 carrier 167 baud 4PSK
	(int)(1.005 * (72 + (72 * (8 * (1 + 159 + 2 + 60)) / 3))),	// 66, 67 ' 4 carrier 167 baud 8PSK
	(int)(1.005 * (72 + (72 * (8 * (1 + 159 + 2 + 60)) / 3))),	// 66, 67 ' 4 carrier 167 baud 8PSK
	(int)(1.005 * (120 * 4 * (1 + 64 + 2 + 16))),	// 68, 69 ' 2 carrier 100 baud 4FSK 
	(int)(1.005 * (120 * 4 * (1 + 64 + 2 + 16))),	// 68, 69 ' 2 carrier 100 baud 4FSK 
	-1,-1,-1,-1,-1,-1,				// 6A - 6F

	(int)(1.005 * (120 + (1 + 64 + 2 + 32) * 4 * 120)),	// 70, 71 ' 8 carrier 100 baud 4PSK
	(int)(1.005 * (120 + (1 + 64 + 2 + 32) * 4 * 120)),	// 70, 71 ' 8 carrier 100 baud 4PSK
	(int)(1.005 * (120 + (120 * (8 * (1 + 108 + 2 + 36)) / 3))),	// 72, 73 ' 8 carrier 100 baud 8PSK
	(int)(1.005 * (120 + (120 * (8 * (1 + 108 + 2 + 36)) / 3))),	// 72, 73 ' 8 carrier 100 baud 8PSK
   	(int)(1.005 * (72 + (1 + 120 + 2 + 40) * 4 * 72)),	//74, 75 ' 8 carrier 167 baud 4PSK
	(int)(1.005 * (72 + (1 + 120 + 2 + 40) * 4 * 72)),	//74, 75 ' 8 carrier 167 baud 4PSK            
	(int)(1.005 * (72 + (72 * (8 * (1 + 159 + 2 + 60)) / 3))),	// 76, 77 ' 2 carrier 167 baud 8PSK ' 8 carrier 167 baud 4PSK
	(int)(1.005 * (72 + (72 * (8 * (1 + 159 + 2 + 60)) / 3))),	// 76, 77 ' 2 carrier 167 baud 8PSK ' 8 carrier 167 baud 4PSK            
	(int)(1.005 * (120 * 4 * (1 + 64 + 2 + 16))),	// 78, 79 ' 4 carrier 100 baud 4FSK 
	(int)(1.005 * (120 * 4 * (1 + 64 + 2 + 16))),	// 78, 79 ' 4 carrier 100 baud 4FSK 
            
	// experimental 600 baud for VHF/UHF FM
            	
	(int)(1.005 * (20 * 4 * 3 * (1 + 200 + 2 + 50))),	// 7A, 7B ' 1 carrier 600 baud 4FSK (3 groups of 200 bytes each for RS compatibility) 
	(int)(1.005 * (20 * 4 * 3 * (1 + 200 + 2 + 50))),	// 7A, 7B ' 1 carrier 600 baud 4FSK (3 groups of 200 bytes each for RS compatibility) 
	(int)(1.005 * (20 * 4 * (1 + 200 + 2 + 50))),	// 7C, 7D ' 1 carrier 600 baud 4FSK short
	(int)(1.005 * (20 * 4 * (1 + 200 + 2 + 50))),	// 7C, 7D ' 1 carrier 600 baud 4FSK short
	-1,-1,					// 7E, 7F
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,	// 80 - 8F
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,	// 90 - 9F
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,	// A0 - AF
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,	// B0 - BF
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,	// C0 - CF

	// experimental SOUNDINGs

	(int)(1.005 * 60 * 18 * 40),		// D0
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,		// D1 - DF

	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// e0 - eF    ACK and NAK
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};	// f0 - ff

	*/

// Function to determine if a valid frame type

//extern const UCHAR isValidFrame[256];

//BOOL IsValidFrameType(UCHAR bytType)
//{
//	//  used in the minimum distance decoder (update if frames added or removed)

//	return (isValidFrame[bytType]);
//}

// Function to determine if frame type is short control frame
  
BOOL IsShortControlFrame(UCHAR bytType)
{
	if (bytType <= 0x1F) return TRUE;  // NAK
	if (bytType == 0x23 || bytType == 0x24 || bytType == 0x29 || bytType == 0x2C || bytType == 0x2D || bytType == 0x2E) return TRUE; // BREAK, IDLE, DISC, END, ConRejBusy, ConRejBW
	if (bytType >= 0xE0) return TRUE;  // ACK
	return FALSE;
}
 
//	 Function to determine if it is a data frame (Even OR Odd) 

BOOL IsDataFrame(UCHAR intFrameType)
{
	const char * String = Name(intFrameType);

	if (intFrameType == PktFrameHeader)
		return TRUE;
	
	if (String == NULL || String[0] == 0)
		return FALSE;

	if (strstr(String, ".E") || strstr(String, ".O"))
		return TRUE;

	return FALSE;
}

//    Subroutine to clear all mixed samples 

void ClearAllMixedSamples()
{
	intFilteredMixedSamplesLength = 0;
	intMFSReadPtr = 0;
	rawSamplesLength = 0;	// Clear saved
}

//  Subroutine to Initialize mixed samples

void InitializeMixedSamples()
{
	// Measure the time from release of PTT to leader detection of reply.

	intARQRTmeasuredMs = min(10000, Now - dttStartRTMeasure); //?????? needs work
	intPriorMixedSamplesLength = 120;  // zero out prior samples in Prior sample buffer
	intFilteredMixedSamplesLength = 0;	// zero out the FilteredMixedSamples array
	intMFSReadPtr = 30;				// reset the MFSReadPtr offset 30 to accomodate the filter delay
}

//	Subroutine to discard all sampled prior to current intRcvdSamplesRPtr

void DiscardOldSamples()
{
	// This restructures the intRcvdSamples array discarding all samples prior to intRcvdSamplesRPtr
 
	//not sure why we need this !!
/*
	if (RcvdSamplesLen - intRcvdSamplesRPtr <= 0)
		RcvdSamplesLen = intRcvdSamplesRPtr = 0;
	else
	{
		// This is rather slow. I'd prefer a cyclic buffer. Lets see....
		
		memmove(intRcvdSamples, &intRcvdSamples[intRcvdSamplesRPtr], (RcvdSamplesLen - intRcvdSamplesRPtr)* 2);
		RcvdSamplesLen -= intRcvdSamplesRPtr;
		intRcvdSamplesRPtr = 0;
	}
*/
}

//	Subroutine to apply 2000 Hz filter to mixed samples 

float xdblZin_1 = 0, xdblZin_2 = 0, xdblZComb= 0;  // Used in the comb generator

	// The resonators 
      
float xdblZout_0[27] = {0.0f};	// resonator outputs
float xdblZout_1[27] = {0.0f};	// resonator outputs delayed one sample
float xdblZout_2[27] = {0.0f};	// resonator outputs delayed two samples
float xdblCoef[27] = {0.0};		// the coefficients
float xdblR = 0.9995f;			// insures stability (must be < 1.0) (Value .9995 7/8/2013 gives good results)
int xintN = 120;				//Length of filter 12000/100


void FSMixFilter2000Hz(short * intMixedSamples, int intMixedSamplesLength)
{
	// assumes sample rate of 12000
	// implements  23 100 Hz wide sections   (~2000 Hz wide @ - 30dB centered on 1500 Hz)

	// FSF (Frequency Selective Filter) variables

	// This works on intMixedSamples, len intMixedSamplesLength;

	// Filtered data is appended to intFilteredMixedSamples

	float dblRn;
	float dblR2;

	float dblZin = 0;
      
	int i, j;

	float intFilteredSample = 0;			//  Filtered sample

	if (intFilteredMixedSamplesLength < 0)
		WriteDebugLog(LOGERROR, "Corrupt intFilteredMixedSamplesLength");

	dblRn = powf(xdblR, xintN);

	dblR2 = powf(xdblR, 2);

	// Initialize the coefficients
    
	if (xdblCoef[26] == 0)
	{
		for (i = 4; i <= 26; i++)
		{
			xdblCoef[i] = 2 * xdblR * cosf(2 * M_PI * i / xintN);  // For Frequency = bin i
		}
	}

	for (i = 0; i < intMixedSamplesLength; i++)
	{
		intFilteredSample = 0;

		if (i < xintN)
			dblZin = intMixedSamples[i] - dblRn * intPriorMixedSamples[i];
		else 
			dblZin = intMixedSamples[i] - dblRn * intMixedSamples[i - xintN];
 
		//Compute the Comb

		xdblZComb = dblZin - xdblZin_2 * dblR2;
		xdblZin_2 = xdblZin_1;
		xdblZin_1 = dblZin;

		// Now the resonators
		for (j = 4; j <= 26; j++)	   // calculate output for 3 resonators 
		{
			xdblZout_0[j] = xdblZComb + xdblCoef[j] * xdblZout_1[j] - dblR2 * xdblZout_2[j];
			xdblZout_2[j] = xdblZout_1[j];
			xdblZout_1[j] = xdblZout_0[j];

			//' scale each by transition coeff and + (Even) or - (Odd) 
			//' Resonators 2 and 13 scaled by .389 get best shape and side lobe supression 
			//' Scaling also accomodates for the filter "gain" of approx 60. 
 
			if (j == 4 || j == 26)
				intFilteredSample += 0.389f * xdblZout_0[j];
			else if ((j & 1) == 0)
				intFilteredSample += xdblZout_0[j];
			else
				intFilteredSample -= xdblZout_0[j];
		}

		intFilteredSample = intFilteredSample * 0.00833333333f;
		intFilteredMixedSamples[intFilteredMixedSamplesLength++] = intFilteredSample;  // rescales for gain of filter
	}
	
	// update the prior intPriorMixedSamples array for the next filter call 
   
	memmove(intPriorMixedSamples, &intMixedSamples[intMixedSamplesLength - xintN], intPriorMixedSamplesLength * 2);		 

	if (intFilteredMixedSamplesLength > 5000)
		WriteDebugLog(LOGERROR, "Corrupt intFilteredMixedSamplesLength");

}

//	Function to apply 150Hz filter used in Envelope correlator

void Filter150Hz(short * intFilterOut)
{
	// assumes sample rate of 12000
	// implements  3 100 Hz wide sections   (~150 Hz wide @ - 30dB centered on 1500 Hz)

	// FSF (Frequency Selective Filter) variables

	static float dblR = 0.9995f;		// insures stability (must be < 1.0) (Value .9995 7/8/2013 gives good results)
	static int intN = 120;				//Length of filter 12000/100
	static float dblRn;
	static float dblR2;
	static float dblCoef[17] = {0.0};			// the coefficients
	float dblZin = 0, dblZin_1 = 0, dblZin_2 = 0, dblZComb= 0;  // Used in the comb generator
	// The resonators 
      
	float dblZout_0[17] = {0.0};	// resonator outputs
	float dblZout_1[17] = {0.0};	// resonator outputs delayed one sample
	float dblZout_2[17] = {0.0};	// resonator outputs delayed two samples

	int i, j;

	float FilterOut = 0;			//  Filtered sample
	float largest = 0;

	dblRn = powf(dblR, intN);

	dblR2 = powf(dblR, 2);

	// Initialize the coefficients
    
	if (dblCoef[17] == 0)
	{
		for (i = 14; i <= 16; i++)
		{
			dblCoef[i] = 2 * dblR * cosf(2 * M_PI * i / intN);  // For Frequency = bin i
		}
	}

	for (i = 0; i < 480; i++)
	{
		if (i < intN)
			dblZin = intFilteredMixedSamples[intMFSReadPtr + i] - dblRn * 0;	// no prior mixed samples
		else
			dblZin = intFilteredMixedSamples[intMFSReadPtr + i] - dblRn * intFilteredMixedSamples[intMFSReadPtr + i - intN];

		// Compute the Comb
		
		dblZComb = dblZin - dblZin_2 * dblR2;
		dblZin_2 = dblZin_1;
		dblZin_1 = dblZin;

		// Now the resonators

		for (j = 14; j <= 16; j++)		   // calculate output for 3 resonators 
		{
			dblZout_0[j] = dblZComb + dblCoef[j] * dblZout_1[j] - dblR2 * dblZout_2[j];
			dblZout_2[j] = dblZout_1[j];
			dblZout_1[j] = dblZout_0[j];
	
			//	scale each by transition coeff and + (Even) or - (Odd) 

			// Scaling also accomodates for the filter "gain" of approx 120. 
			// These transition coefficients fairly close to optimum for WGN 0db PSK4, 100 baud (yield highest average quality) 5/24/2014
 
			if (j == 14 || j == 16)
				FilterOut = 0.2f * dblZout_0[j];	 // this transisiton minimizes ringing and peaks
			else
				FilterOut -= dblZout_0[j];
		}
		intFilterOut[i] = (int)ceil(FilterOut * 0.00833333333);	 // rescales for gain of filter
	}

}

//	Function to apply 75Hz filter used in Envelope correlator

void Filter75Hz(short * intFilterOut, BOOL blnInitialise, int intSamplesToFilter)
{
	// assumes sample rate of 12000
    // implements  3 50 Hz wide sections   (~75 Hz wide @ - 30dB centered on 1500 Hz)
	// FSF (Frequency Selective Filter) variables

	static float dblR = 0.9995f;		// insures stability (must be < 1.0) (Value .9995 7/8/2013 gives good results)
	static int intN = 240;				// Length of filter 12000/50 - delays output 120 samples from input
	static float dblRn;
	static float dblR2;
	static float dblCoef[3] = {0.0};			// the coefficients
	float dblZin = 0, dblZin_1 = 0, dblZin_2 = 0, dblZComb= 0;  // Used in the comb generator
	// The resonators 
      
	float dblZout_0[3] = {0.0};	// resonator outputs
	float dblZout_1[3] = {0.0};	// resonator outputs delayed one sample
	float dblZout_2[3] = {0.0};	// resonator outputs delayed two samples

	int i, j;

	float FilterOut = 0;			//  Filtered sample
	float largest = 0;

	dblRn = powf(dblR, intN);

	dblR2 = powf(dblR, 2);

	// Initialize the coefficients
    
	if (dblCoef[2] == 0)
	{
		for (i = 0; i <= 3; i++)
		{
			dblCoef[i] = 2 * dblR * cosf(2 * M_PI * (29 + i)/ intN);  // For Frequency = bin 29, 30, 31
		}
	}

	for (i = 0; i < intSamplesToFilter; i++)
	{
		if (i < intN)
			dblZin = intFilteredMixedSamples[intMFSReadPtr + i] - dblRn * 0;	// no prior mixed samples
		else
			dblZin = intFilteredMixedSamples[intMFSReadPtr + i] - dblRn * intFilteredMixedSamples[intMFSReadPtr + i - intN];

		// Compute the Comb
		
		dblZComb = dblZin - dblZin_2 * dblR2;
		dblZin_2 = dblZin_1;
		dblZin_1 = dblZin;

		// Now the resonators

		for (j = 0; j < 3; j++)		   // calculate output for 3 resonators 
		{
			dblZout_0[j] = dblZComb + dblCoef[j] * dblZout_1[j] - dblR2 * dblZout_2[j];
			dblZout_2[j] = dblZout_1[j];
			dblZout_1[j] = dblZout_0[j];
	
			//	scale each by transition coeff and + (Even) or - (Odd) 

			// Scaling also accomodates for the filter "gain" of approx 120. 
			// These transition coefficients fairly close to optimum for WGN 0db PSK4, 100 baud (yield highest average quality) 5/24/2014
 
			if (j == 0 || j == 2)
				FilterOut -= 0.39811f * dblZout_0[j];	 // this transisiton minimizes ringing and peaks
			else
				FilterOut += dblZout_0[j];
		}
		intFilterOut[i] = (int)ceil(FilterOut * 0.0041f);	 // rescales for gain of filter
	}
}

// Subroutine to Mix new samples with NCO to tune to nominal 1500 Hz center with reversed sideband and filter. 

void MixNCOFilter(short * intNewSamples, int Length, float dblOffsetHz)
{
	// Correct the dimension of intPriorMixedSamples if needed (should only happen after a bandwidth setting change). 

	int i;
	short intMixedSamples[2400];	// All we need at once ( I hope!)		// may need to be int
	int	intMixedSamplesLength ;		//size of intMixedSamples

	if (Length == 0)
		return;

	// Nominal NCO freq is 3000 Hz  to downmix intNewSamples  (NCO - Fnew) to center of 1500 Hz (invertes the sideband too) 

	dblNCOFreq = 3000 + dblOffsetHz;
	dblNCOPhaseInc = dblNCOFreq * dbl2Pi / 12000;

	intMixedSamplesLength = Length;

	for (i = 0; i < Length; i++)
	{
		intMixedSamples[i] = (int)ceilf(intNewSamples[i] * cosf(dblNCOPhase));  // later may want a lower "cost" implementation of "Cos"
		dblNCOPhase += dblNCOPhaseInc;
		if (dblNCOPhase > dbl2Pi)
			dblNCOPhase -= dbl2Pi;
	}

	
	
	// showed no significant difference if the 2000 Hz filer used for all bandwidths.
//	printtick("Start Filter");
	FSMixFilter2000Hz(intMixedSamples, intMixedSamplesLength);   // filter through the FS filter (required to reject image from Local oscillator)
//	printtick("Done Filter");

	// save for analysys

//	WriteSamples(&intFilteredMixedSamples[oldlen], Length);
//	WriteSamples(intMixedSamples, Length);

}

//	Function to Correct Raw demodulated data with Reed Solomon FEC 

int CorrectRawDataWithRS(UCHAR * bytRawData, UCHAR * bytCorrectedData, int intDataLen, int intRSLen, int bytFrameType, int Carrier)
{
	BOOL blnRSOK;
	BOOL FrameOK;

	//Dim bytNoRS(1 + intDataLen + 2 - 1) As Byte  ' 1 byte byte Count, Data, 2 byte CRC 
	//Array.Copy(bytRawData, 0, bytNoRS, 0, bytNoRS.Length)

	if (CarrierOk[Carrier] && CarrierOk[Carrier] != 1)
		CarrierOk[Carrier] = CarrierOk[Carrier];

	if (CarrierOk[Carrier])	// Already decoded this carrier?
	{
		// Athough we have already checked the data, it may be in the wrong place
		// in the buffer if another carrier was decoded wrong.

		memcpy(bytCorrectedData, &bytRawData[1], bytRawData[0]);    

		WriteDebugLog(LOGDEBUG, "[CorrectRawDataWithRS] Carrier %d already decoded", Carrier);
		return bytRawData[0];			// don't do it again
	}

	if (CheckCRC16FrameType(bytRawData, intDataLen + 1, bytFrameType)) // No RS correction needed
	{
		// return the actual data
		
		memcpy(bytCorrectedData, &bytRawData[1], bytRawData[0]);    
		WriteDebugLog(LOGDEBUG, "[CorrectRawDataWithRS] OK without RS");
		CarrierOk[Carrier] = TRUE;
		return bytRawData[0];
	}
	
	// Try correcting with RS Parity

	FrameOK = RSDecode(bytRawData, intDataLen + 3 + intRSLen, intRSLen, &blnRSOK);

	if (blnRSOK)
	{}
//		WriteDebugLog(LOGDEBUG, "RS Says OK without correction");
	else
	if (FrameOK)
	{}
//		WriteDebugLog(LOGDEBUG, "RS Says OK after %d correction(s)", NErrors);
	else
	{
		WriteDebugLog(LOGDEBUG, "[CorrectRawDataWithRS] RS Says Can't Correct");
		goto returnBad;
	}

    if (FrameOK &&  CheckCRC16FrameType(bytRawData, intDataLen + 1, bytFrameType)) // RS correction successful 
	{
		int intFailedByteCnt = 0;
		
		// need to fix this if we want to use it
		//  test code just to determine how many corrections were applied  ...later remove
        //for (j = 0 ; j < intDataLen + 3; j++)
		//{
		//	if (bytRawData[j] <> bytCorrectedData[j])
		//		intFailedByteCnt++;
		//}

        WriteDebugLog(LOGDEBUG, "[CorrectRawDataWithRS] OK with RS %d corrections", NErrors);
		totalRSErrors += NErrors;
 
		// End of test code

		memcpy(bytCorrectedData, &bytRawData[1], bytRawData[0]);  
		CarrierOk[Carrier] = TRUE;
		return bytRawData[0];
	}
	else
        WriteDebugLog(LOGDEBUG, "[CorrectRawDataWithRS] RS says ok but CRC still bad");
	
	// return uncorrected data without byte count or RS Parity

returnBad:

	memcpy(bytCorrectedData, &bytRawData[1], intDataLen);    

	//Array.Copy(bytRawData, 1, bytCorrectedData, 0, bytCorrectedData.Length) 
     
	CarrierOk[Carrier] = FALSE;
	return intDataLen;
}



// Subroutine to process new samples as received from the sound card via Main.ProcessCapturedData
// Only called when not transmitting

double dblPhaseInc;  // in milliradians
short intNforGoertzel[8];
short intPSKPhase_1[8], intPSKPhase_0[8];
short intCP[8];	  // Cyclic prefix offset 
float dblFreqBin[8];

void ProcessNewSamples(short * Samples, int nSamples)
{
	BOOL blnFrameDecodedOK = FALSE;

//	LookforUZ7HOLeader(Samples, nSamples);

//	printtick("Start afsk");
//	DemodAFSK(Samples, nSamples);
//	printtick("End afsk");

//	return;

	if (ProtocolState == FECSend)
		return;

	// Append new data to anything in rawSamples

	if (rawSamplesLength)
	{
		memcpy(&rawSamples[rawSamplesLength], Samples, nSamples * 2);
		rawSamplesLength += nSamples;

		nSamples = rawSamplesLength;
		Samples = rawSamples;
	}

	rawSamplesLength = 0;

	if (nSamples < 1024)
	{
		memmove(rawSamples, Samples, nSamples * 2);
		rawSamplesLength = nSamples;
		return;							// FFT needs 1024 samples
	}

	UpdateBusyDetector(Samples);

	// searchforleader runs on unmixed and unfilered samples

	// Searching for leader

	if (State == SearchingForLeader)
	{
		// Search for leader as long as 960 samples (8  symbols) available

//		printtick("Start Leader Search");

		if (nSamples >= 1200)
		{
			//	printtick("Start Busy");
//			if (State == SearchingForLeader)
//				UpdateBusyDetector(Samples);
			//	printtick("Done Busy");
		
			if (ProtocolState == FECSend)
					return;
		}
		while (State == SearchingForLeader && nSamples >= 1200)
		{
			int intSN;
			
			blnLeaderFound = SearchFor2ToneLeader3(Samples, nSamples, &dblOffsetHz, &intSN);
//			blnLeaderFound = SearchFor2ToneLeader2(Samples, nSamples, &dblOffsetHz, &intSN);
		
			if (blnLeaderFound)
			{
//				WriteDebugLog(LOGDEBUG, "Got Leader");

				dttLastLeaderDetect = Now;

				nSamples -= 480;
				Samples += 480;		// !!!! needs attention !!!

				InitializeMixedSamples();
				State = AcquireSymbolSync;
			}
			else
			{
				if (SlowCPU)
				{
					nSamples -= 480;
					Samples += 480;		 // advance pointer 2 symbols (40 ms) ' reduce CPU loading
				}
				else
				{
					nSamples -= 240;
					Samples += 240;		// !!!! needs attention !!!
				}
			}
		}
		if (State == SearchingForLeader)
		{
			// Save unused samples

			memmove(rawSamples, Samples, nSamples * 2);
			rawSamplesLength = nSamples;

//			printtick("End Leader Search");

			return;
		}
	}


	// Got leader

	//	At this point samples haven't been processed, and are in Samples, len nSamples

	// I'm going to filter all samples into intFilteredMixedSamples.

//	printtick("Start Mix");

	MixNCOFilter(Samples, nSamples, dblOffsetHz); // Mix and filter new samples (Mixing consumes all intRcvdSamples)
	nSamples = 0;	//	all used

//	printtick("Done Mix Samples");

	// Acquire Symbol Sync 

    if (State == AcquireSymbolSync)
	{
		if ((intFilteredMixedSamplesLength - intMFSReadPtr) > 860)
		{
			blnSymbolSyncFound = Acquire2ToneLeaderSymbolFraming();  // adjust the pointer to the nominal symbol start based on phase
			if (blnSymbolSyncFound)
				State = AcquireFrameSync;
			else
			{
				DiscardOldSamples();
				ClearAllMixedSamples();
				State = SearchingForLeader;
				return;
			}
//			printtick("Got Sym Sync");
		}
	}
	
	//	Acquire Frame Sync
	
	if (State == AcquireFrameSync)
	{
		blnFrameSyncFound = AcquireFrameSyncRSB();
	
		if (blnFrameSyncFound)
		{
			State = AcquireFrameType;
				
			//	Have frame Sync. Remove used samples from buffer

//			printtick("Got Frame Sync");

		}

		// Remove used samples

		intFilteredMixedSamplesLength -= intMFSReadPtr;

			if (intFilteredMixedSamplesLength < 0)
		WriteDebugLog(LOGDEBUG, "Corrupt intFilteredMixedSamplesLength");


		memmove(intFilteredMixedSamples,
			&intFilteredMixedSamples[intMFSReadPtr], intFilteredMixedSamplesLength * 2);

		intMFSReadPtr = 0;

		if ((Now - dttLastLeaderDetect) > 1000)		 // no Frame sync within 1000 ms (may want to make this limit a funciton of Mode and leaders)
		{
			DiscardOldSamples();
			ClearAllMixedSamples();
			State = SearchingForLeader;
			printtick("frame sync timeout");
		}
	}
	
	//	Acquire Frame Type

	if (State == AcquireFrameType)
	{
//		printtick("getting frame type");

		intFrameType = Acquire4FSKFrameType();
		if (intFrameType == -2)
		{
//			sprintf(Msg, "not enough %d %d", intFilteredMixedSamplesLength, intMFSReadPtr);
//			printtick(Msg);
			return;		//  insufficient samples
		}

		if (intFrameType == -1)		  // poor decode quality (large decode distance)
		{
			State = SearchingForLeader;
			ClearAllMixedSamples();
			DiscardOldSamples();
			WriteDebugLog(LOGDEBUG, "poor frame type decode");

			// stcStatus.BackColor = SystemColors.Control
			// stcStatus.Text = ""
			// stcStatus.ControlName = "lblRcvFrame"
			// queTNCStatus.Enqueue(stcStatus)
		}
		else
		{
			//	Get Frame info and Initialise Demodulate variables

			// We've used intMFSReadPtr samples, so remove from Buffer

//			sprintf(Msg, "Got Frame Type %x", intFrameType);
//			printtick(Msg);

			intFilteredMixedSamplesLength -= intMFSReadPtr;
	
			if (intFilteredMixedSamplesLength < 0)
				WriteDebugLog(LOGDEBUG, "Corrupt intFilteredMixedSamplesLength");
	
			memmove(intFilteredMixedSamples,
				&intFilteredMixedSamples[intMFSReadPtr], intFilteredMixedSamplesLength * 2); 

			intMFSReadPtr = 0;

			if (!FrameInfo(intFrameType, &blnOdd, &intNumCar, strMod, &intBaud, &intDataLen, &intRSLen, &bytMinQualThresh, strType))
			{
				printtick("bad frame type");
				State = SearchingForLeader;
				ClearAllMixedSamples();
				DiscardOldSamples();
				return;
			}

			if (IsShortControlFrame(intFrameType))
			{
				// Frame has no data so is now complete
				frameLen = 0;

				// See if IRStoISS shortcut can be invoked

				DrawRXFrame(1, Name(intFrameType));
				
				if (ProtocolState == IRStoISS && intFrameType >= 0xe0)
				{
					//	In this state transition to ISS if  ACK frame 
				
					txSleep(250);

					WriteDebugLog(LOGDEBUG, "[ARDOPprotocol.ProcessNewSamples] ProtocolState=IRStoISS, substate = %s ACK received. Cease BREAKS, NewProtocolState=ISS, substate ISSData", ARQSubStates[ARQState]);
					blnEnbARQRpt = FALSE;	// stop the BREAK repeats
					intLastARQDataFrameToHost = -1; // initialize to illegal value to capture first new ISS frame and pass to host

					if (bytCurrentFrameType == 0) //  hasn't been initialized yet
					{
						WriteDebugLog(LOGDEBUG, "[ARDOPprotocol.ProcessNewSamples, ProtocolState=IRStoISS, Initializing GetNextFrameData");
		   				GetNextFrameData(&intShiftUpDn, 0, "", TRUE); // just sets the initial data, frame type, and sets intShiftUpDn= 0
					}

					SetARDOPProtocolState(ISS);
					intLinkTurnovers += 1;
					ARQState = ISSData;			
					SendData();				 //       Send new data from outbound queue and set up repeats
					goto skipDecode;
				}
     
				// prepare for next

				DiscardOldSamples();
				ClearAllMixedSamples();
				State = SearchingForLeader;
				blnFrameDecodedOK = TRUE;
				WriteDebugLog(LOGDEBUG, "[DecodeFrame] Frame: %s ", Name(intFrameType));

				DecodeCompleteTime = Now;

				goto ProcessFrame;
			}

			DrawRXFrame(0, Name(intFrameType));

			if (intBaud == 25)
				intSampPerSym = 480;
			if (intBaud == 50)
				intSampPerSym = 240;
			else if (intBaud == 100)
				intSampPerSym = 120;
			else if (intBaud == 167)
				intSampPerSym = 72;
			else if (intBaud == 600)
				intSampPerSym = 20;

			if (IsDataFrame(intFrameType))
				SymbolsLeft = intDataLen + intRSLen + 3; // Data has crc + length byte
			else
				SymbolsLeft = intDataLen + intRSLen;	// No CRC

			if (intDataLen == 600)
				SymbolsLeft += 6;		// 600 baud has 3 * RS Blocks

			// Save data rate for PTC reporting

			if (Rate[intFrameType] > 0)
				DataRate = Rate[intFrameType];

			intToneMagsLength = 16 * SymbolsLeft;	// 4 tones, 2 bits per set
			
			memset(intToneMagsIndex, 0, sizeof(intToneMagsIndex));
			
			charIndex = 0;	
			PSKInitDone = 0;
			
			frameLen = 0;
			totalRSErrors = 0;

			DummyCarrier = 0;	// pseudo carrier used for long 600 baud frames
			Decode600Buffer = bytFrameData1;

			if (!IsShortControlFrame(intFrameType))
			{
               //         stcStatus.BackColor = Color.Khaki
               //         stcStatus.Text = strType
               //         stcStatus.ControlName = "lblRcvFrame"
               //         queTNCStatus.Enqueue(stcStatus)
			}

			State = AcquireFrame;
			
			if (ProtocolMode == FEC && IsDataFrame(intFrameType) && ProtocolState != FECSend)
				SetARDOPProtocolState(FECRcv);

			// if a data frame, and not the same frame type as last, reinitialise 
			// correctly received carriers byte and memory ARQ fields

//			if (IsDataFrame(intFrameType) && LastDataFrameType != intFrameType)

			if (intFrameType == PktFrameHeader || intFrameType == PktFrameData)
			{
				memset(CarrierOk, 0, sizeof(CarrierOk));
				memset(intSumCounts, 0, sizeof(intSumCounts));
#ifdef MEMORYARQ
				memset(intToneMagsAvg, 0, sizeof(intToneMagsAvg));
				memset(intCarPhaseAvg, 0, sizeof(intCarPhaseAvg));
				memset(intCarMagAvg, 0, sizeof(intCarMagAvg));
#endif
				LastDataFrameType = intFrameType;
			}
			else if (LastDataFrameType != intFrameType)
			{
				WriteDebugLog(LOGDEBUG, "New frame type - MEMARQ flags reset");
				memset(CarrierOk, 0, sizeof(CarrierOk));
				LastDataFrameType = intFrameType;

				// note that although we only do mem arq if enough RAM we
				// still skip decoding carriers that have been received;

#ifdef MEMORYARQ
				memset(intSumCounts, 0, sizeof(intSumCounts));
				memset(intToneMagsAvg, 0, sizeof(intToneMagsAvg));
				memset(intCarPhaseAvg, 0, sizeof(intCarPhaseAvg));
				memset(intCarMagAvg, 0, sizeof(intCarMagAvg));
#endif
			}

			WriteDebugLog(LOGDEBUG, "MEMARQ Flags %d %d %d %d %d %d %d %d",
				CarrierOk[0], CarrierOk[1], CarrierOk[2], CarrierOk[3],
				CarrierOk[4], CarrierOk[5], CarrierOk[6], CarrierOk[7]);
		}
	}
	// Acquire Frame

	if (State == AcquireFrame)
	{
		// Call DemodulateFrame for each set of samples


		DemodulateFrame(intFrameType);

		if (CarrierOk[0] != 0 && CarrierOk[0] != 1)
			CarrierOk[0] = 0;

		if (State == AcquireFrame)

			// We haven't got it all yet so wait for more samples	
			return;	

		//	We have the whole frame, so process it


//		printtick("got whole frame");

		if (strcmp (strMod, "4FSK") == 0)
			Update4FSKConstellation(&intToneMags[0][0], &intLastRcvdFrameQuality);
		else if (strcmp (strMod, "16FSK") == 0)
			Update16FSKConstellation(&intToneMags[0][0], &intLastRcvdFrameQuality);
		else if (strcmp (strMod, "8FSK") == 0)
			Update8FSKConstellation(&intToneMags[0][0], &intLastRcvdFrameQuality);

		// PSK and QAM quality done in Decode routines

		WriteDebugLog(LOGDEBUG, "Qual = %d", intLastRcvdFrameQuality);

		// This mechanism is to skip actual decoding and reply/change state...no need to decode 

		if (blnBREAKCmd && ProtocolState == IRS && ARQState == IRSData &&
			intFrameType != bytLastACKedDataFrameType)
		{
			// This to immediatly go to IRStoISS if blnBREAKCmd enabled.
		
			//Implements protocol rule 3.4 (allows faster break) and does not require a good frame decode.

			WriteDebugLog(LOGDEBUG, "[ARDOPprotocol.ProcessNewSamples] Skip Data Decoding when blnBREAKCmd and ProtcolState=IRS");
			intFrameRepeatInterval = ComputeInterFrameInterval(1000 + rand() % 2000);
			SetARDOPProtocolState(IRStoISS); // (ONLY IRS State where repeats are used)				
			SendCommandToHost("STATUS QUEUE BREAK new Protocol State IRStoISS");
			blnEnbARQRpt = TRUE;  // setup for repeats until changeover
			WriteDebugLog(LOGDEBUG, "[ARDOPprotocol.ProcessNewSamples] %d bytes to send in ProtocolState: %s: Send BREAK,  New state=IRStoISS (Rule 3.3)",
					bytDataToSendLength,  ARDOPStates[ProtocolState]);
 			EncLen = Encode4FSKControl(BREAK, bytSessionID, bytEncodedBytes);
			Mod4FSKDataAndPlay(BREAK, &bytEncodedBytes[0], EncLen, intARQDefaultDlyMs);		// only returns when all sent

			WriteDebugLog(LOGDEBUG, "[ARDOPprotocol.ProcessNewSamples] Skip Data Decoding when blnBREAKCmd and ProtcolState=IRS");
			blnBREAKCmd = FALSE;
			goto skipDecode;
		}
						
		if (ProtocolState == IRStoISS && IsDataFrame(intFrameType))
		{
			//	In this state answer any data frame with BREAK 
		    // not necessary to decode the frame ....just frame type

			WriteDebugLog(LOGDEBUG, "[ARDOPprotocol.ProcessNewSamples] Skip Data Decoding when ProtcolState=IRStoISS, Answer with BREAK");
			intFrameRepeatInterval = ComputeInterFrameInterval(1000 + rand() % 2000);
			blnEnbARQRpt = TRUE;  // setup for repeats until changeover
 			EncLen = Encode4FSKControl(BREAK, bytSessionID, bytEncodedBytes);
			Mod4FSKDataAndPlay(BREAK, &bytEncodedBytes[0], EncLen, intARQDefaultDlyMs);		// only returns when all sent
			goto skipDecode;
		}						
		
		if (ProtocolState == IRStoISS && intFrameType >= 0xe0)
		{
			//	In this state transition to ISS if  ACK frame 
		
			WriteDebugLog(LOGDEBUG, "[ARDOPprotocol.ProcessNewSamples] ProtocolState=IRStoISS, substate = %s ACK received. Cease BREAKS, NewProtocolState=ISS, substate ISSData", ARQSubStates[ARQState]);
			blnEnbARQRpt = FALSE;	// stop the BREAK repeats
			intLastARQDataFrameToHost = -1; // initialize to illegal value to capture first new ISS frame and pass to host

			if (bytCurrentFrameType == 0) //  hasn't been initialized yet
			{
				WriteDebugLog(LOGDEBUG, "[ARDOPprotocol.ProcessNewSamples, ProtocolState=IRStoISS, Initializing GetNextFrameData");
   				GetNextFrameData(&intShiftUpDn, 0, "", TRUE); // just sets the initial data, frame type, and sets intShiftUpDn= 0
			}

			SetARDOPProtocolState(ISS);
			intLinkTurnovers += 1;
			ARQState = ISSData;			
			SendData();				 //       Send new data from outbound queue and set up repeats
			goto skipDecode;
		}
     
		blnFrameDecodedOK = DecodeFrame(intFrameType, bytData);

ProcessFrame:	

		if (!blnFrameDecodedOK)
			DrawRXFrame(2, Name(intFrameType));

		if (intFrameType == PktFrameData)
		{
			SetLED(PKTLED, TRUE);		// Flash LED
			PKTLEDTimer = Now + 400;	// For 400 Ms	
			return;
		}

		if (blnFrameDecodedOK)
		{
			// Set input level if supported
			
#ifdef HASPOTS
			CheckandAdjustRXLevel(lastmax, lastmin, TRUE);
#endif
			if (AccumulateStats)
				if (IsDataFrame(intFrameType))
					if (strstr (strMod, "PSK"))
						intGoodPSKFrameDataDecodes++;
					else if (strstr (strMod, "QAM"))
						intGoodQAMFrameDataDecodes++;
					else	
						intGoodFSKFrameDataDecodes++;


			if (IsDataFrame(intFrameType))
			{
				SetLED(PKTLED, TRUE);		// Flash LED
				PKTLEDTimer = Now + 400;	// For 400 Ms
			}
		
			if (ProtocolMode == FEC)
			{
				if (IsDataFrame(intFrameType))	// ' check to see if a data frame
					ProcessRcvdFECDataFrame(intFrameType, bytData, blnFrameDecodedOK);
				else if (intFrameType == 0x30)
					AddTagToDataAndSendToHost(bytData, "IDF", frameLen);
				else if (intFrameType >= 0x31 && intFrameType <= 0x38)
					ProcessUnconnectedConReqFrame(intFrameType, bytData);
				else if (intFrameType == PING)
					ProcessPingFrame(bytData);
				else if (intFrameType == DISCFRAME) 
				{
					// Special case to process DISC from previous connection (Ending station must have missed END reply to DISC) Handles protocol rule 1.5
    
					WriteDebugLog(LOGDEBUG, "[ARDOPprotocol.ProcessNewSamples]  DISC frame received in ProtocolMode FEC, Send END with SessionID= %XX", bytLastARQSessionID);

					tmrFinalID = Now + 3000;			
					blnEnbARQRpt = FALSE;

					EncLen = Encode4FSKControl(END, bytLastARQSessionID, bytEncodedBytes);
					Mod4FSKDataAndPlay(END, &bytEncodedBytes[0], EncLen, LeaderLength);		// only returns when all sent

					// Drop through
				}
			}				
			else if (ProtocolMode == RXO)
			{
				ProcessRXOFrame(intFrameType, frameLen, bytData, TRUE);
			}
			else if (ProtocolMode == ARQ)
			{
				if (!blnTimeoutTriggered)
					ProcessRcvdARQFrame(intFrameType, bytData, frameLen, blnFrameDecodedOK);  // Process connected ARQ frames here 

				// If still in DISC monitor it
				
				if (ProtocolState == DISC && Monitor)		  // allows ARQ mode to operate like FEC when not connected
					if (intFrameType == 0x30)				
						AddTagToDataAndSendToHost(bytData, "IDF", frameLen);			
					else if (intFrameType >= 0x31 && intFrameType <= 0x38)
						ProcessUnconnectedConReqFrame(intFrameType, bytData);			
					else if (IsDataFrame(intFrameType)) // check to see if a data frame
						ProcessRcvdFECDataFrame(intFrameType, bytData, blnFrameDecodedOK);			
			}
			else
			{
				// Unknown Mode
				bytData[frameLen] = 0;
				WriteDebugLog(LOGDEBUG, "Received Data, No State %s", bytData);
			}
		}
		else
		{
			//	Bad decode

            if (AccumulateStats)
				if (IsDataFrame(intFrameType))
					if (strstr (strMod, "PSK"))
						intFailedPSKFrameDataDecodes++;
					else if (strstr (strMod, "QAM"))
						intFailedQAMFrameDataDecodes++;
					else
						intFailedFSKFrameDataDecodes++;


            // Debug.WriteLine("[DecodePSKData2] bytPass = " & Format(bytPass, "X"))
	
			if (ProtocolMode == FEC)
			{
				if (IsDataFrame(intFrameType))	// ' check to see if a data frame
					ProcessRcvdFECDataFrame(intFrameType, bytData, blnFrameDecodedOK);
				else if (intFrameType == 0x30)
					AddTagToDataAndSendToHost(bytData, "ERR", frameLen);
			}				
			else if (ProtocolMode == RXO)
			{
				ProcessRXOFrame(intFrameType, frameLen, bytData, FALSE);
			}
			else if (ProtocolMode == ARQ)
			{
				if (ProtocolState == DISC)		  // allows ARQ mode to operate like FEC when not connected
				{
					if (intFrameType == 0x30)				
						AddTagToDataAndSendToHost(bytData, "ERR", frameLen);			

					else if (IsDataFrame(intFrameType))		// check to see if a data frame
						ProcessRcvdFECDataFrame(intFrameType, bytData, blnFrameDecodedOK);
				}
				if (!blnTimeoutTriggered)
					ProcessRcvdARQFrame(intFrameType, bytData, frameLen, blnFrameDecodedOK);  // Process connected ARQ frames here 
 
			}
  			if (ProtocolMode == FEC && ProtocolState != FECSend)
			{
				SetARDOPProtocolState(DISC);
				InitializeConnection();
			}
		}
		if (ProtocolMode == FEC && ProtocolState != FECSend)
		{
			SetARDOPProtocolState(DISC);
			InitializeConnection();
		}
skipDecode:			
		State = SearchingForLeader;
		ClearAllMixedSamples();
		DiscardOldSamples();
		return;

	}
}
// Subroutine to compute Goertzel algorithm and return Real and Imag components for a single frequency bin

void GoertzelRealImag(short intRealIn[], int intPtr, int N, float m, float * dblReal, float * dblImag)
{
	// intRealIn is a buffer at least intPtr + N in length
	// N need not be a power of 2
	// m need not be an integer
	// Computes the Real and Imaginary Freq values for bin m
	// Verified to = FFT results for at least 10 significant digits
	// Timings for 1024 Point on Laptop (64 bit Core Duo 2.2 Ghz)
	//        GoertzelRealImag .015 ms   Normal FFT (.5 ms)
	//  assuming Goertzel is proportional to N and FFT time proportional to Nlog2N
	//  FFT:Goertzel time  ratio ~ 3.3 Log2(N)

	//  Sanity check

	//if (intPtr < 0 Or (intRealIn.Length - intPtr) < N Then
    //        dblReal = 0 : dblImag = 0 : Exit Sub
     //   End If

	float dblZ_1 = 0.0f, dblZ_2 = 0.0f, dblW = 0.0f;
	float dblCoeff = 2 * cosf(2 * M_PI * m / N);
	int i;

	for (i = 0; i <= N; i++)
	{
		if (i == N)
			dblW = dblZ_1 * dblCoeff - dblZ_2;
		else
			dblW = intRealIn[intPtr] + dblZ_1 * dblCoeff - dblZ_2;

		dblZ_2 = dblZ_1;
		dblZ_1 = dblW;
		intPtr++;
	}
	*dblReal = 2 * (dblW - cosf(2 * M_PI * m / N) * dblZ_2) / N;  // scale results by N/2
	*dblImag = 2 * (sinf(2 * M_PI * m / N) * dblZ_2) / N;  // scale results by N/2   (this sign agrees with Scope DSP phase values) 
}

// Subroutine to compute Goertzel algorithm and return Real and Imag components for a single frequency bin with a Hanning Window function

float dblHanWin[120];
float dblHanAng;
int HanWinLen = 0;

void GoertzelRealImagHanning(short intRealIn[], int intPtr, int N, float m, float * dblReal, float * dblImag)
{
	// intRealIn is a buffer at least intPtr + N in length
	// N need not be a power of 2
	// m need not be an integer
	// Computes the Real and Imaginary Freq values for bin m
	// Verified to = FFT results for at least 10 significant digits
	// Timings for 1024 Point on Laptop (64 bit Core Duo 2.2 Ghz)
	//        GoertzelRealImag .015 ms   Normal FFT (.5 ms)
	//  assuming Goertzel is proportional to N and FFT time proportional to Nlog2N
	//  FFT:Goertzel time  ratio ~ 3.3 Log2(N)

	//  Sanity check
 
  	float dblZ_1 = 0.0f, dblZ_2 = 0.0f, dblW = 0.0f;
	float dblCoeff = 2 * cosf(2 * M_PI * m / N);

	int i;

	if (HanWinLen != N)  //if there is any change in N this is then recalculate the Hanning Window...this mechanism reduces use of Cos
	{
		HanWinLen = N;

		dblHanAng = 2 * M_PI / (N - 1);

		for (i = 0; i < N; i++)
		{
			dblHanWin[i] = 0.5 - 0.5 * cosf(i * dblHanAng);
		}
	}

	for (i = 0; i <= N; i++)
	{
		if (i == N)
			dblW = dblZ_1 * dblCoeff - dblZ_2;
		else
			dblW = intRealIn[intPtr]  * dblHanWin[i] + dblZ_1 * dblCoeff - dblZ_2;

		dblZ_2 = dblZ_1;
		dblZ_1 = dblW;
		intPtr++;
	}
	
	*dblReal = 2 * (dblW - cosf(2 * M_PI * m / N) * dblZ_2) / N;  // scale results by N/2
	*dblImag = 2 * (sinf(2 * M_PI * m / N) * dblZ_2) / N;  // scale results by N/2   (this sign agrees with Scope DSP phase values) 
}

float dblHamWin[1200];
float dblHamAng;
int HamWinLen = 0;

void GoertzelRealImagHamming(short intRealIn[], int intPtr, int N, float m, float * dblReal, float * dblImag)
{
	// intRealIn is a buffer at least intPtr + N in length
	// N need not be a power of 2
	// m need not be an integer
	// Computes the Real and Imaginary Freq values for bin m
	// Verified to = FFT results for at least 10 significant digits
	// Timings for 1024 Point on Laptop (64 bit Core Duo 2.2 Ghz)
	//        GoertzelRealImag .015 ms   Normal FFT (.5 ms)
	//  assuming Goertzel is proportional to N and FFT time proportional to Nlog2N
	//  FFT:Goertzel time  ratio ~ 3.3 Log2(N)

	//  Sanity check
 
  	float dblZ_1 = 0.0f, dblZ_2 = 0.0f, dblW = 0.0f;
	float dblCoeff = 2 * cosf(2 * M_PI * m / N);

	int i;

	if (HamWinLen != N)  //if there is any cHamge in N this is then recalculate the Hanning Window...this mechanism reduces use of Cos
	{
		HamWinLen = N;

		dblHamAng = 2 * M_PI / (N - 1);

		for (i = 0; i < N; i++)
		{
			dblHamWin[i] = 0.54f - 0.46f * cosf(i * dblHamAng);
		}
	}

	for (i = 0; i <= N; i++)
	{
		if (i == N)
			dblW = dblZ_1 * dblCoeff - dblZ_2;
		else
			dblW = intRealIn[intPtr]  * dblHamWin[i] + dblZ_1 * dblCoeff - dblZ_2;

		dblZ_2 = dblZ_1;
		dblZ_1 = dblW;
		intPtr++;
	}
	
	*dblReal = 2 * (dblW - cosf(2 * M_PI * m / N) * dblZ_2) / N;  // scale results by N/2
	*dblImag = 2 * (sinf(2 * M_PI * m / N) * dblZ_2) / N;  // scale results by N/2   (this sign agrees with Scope DSP phase values) 
}

// Function to interpolate spectrum peak using Quinn algorithm

float QuinnSpectralPeakLocator(float XkM1Re, float XkM1Im, float XkRe, float XkIm, float XkP1Re, float XkP1Im)
{
	// based on the Quinn algorithm in Streamlining Digital Processing page 139
	// Alpha1 = Re(Xk-1/Xk)
	// Alpha2 = Re(Xk+1/Xk)
	//Delta1 = Alpha1/(1 - Alpha1)
	//'Delta2 = Alpha2/(1 - Alpha2)
	// if Delta1 > 0 and Delta2 > 0 then Delta = Delta2 else Delta = Delta1
	// should be within .1 bin for S:N > 2 dB

	float dblDenom = powf(XkRe, 2) + powf(XkIm, 2);
	float dblAlpha1;
	float dblAlpha2;
	float dblDelta1;
	float dblDelta2;

	dblAlpha1 = ((XkM1Re * XkRe) + (XkM1Im * XkIm)) / dblDenom;
	dblAlpha2 = ((XkP1Re * XkRe) + (XkP1Im * XkIm)) / dblDenom;
	dblDelta1 = dblAlpha1 / (1 - dblAlpha1);
	dblDelta2 = dblAlpha2 / (1 - dblAlpha2);

	if (dblDelta1 > 0 &&  dblDelta2 > 0)
		return dblDelta2;
	else
		return dblDelta1;
}

// Function to interpolate spectrum peak using simple interpolation 

float SpectralPeakLocator(float XkM1Re, float XkM1Im, float XkRe, float XkIm, float XkP1Re, float XkP1Im, float * dblCentMag)
{
	// Use this for Windowed samples instead of QuinnSpectralPeakLocator

	float dblLeftMag, dblRightMag;
	*dblCentMag = sqrtf(powf(XkRe, 2) + powf(XkIm, 2));

	dblLeftMag  = sqrtf(powf(XkM1Re, 2) + powf(XkM1Im, 2));
	dblRightMag  = sqrtf(powf(XkP1Re, 2) + powf(XkP1Im, 2));

	//Factor 1.22 empirically determine optimum for Hamming window
	// For Hanning Window use factor of 1.36
	// For Blackman Window use factor of  1.75
    
	return 1.22 * (dblRightMag - dblLeftMag) / (dblLeftMag + *dblCentMag + dblRightMag);  // Optimized for Hamming Window
}

// Function to detect and tune the 50 baud 2 tone leader (for all bandwidths) Updated version of SearchFor2ToneLeader2 

float dblPriorFineOffset = 1000.0f;

BOOL SearchFor2ToneLeader3(short * intNewSamples, int Length, float * dblOffsetHz, int * intSN)
{
	// This version uses 10Hz bin spacing. Hamming window on Goertzel, and simple spectral peak interpolator
	// It requires about 50% more CPU time when running but produces more sensive leader detection and more accurate tuning
	// search through the samples looking for the telltail 50 baud 2 tone pattern (nominal tones 1475, 1525 Hz)
	// Find the offset in Hz (due to missmatch in transmitter - receiver tuning
	// Finds the S:N (power ratio of the tones 1475 and 1525 ratioed to "noise" averaged from bins at 1425, 1450, 1550, and 1575Hz)
 
	float dblGoertzelReal[56];
	float dblGoertzelImag[56];
	float dblMag[56];
	float dblPower, dblLeftMag, dblRightMag;
	float dblMaxPeak = 0.0, dblMaxPeakSN = 0.0, dblBinAdj;
	int intInterpCnt = 0;  // the count 0 to 3 of the interpolations that were < +/- .5 bin
	int  intIatMaxPeak = 0;
	float dblAlpha = 0.3f;  // Works well possibly some room for optimization Changed from .5 to .3 on Rev 0.1.5.3
	float dblInterpretThreshold= 1.0f; // Good results June 6, 2014 (was .4)  ' Works well possibly some room for optimization
	float dblFilteredMaxPeak = 0;
	int intStartBin, intStopBin;
	float dblLeftCar, dblRightCar, dblBinInterpLeft, dblBinInterpRight, dblCtrR, dblCtrI, dblLeftP, dblRightP;
	float dblLeftR[3], dblLeftI[3], dblRightR[3], dblRightI[3];
	int i;
	int Ptr = 0;
	float dblAvgNoisePerBin, dblCoarsePwrSN, dblBinAdj1475, dblBinAdj1525, dblCoarseOffset = 1000;
	float dblTrialOffset, dblPowerEarly, dblSNdBPwrEarly;

	if ((Length) < 1200)
		return FALSE;		// ensure there are at least 1200 samples (5 symbols of 240 samples)

	if ((Now - dttLastGoodFrameTypeDecode > 20000) && TuningRange > 0)
	{
		// this is the full search over the full tuning range selected.  Uses more CPU time and with possibly larger deviation once connected. 
		
		intStartBin = ((200 - TuningRange) / 10);
		intStopBin = 55 - intStartBin;

		dblMaxPeak = 0;

		// Generate the Power magnitudes for up to 56 10 Hz bins (a function of MCB.TuningRange) 
  
		for (i = intStartBin; i <= intStopBin; i++)
		{
            // note hamming window reduces end effect caused by 1200 samples (not an even multiple of 240)  but spreads response peaks
		
			GoertzelRealImagHamming(intNewSamples, Ptr, 1200, i + 122.5f, &dblGoertzelReal[i], &dblGoertzelImag[i]);
			dblMag[i] = powf(dblGoertzelReal[i], 2) + powf(dblGoertzelImag[i], 2); // dblMag(i) in units of power (V^2)
 		}

		// Search the bins to locate the max S:N in the two tone signal/avg noise.  

 		for (i = intStartBin + 5; i <= intStopBin - 10; i++)	// ' +/- MCB.TuningRange from nominal 
		{
			dblPower = sqrtf(dblMag[i] * dblMag[i + 5]); // using the product to minimize sensitivity to one strong carrier vs the two tone
			// sqrt converts back to units of power from Power ^2
			// don't use center noise bin as too easily corrupted by adjacent carriers

			dblAvgNoisePerBin = (dblMag[i - 5] + dblMag[i - 3] + dblMag[i + 8] + dblMag[i + 10]) / 4;  // Simple average
			dblMaxPeak = dblPower / dblAvgNoisePerBin;
			if (dblMaxPeak > dblMaxPeakSN)
			{
				dblMaxPeakSN = dblMaxPeak;
				dblCoarsePwrSN = 10 * log10f(dblMaxPeak);
				intIatMaxPeak = i + 122;
			}
		}
		// Do the interpolation based on the two carriers at nominal 1475 and 1525Hz

		if (((intIatMaxPeak - 123) >= intStartBin) && ((intIatMaxPeak - 118) <= intStopBin)) // check to ensure no index errors
		{
			// Interpolate the adjacent bins using QuinnSpectralPeakLocator

			dblBinAdj1475 = SpectralPeakLocator(
				dblGoertzelReal[intIatMaxPeak - 123], dblGoertzelImag[intIatMaxPeak - 123],
				dblGoertzelReal[intIatMaxPeak - 122], dblGoertzelImag[intIatMaxPeak - 122], 
				dblGoertzelReal[intIatMaxPeak - 121], dblGoertzelImag[intIatMaxPeak - 121], &dblLeftMag);

			if (dblBinAdj1475 < dblInterpretThreshold && dblBinAdj1475 > -dblInterpretThreshold)
			{
				dblBinAdj = dblBinAdj1475;
				intInterpCnt += 1;
			} 

			dblBinAdj1525 = SpectralPeakLocator(
				dblGoertzelReal[intIatMaxPeak - 118], dblGoertzelImag[intIatMaxPeak - 118], 
				dblGoertzelReal[intIatMaxPeak - 117], dblGoertzelImag[intIatMaxPeak - 117], 
				dblGoertzelReal[intIatMaxPeak - 116], dblGoertzelImag[intIatMaxPeak - 116], &dblRightMag);

			if (dblBinAdj1525 < dblInterpretThreshold && dblBinAdj1525 > -dblInterpretThreshold)
			{
				dblBinAdj += dblBinAdj1525;
        		intInterpCnt += 1;
			}
			if (intInterpCnt == 0)					
			{
				dblPriorFineOffset = 1000.0f;
				return FALSE;
			}
			else
			{	
				dblBinAdj = dblBinAdj / intInterpCnt;	 // average the offsets that are within 1 bin
				dblCoarseOffset = 10.0f * (intIatMaxPeak + dblBinAdj - 147); // compute the Coarse tuning offset in Hz
			}
		}
		else
		{
			dblPriorFineOffset = 1000.0f;
			return FALSE;
		}
	}
	
	// Drop into Narrow Search
  
           
	if (dblCoarseOffset < 999)
		dblTrialOffset = dblCoarseOffset;  // use the CoarseOffset calculation from above
	else
		dblTrialOffset = *dblOffsetHz; // use the prior offset value
	
    if (fabsf(dblTrialOffset) > TuningRange && TuningRange > 0)
	{
		dblPriorFineOffset = 1000.0f;	
		return False;
	}

	dblLeftCar = 147.5f + dblTrialOffset / 10.0f;  // the nominal positions of the two tone carriers based on the last computerd dblOffsetHz
	dblRightCar = 152.5f + dblTrialOffset / 10.0f;

	// Calculate 4 bins total for Noise values in S/N computation (calculate average noise)  ' Simple average of noise bins      
	GoertzelRealImagHamming(intNewSamples, Ptr, 1200, 142.5f + dblTrialOffset / 10.0f, &dblCtrR, &dblCtrI);  // nominal center -75 Hz
	dblAvgNoisePerBin = powf(dblCtrR, 2) + powf(dblCtrI, 2);
	GoertzelRealImagHamming(intNewSamples, Ptr, 1200, 145.0f + dblTrialOffset / 10.0f, &dblCtrR, &dblCtrI); // center - 50 Hz
	dblAvgNoisePerBin += powf(dblCtrR, 2) + powf(dblCtrI, 2);
	GoertzelRealImagHamming(intNewSamples, Ptr, 1200, 155.0 + dblTrialOffset / 10.0f, &dblCtrR, &dblCtrI); // center + 50 Hz
	dblAvgNoisePerBin += powf(dblCtrR, 2) + powf(dblCtrI, 2);
	GoertzelRealImagHamming(intNewSamples, Ptr, 1200, 157.5 + dblTrialOffset / 10.0f, &dblCtrR, &dblCtrI);  // center + 75 Hz
	dblAvgNoisePerBin += powf(dblCtrR, 2) + powf(dblCtrI, 2);
	dblAvgNoisePerBin = dblAvgNoisePerBin * 0.25f; // simple average,  now units of power
  
	// Calculate one bin above and below the two nominal 2 tone positions for Quinn Spectral Peak locator
	GoertzelRealImagHamming(intNewSamples, Ptr, 1200, dblLeftCar - 1, &dblLeftR[0], &dblLeftI[0]);
	GoertzelRealImagHamming(intNewSamples, Ptr, 1200, dblLeftCar, &dblLeftR[1], &dblLeftI[1]);
	dblLeftP = powf(dblLeftR[1], 2) + powf(dblLeftI[1],  2);
	GoertzelRealImagHamming(intNewSamples, Ptr, 1200, dblLeftCar + 1, &dblLeftR[2], &dblLeftI[2]);
	GoertzelRealImagHamming(intNewSamples, Ptr, 1200, dblRightCar - 1, &dblRightR[0], &dblRightI[0]);
	GoertzelRealImagHamming(intNewSamples, Ptr, 1200, dblRightCar, &dblRightR[1], &dblRightI[1]);
	dblRightP = powf(dblRightR[1], 2) + powf(dblRightI[1], 2);
	GoertzelRealImag(intNewSamples, Ptr, 1200, dblRightCar + 1, &dblRightR[2], &dblRightI[2]);

	// Calculate the total power in the two tones 
	// This mechanism designed to reject single carrier but average both carriers if ratios is less than 4:1

	if (dblLeftP > 4 * dblRightP)
		dblPower = dblRightP;
	else if (dblRightP > 4 * dblLeftP)
		dblPower = dblLeftP;
	else
		dblPower = sqrtf(dblLeftP * dblRightP);
 
	dblSNdBPwr = 10 * log10f(dblPower / dblAvgNoisePerBin);

	// Early leader detect code to calculate S:N on the first 2 symbols)
	//  concept is to allow more accurate framing and sync detection and reduce false leader detects

	GoertzelRealImag(intNewSamples, Ptr, 480, 57.0f + dblTrialOffset / 25.0f, &dblCtrR, &dblCtrI); //  nominal center -75 Hz
	dblAvgNoisePerBin = powf(dblCtrR, 2) + powf(dblCtrI, 2);
	GoertzelRealImag(intNewSamples, Ptr, 480, 58.0f + dblTrialOffset / 25.0f, &dblCtrR, &dblCtrI); //  nominal center -75 Hz
	dblAvgNoisePerBin += powf(dblCtrR, 2) + powf(dblCtrI, 2);
	GoertzelRealImag(intNewSamples, Ptr, 480, 62.0f + dblTrialOffset / 25.0f, &dblCtrR, &dblCtrI); //  nominal center -75 Hz
	dblAvgNoisePerBin += powf(dblCtrR, 2) + powf(dblCtrI, 2);
	GoertzelRealImag(intNewSamples, Ptr, 480, 63.0f + dblTrialOffset / 25.0f, &dblCtrR, &dblCtrI); //  nominal center -75 Hz 
	dblAvgNoisePerBin = max(1000.0f, 0.25 * (dblAvgNoisePerBin + powf(dblCtrR, 2) + powf(dblCtrI, 2))); // average of 4 noise bins
	dblLeftCar = 59 + dblTrialOffset / 25;  // the nominal positions of the two tone carriers based on the last computerd dblOffsetHz
	dblRightCar = 61 + dblTrialOffset / 25;

	GoertzelRealImag(intNewSamples, Ptr, 480, dblLeftCar, &dblCtrR, &dblCtrI); // LEFT carrier
	dblLeftP = powf(dblCtrR, 2) + powf(dblCtrI, 2);
	GoertzelRealImag(intNewSamples, Ptr, 480, dblRightCar, &dblCtrR, &dblCtrI); // Right carrier
	dblRightP = powf(dblCtrR, 2) + powf(dblCtrI, 2);

	// the following rejects a single tone carrier but averages the two tones if ratio is < 4:1

	if (dblLeftP > 4 * dblRightP)
		dblPowerEarly = dblRightP;
	else if (dblRightP > 4 * dblLeftP)
		dblPowerEarly = dblLeftP;
	else
		dblPowerEarly = sqrtf(dblLeftP * dblRightP);

	dblSNdBPwrEarly = 10 * log10f(dblPowerEarly / dblAvgNoisePerBin);

	// End of Early leader detect test code 
  
	if (dblSNdBPwr > (4 + Squelch) && dblSNdBPwrEarly > Squelch && (dblAvgNoisePerBin > 100.0f || dblPriorFineOffset != 1000.0f)) // making early threshold = lower (after 3 dB compensation for bandwidth)
	{
//		WriteDebugLog(LOGDEBUG, "Fine Search S:N= %f dB, Early S:N= %f dblAvgNoisePerBin %f ", dblSNdBPwr, dblSNdBPwrEarly, dblAvgNoisePerBin);

		// Calculate the interpolation based on the left of the two tones

		dblBinInterpLeft = SpectralPeakLocator(dblLeftR[0], dblLeftI[0], dblLeftR[1], dblLeftI[1], dblLeftR[2], dblLeftI[2], &dblLeftMag);
		
		// And the right of the two tones

		dblBinInterpRight = SpectralPeakLocator(dblRightR[0], dblRightI[0], dblRightR[1], dblRightI[1], dblRightR[2], dblRightI[2], &dblRightMag);

		// Weight the interpolated values in proportion to their magnitudes
		
		dblBinInterpLeft = dblBinInterpLeft * dblLeftMag / (dblLeftMag + dblRightMag);
		dblBinInterpRight = dblBinInterpRight * dblRightMag / (dblLeftMag + dblRightMag);
	
#ifdef ARMLINUX
		{
			int x = round(dblBinInterpLeft);	// odd, but PI doesnt print floats properly 
			int y = round(dblBinInterpRight);
		
//			WriteDebugLog(LOGDEBUG, " SPL Left= %d  SPL Right= %d Offset %f, LeftMag %f RightMag %f", x, y, *dblOffsetHz, dblLeftMag, dblRightMag);
		}
#else
//		WriteDebugLog(LOGDEBUG, " SPL Left= %f  SPL Right= %f, Offset %f, LeftMag %f RightMag %f",
//			dblBinInterpLeft, dblBinInterpRight, *dblOffsetHz, dblLeftMag, dblRightMag);
#endif    
		if (fabsf(dblBinInterpLeft + dblBinInterpRight) < 1.0) // sanity check for the interpolators 
		{
			if (dblBinInterpLeft + dblBinInterpRight > 0)  // consider different bounding below
				*dblOffsetHz = dblTrialOffset + min((dblBinInterpLeft + dblBinInterpRight) * 10.0f, 3); // average left and right, adjustment bounded to +/- 3Hz max
			else
				*dblOffsetHz = dblTrialOffset + max((dblBinInterpLeft + dblBinInterpRight) * 10.0f, -3);

			// Note the addition of requiring a second detect with small offset dramatically reduces false triggering even at Squelch values of 3
			// The following demonstrated good detection down to -10 dB S:N with squelch = 3 and minimal false triggering. 
			// Added rev 0.8.2.2 11/6/2016 RM

			if (abs(dblPriorFineOffset - *dblOffsetHz) < 2.9f)
			{
				WriteDebugLog(LOGDEBUG, "Prior-Offset= %f", (dblPriorFineOffset - *dblOffsetHz));
                   		
				// Capture power for debugging ...note: convert to 3 KHz noise bandwidth from 25Hz or 12.Hz for reporting consistancy.
	
				sprintf(strDecodeCapture, "Ldr; S:N(3KHz) Early= %f dB, Full %f dB, Offset= %f Hz: ", dblSNdBPwrEarly - 20.8f, dblSNdBPwr  - 24.77f, *dblOffsetHz);

				if (AccumulateStats)
				{              
					dblLeaderSNAvg = ((dblLeaderSNAvg * intLeaderDetects) + dblSNdBPwr) / (1 + intLeaderDetects); 
					intLeaderDetects++;
				}

				dblNCOFreq = 3000 + *dblOffsetHz; // Set the NCO frequency and phase inc for mixing         
				dblNCOPhaseInc = dbl2Pi * dblNCOFreq / 12000;
				dttLastLeaderDetect = dttStartRmtLeaderMeasure = Now;
    
				State = AcquireSymbolSync;
				*intSN = dblSNdBPwr - 24.77; // 23.8dB accomodates ratio of 3Kz BW:10 Hz BW (10Log 3000/10 = 24.77)

				// don't advance the pointer here
              
				dblPriorFineOffset = 1000.0f;
				return TRUE;
			}
			else
				dblPriorFineOffset = *dblOffsetHz;

			// always use 1 symbol inc when looking for next minimal offset
		}
	}
	return FALSE;
}	


//	Function to look at the 2 tone leader and establishes the Symbol framing using envelope search and minimal phase error. 

BOOL Acquire2ToneLeaderSymbolFraming()
{
	float dblCarPh;
	float dblReal, dblImag;
	int intLocalPtr = intMFSReadPtr;  // try advancing one symbol to minimize initial startup errors 
	float dblAbsPhErr;
	float dblMinAbsPhErr = 5000;	 // initialize to an excessive value
	int intIatMinErr;
	float dblPhaseAtMinErr;
	int intAbsPeak = 0;
	int intJatPeak = 0;
	int i;

	// Use Phase of 1500 Hz leader  to establish symbol framing. Nominal phase is 0 or 180 degrees

	if ((intFilteredMixedSamplesLength - intLocalPtr) < 860)
		return FALSE;			// not enough
	
	intLocalPtr = intMFSReadPtr + EnvelopeCorrelator(); // should position the pointer at the symbol boundary

	// Check 2 samples either side of the intLocalPtr for minimum phase error.(closest to Pi or -Pi) 
	// Could be as much as .4 Radians (~70 degrees) depending on sampling positions.
   
	for (i = -2; i <= 2; i++)	 // 0 To 0 '  -2 To 2 ' for just 5 samples
	{
		// using the full symbol seemed to work best on weak Signals (0 to -5 dB S/N) June 15, 2015
	
		GoertzelRealImag(intFilteredMixedSamples, intLocalPtr + i, 120, 30, &dblReal, &dblImag); // Carrier at 1500 Hz nominal Positioning 
		dblCarPh = atan2f(dblImag, dblReal);
		dblAbsPhErr = fabsf(dblCarPh - (ceil(dblCarPh / M_PI) * M_PI));
		if (dblAbsPhErr < dblMinAbsPhErr)
		{
			dblMinAbsPhErr = dblAbsPhErr;
			intIatMinErr = i;
			dblPhaseAtMinErr = dblCarPh;
		}     
	}

	intMFSReadPtr = intLocalPtr + intIatMinErr;
	WriteDebugLog(LOGDEBUG, "[Acquire2ToneLeaderSymbolFraming] intIatMinError= %d", intIatMinErr);
	State = AcquireFrameSync;

	if (AccumulateStats)
		intLeaderSyncs++;

	//Debug.WriteLine("   [Acquire2ToneLeaderSymbolSync] iAtMinError = " & intIatMinErr.ToString & "   Ptr = " & intMFSReadPtr.ToString & "  MinAbsPhErr = " & Format(dblMinAbsPhErr, "#.00"))
	//Debug.WriteLine("   [Acquire2ToneLeaderSymbolSync]      Ph1500 @ MinErr = " & Format(dblPhaseAtMinErr, "#.000"))
        
	//strDecodeCapture &= "Framing; iAtMinErr=" & intIatMinErr.ToString & ", Ptr=" & intMFSReadPtr.ToString & ", MinAbsPhErr=" & Format(dblMinAbsPhErr, "#.00") & ": "

	return TRUE;
}

// Function to establish symbol sync 

int EnvelopeCorrelator()
{
	// Compute the two symbol correlation with the Two tone leader template.
	// slide the correlation one sample and repeat up to 240 steps 
	// keep the point of maximum or minimum correlation...and use this to identify the the symbol start. 

	float dblCorMax  = -1000000.0f;		//  Preset to excessive values
	float dblCorMin  = 1000000.0f;
	int intJatMax = 0, intJatMin = 0;
	float dblCorSum, dblCorProduct, dblCorMaxProduct = 0.0;
	int i,j;
	short int75HzFiltered[720];

	if (intFilteredMixedSamplesLength < intMFSReadPtr + 720)
		return -1;
	
	Filter75Hz(int75HzFiltered, TRUE, 720); // This filter appears to help reduce avg decode distance (10 frames) by about 14%-19% at WGN-5 May 3, 2015
	
	for (j = 0; j < 360; j++)		// Over 1.5 symbols
	{
		dblCorSum = 0;
		for (i = 0; i < 240; i++)	 // over 1 50 baud symbol (may be able to reduce to 1 symbol)
		{
			dblCorProduct = int50BaudTwoToneLeaderTemplate[i] * int75HzFiltered[120 + i + j]; // note 120 accomdates filter delay of 120 samples
			dblCorSum += dblCorProduct;
            if (fabsf(dblCorProduct) > dblCorMaxProduct)
				dblCorMaxProduct = fabsf(dblCorProduct);
		}

		if (fabsf(dblCorSum) > dblCorMax)
		{
			dblCorMax = fabsf(dblCorSum);
			intJatMax = j;
		}		
	}
	
	if (AccumulateStats)
	{
		dblAvgCorMaxToMaxProduct = (dblAvgCorMaxToMaxProduct * intEnvelopeCors + (dblCorMax / dblCorMaxProduct)) / (intEnvelopeCors + 1);
		intEnvelopeCors++;
	}
 
	if (dblCorMax > 40 * dblCorMaxProduct)
	{
		WriteDebugLog(LOGDEBUG, "EnvelopeCorrelator CorMax:MaxProd= %f  J= %d", dblCorMax / dblCorMaxProduct, intJatMax);
		return intJatMax;
	}
	else
		return -1;
}
 

//	Function to acquire the Frame Sync for all Frames 

BOOL AcquireFrameSyncRSB()
{
	// Two improvements could be incorporated into this function:
	//    1) Provide symbol tracking until the frame sync is found (small corrections should be less than 1 sample per 4 symbols ~2000 ppm)
	//    2) Ability to more accurately locate the symbol center (could be handled by symbol tracking 1) above. 

	//  This is for acquiring FSKFrameSync After Mixing Tones Mirrored around 1500 Hz. e.g. Reversed Sideband
	//  Frequency offset should be near 0 (normally within +/- 1 Hz)  
	//  Locate the sync Symbol which has no phase change from the prior symbol (BPSK leader @ 1500 Hz)   

	int intLocalPtr = intMFSReadPtr;
	int intAvailableSymbols = (intFilteredMixedSamplesLength - intMFSReadPtr) / 240;
	float dblPhaseSym1;	//' phase of the first symbol 
	float dblPhaseSym2;	//' phase of the second symbol 
	float dblPhaseSym3;	//' phase of the third symbol

	float dblReal, dblImag;
	float dblPhaseDiff12, dblPhaseDiff23;

	int i;

	if (intAvailableSymbols < 3)
		return FALSE;				// must have at least 360 samples to search
 
	// Calculate the Phase for the First symbol 
	
	GoertzelRealImag(intFilteredMixedSamples, intLocalPtr, 240, 30, &dblReal, &dblImag); // Carrier at 1500 Hz nominal Positioning with no cyclic prefix
	dblPhaseSym1 = atan2f(dblImag, dblReal);
	intLocalPtr += 240;	// advance one symbol
	GoertzelRealImag(intFilteredMixedSamples, intLocalPtr, 240, 30, &dblReal, &dblImag); // Carrier at 1500 Hz nominal Positioning with no cyclic prefix
	dblPhaseSym2 = atan2f(dblImag, dblReal);
	intLocalPtr += 240;		// advance one symbol

	for (i = 0; i <=  intAvailableSymbols - 3; i++)
	{
		// Compute the phase of the next symbol  
	
		GoertzelRealImag(intFilteredMixedSamples, intLocalPtr, 240, 30, &dblReal, &dblImag); // Carrier at 1500 Hz nominal Positioning with no cyclic prefix
		dblPhaseSym3 = atan2f(dblImag, dblReal);
		// Compute the phase differences between sym1-sym2, sym2-sym3
		dblPhaseDiff12 = dblPhaseSym1 - dblPhaseSym2;
		if (dblPhaseDiff12 > M_PI)		// bound phase diff to +/- Pi
			dblPhaseDiff12 -= dbl2Pi;
		else if (dblPhaseDiff12 < -M_PI)
			dblPhaseDiff12 += dbl2Pi;

		dblPhaseDiff23 = dblPhaseSym2 - dblPhaseSym3;
		if (dblPhaseDiff23 > M_PI)		//  bound phase diff to +/- Pi
			dblPhaseDiff23 -= dbl2Pi;
		else if (dblPhaseDiff23 < -M_PI)
			dblPhaseDiff23 += dbl2Pi;

		if (fabsf(dblPhaseDiff12) > 0.6667f * M_PI && fabsf(dblPhaseDiff23) < 0.3333f * M_PI)  // Tighten the margin to 60 degrees
		{
//			intPSKRefPhase = (short)dblPhaseSym3 * 1000;

			intLeaderRcvdMs = (int)ceil((intLocalPtr - 30) / 12);	 // 30 is to accomodate offset of inital pointer for filter length. 
			intMFSReadPtr = intLocalPtr + 240;		 // Position read pointer to start of the symbol following reference symbol 
		
			if (AccumulateStats)
				intFrameSyncs += 1;		 // accumulate tuning stats
	
			//strDecodeCapture &= "Sync; Phase1>2=" & Format(dblPhaseDiff12, "0.00") & " Phase2>3=" & Format(dblPhaseDiff23, "0.00") & ": "
	
			return TRUE;	 // pointer is pointing to first 4FSK data symbol. (first symbol of frame type)
		}
		else
		{
			dblPhaseSym1 = dblPhaseSym2;           
			dblPhaseSym2 = dblPhaseSym3;
			intLocalPtr += 240;			// advance one symbol 
		}
	}

	intMFSReadPtr = intLocalPtr - 480;		 // back up 2 symbols for next attempt (Current Sym2 will become new Sym1)
	return FALSE;	
}

//	 Function to Demod FrameType4FSK

BOOL DemodFrameType4FSK(int intPtr, short * intSamples, int * intToneMags)
{
	float dblReal, dblImag;
	int i;
	int intMagSum;
	UCHAR bytSym;

	if ((intFilteredMixedSamplesLength - intPtr) < 2400)
		return FALSE;

	intToneMagsLength = 10;

	for (i = 0; i < 10; i++)
	{
		GoertzelRealImag(intSamples, intPtr, 240, 1575 / 50.0f, &dblReal, &dblImag);
		intToneMags[4 * i] = (int)powf(dblReal, 2) + powf(dblImag, 2);
		GoertzelRealImag(intSamples, intPtr, 240, 1525 / 50.0f, &dblReal, &dblImag);
		intToneMags[1 + 4 * i] = (int)powf(dblReal, 2) + powf(dblImag, 2);
		GoertzelRealImag(intSamples, intPtr, 240, 1475 / 50.0f, &dblReal, &dblImag);
		intToneMags[2 + 4 * i] = (int)powf(dblReal, 2) + powf(dblImag, 2);
		GoertzelRealImag(intSamples, intPtr, 240, 1425 / 50.0f, &dblReal, &dblImag);
		intToneMags[3 + 4 * i] = (int)powf(dblReal, 2) + powf(dblImag, 2);
		intPtr += 240;

		// intMagSum and bytSym are used only to write tone values to debug log.
		intMagSum = intToneMags[4 * i] + intToneMags[1 + 4 * i] + intToneMags[2 + 4 * i] + intToneMags[3 + 4 * i];
		if (intToneMags[4 * i] > intToneMags[1 + 4 * i] && intToneMags[4 * i] > intToneMags[2 + 4 * i] && intToneMags[4 * i] > intToneMags[3 + 4 * i])
			bytSym = 0;
		else if (intToneMags[1 + 4 * i] > intToneMags[4 * i] && intToneMags[1 + 4 * i] > intToneMags[2 + 4 * i] && intToneMags[1 + 4 * i] > intToneMags[3 + 4 * i])
			bytSym = 1;
		else if (intToneMags[2 + 4 * i] > intToneMags[4 * i] && intToneMags[2 + 4 * i] > intToneMags[1 + 4 * i] && intToneMags[2 + 4 * i] > intToneMags[3 + 4 * i])
			bytSym = 2;
		else
			bytSym = 3;

	    // Include these tone values in debug log only if FileLogLevel is LOGDEBUGPLUS
		WriteDebugLog(LOGDEBUGPLUS, "FrameType_bytSym : %d(%d %03.0f/%03.0f/%03.0f/%03.0f)", bytSym, intMagSum, 100.0*intToneMags[4 * i]/intMagSum, 100.0*intToneMags[1 + 4 * i]/intMagSum, 100.0*intToneMags[2 + 4 * i]/intMagSum, 100.0*intToneMags[3 + 4 * i]/intMagSum);
	}

	return TRUE;
}

// Function to compute the "distance" from a specific bytFrame Xored by bytID using 1 symbol parity 

float ComputeDecodeDistance(int intTonePtr, int * intToneMags, UCHAR bytFrameType, UCHAR bytID)
{
	// intTonePtr is the offset into the Frame type symbols. 0 for first Frame byte 20 = (5 x 4) for second frame byte 

	float dblDistance = 0;
	int int4ToneSum;
	int intToneIndex;
	UCHAR bytMask = 0xC0;
	int j, k;

	for (j = 0; j <= 4; j++)		//  over 5 symbols
	{
		int4ToneSum = 0;
		for (k = 0; k <=3; k++)
		{
			int4ToneSum += intToneMags[intTonePtr + (4 * j) + k];
		}
		if (int4ToneSum == 0)
			int4ToneSum = 1;		//  protects against possible overflow
		if (j < 4)
		    intToneIndex = ((bytFrameType ^ bytID) & bytMask) >> (6 - 2 * j);
		else
			intToneIndex = ComputeTypeParity(bytFrameType);

		dblDistance += 1.0f - ((1.0f * intToneMags[intTonePtr + (4 * j) + intToneIndex]) / (1.0f * int4ToneSum));
		bytMask = bytMask >> 2;
	}
	
	dblDistance = dblDistance / 5;		// normalize back to 0 to 1 range 
	return dblDistance;
}


//	Function to compute the frame type by selecting the minimal distance from all valid frame types.

int MinimalDistanceFrameType(int * intToneMags, UCHAR bytSessionID)
{
	float dblMinDistance1 = 5; // minimal distance for the first byte initialize to large value
	float dblMinDistance2 = 5; // minimal distance for the second byte initialize to large value
	float dblMinDistance3 = 5; // minimal distance for the second byte under exceptional cases initialize to large value
	int intIatMinDistance1, intIatMinDistance2, intIatMinDistance3;
	float dblDistance1, dblDistance2, dblDistance3;
	int i;

	if (ProtocolState == ISS)
	{
		bytValidFrameTypes = bytValidFrameTypesISS;
		bytValidFrameTypesLength = bytValidFrameTypesLengthISS;
	}
	else
	{
		bytValidFrameTypes = bytValidFrameTypesALL;
		bytValidFrameTypesLength = bytValidFrameTypesLengthALL;
	}

	// Search through all the valid frame types finding the minimal distance 
	// This looks like a lot of computation but measured < 1 ms for 135 iterations....RM 11/1/2016

	for (i = 0; i < bytValidFrameTypesLength; i++)
	{
		dblDistance1 = ComputeDecodeDistance(0, intToneMags, bytValidFrameTypes[i], 0);
		dblDistance2 = ComputeDecodeDistance(20, intToneMags, bytValidFrameTypes[i], bytSessionID);

		if (blnPending)
		    dblDistance3 = ComputeDecodeDistance(20, intToneMags, bytValidFrameTypes[i], 0xFF);
		else
			dblDistance3 = ComputeDecodeDistance(20, intToneMags, bytValidFrameTypes[i], bytLastARQSessionID);

		if (dblDistance1 < dblMinDistance1)
		{
			dblMinDistance1 = dblDistance1;
			intIatMinDistance1 = bytValidFrameTypes[i];
		}
		if (dblDistance2 < dblMinDistance2)
		{
			dblMinDistance2 = dblDistance2;
			intIatMinDistance2 = bytValidFrameTypes[i];
		}
		if (dblDistance3 < dblMinDistance3)
		{
			dblMinDistance3 = dblDistance3;
			intIatMinDistance3 = bytValidFrameTypes[i];
		}
	}

	WriteDebugLog(LOGDEBUG, "Frame Decode type %x %x %x Dist %.2f %.2f %.2f Sess %x pend %d conn %d lastsess %d",
		intIatMinDistance1, intIatMinDistance2, intIatMinDistance3, 
		dblMinDistance1, dblMinDistance2, dblMinDistance3, 
		bytSessionID, blnPending, blnARQConnected, bytLastARQSessionID); 
	
	if (bytSessionID == 0xFF)		// ' we are in a FEC QSO, monitoring an ARQ session or have not yet reached the ARQ Pending or Connected status 
	{
		// This handles the special case of a DISC command received from the prior session (where the station sending DISC did not receive an END). 

		if (intIatMinDistance1 == 0x29 && intIatMinDistance3 == 0x29 && ((dblMinDistance1 < 0.3) || (dblMinDistance3 < 0.3)))
		{
			sprintf(strDecodeCapture, "%s MD Decode;1 ID=H%X, Type=H29: %s, D1= %.2f, D3= %.2f",
				 strDecodeCapture, bytLastARQSessionID, Name(intIatMinDistance1), dblMinDistance1, dblMinDistance3);
			
			WriteDebugLog(LOGDEBUG, "[Frame Type Decode OK  ] %s", strDecodeCapture);

			return intIatMinDistance1;
		}
		
		// no risk of damage to an existing ARQConnection with END, BREAK, DISC, or ACK frames so loosen decoding threshold 

		if (intIatMinDistance1 == intIatMinDistance2 && ((dblMinDistance1 < 0.3) || (dblMinDistance2 < 0.3)))
		{
			sprintf(strDecodeCapture, "%s MD Decode;2 ID=H%X, Type=H%X:%s, D1= %.2f, D2= %.2f",
				 strDecodeCapture, bytSessionID, intIatMinDistance1, Name(intIatMinDistance1), dblMinDistance1, dblMinDistance2);
			WriteDebugLog(LOGDEBUG, "[Frame Type Decode OK  ] %s", strDecodeCapture);
			dblOffsetLastGoodDecode = dblOffsetHz;
			dttLastGoodFrameTypeDecode = Now;

			return intIatMinDistance1;
		}
		if ((dblMinDistance1 < 0.3) && (dblMinDistance1 < dblMinDistance2) && IsDataFrame(intIatMinDistance1) )	//  this would handle the case of monitoring an ARQ connection where the SessionID is not 0xFF
		{
			sprintf(strDecodeCapture, "%s MD Decode;3 ID=H%X, Type=H%X:%s, D1= %.2f, D2= %.2f",
				 strDecodeCapture, bytSessionID, intIatMinDistance1, Name(intIatMinDistance1), dblMinDistance1, dblMinDistance2);
			WriteDebugLog(LOGDEBUG, "[Frame Type Decode OK  ] %s", strDecodeCapture);
			
			return intIatMinDistance1;
		}

		if ((dblMinDistance2 < 0.3) && (dblMinDistance2 < dblMinDistance1) && IsDataFrame(intIatMinDistance2))  // this would handle the case of monitoring an FEC transmission that failed above when the session ID is = 0xFF
		{
			sprintf(strDecodeCapture, "%s MD Decode;4 ID=H%X, Type=H%X:%s, D1= %.2f, D2= %.2f",
				 strDecodeCapture, bytSessionID, intIatMinDistance1, Name(intIatMinDistance1), dblMinDistance1, dblMinDistance2);
			WriteDebugLog(LOGDEBUG, "[Frame Type Decode OK  ] %s", strDecodeCapture);

			return intIatMinDistance2;
		}

		sprintf(strDecodeCapture, "%s MD Decode;5 Type1=H%X, Type2=H%X, D1= %.2f, D2= %.2f",
			 strDecodeCapture, intIatMinDistance1, intIatMinDistance2, dblMinDistance1, dblMinDistance2);
		WriteDebugLog(LOGDEBUG, "[Frame Type Decode Fail] %s", strDecodeCapture);
		
		return -1;		// indicates poor quality decode so  don't use

	}

	else if (blnPending)		 // We have a Pending ARQ connection 
	{
		// this should be a Con Ack from the ISS if we are Pending

		if (intIatMinDistance1 == intIatMinDistance2)  // matching indexes at minimal distances so high probablity of correct decode.
		{
			sprintf(strDecodeCapture, "%s MD Decode;6 ID=H%X, Type=H%X:%s, D1= %.2f, D2= %.2f",
				 strDecodeCapture, bytSessionID, intIatMinDistance1, Name(intIatMinDistance1), dblMinDistance1, dblMinDistance2);


			if ((dblMinDistance1 < 0.3) || (dblMinDistance2 < 0.3))
			{
				dblOffsetLastGoodDecode = dblOffsetHz;
				dttLastGoodFrameTypeDecode = Now;		// This allows restricting tuning changes to about +/- 4Hz from last dblOffsetHz
				WriteDebugLog(LOGDEBUG, "[Frame Type Decode OK  ] %s", strDecodeCapture);
				return intIatMinDistance1;
			}
			else
			{
//				WriteDebugLog(LOGDEBUG, "[Frame Type Decode Fail] %s", strDecodeCapture);
				return -1;		 // indicates poor quality decode so  don't use
			}
		}

		//	handles the case of a received ConReq frame based on an ID of &HFF (ISS must have missed ConAck reply from IRS so repeated ConReq)

		else if (intIatMinDistance1 == intIatMinDistance3)	 //matching indexes at minimal distances so high probablity of correct decode.
		{
			sprintf(strDecodeCapture, "%s MD Decode;7 ID=H%X, Type=H%X:%s, D1= %.2f, D3= %.2f",
				 strDecodeCapture, bytSessionID, intIatMinDistance1, Name(intIatMinDistance1), dblMinDistance1, dblMinDistance3);

			if (intIatMinDistance1 >= 0x31 && intIatMinDistance1 <= 0x38 && ((dblMinDistance1 < 0.3) || (dblMinDistance3 < 0.3)))  // Check for ConReq (ISS must have missed previous ConAck  
			{
				dblOffsetLastGoodDecode = dblOffsetHz;
				dttLastGoodFrameTypeDecode = Now;		 // This allows restricting tuning changes to about +/- 4Hz from last dblOffsetHz
				WriteDebugLog(LOGDEBUG, "[Frame Type Decode OK  ] %s", strDecodeCapture);
				return intIatMinDistance1;
			}
			else
			{
//				WriteDebugLog(LOGDEBUG, "[Frame Type Decode Fail] %s", strDecodeCapture);

				return -1;	 // indicates poor quality decode so  don't use
			}
		}
	}
	else if (blnARQConnected)		// ' we have an ARQ connected session.
	{
		if (AccumulateStats)
		{
			dblAvgDecodeDistance = (dblAvgDecodeDistance * intDecodeDistanceCount + 0.5f * (dblMinDistance1 + dblMinDistance2)) / (intDecodeDistanceCount + 1);
			intDecodeDistanceCount++;
		}
		
		if (intIatMinDistance1 == intIatMinDistance2) // matching indexes at minimal distances so high probablity of correct decode.
		{
			if ((intIatMinDistance1 >= 0xE0 && intIatMinDistance1 <=0xFF) || (intIatMinDistance1 == 0x23) || 
				(intIatMinDistance1 == 0x2C) || (intIatMinDistance1 == 0x29))  // Check for critical ACK, BREAK, END, or DISC frames  
			{
				sprintf(strDecodeCapture, "%s MD Decode;8 ID=H%X, Critical Type=H%X: %s, D1= %.2f, D2= %.2f",
					 strDecodeCapture, bytSessionID, intIatMinDistance1, Name(intIatMinDistance1), dblMinDistance1, dblMinDistance2);
				if ((dblMinDistance1 < 0.3f) || (dblMinDistance2 < 0.3f)) // use tighter limits   here
				{
					dblOffsetLastGoodDecode = dblOffsetHz;
					dttLastGoodFrameTypeDecode = Now;		 // This allows restricting tuning changes to about +/- 4Hz from last dblOffsetHz
					WriteDebugLog(LOGDEBUG, "[Frame Type Decode OK  ] %s", strDecodeCapture);
					return intIatMinDistance1;
				}
				else
				{
//				WriteDebugLog(LOGDEBUG, "[Frame Type Decode Fail] %s", strDecodeCapture);
					return -1;		 // indicates poor quality decode so  don't use
				}
			}
			else	//  non critical frames
			{
				sprintf(strDecodeCapture, "%s MD Decode;9 ID=H%X, Type=H%X: %s, D1= %.2f, D2= %.2f",
					 strDecodeCapture, bytSessionID, intIatMinDistance1, Name(intIatMinDistance1), dblMinDistance1, dblMinDistance2);
				//  use looser limits here, there is no risk of protocol damage from these frames
				if ((dblMinDistance1 < 0.4) || (dblMinDistance2 < 0.4))
				{
					WriteDebugLog(LOGDEBUG, "[Frame Type Decode OK  ] %s", strDecodeCapture);
						
					dblOffsetLastGoodDecode = dblOffsetHz;
					dttLastGoodFrameTypeDecode = Now;	 // This allows restricting tuning changes to about +/- 4Hz from last dblOffsetHz
					return intIatMinDistance1;
				}
				else
				{
//					WriteDebugLog(LOGDEBUG, "[Frame Type Decode Fail] %s", strDecodeCapture);
					return -1;		// indicates poor quality decode so  don't use
				}
			}
		}
		else		//non matching indexes
		{
			sprintf(strDecodeCapture, "%s MD Decode;10  Type1=H%X: Type2=H%X: , D1= %.2f, D2= %.2f",
				 strDecodeCapture, intIatMinDistance1 , intIatMinDistance2, dblMinDistance1, dblMinDistance2);
//			WriteDebugLog(LOGDEBUG, "[Frame Type Decode Fail] %s", strDecodeCapture);
			return -1; // indicates poor quality decode so  don't use
		}
	}
	sprintf(strDecodeCapture, "%s MD Decode;11  Type1=H%X: Type2=H%X: , D1= %.2f, D2= %.2f",
		strDecodeCapture, intIatMinDistance1 , intIatMinDistance2, dblMinDistance1, dblMinDistance2);
	WriteDebugLog(LOGDEBUG, "[Frame Type Decode Fail] %s", strDecodeCapture);
	return -1; // indicates poor quality decode so  don't use
}


//	Function to acquire the 4FSK frame type

int Acquire4FSKFrameType()
{
	// intMFSReadPtr is pointing to start of first symbol of Frame Type (total of 10 4FSK symbols in frame type (2 bytes) + 1 parity symbol per byte 
	// returns -1 if minimal distance decoding is below threshold (low likelyhood of being correct)
	// returns -2 if insufficient samples 
	// Else returns frame type 0-255

	int NewType = 0;
	char Offset[32];

	if ((intFilteredMixedSamplesLength - intMFSReadPtr) < (240 * 10))
		return -2;		//  Check for 12 available 4FSK Symbols (but only 10 are used)  

	if (!DemodFrameType4FSK(intMFSReadPtr, intFilteredMixedSamples, &intToneMags[0][0]))
	{
		Update4FSKConstellation(&intToneMags[0][0], &intLastRcvdFrameQuality);
		intMFSReadPtr += (240 * 10);
		return -1;
	}
	
	intRmtLeaderMeasure = (Now - dttStartRmtLeaderMeasure);

	// Now do check received  Tone array for testing minimum distance decoder

	if (ProtocolMode == RXO)  // bytSessionID is uncertain, but alternatives will be tried if unsuccessful.
		NewType = RxoMinimalDistanceFrameType(&intToneMags[0][0]);
	else if (blnPending)			// If we have a pending connection (btween the IRS first decode of ConReq until it receives a ConAck from the iSS)
		NewType = MinimalDistanceFrameType(&intToneMags[0][0], bytPendingSessionID);		 // The pending session ID will become the session ID once connected) 
	else if (blnARQConnected)		// If we are connected then just use the stcConnection.bytSessionID
		NewType = MinimalDistanceFrameType(&intToneMags[0][0], bytSessionID);
	else					// not connected and not pending so use &FF (FEC or ARQ unconnected session ID
		NewType = MinimalDistanceFrameType(&intToneMags[0][0], 0xFF);
  
	if ((NewType > 0x30 && NewType < 0x39) || NewType == PING)
		QueueCommandToHost("PENDING");			 // early pending notice to stop scanners

	sprintf(Offset, "Offset %5.1f", dblOffsetHz);
	SendtoGUI('O', Offset, strlen(Offset));

	if (NewType >= 0 &&  IsShortControlFrame(NewType))		// update the constellation if a short frame (no data to follow)
		Update4FSKConstellation(&intToneMags[0][0], &intLastRcvdFrameQuality);

	if (AccumulateStats)
		if (NewType >= 0)
			intGoodFSKFrameTypes++;
		else
			intFailedFSKFrameTypes++;
	
	intMFSReadPtr += (240 * 10);			 // advance to read pointer to the next symbol (if there is one)
	
	return NewType;
}


//	Demodulate Functions. These are called repeatedly as samples addive
//	and buld a frame in static array  bytFrameData

// Function to demodulate one carrier for all low baud rate 4FSK frame types
 
//	Is called repeatedly to decode multitone modes
int Corrections = 0;

BOOL Demod1Car4FSK()
{
	int Start = 0;
	
	// We can't wait for the full frame as we don't have enough ram, so
	// we do one character at a time, until we run out or end of frame

	// Only continue if we have more than intSampPerSym * 4 chars

	while (State == AcquireFrame)
	{
		if (intFilteredMixedSamplesLength < ((intSampPerSym * 4) + 20)) // allow for correcrions
		{
			// Move any unprocessessed data down buffer

			//	(while checking process - will use cyclic buffer eventually

//			WriteDebugLog(LOGDEBUG, "Corrections %d", Corrections);

			// If corrections is non-zero, we have to adjust
			//	number left

			intFilteredMixedSamplesLength -= Corrections;
	
			if (intFilteredMixedSamplesLength < 0)
				WriteDebugLog(LOGERROR, "Corrupt intFilteredMixedSamplesLength");

			Corrections = 0;

			if (intFilteredMixedSamplesLength > 0)
				memmove(intFilteredMixedSamples,
					&intFilteredMixedSamples[Start], intFilteredMixedSamplesLength * 2); 

			return FALSE;
		}

		// If this is a multicarrier mode, we must call the
		// decode char routing for each carrier
	
		switch (intNumCar)
		{
		case 1:

			intCenterFreq = 1500;
			if (CarrierOk[0] == FALSE)		// Don't redo if already decoded
				Demod1Car4FSKChar(Start, bytFrameData1, 0);
			break;

		case 2:

			intCenterFreq = 1750;
			if (CarrierOk[0] == FALSE)
				Demod1Car4FSKChar(Start, bytFrameData1, 0);
			intCenterFreq = 1250;
			if (CarrierOk[1] == FALSE)
				Demod1Car4FSKChar(Start, bytFrameData2, 1);
			break;

		case 4:

			intCenterFreq = 2250;
			if (CarrierOk[0] == FALSE)
				Demod1Car4FSKChar(Start, bytFrameData1, 0);
			intCenterFreq = 1750;
			if (CarrierOk[1] == FALSE)
				Demod1Car4FSKChar(Start, bytFrameData2, 1);
			intCenterFreq = 1250;
			if (CarrierOk[2] == FALSE)
				Demod1Car4FSKChar(Start, bytFrameData3, 2);
			intCenterFreq = 750;
			if (CarrierOk[3] == FALSE)
				Demod1Car4FSKChar(Start, bytFrameData4, 3);
			break;
		}

		charIndex++;			// Index into received chars
		SymbolsLeft--;			// number still to decode
		Start += intSampPerSym * 4;	// 4 FSK bit pairs per byte
		intFilteredMixedSamplesLength -= intSampPerSym * 4;

		if (SymbolsLeft == 0)	
		{	
			//- prepare for next

			// If variable length packet frame header we only have header - leave rx running
		
			if (intFrameType == PktFrameHeader)
			{
				State = SearchingForLeader;
			
				// Save any unused samples
			
				if (intFilteredMixedSamplesLength > 0 && Start > 0)
					memmove(intFilteredMixedSamples,
						&intFilteredMixedSamples[Start], intFilteredMixedSamplesLength * 2); 

				return TRUE;
			}

			DecodeCompleteTime = Now;
			DiscardOldSamples();
			ClearAllMixedSamples();
			State = SearchingForLeader;
		}
	}
	return TRUE;
}

// Function to demodulate one carrier for all low baud rate 4FSK frame types
 
void Demod1Car4FSKChar(int Start, UCHAR * Decoded, int Carrier)
{
	// Converts intSamples to an array of bytes demodulating the 4FSK symbols with center freq intCenterFreq
	// intPtr should be pointing to the approximate start of the first data symbol  
	// Updates bytData() with demodulated bytes
	// Updates bytMinSymQuality with the minimum (range is 25 to 100) symbol making up each byte.

	float dblReal, dblImag;
	float dblSearchFreq;
	float dblMagSum = 0;
	float  dblMag[4];	// The magnitude for each of the 4FSK frequency bins
	UCHAR bytSym;
	static UCHAR bytSymHistory[3];
	int j;
	UCHAR bytData = 0;
	char DebugMess[1024];

	int * intToneMagsptr = &intToneMags[Carrier][intToneMagsIndex[Carrier]];
	   
	intToneMagsIndex[Carrier] += 16;

	//	ReDim intToneMags(4 * intNumOfSymbols - 1)
    //    ReDim bytData(intNumOfSymbols \ 4 - 1)

	dblSearchFreq = intCenterFreq + (1.5f * intBaud);	// the highest freq (equiv to lowest sent freq because of sideband reversal)

	// Do one symbol
	sprintf(DebugMess, "4FSK_bytSym :");
	for (j = 0; j < 4; j++)		// for each 4FSK symbol (2 bits) in a byte
	{
		dblMagSum = 0;
		GoertzelRealImag(intFilteredMixedSamples, Start, intSampPerSym, dblSearchFreq / intBaud, &dblReal, &dblImag);
		dblMag[0] = powf(dblReal,2) + powf(dblImag, 2);
		dblMagSum += dblMag[0];

        GoertzelRealImag(intFilteredMixedSamples, Start, intSampPerSym, (dblSearchFreq - intBaud) / intBaud, &dblReal, &dblImag);
		dblMag[1] = powf(dblReal,2) + powf(dblImag, 2);
		dblMagSum += dblMag[1];

		GoertzelRealImag(intFilteredMixedSamples, Start, intSampPerSym, (dblSearchFreq - 2 * intBaud) / intBaud, &dblReal, &dblImag);
		dblMag[2] = powf(dblReal,2) + powf(dblImag, 2);
		dblMagSum += dblMag[2];
			
		GoertzelRealImag(intFilteredMixedSamples, Start, intSampPerSym, (dblSearchFreq - 3 * intBaud) / intBaud, &dblReal,& dblImag);
		dblMag[3] = powf(dblReal,2) + powf(dblImag, 2);
		dblMagSum += dblMag[3];

		if (dblMag[0] > dblMag[1] && dblMag[0] > dblMag[2] && dblMag[0] > dblMag[3])
			bytSym = 0;
		else if (dblMag[1] > dblMag[0] && dblMag[1] > dblMag[2] && dblMag[1] > dblMag[3])
			bytSym = 1;
		else if (dblMag[2] > dblMag[0] && dblMag[2] > dblMag[1] && dblMag[2] > dblMag[3])
			bytSym = 2;
		else
			bytSym = 3;

		sprintf(DebugMess + strlen(DebugMess), " %d(%.0f %03.0f/%03.0f/%03.0f/%03.0f)", bytSym, dblMagSum, 100*dblMag[0]/dblMagSum, 100*dblMag[1]/dblMagSum, 100*dblMag[2]/dblMagSum, 100*dblMag[3]/dblMagSum);
		bytData = (bytData << 2) + bytSym;

		// !!!!!!! this needs attention !!!!!!!!

		*intToneMagsptr++ = dblMag[0];
		*intToneMagsptr++ = dblMag[1];
		*intToneMagsptr++ = dblMag[2];
		*intToneMagsptr++ = dblMag[3];
		bytSymHistory[0] = bytSymHistory[1];
		bytSymHistory[1] = bytSymHistory[2];
		bytSymHistory[2] = bytSym;

//		if ((bytSymHistory[0] != bytSymHistory[1]) && (bytSymHistory[1] != bytSymHistory[2]))
		{
			// only track when adjacent symbols are different (statistically about 56% of the time) 
			// this should allow tracking over 2000 ppm sampling rate error	
//			if (Start > intSampPerSym + 2)
//				Track1Car4FSK(intFilteredMixedSamples, &Start, intSampPerSym, dblSearchFreq, intBaud, bytSymHistory);
		}
		Start += intSampPerSym; // advance the pointer one symbol
	}

    // Include these tone values in debug log only if FileLogLevel is LOGDEBUGPLUS
	WriteDebugLog(LOGDEBUGPLUS, "%s", DebugMess);
	if (AccumulateStats)
		intFSKSymbolCnt += 4;
 
	Decoded[charIndex] = bytData;	
	return;
}


void Demod1Car4FSK600Char(int Start, UCHAR * Decoded, int Carrier);

BOOL Demod1Car4FSK600()
{
	int Start = 0;
	
	// We can't wait for the full frame as we don't have enough data, so
	// we do one character at a time, until we run out or end of frame

	// Only continue if we have more than intSampPerSym * 4 chars

	while (State == AcquireFrame)
	{
		if (intFilteredMixedSamplesLength < ((intSampPerSym * 4) + 20)) // allow for correcrions
		{
			// Move any unprocessessed data down buffer

			//	(while checking process - will use cyclic buffer eventually

//			WriteDebugLog(LOGDEBUG, "Corrections %d", Corrections);

			// If corrections is non-zero, we have to adjust
			//	number left

			intFilteredMixedSamplesLength -= Corrections;
	if (intFilteredMixedSamplesLength < 0)
		WriteDebugLog(LOGDEBUG, "Corrupt intFilteredMixedSamplesLength");

			Corrections = 0;

			if (intFilteredMixedSamplesLength > 0)
				memmove(intFilteredMixedSamples,
					&intFilteredMixedSamples[Start], intFilteredMixedSamplesLength * 2); 

			return FALSE;
		}
	
		intCenterFreq = 1500;
		if (CarrierOk[DummyCarrier] == FALSE)
			Demod1Car4FSK600Char(Start, Decode600Buffer, DummyCarrier);

		charIndex++;			// Index into received chars

		SymbolsLeft--;			// number still to decode
		Start += intSampPerSym * 4;	// 4 FSK bit pairs per byte
		intFilteredMixedSamplesLength -= intSampPerSym * 4;

		if (SymbolsLeft == 0)	
		{	
			//- prepare for next

			DecodeCompleteTime = Now;
			DiscardOldSamples();
			ClearAllMixedSamples();
			State = SearchingForLeader;
			
			DummyCarrier = 0;	// pseudo carrier used for long 600 baud frames
			Decode600Buffer = bytFrameData1;

		}
		else
		{
			if (charIndex == 253)	// End of RS fragment (3 per message) 
			{
				DummyCarrier++;		// pseudo carrier used for long 600 baud frames
				charIndex = 0;
				if (DummyCarrier == 1)
					Decode600Buffer = bytFrameData2;
				else
					Decode600Buffer = bytFrameData3;
			}
		}
	}
	return TRUE;
}

void Demod1Car4FSK600Char(int Start, UCHAR * Decoded, int Carrier)
{
  	float dblReal, dblImag;
	float dblSearchFreq;
	float dblMagSum = 0;
//	float  dblMag[4];	// The magnitude for each of the 4FSK frequency bins
	UCHAR bytSym = 0;
	static UCHAR bytSymHistory[3];
	int j, k;
	UCHAR bytData = 0;
	int intSampPerSymbol = 12000 / intBaud;
	int intMaxMag;

	// I think the best way to handle long 600 frames is to
	// treak each RS block as a separate frame. This will save
	// RAM in both Tone Mags and Data buffers

	int * intToneMagsptr = &intToneMags[Carrier][intToneMagsIndex[Carrier]];	// only 1 carrier in 600
	   
	intToneMagsIndex[Carrier] += 16;

	dblSearchFreq = intCenterFreq + (1.5f * intBaud);	// the highest freq (equiv to lowest sent freq because of sideband reversal)

	// Do one symbol

	for (j = 0; j < 4; j++)		// for each 4FSK symbol (2 bits) in a byte
	{
		dblMagSum = 0;	
		intMaxMag = 0;

		for (k = 0; k < 4; k++)
		{
			GoertzelRealImag(intFilteredMixedSamples, Start, intSampPerSymbol, (dblSearchFreq - k * intBaud) / intBaud, &dblReal, &dblImag);
			*intToneMagsptr = powf(dblReal,2) + powf(dblImag, 2);
			dblMagSum += *intToneMagsptr;
			if (*intToneMagsptr > intMaxMag)
			{
				intMaxMag = *intToneMagsptr;
				bytSym = k;
			}
			intToneMagsptr++;
		}
		
		bytData = (bytData<< 2) + bytSym;
		bytSymHistory[0] = bytSymHistory[1];
		bytSymHistory[1] = bytSymHistory[2];
		bytSymHistory[2] = bytSym;

        //If (bytSymHistory(0) <> bytSymHistory(1)) And (bytSymHistory(1) <> bytSymHistory(2)) Then ' only track when adjacent symbols are different (statistically about 56% of the time)
        //            ' this should allow tracking over 2000 ppm sampling rate error
        //            Track1Car4FSK600bd(intSamples, intPtr, intSampPerSymbol, intSearchFreq, intBaud, bytSymHistory)
        //        End If

		Start += intSampPerSym; // advance the pointer one symbol
	}

	Decoded[charIndex] = bytData;	
	return;
}

void Demod1Car8FSKChar(int Start, UCHAR * Decoded, int Carrier);

BOOL Demod1Car8FSK()
{
	int Start = 0;
	
	// We can't wait for the full frame as we don't have enough data, so
	// we do one character at a time, until we run out or end of frame

	// Only continue if we have more than intSampPerSym * 8 chars

	while (State == AcquireFrame)
	{
		if (intFilteredMixedSamplesLength < ((intSampPerSym * 8) + 20)) // allow for correcrions
		{
			// Move any unprocessessed data down buffer

			//	(while checking process - will use cyclic buffer eventually

//			WriteDebugLog(LOGDEBUG, "Corrections %d", Corrections);

			// If corrections is non-zero, we have to adjust
			//	number left

			intFilteredMixedSamplesLength -= Corrections;
	if (intFilteredMixedSamplesLength < 0)
		WriteDebugLog(LOGERROR, "Corrupt intFilteredMixedSamplesLength");

			Corrections = 0;

			if (intFilteredMixedSamplesLength > 0)
				memmove(intFilteredMixedSamples,
					&intFilteredMixedSamples[Start], intFilteredMixedSamplesLength * 2); 

			return FALSE;
		}

		// If this is a multicarrier mode, we must call the
		// decode char routing for each carrier
	
		switch (intNumCar)
		{
		case 1:

			intCenterFreq = 1500;
			if (CarrierOk[0] == FALSE)
				Demod1Car8FSKChar(Start, bytFrameData1, 0);
			break;

		case 2:

			intCenterFreq = 1750;
			if (CarrierOk[0] == FALSE)
				Demod1Car8FSKChar(Start, bytFrameData1, 0);
			intCenterFreq = 1250;
			if (CarrierOk[1] == FALSE)
				Demod1Car8FSKChar(Start, bytFrameData2, 1);
			break;
		}

		SymbolsLeft -=3;			// number still to decode
		Start += intSampPerSym * 8;	// 8 FSK bit triplea 
		intFilteredMixedSamplesLength -= intSampPerSym * 8;

		if (SymbolsLeft <= 0)	
		{	
			//- prepare for next

			DecodeCompleteTime = Now;
			DiscardOldSamples();
			ClearAllMixedSamples();
			State = SearchingForLeader;
		}
	}
	return TRUE;
}

// Function to demodulate one carrier for all 8FSK frame types
 
void Demod1Car8FSKChar(int Start, UCHAR * Decoded, int Carrier)
{
	// Converts intSamples to an array of bytes demodulating the 4FSK symbols with center freq intCenterFreq
	// intPtr should be pointing to the approximate start of the first data symbol  
	// Updates bytData() with demodulated bytes
	// Updates bytMinSymQuality with the minimum (range is 25 to 100) symbol making up each byte.

	float dblReal, dblImag;
	float dblSearchFreq;
	float dblMagSum;
	UCHAR bytSym = 0;
	static UCHAR bytSymHistory[3];
	int j, k;
	unsigned int intThreeBytes = 0;
	int intMaxMag;

	int * intToneMagsptr = &intToneMags[Carrier][intToneMagsIndex[Carrier]];
	   
	intToneMagsIndex[Carrier] += 64;

	dblSearchFreq = intCenterFreq + (1.5f * intBaud);	// the highest freq (equiv to lowest sent freq because of sideband reversal)

	// Do one symbol

	for (j = 0; j < 8; j++)		// for each group of 8 symbols (24 bits) 
	{
		dblMagSum = 0;
		intMaxMag = 0;

		dblSearchFreq = intCenterFreq + 3.5f * intBaud; //' the highest freq (equiv to lowest sent freq because of sideband reversal)
			
		for (k = 0; k < 8; k++)  // for each of 8 possible tones per symbol
		{
			GoertzelRealImag(intFilteredMixedSamples, Start, intSampPerSym, (dblSearchFreq - k * intBaud) / intBaud, &dblReal, &dblImag);
 
			*intToneMagsptr = powf(dblReal, 2) + powf(dblImag, 2);
			dblMagSum += *intToneMagsptr;

			if (*intToneMagsptr > intMaxMag)
			{
				intMaxMag = *intToneMagsptr;
				bytSym = k;
			}

			intToneMagsptr++;
		}

		intThreeBytes = (intThreeBytes << 3) + bytSym;

		bytSymHistory[0] = bytSymHistory[1];
		bytSymHistory[1] = bytSymHistory[2];
		bytSymHistory[2] = bytSym;

//		if ((bytSymHistory[0] != bytSymHistory[1]) && (bytSymHistory[1] != bytSymHistory[2]))
		{
			// only track when adjacent symbols are different (statistically about 56% of the time) 
			// this should allow tracking over 2000 ppm sampling rate error	
//			if (Start > intSampPerSym + 2)
//				Track1Car4FSK(intFilteredMixedSamples, &Start, intSampPerSym, dblSearchFreq, intBaud, bytSymHistory);
		}
		Start += intSampPerSym; // advance the pointer one symbol
	}

	Decoded[charIndex++] = intThreeBytes >> 16;	
	Decoded[charIndex++] = intThreeBytes >> 8;	
	Decoded[charIndex++] = intThreeBytes;	

	if (AccumulateStats)
		intFSKSymbolCnt += 8;;

	return;
}

//	Function to Decode 1 carrier 4FSK 50 baud Connect Request 



void Demod1Car16FSKChar(int Start, UCHAR * Decoded, int Carrier);

BOOL Demod1Car16FSK()
{
	int Start = 0;
	
	// We can't wait for the full frame as we don't have enough data, so
	// we do one character at a time, until we run out or end of frame

	// Only continue if we have more than intSampPerSym * 2 chars

	while (State == AcquireFrame)
	{
		if (intFilteredMixedSamplesLength < ((intSampPerSym * 2) + 20)) // allow for correcrions
		{
			// Move any unprocessessed data down buffer

			//	(while checking process - will use cyclic buffer eventually

//			WriteDebugLog(LOGDEBUG, "Corrections %d", Corrections);

			// If corrections is non-zero, we have to adjust
			//	number left

			intFilteredMixedSamplesLength -= Corrections;
	
			if (intFilteredMixedSamplesLength < 0)
				WriteDebugLog(LOGDEBUG, "Corrupt intFilteredMixedSamplesLength");

			Corrections = 0;

			if (intFilteredMixedSamplesLength > 0)
				memmove(intFilteredMixedSamples,
					&intFilteredMixedSamples[Start], intFilteredMixedSamplesLength * 2); 

			return FALSE;
		}

		// If this is a multicarrier mode, we must call the
		// decode char routing for each carrier
	
		switch (intNumCar)
		{
		case 1:

			intCenterFreq = 1500;
			if (CarrierOk[0] == FALSE)
				Demod1Car16FSKChar(Start, bytFrameData1, 0);
			break;

		case 2:

			intCenterFreq = 1750;
			if (CarrierOk[0] == FALSE)
				Demod1Car16FSKChar(Start, bytFrameData1, 0);
			intCenterFreq = 1250;
			if (CarrierOk[1] == FALSE)
				Demod1Car16FSKChar(Start, bytFrameData2, 1);
			break;
		}

		SymbolsLeft--;			// number still to decode
		Start += intSampPerSym * 2;	// 2 FSK nibbles 
		intFilteredMixedSamplesLength -= intSampPerSym * 2;

		if (intFilteredMixedSamplesLength < 0)
			WriteDebugLog(LOGERROR, "Corrupt intFilteredMixedSamplesLength");

		if (SymbolsLeft <= 0)	
		{	
			//- prepare for next

			DecodeCompleteTime = Now;
			DiscardOldSamples();
			ClearAllMixedSamples();
			State = SearchingForLeader;
		}
	}
	return TRUE;
}

// Function to demodulate one carrier for all 16FSK frame types
 
void Demod1Car16FSKChar(int Start, UCHAR * Decoded, int Carrier)
{
	// Converts intSamples to an array of bytes demodulating the 4FSK symbols with center freq intCenterFreq
	// intPtr should be pointing to the approximate start of the first data symbol  
	// Updates bytData() with demodulated bytes
	// Updates bytMinSymQuality with the minimum (range is 25 to 100) symbol making up each byte.

	float dblReal, dblImag;
	float dblSearchFreq;
	float dblMagSum = 0;
	UCHAR bytSym = 0;
	static UCHAR bytSymHistory[3];
	int j, k;
	UCHAR bytData = 0;
	int intMaxMag;

	int * intToneMagsptr = &intToneMags[Carrier][intToneMagsIndex[Carrier]];
	   
	intToneMagsIndex[Carrier] += 32;
	
	dblSearchFreq = intCenterFreq + (7.5f * intBaud);	// the highest freq (equiv to lowest sent freq because of sideband reversal)

	// Do one symbol
 
	for (j = 0; j < 2; j++)  // for each 16FSK symbol (4 bits) in a byte
	{
		dblMagSum = 0;
		intMaxMag = 0;

		dblSearchFreq = intCenterFreq + 7.5f * intBaud; //' the highest freq (equiv to lowest sent freq because of sideband reversal)
			
		for (k = 0; k < 16; k++)  // for each of 8 possible tones per symbol
		{
			GoertzelRealImag(intFilteredMixedSamples, Start, intSampPerSym, (dblSearchFreq - k * intBaud) / intBaud, &dblReal, &dblImag);
 
			*intToneMagsptr = powf(dblReal, 2) + powf(dblImag, 2);
			dblMagSum += *intToneMagsptr;

			if (*intToneMagsptr > intMaxMag)
			{
				intMaxMag = *intToneMagsptr;
				bytSym = k;
			}
			intToneMagsptr++;
		}

		bytData = (bytData << 4) + bytSym;

		bytSymHistory[0] = bytSymHistory[1];
		bytSymHistory[1] = bytSymHistory[2];
		bytSymHistory[2] = bytSym;

//		if ((bytSymHistory[0] != bytSymHistory[1]) && (bytSymHistory[1] != bytSymHistory[2]))
		{
			// only track when adjacent symbols are different (statistically about 56% of the time) 
			// this should allow tracking over 2000 ppm sampling rate error	
//			if (Start > intSampPerSym + 2)
//				Track1Car4FSK(intFilteredMixedSamples, &Start, intSampPerSym, dblSearchFreq, intBaud, bytSymHistory);
		}
		Start += intSampPerSym; // advance the pointer one symbol
	}

//	WriteDebugLog(LOGDEBUG, "Tone Mags Index %d", charIndex * 32 + 16 * j + k);

	if (AccumulateStats)
		intFSKSymbolCnt += 2;
    
	Decoded[charIndex++] = bytData;;	
	return;
}

//	Function to Decode 1 carrier 4FSK 50 baud Connect Request 



extern int intBW;

BOOL Decode4FSKConReq()
{
	UCHAR strCaller[32];
	UCHAR strTarget [32];
	UCHAR bytCall[6];
	BOOL blnRSOK;
	BOOL FrameOK;

	// Modified May 24, 2015 to use RS encoding vs CRC (similar to ID Frame)
 
	FrameOK = RSDecode(bytFrameData1, 16, 4, &blnRSOK);

	if (FrameOK && blnRSOK == FALSE)
	{
		// RS Claims to have corrected it, but check

		WriteDebugLog(LOGDEBUG, "CONREQ Frame Corrected by RS");

		FrameOK = RSDecode(bytFrameData1, 16, 4, &blnRSOK);

		// Should now be OK without connections, if not RS didn't fix it

		if (blnRSOK == FALSE)
		{
			WriteDebugLog(LOGDEBUG, "CONREQ Still bad after RS");
			FrameOK = FALSE;
		}
	}
	memcpy(bytCall, bytFrameData1, 6);
	DeCompressCallsign(bytCall, strCaller);
	memcpy(bytCall, &bytFrameData1[6], 6);
	DeCompressCallsign(bytCall, strTarget);

//	printtick(strCaller);
//	printtick(strTarget);
	
	sprintf(strRcvFrameTag, "_%s > %s", strCaller, strTarget);
	sprintf(bytData, "%s %s", strCaller, strTarget);

	// Recheck the returned data by reencoding
	
	if (intFrameType == 0x31)
		intBW = 200;
	else if (intFrameType == 0x32)
		intBW = 500;
	else if (intFrameType == 0x33)
		intBW = 1000;
	else if (intFrameType == 0x34)
		intBW = 2000;

	if (FrameOK)
		memcpy(lastGoodID, strCaller, 10);
	else
		SendCommandToHost("CANCELPENDING");	

	intConReqSN = Compute4FSKSN();
	WriteDebugLog(LOGDEBUG, "DemodDecode4FSKConReq:  S:N=%d Q=%d", intConReqSN, intLastRcvdFrameQuality);
	intConReqQuality = intLastRcvdFrameQuality;

	return FrameOK;
}

// Experimental test code to evaluate trying to compute the S:N and Multipath index from a Connect Request or Ping Frame  3/6/17

int Compute4FSKSN()
{
	int intSNdB = 0;

	// Status 3/6/17:
	// Code below appears to work well with WGN tracking S:N in ARDOP Test form from about -5 to +25 db S:N
	// This code can be used to analyze any 50 baud 4FSK frame but normally will be used on a Ping and a ConReq.
	// First compute the S:N defined as (approximately) the strongest tone in the group of 4FSK compared to the average of the other 3 tones
 
	int intNumSymbols  = intToneMagsLength /4;
	float dblAVGSNdB = 0;
	int intDominateTones[64];
	int intNonDominateToneSum;
	float intAvgNonDominateTone;
	int i, j;
	int SNcount = 0;

	// First compute the average S:N for all symbols 

	for (i = 0; i < intNumSymbols; i++)		//  for each symbol
	{
		intNonDominateToneSum = 10;			// Protect divide by zero
		intDominateTones[i] = 0;

		for (j = 0; j < 4; j++)			 // for each tone
		{
			if (intToneMags[0][4 * i + j] > intDominateTones[i])
				intDominateTones[i] = intToneMags[0][4 * i + j];
			
			intNonDominateToneSum += intToneMags[0][4 * i + j];
		}
		
		intAvgNonDominateTone = (intNonDominateToneSum - intDominateTones[i])/ 3; // subtract out the Dominate Tone from the sum
		
		// Note subtract intAvgNonDominateTone below to compute S:N instead of (S+N):N
		// note 10 * log used since tone values are already voltage squared (avoids SQRT) 
		// the S:N calculation is limited to a Max of + 50 dB to avoid distorting the average in very low noise environments 
            
		dblAVGSNdB += min(50.0f, 10.0f * log10f((intDominateTones[i] - intAvgNonDominateTone) / intAvgNonDominateTone)); //  average in the S:N;
	}
	intSNdB = (dblAVGSNdB / intNumSymbols) - 17.8f;	//  17.8 converts from nominal 50 Hz "bin" BW to standard 3 KHz BW (10* Log10(3000/50))
	
	return intSNdB;
}

// Function to Demodulate and Decode 1 carrier 4FSK 50 baud Ping frame  

BOOL Decode4FSKPing()
{
	UCHAR strCaller[10];
	UCHAR strTarget [10];
	UCHAR bytCall[6];
	BOOL blnRSOK;
	BOOL FrameOK;
	int intSNdB;

	FrameOK = RSDecode(bytFrameData1, 16, 4, &blnRSOK);

	if (FrameOK && blnRSOK == FALSE)
	{
		// RS Claims to have corrected it, but check

		WriteDebugLog(LOGDEBUG, "PING Frame Corrected by RS");

		FrameOK = RSDecode(bytFrameData1, 16, 4, &blnRSOK);

		// Should now be OK without connections, if not RS didn't fix it

		if (blnRSOK == FALSE)
		{
			WriteDebugLog(LOGDEBUG, "PING Still bad after RS");
			FrameOK = FALSE;
		}
	}

	memcpy(bytCall, bytFrameData1, 6);
	DeCompressCallsign(bytCall, strCaller);
	memcpy(bytCall, &bytFrameData1[6], 6);
	DeCompressCallsign(bytCall, strTarget);

//	printtick(strCaller);
//	printtick(strTarget);
	
	sprintf(strRcvFrameTag, "_%s > %s", strCaller, strTarget);
	sprintf(bytData, "%s %s", strCaller, strTarget);

	if (FrameOK == FALSE)
	{
		SendCommandToHost("CANCELPENDING");	
		return FALSE;
	}

	DrawRXFrame(1, bytData);


	intSNdB = Compute4FSKSN();

	if (ProtocolState == DISC)
	{
		char Msg[80];

		sprintf(Msg, "PING %s>%s %d %d", strCaller, strTarget, intSNdB, intLastRcvdFrameQuality);
		SendCommandToHost(Msg);

		WriteDebugLog(LOGDEBUG, "[DemodDecode4FSKPing] PING %s>%s S:N=%d Q=%d", strCaller, strTarget, intSNdB, intLastRcvdFrameQuality);
		
		stcLastPingdttTimeReceived = time(NULL);
		memcpy(stcLastPingstrSender, strCaller, 10);
		memcpy(stcLastPingstrTarget, strTarget, 10);
		stcLastPingintRcvdSN = intSNdB;
		stcLastPingintQuality = intLastRcvdFrameQuality;

		return TRUE;
	}
	else
		SendCommandToHost("CANCELPENDING");	

	return FALSE;
}


// Function to Decode 1 carrier 4FSK 50 baud Connect Ack with timing 

BOOL Decode4FSKConACK(UCHAR bytFrameType, int * intTiming)
{
	int Timing = 0;

 //Dim bytCall(5) As Byte


	if (bytFrameData1[0] == bytFrameData1[1]) 
		Timing = 10 * bytFrameData1[0];
	else if (bytFrameData1[0] == bytFrameData1[2])
		Timing = 10 * bytFrameData1[0];
	else if (bytFrameData1[1] == bytFrameData1[2])
		Timing = 10 * bytFrameData1[1];

	if (Timing >= 0)
	{
		*intTiming = Timing;

		// strRcvFrameTag = "_" & intTiming.ToString & " ms"
  
		WriteDebugLog(LOGDEBUG, "[DemodDecode4FSKConACK]  Remote leader timing reported: %d ms", *intTiming);

		if (AccumulateStats)
			intGoodFSKFrameDataDecodes++;
         
		//intTestFrameCorrectCnt++;
		intReceivedLeaderLen = intLeaderRcvdMs;
		bytLastReceivedDataFrameType = 0;  // initialize the LastFrameType to an illegal Data frame
        return TRUE;
	}
	
	if (AccumulateStats)
		intFailedFSKFrameDataDecodes++;

	return FALSE;
}


//  Function  Decode 1 carrier 4FSK 50 baud PingACK with S:N and Quality 
BOOL Decode4FSKPingACK(UCHAR bytFrameType, int * intSNdB, int * intQuality)
{
	int Ack = -1;

	if ((bytFrameData1[0] == bytFrameData1[1])|| (bytFrameData1[0] == bytFrameData1[2]))
		Ack = bytFrameData1[0];
	else
		if (bytFrameData1[1] == bytFrameData1[2])
			Ack = bytFrameData1[1];

	if (Ack >= 0)
	{
		*intSNdB = ((Ack & 0xF8) >> 3) - 10;	// Range -10 to + 21 dB steps of 1 dB
        *intQuality = (Ack & 7) * 10 + 30;		// Range 30 to 100 steps of 10
       
		if (*intSNdB == 21)
			WriteDebugLog(LOGDEBUG, "[DemodDecode4FSKPingACK]  S:N> 20 dB Quality=%d" ,*intQuality);
		else
			WriteDebugLog(LOGDEBUG, "[DemodDecode4FSKPingACK]  S:N= %d dB Quality=%d",  *intSNdB, *intQuality);

		blnPINGrepeating = False;
		blnFramePending = False;	//  Cancels last repeat
		return TRUE;
	}
	return FALSE;
}


BOOL Decode4FSKID(UCHAR bytFrameType, char * strCallID, char * strGridSquare)
{
	UCHAR bytCall[10];
	UCHAR temp[20];
	BOOL blnRSOK;
	BOOL FrameOK;
	unsigned char * p = bytFrameData1;


	Debugprintf("%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x ",
		p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);


	FrameOK = RSDecode(bytFrameData1, 16, 4, &blnRSOK);

	if (FrameOK && blnRSOK == FALSE)
	{
		// RS Claims to have corrected it, but check

		WriteDebugLog(LOGDEBUG, "ID Frame Corrected by RS");

		FrameOK = RSDecode(bytFrameData1, 16, 4, &blnRSOK);

		// Should now be OK without connections, if not RS didn't fix it

		if (blnRSOK == FALSE)
		{
			WriteDebugLog(LOGDEBUG, "ID Still bad after RS");
			FrameOK = FALSE;
		}
	}

	memcpy(bytCall, bytFrameData1, 6);
	DeCompressCallsign(bytCall, strCallID);
	memcpy(bytCall, &bytFrameData1[6], 6);
	DeCompressGridSquare(bytCall, temp);
      
	if (strlen(temp) > 5)
	{
		temp[4] = tolower(temp[4]);
		temp[5] = tolower(temp[5]);
	}
	sprintf(strGridSquare, "[%s]", temp);

	if (AccumulateStats)
		if (FrameOK)
			intGoodFSKFrameDataDecodes++;
		else
			intFailedFSKFrameDataDecodes++;

	if (FrameOK)
		memcpy(lastGoodID, strCallID, 10);

	return FrameOK;	
}


 
//  Function to Demodulate Frame based on frame type
//	Will be called repeatedly as new samples arrive

void DemodulateFrame(int intFrameType)
{
 //       Dim stcStatus As Status = Nothing

	int intConstellationQuality = 0;

 //       ReDim bytData(-1)

	strRcvFrameTag[0] = 0;

	//stcStatus.ControlName = "lblRcvFrame"

	//	DataACK/NAK and short control frames

	if ((intFrameType >= 0 && intFrameType <= 0x1f) ||  intFrameType == 0xe0)	 // DataACK/NAK
	{
		//blnDecodeOK = DecodeACKNAK(intFrameType, intRcvdQuality)
        //    stcStatus.Text = objFrameInfo.Name(intFrameType) & strRcvFrameTag
        //ElseIf (objFrameInfo.IsShortControlFrame(intFrameType)) Then ' Short Control Frames
        //    blnDecodeOK = TRUE
        //    stcStatus.Text = objFrameInfo.Name(intFrameType)
        //End If
			
		Demod1Car4FSK();
		return;
	}

	if ((intFrameType >= 0x30 && intFrameType <= 0x38) || intFrameType == PING)
	{
		// ID and CON Req

		Demod1Car4FSK();
		return;
	}

	switch (intFrameType)
	{
		case 0x39:
		case 0x3A:
		case 0x3B:
		case 0x3C:		 // Connect ACKs with Timing
		case PINGACK:
 
		Demod1Car4FSK();

		break;
		
		// 1 Carrier Data frames
		// PSK Data

					
		case 0x40:
		case 0x41:
		case 0x42:
		case 0x43:
		case 0x44:
		case 0x45:

			DemodPSK();
			break;

		// 1 car 16qam

		case 0x46:
		case 0x47:

			DemodQAM();
			break;

		//4FSK Data

		case 0x48:
		case 0x49:
		case 0x4A:
		case 0x4B:
		case 0x4C:
		case 0x4D:

			Demod1Car4FSK();
			break;

 		//2 Carrier PSK Data frames

		case 0x50:
		case 0x51:
		case 0x52:
		case 0x53:

			DemodPSK();
			break;

		case 0x54:
		case 0x55:

			// 2 car 16qam

			DemodQAM();
			break;

  	
		// 1000 Hz  Data frames

		case 0x60:
		case 0x61:
		case 0x62:
		case 0x63:

			DemodPSK();
			break;

		case 0x64:
		case 0x65:

			DemodQAM();
			break;

			// 2000 Hz PSK 8 Carr Data frames
			
		case 0x70:
		case 0x71:
		case 0x72:
		case 0x73:
	
			DemodPSK();
			break;

		case 0x74:
		case 0x75:

			DemodQAM();
			break;

       // 4FSK Data (600 bd)

		case 0x7A:
		case 0x7B:
		case 0x7C:
		case 0x7D:

			Demod1Car4FSK600();
			break;

		case PktFrameHeader:	// Experimantal Variable Length Frame 

			Demod1Car4FSK();
			break;

		case PktFrameData:	// Experimantal Variable Length Frame 
			
			if (strcmp(strMod, "4FSK") == 0)
				Demod1Car4FSK();
			else if (strcmp(strMod, "16QAM") == 0)
				DemodQAM();
			else
				DemodPSK();
			break;


  /*              ' Experimental Sounding frame
            Case 0xD0
                DemodSounder(intMFSReadPtr, intFilteredMixedSamples)
                blnDecodeOK = TRUE
  */
		default:

			WriteDebugLog(LOGDEBUG, "Unsupported frame type %x", intFrameType);
			DiscardOldSamples();
			ClearAllMixedSamples();
			State = SearchingForLeader;


			intFilteredMixedSamplesLength = 0;	// Testing
	}
}


// Function to Strip quality from ACK/NAK frame types

BOOL DecodeACKNAK(int intFrameType, int *  intQuality)
{
	*intQuality = 38 + (2 * (intFrameType & 0x1F));  //mask off lower 5 bits ' Range of 38 to 100 in steps of 2
     // strRcvFrameTag = "_Q" & intQuality.ToString
	return TRUE;
}


int intSNdB = 0, intQuality = 0;


BOOL DecodeFrame(int xxx, UCHAR * bytData)
{
	BOOL blnDecodeOK = FALSE;
	char strCallerCallsign[10] = "";
	char strTargetCallsign[10] = "";
	char strIDCallSign[11] = "";
	char strGridSQ[20] = "";
	int intTiming;
	int intRcvdQuality;
	char Reply[80];
	char Good[8] = {1,1,1,1,1,1,1,1};


	strRcvFrameTag[0] = 0;

	//DataACK/NAK and short control frames 

	if (CarrierOk[0] != 0 && CarrierOk[0] != 1)
		CarrierOk[0] = 0;

	if ((intFrameType >= 0 && intFrameType <= 0x1F) || intFrameType >= 0xE0) // DataACK/NAK
	{
		blnDecodeOK = DecodeACKNAK(intFrameType, &intRcvdQuality);
		if (blnDecodeOK)
			DrawRXFrame(1, Name(intFrameType));
		else
			DrawRXFrame(2, Name(intFrameType));

		goto returnframe;
	}
	else if (IsShortControlFrame(intFrameType)) // Short Control Frames
	{
		blnDecodeOK = TRUE;
		DrawRXFrame(1, Name(intFrameType));

		goto returnframe;
	}

	totalRSErrors = 0;
			
	if (CarrierOk[0] != 0 && CarrierOk[0] != 1)
		CarrierOk[0] = 0;

	WriteDebugLog(LOGDEBUG, "DecodeFrame MEMARQ Flags %d %d %d %d %d %d %d %d",
		CarrierOk[0], CarrierOk[1], CarrierOk[2], CarrierOk[3],
		CarrierOk[4], CarrierOk[5], CarrierOk[6], CarrierOk[7]);

	switch (intFrameType)
	{
		case 0x39:
		case 0x3A:
		case 0x3B:
		case 0x3C:		 // Connect ACKs with Timing
 
			blnDecodeOK = Decode4FSKConACK(intFrameType, &intTiming);

			if (blnDecodeOK)
			{
				bytData[0] = intTiming / 10;
				DrawRXFrame(1, Name(intFrameType));
			}

		break;
		
		case PINGACK:		 // 3D
 
			blnDecodeOK = Decode4FSKPingACK(intFrameType, &intSNdB, &intQuality);
			
			if (blnDecodeOK && ProtocolState == DISC && Now  - dttLastPINGSent < 5000)	
			{
				sprintf(Reply, "PINGACK %d %d", intSNdB, intQuality);
				SendCommandToHost(Reply);

				DrawRXFrame(1, Reply);
			}
 			break;
    

		case 0x30:		 // ID Frame, 
						
			blnDecodeOK = Decode4FSKID(0x30, strIDCallSign, strGridSQ);
			
			frameLen = sprintf(bytData, "ID:%s %s:" , strIDCallSign, strGridSQ);

			if (blnDecodeOK)
				DrawRXFrame(1, bytData);

			break;

		case 0x31:
		case 0x32:
		case 0x33:
		case 0x34:
		case 0x35:
		case 0x36:
		case 0x37:
		case 0x38:

			blnDecodeOK = Decode4FSKConReq();
			if (blnDecodeOK)
				DrawRXFrame(1, Name(intFrameType));

			break;

		case PING:	// 0x3E

			blnDecodeOK = Decode4FSKPing();
			break;


		//   PSK 1 Carrier Data
	
		case 0x40:
		case 0x41:
		case 0x42:
		case 0x43:
		case 0x44:
		case 0x45:

			blnDecodeOK = CarrierOk[0];
	
			if (blnDecodeOK)
				DrawRXFrame(1, Name(intFrameType));

			break;

		// QAM
		
		case 0x46:
		case 0x47:
		case 0x54:
		case 0x55:
		case 0x64:
		case 0x65:
		case 0x74:
		case 0x75:

			if (memcmp(CarrierOk, Good, intNumCar) == 0)
				blnDecodeOK = TRUE;

			if (blnDecodeOK)
				DrawRXFrame(1, Name(intFrameType));

			break;

		// FSK 1 Carrier Modes

		case 0x48:
		case 0x49:
		case 0x4a:
		case 0x4b:
		case 0x4c:
		case 0x4d:

			frameLen = CorrectRawDataWithRS(bytFrameData1, bytData, intDataLen, intRSLen, intFrameType, 0);
			blnDecodeOK = CarrierOk[0];

			if (blnDecodeOK)
				DrawRXFrame(1, Name(intFrameType));
			break;


		// 2 Carrier PSK Data frames
			
		case 0x50:
		case 0x51:
		case 0x52:
		case 0x53:

		if (CarrierOk[0] && CarrierOk[1])
			blnDecodeOK = TRUE;

			if (blnDecodeOK)
				DrawRXFrame(1, Name(intFrameType));

			break;

		// 1000 Hz Data frames 4 Carrier

		case 0x60:
		case 0x61:
		case 0x62:
		case 0x63:

			if (memcmp(CarrierOk, Good, intNumCar) == 0)
				blnDecodeOK = TRUE;

			if (blnDecodeOK)
				DrawRXFrame(1, Name(intFrameType));

			break;
	
		// 2000 Hz Data frames 8 Carrier

		case 0x70:
		case 0x71:
		case 0x72:
		case 0x73:

			if (memcmp(CarrierOk, Good, intNumCar) == 0)
				blnDecodeOK = TRUE;

			if (blnDecodeOK)
				DrawRXFrame(1, Name(intFrameType));
			break;

		case 0x78:
		case 0x79:

			// 4 Carrier FSK Modes

			frameLen = CorrectRawDataWithRS(bytFrameData1, bytData, intDataLen, intRSLen, intFrameType, 0);
			frameLen +=  CorrectRawDataWithRS(bytFrameData2, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 1);
			frameLen +=  CorrectRawDataWithRS(bytFrameData3, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 2);
			frameLen +=  CorrectRawDataWithRS(bytFrameData4, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 3);


			if (CarrierOk[0] && CarrierOk[1] && CarrierOk[2] && CarrierOk[3]) 
				blnDecodeOK = TRUE;

			if (blnDecodeOK)
				DrawRXFrame(1, Name(intFrameType));

		case 0x7A:
		case 0x7B:

			// 600 Baud Data. Frame has 3 * 200 RS Blocks

			intDataLen = 200;
			intRSLen = 50;

			frameLen = CorrectRawDataWithRS(bytFrameData1, bytData, intDataLen, intRSLen, intFrameType, 0);
			frameLen +=  CorrectRawDataWithRS(bytFrameData2, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 1);
			frameLen +=  CorrectRawDataWithRS(bytFrameData3, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 2);

			intDataLen = 600;
			intRSLen = 150;

			if (CarrierOk[0] && CarrierOk[1] && CarrierOk[2]) 
				blnDecodeOK = TRUE;

			if (blnDecodeOK)
				DrawRXFrame(1, Name(intFrameType));

			break;

		case 0x7C:
		case 0x7D:

			// 600 Baud Short. 

			frameLen = CorrectRawDataWithRS(bytFrameData1, bytData, intDataLen, intRSLen, intFrameType, 0);

			blnDecodeOK = CarrierOk[0];

			if (blnDecodeOK)
				DrawRXFrame(1, Name(intFrameType));

			break;

		case PktFrameHeader:
		{
			// Variable Length Packet Frame Header
			// 6 bits Type 10 Bits Len

			int Len;
			int pktNumCar;
			int pktDataLen;
			int pktRSLen;
						
			frameLen = CorrectRawDataWithRS(bytFrameData1, bytData, intDataLen, intRSLen, intFrameType, 0);
		
			if (CarrierOk[0])
			{
					DrawRXFrame(1, Name(intFrameType));

					pktRXMode = bytFrameData1[1] >> 2;
					pktNumCar = pktCarriers[pktRXMode];

					Len =  ((bytFrameData1[1] & 0x3) << 8) | bytFrameData1[2];
				}
//	Now only using one carrier

//				else if (CarrierOk[1])
//				{
//					pktRXMode = bytFrameData2[1] >> 5;
//					pktNumCar = ((bytFrameData2[1] & 0x1c) >> 2) + 1;
//					Len =  ((bytFrameData2[1] & 0x3) << 8) | bytFrameData2[2];
//				}
				else
				{
					// Cant decode
	
					DiscardOldSamples();
					ClearAllMixedSamples();
					break;
				}

				DrawRXFrame(0, Name(PktFrameData));
				
				strcpy(strMod, &pktMod[pktRXMode][0]);

				// Reset to receive rest of frame

				pktDataLen = (Len + (pktNumCar - 1))/pktNumCar; // Round up

				// This must match the encode settings
				
				pktRSLen = pktDataLen >> 2;			// Try 25% for now
				if (pktRSLen & 1)
					pktRSLen++;						// Odd RS bytes no use

				if (pktRSLen < 4)
					pktRSLen = 4;					// At least 4

				SymbolsLeft = pktDataLen + pktRSLen + 3; // Data has crc + length byte
				State = AcquireFrame;
				intFrameType = PktFrameData;
				CarrierOk[1] = CarrierOk[0] = 0;
				charIndex = 0;	
				frameLen = 0;
				intPhasesLen = 0;
				memset(intToneMagsIndex, 0, sizeof(intToneMagsIndex));
				intDataLen = pktDataLen;
				intRSLen = pktRSLen;
				intNumCar = pktNumCar;
				PSKInitDone = 0;

				WriteDebugLog(6, "Pkt Frame Header Type %s Len %d", strMod, Len);
				strlop(strMod, '/');
				blnDecodeOK = TRUE;

				return 0;
		}

					
		case PktFrameData:
		{
			if (pktFSK[pktRXMode])
			{
				// Need to Check RS

				frameLen = CorrectRawDataWithRS(bytFrameData1, bytData, intDataLen, intRSLen, intFrameType, 0);
				if (intNumCar > 1)
					frameLen +=  CorrectRawDataWithRS(bytFrameData2, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 1);

				if (intNumCar > 2)
				{
					frameLen +=  CorrectRawDataWithRS(bytFrameData3, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 2);
					frameLen +=  CorrectRawDataWithRS(bytFrameData4, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 3);
				}
			}

			if (memcmp(CarrierOk, Good, intNumCar) == 0)
			{
				blnDecodeOK = TRUE;
				DrawRXFrame(1, Name(intFrameType));

				// Packet Data  - if KISS interface ias active
				// Pass to Host as KISS frame, else pass to
				// Session code

				// Data in bytData  len in frameLen

#ifdef TEENSY
				L2Routine(bytData, frameLen, intLastRcvdFrameQuality, totalRSErrors, intNumCar, pktRXMode);
#else
//				if (PKTCONNECTED)
//					SendFrametoHost(bytData, frameLen);
//				else
					L2Routine(bytData, frameLen, intLastRcvdFrameQuality, totalRSErrors, intNumCar, pktRXMode);
#endif
			}
			break;
		}



//                ' Experimental Sounding frame
//            Case 0xD0
//                DemodSounder(intMFSReadPtr, intFilteredMixedSamples)
//                blnDecodeOK = TRUE

	}

	if (blnDecodeOK)
	{
		WriteDebugLog(LOGINFO, "[DecodeFrame] Frame: %s Decode PASS,   Constellation Quality= %d", Name(intFrameType),  intLastRcvdFrameQuality);
#ifdef PLOTCONSTELLATION
		if (intFrameType >= 0x30 && intFrameType <= 0x38)
			DrawDecode(lastGoodID);		// ID or CONREQ
		else
			DrawDecode("PASS");
		updateDisplay();
#endif
	}

	else
	{
		WriteDebugLog(LOGINFO, "[DecodeFrame] Frame: %s Decode FAIL,   Constellation Quality= %d", Name(intFrameType),  intLastRcvdFrameQuality);
#ifdef PLOTCONSTELLATION
		DrawDecode("FAIL");
		updateDisplay();
#endif
	}
	//	if a data frame with few error and quality very low, adjust

	if (blnDecodeOK && (totalRSErrors / intNumCar) < (intRSLen / 4) && intLastRcvdFrameQuality < 80)
	{
		WriteDebugLog(LOGDEBUG, "RS Errors %d Carriers %d RLen %d Qual %d - adjusting Qual",
			totalRSErrors, intNumCar, intRSLen, intLastRcvdFrameQuality);
		
		intLastRcvdFrameQuality = 80;
	}


returnframe:

	if (blnDecodeOK && intFrameType >= 0x40 && intFrameType <= 0x7F)
		bytLastReceivedDataFrameType = intFrameType;
	
//	if (blnDecodeOK)
//		stcStatus.BackColor = Color.LightGreen;
//	else
//		stcStatus.BackColor = Color.LightSalmon;
   
//	queTNCStatus.Enqueue(stcStatus);

//	if (DebugLog)
//		if (blnDecodeOK)
//			WriteDebugLog(LOGDEBUG, "[DecodeFrame] Frame: %s Decode PASS, Constellation Quality= %d", Name(intFrameType), intLastRcvdFrameQuality);
//		else
//			WriteDebugLog(LOGDEBUG, "[DecodeFrame] Frame: %s Decode FAIL, Constellation Quality= %d", Name(intFrameType), intLastRcvdFrameQuality);

	return blnDecodeOK;
}

// Subroutine to update the 4FSK Constellation

void drawFastVLine(int x0, int y0, int length, int color);
void drawFastHLine(int x0, int y0, int length, int color);

void Update4FSKConstellation(int * intToneMags, int * intQuality)
{
	// Subroutine to update bmpConstellation plot for 4FSK modes...
        
	int intToneSum = 0;
	int intMagMax = 0;
	float dblPi4  = 0.25 * M_PI;
	float dblDistanceSum = 0;
	int intRad = 0;
	int i, x, y;


	int clrPixel;
	int yCenter = (ConstellationHeight)/ 2;
	int xCenter = (ConstellationWidth) / 2;

	clearDisplay();


	for (i = 0; i < intToneMagsLength; i += 4)  // for the number of symbols represented by intToneMags
	{
		intToneSum = intToneMags[i] + intToneMags[i + 1] + intToneMags[i + 2] + intToneMags[i + 3];
        
		if (intToneMags[i] > intToneMags[i + 1] && intToneMags[i] > intToneMags[i + 2] && intToneMags[i] > intToneMags[i + 3])
		{
			if (intToneSum > 0)
				intRad = max(5, 42 - 80 * (intToneMags[i + 1] + intToneMags[i + 2] + intToneMags[i + 3]) / intToneSum);

			dblDistanceSum += (42 - intRad);
			intRad = (intRad * PLOTRADIUS) / 50; // rescale for OLED (50 instead of 42 as we rotate constellation 35 degrees 
			x = xCenter + intRad;
			y = yCenter + intRad;
		}
		else if (intToneMags[i + 1] > intToneMags[i] && intToneMags[i + 1] > intToneMags[i + 2] && intToneMags[i + 1] > intToneMags[i + 3])
		{
			if (intToneSum > 0)
				intRad = max(5, 42 - 80 * (intToneMags[i] + intToneMags[i + 2] + intToneMags[i + 3]) / intToneSum);

			dblDistanceSum += (42 - intRad);
			intRad = (intRad * PLOTRADIUS) / 50; // rescale for OLED (50 instead of 42 as we rotate constellation 35 degrees 
			x = xCenter + intRad;
			y = yCenter - intRad;
		}
		else if (intToneMags[i + 2] > intToneMags[i] && intToneMags[i + 2] > intToneMags[i + 1] && intToneMags[i + 2] > intToneMags[i + 3]) 
		{
            if (intToneSum > 0)
				intRad = max(5, 42 - 80 * (intToneMags[i + 1] + intToneMags[i] + intToneMags[i + 3]) / intToneSum);

			dblDistanceSum += (42 - intRad);
			intRad = (intRad * PLOTRADIUS) / 50; // rescale for OLED (50 instead of 42 as we rotate constellation 35 degrees 
			x = xCenter - intRad;
			y = yCenter - intRad;
		}
		else if (intToneSum > 0)
		{
			intRad = max(5, 42 - 80 * (intToneMags[i + 1] + intToneMags[i + 2] + intToneMags[i]) / intToneSum);	

			dblDistanceSum += (42 - intRad);
			intRad = (intRad * PLOTRADIUS) / 50; // rescale for OLED (50 instead of 42 as we rotate constellation 35 degrees 
			x = xCenter - intRad;
			y = yCenter + intRad;
		}

#ifdef PLOTCONSTELLATION
		if (intRad < 15)
			clrPixel = Tomato;
		else if (intRad < 30)
			clrPixel = Gold;
		else
			clrPixel = Lime;

		mySetPixel(x, y, clrPixel);
#endif

	}

	*intQuality = 100 - (2.7f * (dblDistanceSum / (intToneMagsLength / 4))); // ' factor 2.7 emperically chosen for calibration (Qual range 25 to 100)

	if (*intQuality < 0)
		*intQuality = 0;
	else if (*intQuality > 100)
		*intQuality = 100;

	if (AccumulateStats)
	{
		int4FSKQualityCnts += 1;
		int4FSKQuality += *intQuality;
	}

#ifdef PLOTCONSTELLATION
	DrawAxes(*intQuality, shortName(intFrameType), strMod);
#endif

	return;
}



// Subroutine to update the 16FSK constallation

void Update16FSKConstellation(int * intToneMags, int * intQuality)
{
	//	Subroutine to update bmpConstellation plot for 16FSK modes...


	int	intToneSum = 0;
	float intMagMax = 0;
	float dblDistanceSum = 0;
	float dblPlotRotation = 0;
//            Dim stcStatus As Status
	int	intRad;
//            Dim clrPixel As System.Drawing.Color
	int	intJatMaxMag;
	int i, j;

#ifdef PLOTCONSTELLATION

	float dblAng;
	int x, y,clrPixel;
	int yCenter = (ConstellationHeight - 2)/ 2;
	int xCenter = (ConstellationWidth - 2) / 2;

	clearDisplay();
#endif


	for (i = 0; i< intToneMagsLength; i += 16)  // for the number of symbols represented by intToneMags
	{
		intToneSum = 0;
		intMagMax = 0;

		for (j = 0; j < 16; j++)
		{
			if (intToneMags[i + j] > intMagMax)
			{
				intMagMax = intToneMags[i + j];
				intJatMaxMag = j;
			}
			intToneSum += intToneMags[i + j];
		}
		intRad = max(5, 42 - 40 * (intToneSum - intMagMax) / intToneSum);
		dblDistanceSum += (43 - intRad);

#ifdef PLOTCONSTELLATION		
		if (intRad < 15)
			clrPixel = Tomato;
		else if (intRad < 30)
			clrPixel = Gold;
		else
			clrPixel = Lime;

		// plot the symbols rotated to avoid the axis

		intRad = (intRad * PLOTRADIUS) /42; // rescale for OLED
		dblAng = M_PI / 16.0f + (intJatMaxMag * M_PI / 8);
  
		x = xCenter + intRad * cosf(dblAng);
		y = yCenter + intRad * sinf(dblAng);
		mySetPixel(x, y, clrPixel);    
#endif

	}
		
	*intQuality = max(0, (100 - 2.2 * (dblDistanceSum / (intToneMagsLength / 16))));	 // factor 2.2 emperically chosen for calibration (Qual range 25 to 100)
//	*intQuality = max(0, (100 - 1.0 * (dblDistanceSum / (intToneMagsLength / 16))));	 // factor 2.2 emperically chosen for calibration (Qual range 25 to 100)

	if(AccumulateStats)
	{
		int16FSKQualityCnts++;
		int16FSKQuality += *intQuality;
	}
#ifdef PLOTCONSTELLATION
	DrawAxes(*intQuality, shortName(intFrameType), strMod);
#endif
}

//	Subroutine to udpate the 8FSK Constellation

void Update8FSKConstellation(int * intToneMags, int * intQuality)
{
	//	Subroutine to update bmpConstellation plot for 8FSK modes...
         
	int intToneSum = 0;
	int intMagMax = 0;
	float dblPi4  = 0.25 * M_PI;
	float dblDistanceSum = 0;
	int intRad = 0;
	int i, j, intJatMaxMag;

#ifdef PLOTCONSTELLATION

	float dblAng;
	int yCenter = (ConstellationHeight - 2)/ 2;
	int xCenter = (ConstellationWidth - 2) / 2;
	unsigned short clrPixel = WHITE;
	unsigned short x, y;

	clearDisplay();
#endif

	for (i = 0; i < intToneMagsLength; i += 8)  // for the number of symbols represented by intToneMags
	{
		intToneSum = 0;
		intMagMax = 0;

 		for (j = 0; j < 8; j++)
		{
			if (intToneMags[i + j] > intMagMax)
			{
				intMagMax = intToneMags[i + j];
				intJatMaxMag = j;
			}
			intToneSum += intToneMags[i + j];
		}

		intRad = max(5, 42 - 40 * (intToneSum - intMagMax) / intToneSum);
		dblDistanceSum += (43 - intRad);
								
#ifdef PLOTCONSTELLATION		
		if (intRad < 15)
			clrPixel = Tomato;
		else if (intRad < 30)
			clrPixel = Gold;
		else
			clrPixel = Lime;

		// plot the symbols rotated to avoid the axis

		intRad = (intRad * PLOTRADIUS) /42; // rescale for OLED

		dblAng = M_PI / 9.0f + (intJatMaxMag * M_PI / 4);
  
		x = xCenter + intRad * cosf(dblAng);
		y = yCenter + intRad * sinf(dblAng);
		mySetPixel(x, y, clrPixel);    
#endif
	}
		
	*intQuality = max(0, (100 - 2.0 * (dblDistanceSum / (intToneMagsLength / 8))));	 // factor 2.0 emperically chosen for calibration (Qual range 25 to 100)

	if(AccumulateStats)
	{
		int8FSKQualityCnts++;
		int8FSKQuality += *intQuality;
	}
#ifdef PLOTCONSTELLATION
	DrawAxes(*intQuality, shortName(intFrameType), strMod);
#endif
	return;
}



//	Subroutine to Update the PhaseConstellation

int UpdatePhaseConstellation(short * intPhases, short * intMag, char * strMod, BOOL blnQAM)
{
	// Subroutine to update bmpConstellation plot for PSK modes...
	// Skip plotting and calculations of intPSKPhase(0) as this is a reference phase (9/30/2014)

	int intPSKPhase = strMod[0] - '0';
	float dblPhaseError; 
	float dblPhaseErrorSum = 0;
	int intPSKIndex;
	int intP = 0;
	float dblRad = 0;
	float dblAvgRad = 0;
	float intMagMax = 0;
	float dblPi4 = 0.25 * M_PI;
	float dbPhaseStep;
	float dblRadError = 0;
	float dblPlotRotation = 0;
	int intRadInner = 0, intRadOuter = 0;
	float dblAvgRadOuter = 0, dblAvgRadInner = 0, dblRadErrorInner = 0, dblRadErrorOuter = 0;
 
	int i,j, k, intQuality;

#ifdef PLOTCONSTELLATION

	int intX, intY;
	int yCenter = (ConstellationHeight - 2)/ 2;
	int xCenter = (ConstellationWidth - 2) / 2;

	unsigned short clrPixel = WHITE;

	clearDisplay();
#endif

	if (intPSKPhase == 4)
		intPSKIndex = 0;
	else
		intPSKIndex = 1;

	if (blnQAM)
	{
		intPSKPhase = 8;
		intPSKIndex = 1;
		dbPhaseStep  = 2 * M_PI / intPSKPhase;
		for (j = 1; j < intPhasesLen; j++)   // skip the magnitude of the reference in calculation
		{
			intMagMax = max(intMagMax, intMag[j]); // find the max magnitude to auto scale
		}

		for (k = 1; k < intPhasesLen; k++)
		{
			if (intMag[k] < 0.75f * intMagMax)
			{
				dblAvgRadInner += intMag[k];
				intRadInner++;
			}
			else
			{
				dblAvgRadOuter += intMag[k];
				intRadOuter++;
			}
		}

		dblAvgRadInner = dblAvgRadInner / intRadInner;
		dblAvgRadOuter = dblAvgRadOuter / intRadOuter;
	}
	else
	{
		dbPhaseStep  = 2 * M_PI / intPSKPhase;
		for (j = 1; j < intPhasesLen; j++)   // skip the magnitude of the reference in calculation
		{
			intMagMax = max(intMagMax, intMag[j]); // find the max magnitude to auto scale
            dblAvgRad += intMag[j];	
		}
	}
           
	dblAvgRad = dblAvgRad / (intPhasesLen - 1); // the average radius

	for (i = 1; i <  intPhasesLen; i++)  // Don't plot the first phase (reference)
	{
		intP = round((0.001f * intPhases[i]) / dbPhaseStep);

		// compute the Phase and Radius errors
 
		if (intMag[i] > (dblAvgRadInner + dblAvgRadOuter) / 2) 
			dblRadErrorOuter += fabsf(dblAvgRadOuter - intMag[i]);
		else
			dblRadErrorInner += fabsf(dblAvgRadInner - intMag[i]);

		dblPhaseError = fabsf(((0.001 * intPhases[i]) - intP * dbPhaseStep)); // always positive and < .5 *  dblPhaseStep
		dblPhaseErrorSum += dblPhaseError;

#ifdef PLOTCONSTELLATION
		dblRad = PLOTRADIUS * intMag[i] / intMagMax; //  scale the radius dblRad based on intMagMax
		intX = xCenter + dblRad * cosf(dblPlotRotation + intPhases[i] / 1000.0f);
		intY = yCenter + dblRad * sinf(dblPlotRotation + intPhases[i] / 1000.0f);
    
		
		if (intX > 0 && intY > 0)
			if (intX != xCenter && intY != yCenter)
				mySetPixel(intX, intY, Yellow); // don't plot on top of axis
#endif
	}

	if (blnQAM) 
	{
//		intQuality = max(0, ((100 - 200 * (dblPhaseErrorSum / (intPhasesLen)) / dbPhaseStep))); // ignore radius error for (PSK) but include for QAM
		intQuality = max(0, (1 - (dblRadErrorInner / (intRadInner * dblAvgRadInner) + dblRadErrorOuter / (intRadOuter * dblAvgRadOuter))) * (100 - 200 * (dblPhaseErrorSum / intPhasesLen) / dbPhaseStep));

//		intQuality = max(0, ((100 - 200 * (dblPhaseErrorSum / (intPhasesLen)) / dbPhaseStep))); // ignore radius error for (PSK) but include for QAM
		
		if (AccumulateStats)
		{
			intQAMQualityCnts += 1;
			intQAMQuality += intQuality;
			intQAMSymbolsDecoded += intPhasesLen;
		}
	}
	else
	{
		intQuality =  max(0, ((100 - 200 * (dblPhaseErrorSum / (intPhasesLen)) / dbPhaseStep))); // ignore radius error for (PSK) but include for QAM
	
		if (AccumulateStats)
		{
			intPSKQualityCnts[intPSKIndex]++;
			intPSKQuality[intPSKIndex] += intQuality;
			intPSKSymbolsDecoded += intPhasesLen;
		}
	}	
#ifdef PLOTCONSTELLATION
	DrawAxes(intQuality, shortName(intFrameType), strMod);
#endif
	return intQuality;

}


// Subroutine to track 1 carrier 4FSK. Used for both single and multiple simultaneous carrier 4FSK modes.


VOID Track1Car4FSK(short * intSamples, int * intPtr, int intSampPerSymbol, float dblSearchFreq, int intBaud, UCHAR * bytSymHistory)
{
	// look at magnitude of the tone for bytHistory(1)  2 sample2 earlier and 2 samples later.  and pick the maximum adjusting intPtr + or - 1
	// this seems to work fine on test Mar 16, 2015. This should handle sample rate offsets (sender to receiver) up to about 2000 ppm

	float dblReal, dblImag, dblMagEarly, dblMag, dblMagLate;
	float dblBinToSearch = (dblSearchFreq - (intBaud * bytSymHistory[1])) / intBaud; //  select the 2nd last symbol for magnitude comparison


	GoertzelRealImag(intSamples, (*intPtr - intSampPerSymbol - 2), intSampPerSymbol, dblBinToSearch, &dblReal, &dblImag);
	dblMagEarly = powf(dblReal, 2) + powf(dblImag, 2);
	GoertzelRealImag(intSamples, (*intPtr - intSampPerSymbol), intSampPerSymbol, dblBinToSearch, &dblReal, &dblImag);
	dblMag = powf(dblReal, 2) + powf(dblImag, 2);
	GoertzelRealImag(intSamples, (*intPtr - intSampPerSymbol + 2), intSampPerSymbol, dblBinToSearch, &dblReal, &dblImag);
	dblMagLate = powf(dblReal, 2) + powf(dblImag, 2);

	if (dblMagEarly > dblMag && dblMagEarly > dblMagLate)
	{
		*intPtr --;
		Corrections--;
		if (AccumulateStats)
			intAccumFSKTracking--;
	}
	else if (dblMagLate > dblMag && dblMagLate > dblMagEarly)
	{
		*intPtr ++;
		Corrections++;
		if (AccumulateStats)
			intAccumFSKTracking++;
	}
}

//	Function to Decode one Carrier of PSK modulation 

//	Ideally want to be able to call on for each symbol, as I don't have the
//	RAM to build whole frame

//	Call for each set of 4 or 8 Phase Values

int pskStart = 0;


VOID Decode1CarPSK(UCHAR * Decoded, int Carrier)
{
	unsigned int int24Bits;
	UCHAR bytRawData;
	int k;
	int Len = intPhasesLen;

	if (CarrierOk[Carrier])
		return;							// don't do it again

	pskStart = 0;
	charIndex = 0;

    	
	while (Len >= 0)
	{

		// Phase Samples are in intPhases

		switch (intPSKMode)
		{
		case 4:		// process 4 sequential phases per byte (2 bits per phase)

			for (k = 0; k < 4; k++)
			{
				if (k == 0)
					bytRawData = 0;
				else
					bytRawData <<= 2;
				
				if (intPhases[Carrier][pskStart] < 786 && intPhases[Carrier][pskStart] > -786)
				{
				}		// Zero so no need to do anything
				else if (intPhases[Carrier][pskStart] >= 786 && intPhases[Carrier][pskStart] < 2356)
					bytRawData += 1;
				else if (intPhases[Carrier][pskStart] >= 2356 || intPhases[Carrier][pskStart] <= -2356)
					bytRawData += 2;
				else
					bytRawData += 3;

				pskStart++;
			}

			Decoded[charIndex++] = bytRawData;
			Len -= 4;
			break;

		case 8: // Process 8 sequential phases (3 bits per phase)  for 24 bits or 3 bytes  

			//	Status verified on 1 Carrier 8PSK with no RS needed for High S/N

			//	Assume we check for 8 available phase samples before being called

			int24Bits = 0;

			for (k = 0; k < 8; k++)
			{
				int24Bits <<= 3;

				if (intPhases[Carrier][pskStart] < 393 && intPhases[Carrier][pskStart] > -393)
				{
				}		// Zero so no need to do anything
				else if (intPhases[Carrier][pskStart] >= 393 && intPhases[Carrier][pskStart] < 1179)
					int24Bits += 1;
				else if (intPhases[Carrier][pskStart] >= 1179 && intPhases[Carrier][pskStart] < 1965)
					int24Bits += 2;
				else if (intPhases[Carrier][pskStart] >= 1965 && intPhases[Carrier][pskStart] < 2751)
				int24Bits += 3;
				else if (intPhases[Carrier][pskStart] >= 2751 || intPhases[Carrier][pskStart] < -2751)
					int24Bits += 4;
				else if (intPhases[Carrier][pskStart] >= -2751 && intPhases[Carrier][pskStart] < -1965)
				int24Bits += 5;
				else if (intPhases[Carrier][pskStart] >= -1965 && intPhases[Carrier][pskStart] <= -1179)
					int24Bits += 6;
				else 
					int24Bits += 7;

				pskStart ++;
	
			}
			Decoded[charIndex++] = int24Bits >> 16;
			Decoded[charIndex++] = int24Bits >> 8;
			Decoded[charIndex++] = int24Bits;

			Len -= 8;
			break;
	
		default:
			return; //????
		}
	}
	return;
}

//	Function to compute PSK symbol tracking (all PSK modes, used for single or multiple carrier modes) 

int Track1CarPSK(int intCarFreq, char * strPSKMod, float dblUnfilteredPhase, BOOL blnInit)
{
	// This routine initializes and tracks the phase offset per symbol and adjust intPtr +/-1 when the offset creeps to a threshold value.
	// adjusts (by Ref) intPtr 0, -1 or +1 based on a filtering of phase offset. 
	// this seems to work fine on test Mar 21, 2015. May need optimization after testing with higher sample rate errors. This should handle sample rate offsets (sender to receiver) up to about 2000 ppm

	float dblAlpha = 0.3f; // low pass filter constant  may want to optimize value after testing with large sample rate error. 
		// (Affects how much averaging is done) lower values of dblAlpha will minimize adjustments but track more slugishly.

	float dblPhaseOffset;

	static float dblTrackingPhase = 0;
	static float dblModFactor;
	static float dblRadiansPerSample;  // range is .4188 @ car freq = 800 to 1.1195 @ car freq 2200
	static float dblPhaseAtLastTrack;
	static int intCountAtLastTrack;
	static float dblFilteredPhaseOffset;

	if (blnInit)
	{
		// dblFilterredPhase = dblUnfilteredPhase;
		dblTrackingPhase = dblUnfilteredPhase;
		
		if (strPSKMod[0] == '8' || strPSKMod[0] == '1')
			dblModFactor = M_PI / 4;
		else if (strPSKMod[0] == '4')
			dblModFactor = M_PI / 2;

		dblRadiansPerSample = (intCarFreq * dbl2Pi) / 12000.0f;
		dblPhaseOffset = dblUnfilteredPhase - dblModFactor * round(dblUnfilteredPhase / dblModFactor);
		dblPhaseAtLastTrack = dblPhaseOffset;
		dblFilteredPhaseOffset = dblPhaseOffset;
		intCountAtLastTrack = 0;
		return 0;
	}

	intCountAtLastTrack += 1;
	dblPhaseOffset = dblUnfilteredPhase - dblModFactor * round(dblUnfilteredPhase / dblModFactor);
	dblFilteredPhaseOffset = (1 - dblAlpha) * dblFilteredPhaseOffset + dblAlpha * dblPhaseOffset;

	if ((dblFilteredPhaseOffset - dblPhaseAtLastTrack) > dblRadiansPerSample)
	{
		//Debug.WriteLine("Filtered>LastTrack: Cnt=" & intCountAtLastTrack.ToString & "  Filtered = " & Format(dblFilteredPhaseOffset, "00.000") & "  Offset = " & Format(dblPhaseOffset, "00.000") & "  Unfiltered = " & Format(dblUnfilteredPhase, "00.000"))
		dblFilteredPhaseOffset = dblPhaseOffset - dblRadiansPerSample;
		dblPhaseAtLastTrack = dblFilteredPhaseOffset;
	
		if (AccumulateStats)
		{
			if (strPSKMod[0] == '1')	// 16QAM" Then
			{ 
				intQAMTrackAttempts++;
				intAccumQAMTracking--;
			}
			else
			{
				intPSKTrackAttempts++;
				intAccumPSKTracking--;
			}
		}
		return -1;
	}

	if ((dblPhaseAtLastTrack - dblFilteredPhaseOffset) > dblRadiansPerSample)
	{
		//'Debug.WriteLine("Filtered<LastTrack: Cnt=" & intCountAtLastTrack.ToString & "  Filtered = " & Format(dblFilteredPhaseOffset, "00.000") & "  Offset = " & Format(dblPhaseOffset, "00.000") & "  Unfiltered = " & Format(dblUnfilteredPhase, "00.000"))
		dblFilteredPhaseOffset = dblPhaseOffset + dblRadiansPerSample;
		dblPhaseAtLastTrack = dblFilteredPhaseOffset;

		if (AccumulateStats)
		{
			if (strPSKMod[0] == '1')	// 16QAM" Then
			{ 
				intQAMTrackAttempts++;
				intAccumQAMTracking++;
			}
			else
			{
				intPSKTrackAttempts++;
				intAccumPSKTracking++;
			}
		}
		return 1;
	}
	// 'Debug.WriteLine("Filtered Phase = " & Format(dblFilteredPhaseOffset, "00.000") & "  Offset = " & Format(dblPhaseOffset, "00.000") & "  Unfiltered = " & Format(dblUnfilteredPhase, "00.000"))

	return 0;
}
 
// Function to compute the differenc of two angles 

int ComputeAng1_Ang2(int intAng1, int intAng2)
{
	// do an angle subtraction intAng1 minus intAng2 (in milliradians) 
	// Results always between -3142 and 3142 (+/- Pi)

	int intDiff;

	intDiff = intAng1 - intAng2;

	if (intDiff < -3142)
		intDiff += 6284;
	else if (intDiff > 3142 )
		intDiff -= 6284;

	return intDiff;
}

// Subroutine to "rotate" the phases to try and set the average offset to 0. 

void CorrectPhaseForTuningOffset(short * intPhase, int intPhaseLength, char * strMod)
{
	// A tunning error of -1 Hz will rotate the phase calculation Clockwise ~ 64 milliradians (~4 degrees)
	//   This corrects for:
	// 1) Small tuning errors which result in a phase bias (rotation) of then entire constellation
	// 2) Small Transmitter/receiver drift during the frame by averaging and adjusting to constellation to the average. 
	//   It only processes phase values close to the nominal to avoid generating too large of a correction from outliers: +/- 30 deg for 4PSK, +/- 15 deg for 8PSK
	//  Is very affective in handling initial tuning error.  

	// This only works if you collect all samples before decoding them. 
	// Can I do something similar????

	short intPhaseMargin  = 2095 / intPSKMode; // Compute the acceptable phase correction range (+/-30 degrees for 4 PSK)
	short intPhaseInc = 6284 / intPSKMode;
	int intTest;
	int i;
	int intOffset, intAvgOffset, intAvgOffsetBeginning, intAvgOffsetEnd;
	int intAccOffsetCnt = 0, intAccOffsetCntBeginning = 0, intAccOffsetCntEnd = 0;
	int	intAccOffsetBeginning = 0, intAccOffsetEnd = 0, intAccOffset = 0;
    int intPSKMode;


	if (strcmp(strMod, "8PSK") == 0 || strcmp(strMod, "16QAM") == 0)
		intPSKMode = 8;
	else
		intPSKMode = 4;
       
	// Note Rev 0.6.2.4 The following phase margin value increased from 2095 (120 deg) to 2793 (160 deg) yielded an improvement in decode at low S:N

	intPhaseMargin  = 2793 / intPSKMode; // Compute the acceptable phase correction range (+/-30 degrees for 4 PSK)
	intPhaseInc = 6284 / intPSKMode;

	// Compute the average offset (rotation) for all symbols within +/- intPhaseMargin of nominal
            
	for (i = 0; i <  intPhaseLength; i++)
	{
		intTest = (intPhase[i] / intPhaseInc);
		intOffset = intPhase[i] - intTest * intPhaseInc;

		if ((intOffset >= 0 && intOffset <= intPhaseMargin) || (intOffset < 0 && intOffset >= -intPhaseMargin))
		{
			intAccOffsetCnt += 1;
			intAccOffset += intOffset;
			
			if (i <= intPhaseLength / 4)
			{
				intAccOffsetCntBeginning += 1;
				intAccOffsetBeginning += intOffset;
			}
			else if (i >= (3 * intPhaseLength) / 4)
			{
				intAccOffsetCntEnd += 1;
				intAccOffsetEnd += intOffset;
			}
		}
	}
	
	if (intAccOffsetCnt > 0)
		intAvgOffset = (intAccOffset / intAccOffsetCnt);
	if (intAccOffsetCntBeginning > 0)
		intAvgOffsetBeginning = (intAccOffsetBeginning / intAccOffsetCntBeginning);
	if (intAccOffsetCntEnd > 0)
		intAvgOffsetEnd = (intAccOffsetEnd / intAccOffsetCntEnd);
     
	//WriteDebugLog(LOGDEBUG, "[CorrectPhaseForOffset] Beginning: %d End: %d Total: %d",
		//intAvgOffsetBeginning, intAvgOffsetEnd, intAvgOffset);

	if ((intAccOffsetCntBeginning > intPhaseLength / 8) && (intAccOffsetCntEnd > intPhaseLength / 8))
	{
		for (i = 0; i < intPhaseLength; i++)
		{
			intPhase[i] = intPhase[i] - ((intAvgOffsetBeginning * (intPhaseLength - i) / intPhaseLength) + (intAvgOffsetEnd * i / intPhaseLength));
			if (intPhase[i] > 3142)
				intPhase[i] -= 6284;
			else if (intPhase[i] < -3142)
				intPhase[i] += 6284;
		}
		WriteDebugLog(LOGDEBUG, "[CorrectPhaseForTuningOffset] AvgOffsetBeginning=%d AvgOffsetEnd=%d AccOffsetCnt=%d/%d",
				intAvgOffsetBeginning, intAvgOffsetEnd, intAccOffsetCnt, intPhaseLength);
	}
	else if (intAccOffsetCnt > intPhaseLength / 2)
	{
		for (i = 0; i < intPhaseLength; i++)
		{
			intPhase[i] -= intAvgOffset;
			if (intPhase[i] > 3142)
				intPhase[i] -= 6284;
			else if (intPhase[i] < -3142)
				intPhase[i] += 6284;
		}
		WriteDebugLog(LOGDEBUG, "[CorrectPhaseForTuningOffset] AvgOffset=%d AccOffsetCnt=%d/%d",
				intAvgOffset, intAccOffsetCnt, intPhaseLength);

	}
}

// Function to Decode one Carrier of 16QAM modulation 

//	Call for each set of 4 or 8 Phase Values

short intCarMagThreshold[8] = {0};


VOID Decode1CarQAM(UCHAR * Decoded, int Carrier)
{
	unsigned int intData;
	int k;
	float dblAlpha = 0.1f; // this determins how quickly the rolling average dblTrackingThreshold responds.

	// dblAlpha value of .1 seems to work well...needs to be tested on fading channel (e.g. Multipath)
	
	int Threshold = intCarMagThreshold[Carrier];
	int Len = intPhasesLen;

	if (CarrierOk[Carrier])
		return;							// don't do it again

	pskStart = 0;
	charIndex = 0;

	// We calculated initial mag from reference symbol

	// use filtered tracking of refernce phase amplitude
	// (should be full amplitude value)
     
	// On WGN this appears to improve decoding threshold about 1 dB 9/3/2016
    	
	while (Len >= 0)
	{
		// Phase Samples are in intPhases

		intData = 0;

		for (k = 0; k < 2; k++)
		{
			intData <<= 4;

			if (intPhases[Carrier][pskStart] < 393 && intPhases[Carrier][pskStart] > -393)
			{
			}		// Zero so no need to do anything
			else if (intPhases[Carrier][pskStart] >= 393 && intPhases[Carrier][pskStart] < 1179)
				intData += 1;
			else if (intPhases[Carrier][pskStart] >= 1179 && intPhases[Carrier][pskStart] < 1965)
				intData += 2;
			else if (intPhases[Carrier][pskStart] >= 1965 && intPhases[Carrier][pskStart] < 2751)
				intData += 3;
			else if (intPhases[Carrier][pskStart] >= 2751 || intPhases[Carrier][pskStart] < -2751)
				intData += 4;
			else if (intPhases[Carrier][pskStart] >= -2751 && intPhases[Carrier][pskStart] < -1965)
				intData += 5;
			else if (intPhases[Carrier][pskStart] >= -1965 && intPhases[Carrier][pskStart] <= -1179)
				intData += 6;
			else 
				intData += 7;

			if (intMags[Carrier][pskStart] < Threshold)
			{
				intData += 8;		//  add 8 to "inner circle" symbols. 
				Threshold = (Threshold * 900 + intMags[Carrier][pskStart] * 150) / 1000;
			}
			else
			{
				Threshold = ( Threshold * 900 + intMags[Carrier][pskStart] * 75) / 1000;
			}
		
			intCarMagThreshold[Carrier] = Threshold;
			pskStart++;
		}
		Decoded[charIndex++] = intData;
		Len -=2;
	}
}
//	Functions to demod all PSKData frames single or multiple carriers 


VOID InitDemodPSK()
{
	// Called at start of frame

	int i;
	float dblPhase, dblReal, dblImag;

	intPSKMode = strMod[0] - '0';
	PSKInitDone = TRUE;
	intPhasesLen = 0;

	if (intPSKMode == 8)
		dblPhaseInc = 2 * M_PI * 1000 / 8;
	else
		dblPhaseInc = 2 * M_PI * 1000 / 4;

	if (intBaud == 167)
		intSampPerSym = 72;
	else
		intSampPerSym = 120;

	if (intNumCar == 1)
		intCarFreq = 1500;
	else
		intCarFreq = 1400 + (intNumCar / 2) * 200; // start at the highest carrier freq which is actually the lowest transmitted carrier due to Reverse sideband mixing
  
	for (i= 0; i < intNumCar; i++)
	{
		if (intBaud == 100)
		{
			//Experimental use of Hanning Windowing
				
            intNforGoertzel[i] = 120;
            dblFreqBin[i] = intCarFreq / 100;
            intCP[i] = 0;
		}
		else if (intBaud == 167)
		{
			intCP[i] = 6;  // Need to optimize wwith multi path channels (little difference between 6 and 12 @ wgn10, 4 Car 1000 Hz)
  
            intNforGoertzel[i] = 60;
            dblFreqBin[i] = intCarFreq / 200;
		}
 
/*		if (intBaud == 100 && intCarFreq == 1500) 
		{
		intCP[i] = 20;  //  These values selected for best decode percentage (92%) and best average 4PSK Quality (82) on MPP0dB channel
		dblFreqBin[i] = intCarFreq / 150;
		intNforGoertzel[i] = 80;
		}
		else if (intBaud == 100)
		{
			intCP[i] = 28; // This value selected for best decoding percentage (56%) and best Averag 4PSK Quality (77) on mpg +5 dB
			intNforGoertzel[i] = 60;
			dblFreqBin[i] = intCarFreq / 200;
		}
		else if (intBaud == 167)
		{
			intCP[i] = 6;  // Need to optimize (little difference between 6 and 12 @ wgn5, 2 Car 500 Hz)
			intNforGoertzel[i] = 60;
			dblFreqBin[i] = intCarFreq / 200;
		}
*/	
		// Get initial Reference Phase		

		if (intCP[i] == 0)
			GoertzelRealImagHanning(intFilteredMixedSamples, 0, intNforGoertzel[i], dblFreqBin[i], &dblReal, &dblImag);
		else
			GoertzelRealImag(intFilteredMixedSamples, intCP[i], intNforGoertzel[i], dblFreqBin[i], &dblReal, &dblImag);
            
		dblPhase = atan2f(dblImag, dblReal);
		Track1CarPSK(intCarFreq, strMod, dblPhase, TRUE);
		intPSKPhase_1[i] = 1000 * dblPhase;

		// Set initial mag from Reference Phase (which should be full power)
		// Done here as well as in initQAM for pkt where we may switch mode midpacket

		intCarMagThreshold[i] = sqrtf(powf(dblReal, 2) + powf(dblImag, 2));
		intCarMagThreshold[i] *= 0.75;

		intCarFreq -= 200;  // Step through each carrier Highest to lowest which is equivalent to lowest to highest before RSB mixing. 
	}
}

int Demod1CarPSKChar(int Start, int Carrier);
void SavePSKSamples(int i);

void DemodPSK()
{
	int Used[8] = {0}, Carrier;
	int Start = 0;
	int MemARQRetries = 0;

	// We can't wait for the full frame as we don't have enough RAM, so
	// we do one DMA Buffer at a time, until we run out or end of frame

	// Only continue if we have enough samples
	
	intPSKMode = strMod[0] - '0';

	while (State == AcquireFrame)
	{
		if (intFilteredMixedSamplesLength < intPSKMode * intSampPerSym + 10) // allow for a few phase corrections
		{
			// Move any unprocessessed data down buffer

			//	(while checking process - will use cyclic buffer eventually

			if (intFilteredMixedSamplesLength > 0 && Start > 0)
				memmove(intFilteredMixedSamples,
					&intFilteredMixedSamples[Start], intFilteredMixedSamplesLength * 2); 

			return;
		}
		

		if (PSKInitDone == 0)		// First time through
		{	
			if (intFilteredMixedSamplesLength < 2 * intPSKMode * intSampPerSym + 10) 
				return;				// Wait for at least 2 chars worth

			InitDemodPSK();
			intFilteredMixedSamplesLength -= intSampPerSym;
			Start += intSampPerSym;	
		}

		// If this is a multicarrier mode, we must call the
		// decode char routing for each carrier

		if (intNumCar == 1)
			intCarFreq = 1500;
		else
			intCarFreq = 1400 + (intNumCar / 2) * 200; // start at the highest carrier freq which is actually the lowest transmitted carrier due to Reverse sideband mixing

		Used[0] = Demod1CarPSKChar(Start, 0);

		if (intNumCar > 1)
		{
			intPhasesLen -= intPSKMode;
			intCarFreq -= 200;  // Step through each carrier Highest to lowest which is equivalent to lowest to highest before RSB mixing. 

			Used[1] = Demod1CarPSKChar(Start, 1);
		}

		if (intNumCar > 2)
		{
			intPhasesLen -= intPSKMode;
			intCarFreq -= 200;  // Step through each carrier Highest to lowest which is equivalent to lowest to highest before RSB mixing. 
			Used[2] = Demod1CarPSKChar(Start, 2);
			
			intPhasesLen -= intPSKMode;
			intCarFreq -= 200;  // Step through each carrier Highest to lowest which is equivalent to lowest to highest before RSB mixing. 
			Used[3] = Demod1CarPSKChar(Start, 3);
		}

		if (intNumCar > 4)
		{
			intPhasesLen -= intPSKMode;
			intCarFreq -= 200;  // Step through each carrier Highest to lowest which is equivalent to lowest to highest before RSB mixing. 

			Used[4] = Demod1CarPSKChar(Start, 4);
			intPhasesLen -= intPSKMode;
			intCarFreq -= 200;  // Step through each carrier Highest to lowest which is equivalent to lowest to highest before RSB mixing. 

			Used[5] = Demod1CarPSKChar(Start, 5);
			intPhasesLen -= intPSKMode;
			intCarFreq -= 200;  // Step through each carrier Highest to lowest which is equivalent to lowest to highest before RSB mixing. 

			Used[6] = Demod1CarPSKChar(Start, 6);
			intPhasesLen -= intPSKMode;
			intCarFreq -= 200;  // Step through each carrier Highest to lowest which is equivalent to lowest to highest before RSB mixing. 

			Used[7] = Demod1CarPSKChar(Start, 7);	
		}

		if (intPSKMode == 4)
			SymbolsLeft--;		// number still to decode
		else
			SymbolsLeft -=3;

		// If/when we reenable phase correstion we can take average of Used values.
		// ?? Should be also keep start value per carrier ??

		Start += Used[0];
		intFilteredMixedSamplesLength -= Used[0];

		if (intFilteredMixedSamplesLength < 0)
			WriteDebugLog(LOGERROR, "Corrupt intFilteredMixedSamplesLength");

		if (SymbolsLeft > 0)
			continue;	

		// Decode the phases

		DecodeCompleteTime = Now;

//		CorrectPhaseForTuningOffset(&intPhases[0][0], intPhasesLen, strMod);
			
//		if (intNumCar > 1)
//			CorrectPhaseForTuningOffset(&intPhases[1][0], intPhasesLen, strMod);
			
		if (intNumCar > 2)
		{
//			CorrectPhaseForTuningOffset(&intPhases[2][0], intPhasesLen, strMod);
//			CorrectPhaseForTuningOffset(&intPhases[3][0], intPhasesLen, strMod);
		}
		if (intNumCar > 4)
		{
//			CorrectPhaseForTuningOffset(&intPhases[4][0], intPhasesLen, strMod);
//			CorrectPhaseForTuningOffset(&intPhases[5][0], intPhasesLen, strMod);
//			CorrectPhaseForTuningOffset(&intPhases[6][0], intPhasesLen, strMod);
//			CorrectPhaseForTuningOffset(&intPhases[7][0], intPhasesLen, strMod);
		}

		// Rick uses the last carier for Quality
		intLastRcvdFrameQuality = UpdatePhaseConstellation(&intPhases[intNumCar - 1][0], &intMags[intNumCar - 1][0], strMod, FALSE);

		Decode1CarPSK(bytFrameData1, 0);
		frameLen = CorrectRawDataWithRS(bytFrameData1, bytData, intDataLen, intRSLen, intFrameType, 0);
		
		if (intNumCar > 1)
		{
			Decode1CarPSK(bytFrameData2, 1);
			frameLen +=  CorrectRawDataWithRS(bytFrameData2, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 1);
		}
		if (intNumCar > 2)
		{
			Decode1CarPSK(bytFrameData3, 2);
			Decode1CarPSK(bytFrameData4, 3);
			frameLen +=  CorrectRawDataWithRS(bytFrameData3, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 2);
			frameLen +=  CorrectRawDataWithRS(bytFrameData4, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 3);

		}
		if (intNumCar > 4)
		{
			Decode1CarPSK(bytFrameData5, 4);
			Decode1CarPSK(bytFrameData6, 5);
			Decode1CarPSK(bytFrameData7, 6);
			Decode1CarPSK(bytFrameData8, 7);
			frameLen +=  CorrectRawDataWithRS(bytFrameData5, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 4);
			frameLen +=  CorrectRawDataWithRS(bytFrameData6, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 5);
			frameLen +=  CorrectRawDataWithRS(bytFrameData7, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 6);
			frameLen +=  CorrectRawDataWithRS(bytFrameData8, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 7);

		}

		// If variable length packet frame header we only have header - leave rx running
		
		if (intFrameType == PktFrameHeader)
		{
			State = SearchingForLeader;
			
			// Save any unused samples
			
			if (intFilteredMixedSamplesLength > 0 && Start > 0)
				memmove(intFilteredMixedSamples,
					&intFilteredMixedSamples[Start], intFilteredMixedSamplesLength * 2); 

			return;
		}
	
#ifdef MEMORYARQ

		for (Carrier = 0; Carrier < intNumCar; Carrier++)
		{
			if (!CarrierOk[Carrier])
			{
				// Decode error - save data for MEM ARQ

				SavePSKSamples(Carrier);
				
				if (intSumCounts[Carrier] > 1)
				{
					Decode1CarQAM(bytFrameData[Carrier], Carrier); // try to decode based on the WeightedAveragePhases
					MemARQRetries++;
				}
			}
		}

		if (MemARQRetries)
		{
			// We've retryed to decode - see if ok now

			int OKNow = TRUE;

			WriteDebugLog(LOGDEBUG, "DemodPSK retry RS on MEM ARQ Corrected frames");
			frameLen = 0;
	
			for (Carrier = 0; Carrier < intNumCar; Carrier++)
			{
				frameLen += CorrectRawDataWithRS(bytFrameData[Carrier], bytData, intDataLen, intRSLen, intFrameType, Carrier);
				if (CarrierOk[Carrier] == 0)
					OKNow = FALSE;
			}

			if (OKNow && AccumulateStats) 
				intGoodPSKSummationDecodes++;
		}
#endif
	
		// prepare for next

		State = SearchingForLeader;
		DiscardOldSamples();
		ClearAllMixedSamples();
	}
	return;
}

// Function to demodulate one carrier for all PSK frame types
int Demod1CarPSKChar(int Start, int Carrier)
{
	// Converts intSample to an array of differential phase and magnitude values for the Specific Carrier Freq
	// intPtr should be pointing to the approximate start of the first reference/training symbol (1 of 3) 
	// intPhase() is an array of phase values (in milliradians range of 0 to 6283) for each symbol 
	// intMag() is an array of Magnitude values (not used in PSK decoding but for constellation plotting or QAM decoding)
	// Objective is to use Minimum Phase Error Tracking to maintain optimum pointer position

	//	This is called for one DMA buffer of samples (normally 1200)

	float dblReal, dblImag;
	int intMiliRadPerSample = intCarFreq * M_PI / 6;
	int i;
	int intNumOfSymbols = intPSKMode;
	int origStart = Start;;

	if (CarrierOk[Carrier])		// Already decoded this carrier?
	{
		intPhasesLen += intNumOfSymbols;
		return intSampPerSym * intNumOfSymbols;
	}

	for (i = 0; i <  intNumOfSymbols; i++)
	{
		if (intCP[Carrier] == 0)
			GoertzelRealImagHanning(intFilteredMixedSamples, Start, intNforGoertzel[Carrier], dblFreqBin[Carrier], &dblReal, &dblImag);
		else
			GoertzelRealImag(intFilteredMixedSamples, Start + intCP[Carrier], intNforGoertzel[Carrier], dblFreqBin[Carrier], &dblReal, &dblImag);
	
		intMags[Carrier][intPhasesLen] = sqrtf(powf(dblReal, 2) + powf(dblImag, 2));
		intPSKPhase_0[Carrier] = 1000 * atan2f(dblImag, dblReal);
		intPhases[Carrier][intPhasesLen] = -(ComputeAng1_Ang2(intPSKPhase_0[Carrier], intPSKPhase_1[Carrier]));

/*
		if (Carrier == 0)
		{
			Corrections = Track1CarPSK(intCarFreq, strMod, atan2f(dblImag, dblReal), FALSE);

			if (Corrections != 0)
			{
				Start += Corrections;

				if (intCP[i] == 0)
					GoertzelRealImagHanning(intFilteredMixedSamples, Start, intNforGoertzel[Carrier], dblFreqBin[Carrier], &dblReal, &dblImag);
				else
					GoertzelRealImag(intFilteredMixedSamples, Start + intCP[Carrier], intNforGoertzel[Carrier], dblFreqBin[Carrier], &dblReal, &dblImag);

				intPSKPhase_0[Carrier] = 1000 * atan2f(dblImag, dblReal);
			}
		}
*/
		intPSKPhase_1[Carrier] = intPSKPhase_0[Carrier];
		intPhasesLen++;
		Start += intSampPerSym;
	
	}
	if (AccumulateStats)
		intPSKSymbolCnt += intNumOfSymbols;

	return (Start - origStart);	// Symbols we've consumed
}

VOID InitDemodQAM()
{
	// Called at start of frame

	int i;
	float dblPhase, dblReal, dblImag;

	intPSKMode = 8;				// 16QAM uses 8 PSK
	dblPhaseInc = 2 * M_PI * 1000 / 8;
	intPhasesLen = 0;

	PSKInitDone = TRUE;

	intSampPerSym = 120;

	if (intNumCar == 1)
		intCarFreq = 1500;
	else
		intCarFreq = 1400 + (intNumCar / 2) * 200; // start at the highest carrier freq which is actually the lowest transmitted carrier due to Reverse sideband mixing
  
	for (i= 0; i < intNumCar; i++)
	{
		// Only 100 Hz for QAM
						
		intCP[i] = 0;
		intNforGoertzel[i] = 120;
		dblFreqBin[i] = intCarFreq / 100;
	
		// Get initial Reference Phase
		
		GoertzelRealImagHanning(intFilteredMixedSamples, intCP[i], intNforGoertzel[i], dblFreqBin[i], &dblReal, &dblImag);
		dblPhase = atan2f(dblImag, dblReal);

		// Set initial mag from Reference Phase (which should be full power)

		intCarMagThreshold[i] = sqrtf(powf(dblReal, 2) + powf(dblImag, 2));
		intCarMagThreshold[i] *= 0.75;

		Track1CarPSK(intCarFreq, strMod, dblPhase, TRUE);
		intPSKPhase_1[i] = 1000 * dblPhase;
		intCarFreq -= 200;  // Step through each carrier Highest to lowest which is equivalent to lowest to highest before RSB mixing. 
	}
}

int Demod1CarQAMChar(int Start, int Carrier);

#ifdef MEMORYARQ

//	Function to average two angles using magnitude weighting

short WeightedAngleAvg(short intAng1, short intAng2)
{
	// Ang1 and Ang 2 are in the range of -3142 to + 3142 (miliradians)
	// works but should come up with a routine that avoids Sin, Cos, Atan2
    // Modified in Rev 0.3.5.1 to "weight" averaging by intMag1 and intMag2 (why!!!)

	float dblSumX, dblSumY;

	dblSumX = cosf(intAng1 / 1000.0) + cosf(intAng2 / 1000.0);
	dblSumY = sinf(intAng1 / 1000.0) + sinf(intAng2 / 1000.0);
        
	return (1000 * atan2f(dblSumY, dblSumX));
}

void SaveQAMSamples(int i)
{
	int m;

	if (intSumCounts[i] == 0)
	{
		// First try - initialize Sum counts Phase average and Mag Average 

		for (m = 0; m < intPhasesLen; m++)
		{
			intCarPhaseAvg[i][m] = intPhases[i][m];
			intCarMagAvg[i][m] = intMags[i][m];
		}
	}
	else
	{
		for (m = 0; m < intPhasesLen; m++)
		{
			intCarPhaseAvg[i][m] = WeightedAngleAvg(intCarPhaseAvg[i][m], intPhases[i][m]);
			intPhases[i][m] = intCarPhaseAvg[i][m];
			// Use simple weighted average for Mags 
			intCarMagAvg[i][m] = (intCarMagAvg[i][m] * intSumCounts[i] + intMags[i][m]) / (intSumCounts[i] + 1);		
			intMags[i][m] = intCarMagAvg[i][m];
		}
	}
	intSumCounts[i]++;
}

void SavePSKSamples(int i)
{
	int m;

	if (intSumCounts[i] == 0)
	{
		// First try - initialize Sum counts Phase average and Mag Average 

		for (m = 0; m < intPhasesLen; m++)
		{
			intCarPhaseAvg[i][m] = intPhases[i][m];
		}
	}
	else
	{
		for (m = 0; m < intPhasesLen; m++)
		{
			intCarPhaseAvg[i][m] = WeightedAngleAvg(intCarPhaseAvg[i][m], intPhases[i][m]);
			intPhases[i][m] = intCarPhaseAvg[i][m];
		}
	}
	intSumCounts[i]++;
}			

#endif

BOOL DemodQAM()
{
	int Used = 0;
	int Start = 0;

	// We can't wait for the full frame as we don't have enough RAM, so
	// we do one DMA Buffer at a time, until we run out or end of frame

	// Only continue if we have enough samples

	while (State == AcquireFrame)
	{
		if (intFilteredMixedSamplesLength < 8 * intSampPerSym + 10) // allow for a few phase corrections
		{
			// Move any unprocessessed data down buffer

			//	(while checking process - will use cyclic buffer eventually

			if (intFilteredMixedSamplesLength > 0)
				memmove(intFilteredMixedSamples,
					&intFilteredMixedSamples[Start], intFilteredMixedSamplesLength * 2); 

			return FALSE;
		}
		
		if (PSKInitDone == 0)		// First time through
		{	
			if (intFilteredMixedSamplesLength < 9 * intSampPerSym + 10) 
				return FALSE;				// Wait for at least 2 chars worth

			InitDemodQAM();
			intFilteredMixedSamplesLength -= intSampPerSym;
			Start += intSampPerSym;	
		}

		// If this is a multicarrier mode, we must call the
		// decode char routine for each carrier

		if (intNumCar == 1)
			intCarFreq = 1500;
		else
			intCarFreq = 1400 + (intNumCar / 2) * 200; // start at the highest carrier freq which is actually the lowest transmitted carrier due to Reverse sideband mixing

	
		Used = Demod1CarQAMChar(Start, 0);		// demods two phase values - enough for one char
	
		if (intNumCar > 1)
		{
			intPhasesLen -= 2;
			intCarFreq -= 200;  // Step through each carrier Highest to lowest which is equivalent to lowest to highest before RSB mixing. 
			Demod1CarQAMChar(Start, 1);
		}

		if (intNumCar > 2)
		{
			intPhasesLen -= 2;
			intCarFreq -= 200;  // Step through each carrier Highest to lowest which is equivalent to lowest to highest before RSB mixing. 
			Demod1CarQAMChar(Start, 2);
			intPhasesLen -= 2;
			intCarFreq -= 200;  // Step through each carrier Highest to lowest which is equivalent to lowest to highest before RSB mixing. 
			Demod1CarQAMChar(Start, 3);
		}

		if (intNumCar > 4)
		{
			intPhasesLen -= 2;
			intCarFreq -= 200;  // Step through each carrier Highest to lowest which is equivalent to lowest to highest before RSB mixing. 
			Demod1CarQAMChar(Start, 4);
	
			intPhasesLen -= 2;
			intCarFreq -= 200;  // Step through each carrier Highest to lowest which is equivalent to lowest to highest before RSB mixing. 
			Demod1CarQAMChar(Start, 5);

			intPhasesLen -= 2;
			intCarFreq -= 200;  // Step through each carrier Highest to lowest which is equivalent to lowest to highest before RSB mixing. 
			Demod1CarQAMChar(Start, 6);

			intPhasesLen -= 2;
			intCarFreq -= 200;  // Step through each carrier Highest to lowest which is equivalent to lowest to highest before RSB mixing. 
			Demod1CarQAMChar(Start, 7);	
		}

		SymbolsLeft--;		// number still to decode - we've done one
	
		Start += Used;
		intFilteredMixedSamplesLength -= Used;

		if (SymbolsLeft <= 0)	
		{
			// Frame complete - decode it

			DecodeCompleteTime = Now;

		CorrectPhaseForTuningOffset(&intPhases[0][0], intPhasesLen, strMod);
			
//		if (intNumCar > 1)
//			CorrectPhaseForTuningOffset(&intPhases[1][0], intPhasesLen, strMod);
			
		if (intNumCar > 2)
		{
//			CorrectPhaseForTuningOffset(&intPhases[2][0], intPhasesLen, strMod);
//			CorrectPhaseForTuningOffset(&intPhases[3][0], intPhasesLen, strMod);
		}
		if (intNumCar > 4)
		{
//			CorrectPhaseForTuningOffset(&intPhases[4][0], intPhasesLen, strMod);
//			CorrectPhaseForTuningOffset(&intPhases[5][0], intPhasesLen, strMod);
//			CorrectPhaseForTuningOffset(&intPhases[6][0], intPhasesLen, strMod);
//			CorrectPhaseForTuningOffset(&intPhases[7][0], intPhasesLen, strMod);
		}

			intLastRcvdFrameQuality = UpdatePhaseConstellation(&intPhases[intNumCar - 1][0], &intMags[intNumCar - 1][0], strMod, TRUE);

			Decode1CarQAM(bytFrameData1, 0);
			frameLen = CorrectRawDataWithRS(bytFrameData1, bytData, intDataLen, intRSLen, intFrameType, 0);

			if (intNumCar > 1)
			{
				Decode1CarQAM(bytFrameData2, 1);
				frameLen +=  CorrectRawDataWithRS(bytFrameData2, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 1);

			}

		if (intNumCar > 2)
		{
			Decode1CarQAM(bytFrameData3, 2);
			Decode1CarQAM(bytFrameData4, 3);
			frameLen +=  CorrectRawDataWithRS(bytFrameData3, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 2);
			frameLen +=  CorrectRawDataWithRS(bytFrameData4, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 3);

		}
		if (intNumCar > 4)
		{
			Decode1CarQAM(bytFrameData5, 4);
			Decode1CarQAM(bytFrameData6, 5);
			Decode1CarQAM(bytFrameData7, 6);
			Decode1CarQAM(bytFrameData8, 7);
			frameLen +=  CorrectRawDataWithRS(bytFrameData5, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 4);
			frameLen +=  CorrectRawDataWithRS(bytFrameData6, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 5);
			frameLen +=  CorrectRawDataWithRS(bytFrameData7, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 6);
			frameLen +=  CorrectRawDataWithRS(bytFrameData8, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 7);

		}



			// Check Data


#ifdef MEMORYARQ

			if ((!CarrierOk[0] || !CarrierOk[1]) && intFrameType != PktFrameHeader)
			{
				// Decode error - save data for MEM ARQ

				if (!CarrierOk[0])
				{
					SaveQAMSamples(0);
					if (intSumCounts[0] > 1)
					{
						Decode1CarQAM(bytFrameData1, 0); // try to decode based on the WeightedAveragePhases
					}
				}			
				if (!CarrierOk[1])
				{
					SaveQAMSamples(1);
					if (intSumCounts[1] > 1)
					{
						Decode1CarQAM(bytFrameData2, 1);
					}
				}

				if (intSumCounts[0] > 1 || intSumCounts[1] > 1)
				{
					// We've retryed to decode - see if ok now

					WriteDebugLog(LOGDEBUG, "DemodQAM retry RS on MEM ARQ Corrected frames");

					frameLen = CorrectRawDataWithRS(bytFrameData1, bytData, intDataLen, intRSLen, intFrameType, 0);
		
					if (intNumCar > 1)
						frameLen +=  CorrectRawDataWithRS(bytFrameData2, &bytData[frameLen], intDataLen, intRSLen, intFrameType, 1);

					if (CarrierOk[0] && CarrierOk[1])
						if (AccumulateStats) intGoodQAMSummationDecodes++;
				}
			}
#endif
			// prepare for next

			DiscardOldSamples();
			ClearAllMixedSamples();
			State = SearchingForLeader;
		}
	}
	return TRUE;
}

int Demod1CarQAMChar(int Start, int Carrier)
{
	// Converts intSample to an array of differential phase and magnitude values for the Specific Carrier Freq
	// intPtr should be pointing to the approximate start of the first reference/training symbol (1 of 3) 
	// intPhase() is an array of phase values (in milliradians range of 0 to 6283) for each symbol 
	// intMag() is an array of Magnitude values (not used in PSK decoding but for constellation plotting or QAM decoding)
	// Objective is to use Minimum Phase Error Tracking to maintain optimum pointer position

	//	This is called for one DMA buffer of samples (normally 1200)

	float dblReal, dblImag;
	int intMiliRadPerSample = intCarFreq * M_PI / 6;
	int i;
	int intNumOfSymbols = 2;
	int origStart = Start;;

	if (CarrierOk[Carrier])		// Already decoded this carrier?
	{
		intPhasesLen += intNumOfSymbols;
		return intSampPerSym * intNumOfSymbols;
	}

	for (i = 0; i <  intNumOfSymbols; i++)
	{
	//	GoertzelRealImag(intFilteredMixedSamples, Start + intCP[Carrier], intNforGoertzel[Carrier], dblFreqBin[Carrier], &dblReal, &dblImag);
		GoertzelRealImagHanning(intFilteredMixedSamples, Start + intCP[Carrier], intNforGoertzel[Carrier], dblFreqBin[Carrier], &dblReal, &dblImag);
		intMags[Carrier][intPhasesLen] = sqrtf(powf(dblReal, 2) + powf(dblImag, 2));
		intPSKPhase_0[Carrier] = 1000 * atan2f(dblImag, dblReal);
		intPhases[Carrier][intPhasesLen] = -(ComputeAng1_Ang2(intPSKPhase_0[Carrier], intPSKPhase_1[Carrier]));


/*
		if (Carrier == 0)
		{
			Corrections = Track1CarPSK(intCarFreq, strMod, atan2f(dblImag, dblReal), FALSE);

			if (Corrections != 0)
			{
				Start += Corrections;

		//	GoertzelRealImag(intFilteredMixedSamples, Start + intCP[Carrier], intNforGoertzel[Carrier], dblFreqBin[Carrier], &dblReal, &dblImag);
				GoertzelRealImagHanning(intFilteredMixedSamples, Start + intCP[Carrier], intNforGoertzel[Carrier], dblFreqBin[Carrier], &dblReal, &dblImag);
				intPSKPhase_0[Carrier] = 1000 * atan2f(dblImag, dblReal);
			}
		}
*/
		intPSKPhase_1[Carrier] = intPSKPhase_0[Carrier];
		intPhasesLen++;
		Start += intSampPerSym;
	}
       
	if (AccumulateStats)
		intQAMSymbolCnt += intNumOfSymbols;

	return (Start - origStart);	// Symbols we've consumed
}

//	function to decode one carrier from tones (used to decode from Averaged intToneMags) 

BOOL Decode1Car4FSKFromTones(UCHAR * bytData, int intToneMags)
{
	//	Decodes intToneMags() to an array of bytes   
    //	Updates bytData() with decoded 

/*
	UCHAR bytSym;
	int intIndex;

        ReDim bytData(intToneMags.Length \ 16 - 1)

        For i As Integer = 0 To bytData.Length - 1 ' For each data byte
            intIndex = 16 * i
            For j As Integer = 0 To 3 ' for each 4FSK symbol (2 bits) in a byte
                If intToneMags(intIndex) > intToneMags(intIndex + 1) And intToneMags(intIndex) > intToneMags(intIndex + 2) And intToneMags(intIndex) > intToneMags(intIndex + 3) Then
                    bytSym = 0
                ElseIf intToneMags(intIndex + 1) > intToneMags(intIndex) And intToneMags(intIndex + 1) > intToneMags(intIndex + 2) And intToneMags(intIndex + 1) > intToneMags(intIndex + 3) Then
                    bytSym = 1
                ElseIf intToneMags(intIndex + 2) > intToneMags(intIndex) And intToneMags(intIndex + 2) > intToneMags(intIndex + 1) And intToneMags(intIndex + 2) > intToneMags(intIndex + 3) Then
                    bytSym = 2
                Else
                    bytSym = 3
                End If
                bytData(i) = (bytData(i) << 2) + bytSym
                intIndex += 4
            Next j
        Next i
        Return True
    End Function  '  Decode1Car4FSKFromTones
*/
	return TRUE;
}

/*    ' Function to decode one carrier from tones (used to decode from Averaged intToneMags) 
    Private Function Decode1Car8FSKFromTones(ByRef bytData() As Byte, ByRef intToneMags() As Int32) As Boolean
        ' Decodes intToneMags() to an array of bytes   
        ' Updates bytData() with decoded 

        Dim bytSym As Byte
        Dim intThreeBytes As Int32
        ReDim bytData(3 * intToneMags.Length \ 64 - 1)
        Dim intMaxMag As Int32
        For i As Integer = 0 To (bytData.Length \ 3) - 1   ' For each group of 3 bytes data byte
            intThreeBytes = 0
            For j As Integer = 0 To 7 ' for each group of 8 symbols (24 bits) 
                intMaxMag = 0
                For k As Integer = 0 To 7 ' for each of 8 possible tones per symbol
                    If intToneMags((i * 64) + 8 * j + k) > intMaxMag Then
                        intMaxMag = intToneMags((i * 64) + 8 * j + k)
                        bytSym = k
                    End If
                Next k
                intThreeBytes = (intThreeBytes << 3) + bytSym
            Next j
            bytData(3 * i) = (intThreeBytes And &HFF0000) >> 16
            bytData(3 * i + 1) = (intThreeBytes And &HFF00) >> 8
            bytData(3 * i + 2) = (intThreeBytes And &HFF)
        Next i
        Return True
    End Function  '  Decode1Car8FSKFromTones

    ' Function to decode one carrier from tones (used to decode from Averaged intToneMags) 
    Private Function Decode1Car16FSKFromTones(ByRef bytData() As Byte, ByRef intToneMags() As Int32) As Boolean
        ' Decodes intToneMags() to an array of bytes   
        ' Updates bytData() with decoded tones 

        Dim bytSym As Byte
        Dim intMaxMag As Int32
        ReDim bytData(intToneMags.Length \ 32 - 1)
        For i As Integer = 0 To bytData.Length - 1 ' For each data byte
            For j As Integer = 0 To 1 ' for each 16FSK symbol (4 bits) in a byte
                intMaxMag = 0
                For k As Integer = 0 To 15
                    If intToneMags(i * 32 + 16 * j + k) > intMaxMag Then
                        intMaxMag = intToneMags(i * 32 + 16 * j + k)
                        bytSym = k
                    End If
                Next k
                bytData(i) = (bytData(i) << 4) + bytSym
            Next j
        Next i
        Return True
    End Function  '  Decode1Car16FSKFromTones

*/


/*
//	Subroutine to update the Busy detector when not displaying Spectrum or Waterfall (graphics disabled)
 		
int LastBusyCheck = 0;
extern BOOL blnBusyStatus;

int intWaterfallRow = 0;

void UpdateBusyDetector(short * bytNewSamples)
{
	float dblReF[1024];
	float dblImF[1024];
	float dblMag[206];

	
	static BOOL blnLastBusyStatus;
	
	float dblMagAvg = 0;
	int intTuneLineLow, intTuneLineHi, intDelta;
	int i;

	if (ProtocolState != DISC)		// ' Only process busy when in DISC state
		return;

	if (State != SearchingForLeader)
		return;						// only when looking for leader

	if (Now - LastBusyCheck < 100)
		return;

	LastBusyCheck = Now;

	FourierTransform(1024, bytNewSamples, &dblReF[0], &dblImF[0], FALSE);

	for (i = 0; i <  206; i++)
	{
		//	starting at ~300 Hz to ~2700 Hz Which puts the center of the signal in the center of the window (~1500Hz)
            
		dblMag[i] = powf(dblReF[i + 25], 2) + powf(dblImF[i + 25], 2);	 // first pass 
		dblMagAvg += dblMag[i];
	}
	intDelta = (ExtractARQBandwidth() / 2 + TuningRange) / 11.719f;

	intTuneLineLow = max((103 - intDelta), 3);
	intTuneLineHi = min((103 + intDelta), 203);
   
	// At the moment we only get here what seaching for leader,
	// but if we want to plot spectrum we should call
	// it always

	if (ProtocolState == DISC)		// ' Only process busy when in DISC state
	{
		blnBusyStatus = BusyDetect3(dblMag, intTuneLineLow, intTuneLineHi);
		
		if (blnBusyStatus && !blnLastBusyStatus)
		{
			QueueCommandToHost("BUSY TRUE");
         	newStatus = TRUE;				// report to PTC
		}
		//    stcStatus.Text = "True"
            //    queTNCStatus.Enqueue(stcStatus)
            //    'Debug.WriteLine("BUSY TRUE @ " & Format(DateTime.UtcNow, "HH:mm:ss"))
			
		else if (blnLastBusyStatus && !blnBusyStatus)
		{
			QueueCommandToHost("BUSY FALSE");
			newStatus = TRUE;				// report to PTC
		} 
		//    stcStatus.Text = "False"
        //    queTNCStatus.Enqueue(stcStatus)
        //    'Debug.WriteLine("BUSY FALSE @ " & Format(DateTime.UtcNow, "HH:mm:ss"))

		blnLastBusyStatus = blnBusyStatus;
	}

#ifdef PLOTSPECTRUM

	dblMagAvg = log10f(dblMagAvg / 5000.0f);
	
	for (i = 0; i < 206; i++)
	{
		// The following provides some AGC over the waterfall to compensate for avg input level.
        
		float y1 = (0.25f + 2.5f / dblMagAvg) * log10f(0.01 + dblMag[i]);
        int objColor;
		int clrTLC = WHITE;

		// Set the pixel color based on the intensity (log) of the spectral line
		if (y1 > 6.5)
			objColor = Orange; // Strongest spectral line 
		else if (y1 > 6)
			objColor = Khaki;
		else if (y1 > 5.5)
			objColor = Cyan;
		else if (y1 > 5)
			objColor = DeepSkyBlue;
		else if (y1 > 4.5)
			objColor = RoyalBlue;
		else if (y1 > 4)
			objColor = Navy;
		else
			objColor = Black;
		
		if (i == 103)
			mySetPixel(intWaterfallRow, i/2 + 10, Tomato);  // 1500 Hz line (center)
		else if (i == intTuneLineLow || i == intTuneLineLow - 1 || i == intTuneLineHi || i == intTuneLineHi + 1)
			mySetPixel(intWaterfallRow, i/2 + 10, clrTLC);
		else
			mySetPixel(intWaterfallRow, i/2 + 10, objColor); // ' Else plot the pixel as received
	}
	updateDisplay();
	intWaterfallRow++;
	if (intWaterfallRow > 60)
		intWaterfallRow = 2;

#endif  


}

*/
