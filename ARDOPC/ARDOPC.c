// ARDOPC.cpp : Defines the entry point for the console application.
//

const char ProductName[] = "ardopc";

// Version k  Fix conflicting definitions of bytDataToSend
// Version m  Add CM108 PTT (Sept 2021)

// On Linux Some cards seem to stop sending but not report not running, so also check that avail is decreasing
//     (Debug 1 to 5)

// Version n Add Hamlib PTT (incomplete) (July 2022)

// Version p Leader timing changes (Jan 23)
// Version q Fix extradelay (Jan 23)


#ifdef WIN32
#define _CRT_SECURE_NO_DEPRECATE
#define _USE_32BIT_TIME_T

#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#else
#include <unistd.h>
#define SOCKET int
#define closesocket close
#endif

#include "ARDOPC.h"
#include "getopt.h"

void CompressCallsign(char * Callsign, UCHAR * Compressed);
void CompressGridSquare(char * Square, UCHAR * Compressed);
void  ASCIIto6Bit(char * Padded, UCHAR * Compressed);
void GetTwoToneLeaderWithSync(int intSymLen);
void SendID(BOOL blnEnableCWID);
void PollReceivedSamples();
void CheckTimers();
BOOL GetNextARQFrame();
BOOL TCPHostInit();
BOOL SerialHostInit();
BOOL KISSInit();
void SerialHostPoll();
void TCPHostPoll();
BOOL MainPoll();
VOID PacketStartTX();
void PlatformSleep();
BOOL BusyDetect2(float * dblMag, int intStart, int intStop);
BOOL IsPingToMe(char * strCallsign);
void LookforPacket(float * dblMag, float dblMagAvg, int count, float * real, float * imag);
void PktARDOPStartTX();

// Config parameters

char GridSquare[9] = "";
char Callsign[10] = "";
BOOL wantCWID = FALSE;
BOOL CWOnOff = FALSE;
BOOL NeedID = FALSE;		// SENDID Command Flag
BOOL NeedCWID = FALSE;		// SENDCWID Command Flag
BOOL NeedConReq = FALSE;	// ARQCALL Command Flag
BOOL NeedPing = FALSE;		// PING Command Flag
BOOL NeedTwoToneTest = FALSE;
enum _ARQBandwidth CallBandwidth = UNDEFINED;
BOOL UseKISS = TRUE;			// Enable Packet (KISS) interface
int PingCount;

BOOL blnPINGrepeating = False;
BOOL blnFramePending = False;	//  Cancels last repeat
int intPINGRepeats = 0;

#ifdef TEENSY
int WaterfallActive = 0;		// Waterfall display off
int SpectrumActive = 0;			// Spectrum display off
#else
int WaterfallActive = 1;		// Waterfall display on
int SpectrumActive = 0;			// Spectrum display off
#endif

char ConnectToCall[16] = "";

#ifdef TEENSY
int LeaderLength = 500;
#else
int LeaderLength = 240;
#endif
unsigned int ARQTimeout = 120;
int TuningRange = 100;
int TXLevel = 300;				// 300 mV p-p Used on Teensy
int RXLevel = 0;				// Configured Level - zero means auto tune
int autoRXLevel = 1500;			// calculated level
int ARQConReqRepeats = 5;
BOOL DebugLog = TRUE;
BOOL CommandTrace = TRUE;
int DriveLevel = 100;
char strFECMode[16] = "4FSK.500.100";
int FECRepeats = 0;
BOOL FECId = FALSE;
int Squelch = 5;
int BusyDet = 5;
enum _ARQBandwidth ARQBandwidth = B2000MAX;
char HostPort[80] = "";
int port = 8515;
int pktport = 0;
BOOL RadioControl = FALSE;
BOOL SlowCPU = FALSE;
BOOL AccumulateStats = TRUE;
BOOL Use600Modes = FALSE;
BOOL FSKOnly = FALSE;
BOOL fastStart = TRUE;
BOOL ConsoleLogLevel = LOGDEBUG;
BOOL FileLogLevel = LOGDEBUGPLUS;
BOOL EnablePingAck = TRUE;

BOOL gotGPIO = FALSE;
BOOL useGPIO = FALSE;

int pttGPIOPin = -1;
BOOL pttGPIOInvert = FALSE;

HANDLE hCATDevice = 0;	
char CATPort[80] = "";			// Port for CAT.
int CATBAUD = 19200;
int EnableHostCATRX = FALSE;	// Set when host sends RADIOHEX command

HANDLE hPTTDevice = 0;
char PTTPort[80] = "";			// Port for Hardware PTT - may be same as control port.
int PTTBAUD = 19200;

UCHAR PTTOnCmd[64];
UCHAR PTTOnCmdLen = 0;

UCHAR PTTOffCmd[64];
UCHAR PTTOffCmdLen = 0;

// Stats

//    Public Structure QualityStats
  
int int4FSKQuality;
int int4FSKQualityCnts;
int int8FSKQuality;
int int8FSKQualityCnts;
int int16FSKQuality;
int int16FSKQualityCnts;
int intFSKSymbolsDecoded;
int intPSKQuality[2];
int intPSKQualityCnts[2];
int intPSKSymbolsDecoded; 

int intQAMQuality;
int intQAMQualityCnts;
int intQAMSymbolsDecoded;


char stcLastPingstrSender[10];
char stcLastPingstrTarget[10];
int stcLastPingintRcvdSN;
int stcLastPingintQuality;
time_t stcLastPingdttTimeReceived;

BOOL blnInitializing = FALSE;

BOOL blnLastPTT = FALSE;

BOOL PlayComplete = FALSE;

BOOL blnBusyStatus;
BOOL newStatus;

unsigned int tmrSendTimeout;

int intCalcLeader;        // the computed leader to use based on the reported Leader Length
int intRmtLeaderMeasure = 0;

int dttCodecStarted;

enum _ReceiveState State;
enum _ARDOPState ProtocolState;

const char ARDOPStates[8][9] = {"OFFLINE", "DISC", "ISS", "IRS", "IDLE", "IRStoISS", "FECSEND", "FECRCV"};

struct SEM Semaphore = {0, 0, 0, 0};

BOOL SoundIsPlaying = FALSE;
BOOL Capturing = TRUE;

int DecodeCompleteTime;

BOOL blnAbort = FALSE;
int intRepeatCount;
BOOL blnARQDisconnect = FALSE;

int dttLastPINGSent;

enum _ProtocolMode ProtocolMode = FEC;

extern BOOL blnEnbARQRpt;
extern BOOL blnDISCRepeating;
extern char strRemoteCallsign[10];
extern char strLocalCallsign[10];
extern char strFinalIDCallsign[10];
extern int dttTimeoutTrip;
extern unsigned int dttLastFECIDSent;
extern int intFrameRepeatInterval;
extern BOOL blnPending;
extern unsigned int tmrIRSPendingTimeout;
extern unsigned int tmrFinalID;
extern unsigned int tmrPollOBQueue;
int Encode4FSKControl(UCHAR bytFrameType, UCHAR bytSessionID, UCHAR * bytreturn);
void SendPING(char * strMycall, char * strTargetCall, int intRpt);

int intRepeatCnt;

extern SOCKET TCPControlSock, TCPDataSock, PktSock;

BOOL blnClosing = FALSE;
BOOL blnCodecStarted = FALSE;

unsigned int dttNextPlay = 0;

extern BOOL InitRXO;

const UCHAR bytValidFrameTypesALL[]=
{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
35, 36, 41, 44, 45,
 46, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, PINGACK, PING,

	0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,	// 40 - 4F
	0x4A,0x4B,0x4C,0x4D,
	0x50,0x51,0x52,0x53,0x54,0x55,						// 50 - 5F 
	0x60,0x61,0x62,0x63,0x64,0x65,						// 60 - 6F
	0x70,0x71,0x72,0x73,0x74,0x75,0x7A,0x7B,0x7C,0x7D,	// 70 - 7F

 PktFrameHeader, 208, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234,
 124, 125, 208, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234,
 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248,
 249, 250, 251, 252, 253, 254, 255};

const UCHAR bytValidFrameTypesISS[]=		// ACKs, NAKs, END, DISC, BREAK
{
 //NAK
 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
 // BREAK, DISC, or END
 BREAK, DISCFRAME, END, ConRejBusy, ConRejBW, 
// Con req and Con ACK
 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
 //ACK
 PktFrameHeader, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255};


const UCHAR * bytValidFrameTypes;

int bytValidFrameTypesLengthISS = sizeof(bytValidFrameTypesISS);
int bytValidFrameTypesLengthALL = sizeof(bytValidFrameTypesALL);
int bytValidFrameTypesLength;

/*
const UCHAR isValidFrame[256]= 
{
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,	// 00 - 0F    ACK and NAK
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,	// 10 - 1F
	0,0,0,1,1,0,0,0,0,1,0,0,1,1,1,0,	// BREAK=23, IDLE=24, DISC=29, END=2C, ConRejBusy=2D, ConRejBW=2E

	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,	// 30 - 3F (3d PING Ack 3E Ping
	1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,	// 40 - 4F
	1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,	// 50 - 5F 
	1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,	// 60 - 6F
	1,1,1,1,1,1,0,0,0,0,1,1,1,1,0,0,	// 70 - 7F
			

	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 80 - 8F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 90 - 9F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// A0 - AF
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// B0 - BF
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// C0 - CF

	// experimental SOUNDINGs D0

	1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// D0 - DF
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,	// e0 - eF    ACK and NAK
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1		// f0 - ff
};
*/

BOOL blnTimeoutTriggered = FALSE;

//	We can't keep the audio samples for retry, but we can keep the
//	encoded data

unsigned char bytEncodedBytes[1800] ="";		// I think the biggest is 600 bd 768 + overhead
int EncLen;


extern UCHAR bytSessionID;

int intLastRcvdFrameQuality;

int intAmp = 26000;	   // Selected to have some margin in calculations with 16 bit values (< 32767) this must apply to all filters as well. 

const char strAllDataModes[18][15] =
		{"4FSK.200.50S", "4PSK.200.100S",
		"4PSK.200.100", "8PSK.200.100", "16QAM.200.100",
		"4FSK.500.100S", "4FSK.500.100",
		"4PSK.500.100", "8PSK.500.100", "16QAM.500.100",
		"4PSK.1000.100", "8PSK.1000.100", "16QAM.1000.100", 
		"4PSK.2000.100", "8PSK.2000.100", "16QAM.2000.100", 
		"4FSK.2000.600", "4FSK.2000.600S"};

int strAllDataModesLen = 18;

// Frame Speed By Type (from Rick's spreadsheet)

const short Rate[256] = 
{
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 00 - 0F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 10 - 1F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 20 - 2F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 30 - 3F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 40 - 4F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 50 - 5F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 60 - 6F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 70 - 7F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 80 - 8F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 90 - 9F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// a0 - AF
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// b0 - BF
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// c0 - CF
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// d0 - DF
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// e0 - EF
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};	// f0 - FF

const short FrameSize[256] =
{
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 00 - 0F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 10 - 1F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 20 - 2F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 30 - 3F
	64,64,16,16,108,108,128,128,16,16,64,64,32,32,0,0,	// 40 - 4F
	128,128,216,216,256,256,0,0,0,0,0,0,0,0,0,0,	// 50 - 5F
	256,256,432,432,512,512,0,0,0,0,0,0,0,0,0,0,	// 60 - 6F
	512,512,864,864,1024,1024,0,0,0,0,600,600,200,200,0,0,	// 70 - 7F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 80 - 8F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// 90 - 9F
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// a0 - AF
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// b0 - BF
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// c0 - CF
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// d0 - DF
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,	// e0 - EF
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};	// f0 - FF


	
const char strFrameType[256][18] = {
	"DataNAK", //  Range 0x00 to 0x1F includes 5 bits for quality 1 Car, 200Hz,4FSK
	"","","","","","","","","","","","","","","","",
	"","","","","","","","","","","","","","","","",
	"","",

        //Short Control Frames 1 Car, 200Hz,4FSK  ' Reassigned May 22, 2015 for maximum "distance"
 
	"BREAK", "IDLE", "",
	"", "", "",
	"DISC", "", "",
	"END",
	"ConRejBusy",
	"ConRejBW",
	"",

	//Special frames 1 Car, 200Hz,4FSK 0x30 +

	"IDFrame",
	"ConReq200M",
	"ConReq500M",
	"ConReq1000M",
	"ConReq2000M",
	"ConReq200F",
	"ConReq500F",
	"ConReq1000F",
	"ConReq2000F",
	"ConAck200",
	"ConAck500",
	"ConAck1000",
	"ConAck2000",
	"PingAck",
	"Ping",
	"",
	// 200 Hz Bandwidth Data 
	// 1 Car PSK Data Modes 200 HzBW  100 baud 
	
	"4PSK.200.100.E",	// 0x40
	"4PSK.200.100.O",
	"4PSK.200.100S.E",
	"4PSK.200.100S.O",
	"8PSK.200.100.E",
	"8PSK.200.100.O",

		// 1 Car 16QAM Data mode 200 Hz BW, 100 baud

	"16QAM.200.100.E",	// 46
    "16QAM.200.100.O",	// 47

	// 1 Car 4FSK Data mode 200 HzBW, 50 baud 

	//"4FSK.200.50.E",
	//"4FSK.200.50.O",
	"4FSK.200.50S.E", // 48
	"4FSK.200.50S.O",
	"4FSK.500.100.E",
	"4FSK.500.100.O",
	"4FSK.500.100S.E",
	"4FSK.500.100S.O",
	"",//"8FSK.200.25.E",
	"",//"8FSK.200.25.O",

	//' 2 Car PSK Data Modes 100 baud
	"4PSK.500.100.E", // 50
	"4PSK.500.100.O",
	"8PSK.500.100.E",
	"8PSK.500.100.O",

	// 2 Car Data modes 16 QAM baud  

	"16QAM.500.100.E",	//54
	"16QAM.500.100.O",
	"", "",				// 56, 57 were 500 167 modes	

	// 1 Car 16FSK mode 25 baud

//	"16FSK.500.25.E", // 58
//	"16FSK.500.25.O",
//	"16FSK.500.25S.E",
//	"16FSK.500.25S.O",

	"","","","",		// 58 -5B
	"","","","",		// 5C-5F

	//1 Khz Bandwidth Data Modes 
	//  4 Car 100 baud PSK
	"4PSK.1000.100.E", //60
	"4PSK.1000.100.O",
	"8PSK.1000.100.E",
	"8PSK.1000.100.O",
	// 4 car 167 baud PSK
	"16QAM.1000.100.E",
	"16QAM.1000.100.O",
	"",
	"",
	// 2 Car 4FSK 100 baud
	"", //68
	"","","","","","","",

	// 2Khz Bandwidth Data Modes 
	//  8 Car 100 baud PSK
	"4PSK.2000.100.E", //70 
	"4PSK.2000.100.O",
	"8PSK.2000.100.E",
	"8PSK.2000.100.O",
	//  8 Car 167 baud PSK
	"16QAM.2000.100.E",
	"16QAM.2000.100.O",	// 75
	"","","","",		// 79=79
	// 1 Car 4FSK 600 baud (FM only)
	"4FSK.2000.600.E", // Experimental //7A
	"4FSK.2000.600.O", // Experimental
	"4FSK.2000.600S.E", // Experimental
	"4FSK.2000.600S.O", // Experimental //7D
	"","",	// 7e-7f
	"","","","","","","","","","","","","","","","",
	"","","","","","","","","","","","","","","","",
	"","","","","","","","","","","","","","","","",
	"","","","","","","","","","","","","","","","",
	"PktHddr","PktData","","","","","","","","","","","","","","", //C0

	//Frame Types 0xA0 to 0xDF reserved for experimentation 
	"SOUND2K" //D0
	"","","","","","","","","","","","","","","","",
    //Data ACK  1 Car, 200Hz,4FSK
	"DataACK"		// Range 0xE0 to 0xFF includes 5 bits for quality 
};

const char shortFrameType[256][12] = {
	"DataNAK", //  Range 0x00 to 0x1F includes 5 bits for quality 1 Car, 200Hz,4F
	"","","","","","","","","","","","","","","","",
	"","","","","","","","","","","","","","","","",
	"","",

        //Short Control Frames 1 Car, 200Hz,4F  ' Reassigned May 22, 2015 for maximum "distance"
 
	"BREAK", "IDLE", "",
	"", "", "",
	"DISC", "", "",
	"END",
	"ConRejBusy",
	"ConRejBW",
	"",

	//Special frames 1 Car, 200Hz,4F 0x30 +

	"IDFrame",
	"ConReq200M",
	"ConReq500M",
	"ConReq1KM",
	"ConReq2KM",
	"ConReq200F",
	"ConReq500F",
	"ConReq1KF",
	"ConReq2KF",
	"ConAck200",
	"ConAck500",
	"ConAck1K",
	"ConAck2K",
	// Types 0x3D to 0x3F reserved
	"PingAck","Ping","",
	// 200 Hz Bandwidth Data 
	// 1 Car P Data Modes 200 HzBW  100 baud 
	
	"4P.200.100",	// 0x40
	"4P.200.100",
	"4P.200.100S",
	"4P.200.100S",
	"8P.200.100",
	"8P.200.100",

		// 1 Car 16QAM Data mode 200 Hz BW, 100 baud

	"16Q.200.100",	// 46
    "16Q.200.100",	// 47


	"4F.200.50S", // 48
	"4F.200.50S",
	"4F.500.100",
	"4F.500.100",
	"4F.500.100S",
	"4F.500.100S",
	"",
	"",

	//' 2 Car P Data Modes 100 baud
	"4P.500.100", // 50
	"4P.500.100",
	"8P.500.100",
	"8P.500.100",

	// 2 Car Data modes 16 Q baud  

	"16Q.500.100",	//54
	"16Q.500.100",
	"", "",				// 56, 57 were 500 167 modes	

	"", // 58
	"",
	"",
	"",
	
	"",	// 5C
    "",	// 5D
	"","",				// 5E/F

	//1 Khz Bandwidth Data Modes 
	//  4 Car 100 baud P
	"4P.1K.100", //60
	"4P.1K.100",
	"8P.1K.100",
	"8P.1K.100",
	// 4 car 167 baud P
	"16Q.1K.100",
	"16Q.1K.100",
	"8",
	"8",
	// 2 Car 4F 100 baud
	"", //68
	"","","","","","","",

	// 2Khz Bandwidth Data Modes 
	//  8 Car 100 baud P
	"4P.2K.100", //70 
	"4P.2K.100",
	"8P.2K.100",
	"8P.2K.100",
	//  8 Car 167 baud P
	"16Q.2K.100",
	"16Q.2K.100",
	"",
	"",
	// 4 Car 4F 100 baud
	"",
	"",
	// 1 Car 4F 600 baud (FM only)
	"4F.2K.600", // Experimental 
	"4F.2K.600", // Experimental
	"4F.2K.600S", // Experimental
	"4F.2K.600S", // Experimental //7d
	"","",	// 7e-7f
	"","","","","","","","","","","","","","","","",
	"","","","","","","","","","","","","","","","",
	"","","","","","","","","","","","","","","","",
	"","","","","","","","","","","","","","","","",
	"PktHDDR","PktData","","","","","","","","","","","","","","", //C0

	//Frame Types 0xA0 to 0xDF reserved for experimentation 
	"SOUND2K" //D0
	"","","","","","","","","","","","","","","","",
    //Data ACK  1 Car, 200Hz,4F
	"DataACK"		// Range 0xE0 to 0xFF includes 5 bits for quality 
};


char * strlop(char * buf, char delim)
{
	// Terminate buf at delim, and return rest of string

	char * ptr = strchr(buf, delim);

	if (ptr == NULL) return NULL;

	*(ptr)++=0;

	return ptr;
}

void GetSemaphore()
{
}

void FreeSemaphore()
{
}

BOOL CheckValidCallsignSyntax(char * strCallsign)
{
	// Function for checking valid call sign syntax

	char * Dash = strchr(strCallsign, '-');
	int callLen = strlen(strCallsign);
	char * ptr = strCallsign;
	int SSID;

	if (Dash)
	{
		callLen = Dash - strCallsign;

		SSID = atoi(Dash + 1);
		if (SSID > 15)
			return FALSE;

		if (strlen(Dash + 1) > 2)
			return FALSE;

		if (!isalnum(*(Dash + 1)))
			return FALSE;
	}
		
	if (callLen > 7 || callLen < 3)
			return FALSE;

	while (callLen--)
	{
		if (!isalnum(*(ptr++)))
			return FALSE;
	}
	return TRUE;
}

//	 Function to check for proper syntax of a 4, 6 or 8 character GS

BOOL CheckGSSyntax(char * GS)
{
	int Len = strlen(GS);

	if (!(Len == 4 || Len == 6 || Len == 8))
		return FALSE;

	if (!isalpha(GS[0]) || !isalpha(GS[1]))
		return FALSE;
	
	if (!isdigit(GS[2]) || !isdigit(GS[3]))
		return FALSE;

	if (Len == 4)
		return TRUE;

	if (!isalpha(GS[4]) || !isalpha(GS[5]))
		return FALSE;

	if (Len == 6)
		return TRUE;

	if (!isdigit(GS[6]) || !isdigit(GS[7]))
		return FALSE;

	return TRUE;
}

// Function polled by Main polling loop to see if time to play next wave stream

BOOL GetNextFrame()
{
	// returning TRUE sets frame pending in Main

	if (ProtocolMode == FEC || ProtocolState == FECSend)
	{
		if (ProtocolState == FECSend || ProtocolState == FECRcv || ProtocolState == DISC)
			return GetNextFECFrame();
		else
			return FALSE;
	}
	if (ProtocolMode == ARQ)
//		if (ARQState == None)
//			return FALSE;
//		else
            return GetNextARQFrame();

	return FALSE;
}
     
#ifdef WIN32

extern LARGE_INTEGER Frequency;
extern LARGE_INTEGER StartTicks;
extern LARGE_INTEGER NewTicks;

#endif

extern int NErrors;

void testRS()
{
	// feed random data into RS to check robustness

	BOOL blnRSOK, FrameOK;
	char bytRawData[256];
	int DataLen = 128;
	int intRSLen = 64;
	int i;

	for (i = 0; i < DataLen; i++)
	{
		bytRawData[i] = rand() % 256;
	}

	FrameOK = RSDecode(bytRawData, DataLen, intRSLen, &blnRSOK);
}

void setProtocolMode(char* strMode)
{
	if (strcmp(strMode, "ARQ") == 0)
	{
		WriteDebugLog(LOGINFO, "Setting ProtocolMode to ARQ.");
		ProtocolMode = ARQ;
	}
	else
	if (strcmp(strMode, "RXO") == 0)
	{
		WriteDebugLog(LOGINFO, "Setting ProtocolMode to RXO.");
		ProtocolMode = RXO;
	}
	else
	if (strcmp(strMode, "FEC") == 0)
	{
		WriteDebugLog(LOGINFO, "Setting ProtocolMode to FEC.");
		ProtocolMode = FEC;
	}
	else
	{
		WriteDebugLog(LOGWARNING, "WARNING: Invalid argument to setProtocolMode.  %s given, but expected one of ARQ, RXO, or FEC.  Setting ProtocolMode to ARQ as a default.", strMode);
		ProtocolMode = ARQ;
	}
}

void ardopmain()
{
	blnTimeoutTriggered = FALSE;
	SetARDOPProtocolState(DISC);

	InitSound();

	if (SerialMode)
		SerialHostInit();
	else
		TCPHostInit();

	if (UseKISS)
	{
		KISSInit();
#ifdef USE_SOUNDMODEM
		afskInit();
#endif
	}
//	while (1)
//		testRS();

	tmrPollOBQueue = Now + 10000;

	if (InitRXO)
		setProtocolMode("RXO");
	else
		setProtocolMode("ARQ");

	while(!blnClosing)
	{
		PollReceivedSamples();
		if (ProtocolMode != RXO)
		{
			CheckTimers();
			if (SerialMode)
				SerialHostPoll();
			else
				TCPHostPoll();
			MainPoll();
		}
		PlatformSleep(10);
	}

	if (!SerialMode)
	{
		closesocket(TCPControlSock);
		closesocket(TCPDataSock);
		closesocket(PktSock);
	}
	return;
}


void SendCWID(char * Callsign, BOOL x)
{
}

// Subroutine to generate 1 symbol of leader

//	 returns pointer to Frame Type Name

const char * Name(UCHAR bytID)
{
	if (bytID < 0x20)
		return strFrameType[0];
	else if (bytID >= 0xE0)
		return strFrameType[0xE0];
	else
		return strFrameType[bytID];
}

//	 returns pointer to Frame Type Name

const char * shortName(UCHAR bytID)
{
	if (bytID < 0x20)
		return shortFrameType[0];
	else if (bytID >= 0xE0)
		return shortFrameType[0xE0];
	else
		return shortFrameType[bytID];
}
// Function to look up frame info from bytFrameType

BOOL FrameInfo(UCHAR bytFrameType, int * blnOdd, int * intNumCar, char * strMod,
			   int * intBaud, int * intDataLen, int * intRSLen, UCHAR * bytQualThres, char * strType)
{
	//Used to "lookup" all parameters by frame Type. 
	// returns TRUE if all fields updated otherwise FALSE (improper bytFrameType)

	// 1 Carrier 4FSK control frames 
   
	if ((bytFrameType >= 0 &&  bytFrameType <= 0x1F) || bytFrameType >= 0xE0)
	{
		*blnOdd = (1 & bytFrameType) != 0;
		*intNumCar = 1;
		*intDataLen = 0;
		*intRSLen = 0;
		strcpy(strMod, "4FSK");
		*intBaud = 50;
		*bytQualThres = 40;
	}
	else
	{

	switch(bytFrameType)
	{
	case 0x23:
	case 0x24:
	case 0x29:

		*blnOdd = (1 & bytFrameType) != 0;
		*intNumCar = 1;
		*intDataLen = 0;
		*intRSLen = 0;
		strcpy(strMod, "4FSK");
		*intBaud = 50;
		*bytQualThres = 60;
		break;
	
	case 0x2C:
	case 0x2D:
	case 0x2E:
		
		*blnOdd = (1 & bytFrameType) != 0;
		*intNumCar = 1;
		*intDataLen = 0;
		*intRSLen = 0;
		strcpy(strMod, "4FSK");
		*intBaud = 50;
		*bytQualThres = 60;
		break;

	case 0x30:
	case 0x31:
	case 0x32:
	case 0x33:
	case 0x34:
	case 0x35:
	case 0x36:
	case 0x37:
	case 0x38:
	case 0x3E:		// Ping

		*blnOdd = (1 & bytFrameType) != 0;
		*intNumCar = 1;
		*intDataLen = 12;
		*intRSLen = 4;			// changed 0.8.0
		strcpy(strMod, "4FSK");
		*intBaud = 50;
		*bytQualThres = 50;
		break;

	case 0x39:
	case 0x3A:
	case 0x3B:
	case 0x3C:
	case 0x3D:

		*blnOdd = (1 & bytFrameType) != 0;
		*intNumCar = 1;
		*intDataLen = 3;
		*intRSLen = 0;
		strcpy(strMod, "4FSK");
		*intBaud = 50;
		*bytQualThres = 50;
		break;

	case 0xe0:

		*blnOdd = (1 & bytFrameType) != 0;
		*intNumCar = 1;
		*intDataLen = 0;
		*intRSLen = 0;
		strcpy(strMod, "4FSK");
		*intBaud = 50;
		*bytQualThres = 60;
		break;

	// 1 Carrier Data modes
    //  100 baud PSK (200 baud not compatible with 200 Hz bandwidth)  (Note 1 carrier modes Qual Threshold reduced to 30 (was 50) for testing April 20, 2015

	case 0x40:
	case 0x41:

		*blnOdd = (1 & bytFrameType) != 0;
		*intNumCar = 1;
		*intDataLen = 64;
		*intRSLen = 32;
		strcpy(strMod, "4PSK");
		*intBaud = 100;
		*bytQualThres = 30;	
		break;
	
	case 0x42:
	case 0x43:

		*blnOdd = (1 & bytFrameType) != 0;
		*intNumCar = 1;
		*intDataLen = 16;
		*intRSLen = 8;
		strcpy(strMod, "4PSK");
		*intBaud = 100;
		*bytQualThres = 30;
		break;
	
	case 0x44:
	case 0x45:

		*blnOdd = (1 & bytFrameType) != 0;
		*intNumCar = 1;
		*intDataLen = 108;
		*intRSLen = 36;
		strcpy(strMod, "8PSK");
		*intBaud = 100;
		*bytQualThres = 30;
		break;

	case 0x46:
 	case 0x47:

		// 100 baud 16QAM 
  
		*blnOdd = (1 & bytFrameType) != 0;
		*intNumCar = 1;
		*intDataLen = 128;
		*intRSLen = 64;
		strcpy(strMod, "16QAM");
		*intBaud = 100;
		*bytQualThres = 30;
 		break;



	case 0x48:
	case 0x49:

		*blnOdd = (1 & bytFrameType) != 0;
		*intNumCar = 1;
		*intDataLen = 16;
		*intRSLen = 4;
		strcpy(strMod, "4FSK");
		*intBaud = 50;
		*bytQualThres = 30;
		break;
	
	case 0x4A:
	case 0x4B:

		*blnOdd = (1 & bytFrameType) != 0;
		*intNumCar = 1;
		*intDataLen = 64;
		*intRSLen = 16;
		strcpy(strMod, "4FSK");
		*intBaud = 100;
		*bytQualThres = 30;
		break;
	
	case 0x4C:
	case 0x4D:

		*blnOdd = (1 & bytFrameType) != 0;
		*intNumCar = 1;
		*intDataLen = 32;
		*intRSLen = 8;
		strcpy(strMod, "4FSK");
		*intBaud = 100;
		*bytQualThres = 30;
		break;
			
 
	// 2 Carrier Data Modes
	// 100 baud 

	case 0x50:
	case 0x51:

		*blnOdd = (1 & bytFrameType) != 0;
		*intNumCar = 2;
		*intDataLen = 64;
		*intRSLen = 32;
		strcpy(strMod, "4PSK");
		*intBaud = 100;
		*bytQualThres = 50;
 		break;

	case 0x52:
	case 0x53:

		*blnOdd = bytFrameType & 1;
		*intNumCar = 2;
		*intDataLen = 108;
		*intRSLen = 36;
		strcpy(strMod, "8PSK");
		*intBaud = 100;
		*bytQualThres = 50;
		break;
						
	// 16 QAM 2 Carrier 
    
	case 0x54:
	case 0x55:

		// 100 baud 16QAM
  
		*blnOdd = bytFrameType & 1;
		*intNumCar = 2;
		*intDataLen = 128;
		*intRSLen = 64;
		strcpy(strMod, "16QAM");
		*intBaud = 100;
		*bytQualThres = 50;
		break;
		
	//' 4 Carrier Data Modes
	//	100 baud
     	
	case 0x60:
	case 0x61:

		*blnOdd = bytFrameType & 1;
		*intNumCar = 4;
		*intDataLen = 64;
		*intRSLen = 32;
		strcpy(strMod, "4PSK");
		*intBaud = 100;
		*bytQualThres = 50;
		break;
	
	case 0x62:
	case 0x63:

		*blnOdd = bytFrameType & 1;
		*intNumCar = 4;
		*intDataLen = 108;
		*intRSLen = 36;
		strcpy(strMod, "8PSK");
		*intBaud = 100;
		*bytQualThres = 50;
		break;

	//	16QAM Baud

	case 0x64:
	case 0x65:

		*blnOdd = bytFrameType & 1;
		*intNumCar = 4;
		*intDataLen = 128;
		*intRSLen = 64;
		strcpy(strMod, "16QAM");
		*intBaud = 100;
		*bytQualThres = 50;
		break;


	// 8 Carrier Data modes
	//	100 baud

	case 0x70:
	case 0x71:

		*blnOdd = bytFrameType & 1;
		*intNumCar = 8;
		*intDataLen = 64;
		*intRSLen = 32;
		strcpy(strMod, "4PSK");
		*intBaud = 100;
		*bytQualThres = 50;
		break;

	case 0x72:
	case 0x73:

		*blnOdd = bytFrameType & 1;
		*intNumCar = 8;
		*intDataLen = 108;
		*intRSLen = 36;
		strcpy(strMod, "8PSK");
		*intBaud = 100;
		*bytQualThres = 50;
		break;

	// 16QAM 

	case 0x74:
	case 0x75:

		*blnOdd = bytFrameType & 1;
		*intNumCar = 8;
		*intDataLen = 128;
		*intRSLen = 64;
		strcpy(strMod, "16QAM");
		*intBaud = 100;
		*bytQualThres = 50;
		break;
		

	// 600 baud 4FSK 2000 Hz bandwidth 

	case 0x7a:
	case 0x7b:

		*blnOdd = (1 & bytFrameType) != 0;
		*intNumCar = 1;
		*intDataLen = 600;
		*intRSLen = 150;
		strcpy(strMod, "4FSK");
		*intBaud = 600;
		*bytQualThres = 30;
		break;
 
	case 0x7C:
	case 0x7D:

		*blnOdd = (1 & bytFrameType) != 0;
		*intNumCar = 1;
		*intDataLen = 200;
		*intRSLen = 50;
		strcpy(strMod, "4FSK");
		*intBaud = 600;
		*bytQualThres = 30;
		break;

	case PktFrameHeader:

		// Special Variable Length frame

		// This defines the header, 4PSK.500.100. Length is 6 bytes
		// Once we have that we receive the rest of the packet in the 
		// mode defined in the header.
		// Header is 4 bits Type 12 Bits Len 2 bytes CRC 2 bytes RS

		*blnOdd = 0;
		*intNumCar = 1;
		*intDataLen = 2;
		*intRSLen = 2;
		strcpy(strMod, "4FSK");
		*intBaud = 100;
		*bytQualThres = 50;
 		break;

	case PktFrameData:

		// Special Variable Length frame

		// This isn't ever transmitted but is used to define the
		// current setting for the data frame. Mode and Length 
		// are variable
		

		*blnOdd = 1;
		*intNumCar = pktCarriers[pktMode];
		*intDataLen = pktDataLen;
		*intRSLen = pktRSLen;
		strcpy(strMod, &pktMod[pktMode][0]);
		strlop(strMod, '/');
		*intBaud = 100;
		*bytQualThres = 50;
 		break;

	default:
		WriteDebugLog(LOGCRIT, "No data for frame type = 0x" ,bytFrameType);
        return FALSE;
	}
	}
	
	if (bytFrameType >= 0 && bytFrameType <= 0x1F)
		strcpy(strType,strFrameType[0]);
	else
		if (bytFrameType >= 0xE0) 
			strcpy(strType,strFrameType[0xE0]);
        else
			strcpy(strType,strFrameType[bytFrameType]);

	return TRUE;
}

int NPAR = -1;	// Number of Parity Bytes - used in RS Code

int MaxErrors = 0;

int RSEncode(UCHAR * bytToRS, UCHAR * RSBytes, int DataLen, int RSLen)
{
	// This just returns the Parity Bytes. I don't see the point
	// in copying the message about

	unsigned char Padded[256];		// The padded Data

	int Length = DataLen + RSLen;	// Final Length of packet
	int PadLength = 255 - Length;	// Padding bytes needed for shortened RS codes

	//	subroutine to do the RS encode. For full length and shortend RS codes up to 8 bit symbols (mm = 8)

	if (NPAR != RSLen)		// Changed RS Len, so recalc constants;
	{
		NPAR = RSLen;
		MaxErrors = NPAR / 2;
		initialize_ecc();
	}

	// Copy the supplied data to end of data array.

	memset(Padded, 0, PadLength);
	memcpy(&Padded[PadLength], bytToRS, DataLen); 

	encode_data(Padded, 255-RSLen, RSBytes);

	return RSLen;
}

//	Main RS decode function

extern int index_of[];
extern int recd[];
int Corrected[256];
extern int tt;		//  number of errors that can be corrected 
extern int kk;		// Info Symbols

BOOL blnErrorsCorrected;

#define NEWRS

BOOL RSDecode(UCHAR * bytRcv, int Length, int CheckLen, BOOL * blnRSOK)
{	
#ifdef NEWRS

	// Using a modified version of Henry Minsky's code
	
	//Copyright Henry Minsky (hqm@alum.mit.edu) 1991-2009

	// Rick's Implementation processes the byte array in reverse. and also 
	//	has the check bytes in the opposite order. I've modified the encoder
	//	to allow for this, but so far haven't found a way to mske the decoder
	//	work, so I have to reverse the data and checksum to decode G8BPQ Nov 2015

	//	returns TRUE if was ok or correction succeeded, FALSE if correction impossible

	UCHAR intTemp[256];				// WOrk Area to pass to Decoder		
	int i;
	UCHAR * ptr2 = intTemp;
	UCHAR * ptr1 = &bytRcv[Length - CheckLen -1]; // Last Byte of Data

	int DataLen = Length - CheckLen;
	int PadLength = 255 - Length;		// Padding bytes needed for shortened RS codes

	*blnRSOK = FALSE;

	if (Length > 255 || Length < (1 + CheckLen))		//Too long or too short 
		return FALSE;

	if (NPAR != CheckLen)		// Changed RS Len, so recalc constants;
	{
		NPAR = CheckLen;
		MaxErrors = NPAR /2;

		initialize_ecc();
	}


	//	We reverse the data while zero padding it to speed things up

	//	We Need (Data Reversed) (Zero Padding) (Checkbytes Reversed)

	// Reverse Data

	for (i = 0; i < DataLen; i++)
	{
	  *(ptr2++) = *(ptr1--);
	}

	//	Clear padding

	memset(ptr2, 0, PadLength);	

	ptr2+= PadLength;
	
	// Error Bits

	ptr1 = &bytRcv[Length - 1];			// End of check bytes

	for (i = 0; i < CheckLen; i++)
	{
	  *(ptr2++) = *(ptr1--);
	}
	
	decode_data(intTemp, 255);

	// check if syndrome is all zeros 

	if (check_syndrome() == 0)
	{
		// RS ok, so no need to correct

		*blnRSOK = TRUE;
		return TRUE;		// No Need to Correct
	}

    if (correct_errors_erasures (intTemp, 255, 0, 0) == 0) // Dont support erasures at the momnet

		// Uncorrectable

		return FALSE;

	// Data has been corrected, so need to reverse again

	ptr1 = &intTemp[DataLen - 1];
	ptr2 = bytRcv; // Last Byte of Data

	for (i = 0; i < DataLen; i++)
	{
	  *(ptr2++) = *(ptr1--);
	}

	// ?? Do we need to return the check bytes ??

	// Yes, so we can redo RS Check on supposedly connected frame

	ptr1 = &intTemp[254];	// End of Check Bytes

 	for (i = 0; i < CheckLen; i++)
	{
	  *(ptr2++) = *(ptr1--);
	}

	return TRUE;
}

#else

	// Old (Rick's) code

	// Sets blnRSOK if OK without correction

	// Returns TRUE if OK oe Corrected
	// False if Can't correct


	UCHAR intTemp[256];				// Work Area to pass to Decoder		
	int i;
	int intStartIndex;
	UCHAR * ptr2 = intTemp;
	UCHAR * ptr1 = bytRcv;
	BOOL RSWasOK;

	int DataLen = Length - CheckLen;
	int PadLength = 255 - Length;		// Padding bytes needed for shortened RS codes

	*blnRSOK = FALSE;

	if (Length > 255 || Length < (1 + CheckLen))		//Too long or too short 
		return FALSE;


	if (NPAR != CheckLen)		// Changed RS Len, so recalc constants;
	{
		NPAR = CheckLen;
		tt = sqrt(NPAR);
		kk = 255-CheckLen; 
		generate_gf();
		gen_poly();
	}
	
	intStartIndex =  255 - Length; // set the start point for shortened RS codes

	//	We always work on a 255 byte buffer, prepending zeros if neccessary

 	//	Clear padding

	memset(ptr2, 0, PadLength);	
	ptr2 += PadLength;

	memcpy(ptr2, ptr1, Length);
	
	// convert to indexed form

	for(i = 0; i < 256; i++)
	{
//		intIsave = i;
//		intIndexSave = index_of[intTemp[i]];
		recd[i] = index_of[intTemp[i]];
	}

//	printtick("entering decode_rs");

	blnErrorsCorrected = FALSE;

	RSWasOK = decode_rs();

//	printtick("decode_rs Done");

	*blnRSOK = RSWasOK;

	if (RSWasOK)
		return TRUE;

	if(blnErrorsCorrected)
	{
		for (i = 0; i < DataLen; i++)
		{
			bytRcv[i] = recd[i + intStartIndex];
		}
		return TRUE;
	}

	return FALSE;
}
#endif

// Function to encode data for all PSK frame types

int EncodePSKData(UCHAR bytFrameType, UCHAR * bytDataToSend, int Length, unsigned char * bytEncodedBytes)
{
	// Objective is to use this to use this to send all PSK data frames 
	// 'Output is a byte array which includes:
	//  1) A 2 byte Header which include the Frame ID.  This will be sent using 4FSK at 50 baud. It will include the Frame ID and ID Xored by the Session bytID.
	//  2) n sections one for each carrier that will inlcude all data (with FEC appended) for the entire frame. Each block will be identical in length.
	//  Ininitial implementation:
	//    intNum Car may be 1, 2, 4 or 8
	//    intBaud may be 100, 167
	//    intPSKMode may be 4 (4PSK) or 8 (8PSK) 
	//    bytDataToSend must be equal to or less than max data supported by the frame or a exception will be logged and an empty array returned

	//  First determine if bytDataToSend is compatible with the requested modulation mode.

	int intNumCar, intBaud, intDataLen, intRSLen, bytDataToSendLengthPtr, intEncodedDataPtr;

	int intCarDataCnt, intStartIndex;
	BOOL blnOdd;
	char strType[18];
	char strMod[16];
	BOOL blnFrameTypeOK;
	UCHAR bytQualThresh;
	int i;
	UCHAR * bytToRS = &bytEncodedBytes[2]; 

	blnFrameTypeOK = FrameInfo(bytFrameType, &blnOdd, &intNumCar, strMod, &intBaud, &intDataLen, &intRSLen, &bytQualThresh, strType);

	if (intDataLen == 0 || Length == 0 || !blnFrameTypeOK)
	{
		//Logs.Exception("[EncodeFSKFrameType] Failure to update parameters for frame type H" & Format(bytFrameType, "X") & "  DataToSend Len=" & bytDataToSend.Length.ToString)
		return 0;
	}
		
	//	Generate the 2 bytes for the frame type data:
	
	bytEncodedBytes[0] = bytFrameType;
	bytEncodedBytes[1] = bytFrameType ^ bytSessionID;

	bytDataToSendLengthPtr = 0;
	intEncodedDataPtr = 2;

	// Now compute the RS frame for each carrier in sequence and move it to bytEncodedBytes 
		
	for (i = 0; i < intNumCar; i++)		//  across all carriers
	{
		intCarDataCnt = Length - bytDataToSendLengthPtr;
			
		if (intCarDataCnt > intDataLen) // why not > ??
		{
			// Won't all fit 

			bytToRS[0] = intDataLen;
			intStartIndex = intEncodedDataPtr;
			memcpy(&bytToRS[1], &bytDataToSend[bytDataToSendLengthPtr], intDataLen);
			bytDataToSendLengthPtr += intDataLen;
		}
		else
		{
			// Last bit

			bytToRS[0] = intCarDataCnt;  // Could be 0 if insuffient data for # of carriers 

			if (intCarDataCnt > 0)
			{
				memcpy(&bytToRS[1], &bytDataToSend[bytDataToSendLengthPtr], intCarDataCnt);
				bytDataToSendLengthPtr += intCarDataCnt;
			}	
		}
		
		GenCRC16FrameType(bytToRS, intDataLen + 1, bytFrameType); // calculate the CRC on the byte count + data bytes

		RSEncode(bytToRS, bytToRS+intDataLen+3, intDataLen + 3, intRSLen);  // Generate the RS encoding ...now 14 bytes total
     
 		//  Need: (2 bytes for Frame Type) +( Data + RS + 1 byte byteCount + 2 Byte CRC per carrier)

 		intEncodedDataPtr += intDataLen + 3 + intRSLen;

		bytToRS += intDataLen + 3 + intRSLen;		
	}
	return intEncodedDataPtr;
}


// Function to encode data for all FSK frame types
  
int EncodeFSKData(UCHAR bytFrameType, UCHAR * bytDataToSend, int Length, unsigned char * bytEncodedBytes)
{
	// Objective is to use this to use this to send all 4FSK data frames 
	// 'Output is a byte array which includes:
	//  1) A 2 byte Header which include the Frame ID.  This will be sent using 4FSK at 50 baud. It will include the Frame ID and ID Xored by the Session bytID.
	//  2) n sections one for each carrier that will inlcude all data (with FEC appended) for the entire frame. Each block will be identical in length.
	//  Ininitial implementation:
	//    intNum Car may be 1, 2, 4 or 8
	//    intBaud may be 50, 100
	//    strMod is 4FSK) 
	//    bytDataToSend must be equal to or less than max data supported by the frame or a exception will be logged and an empty array returned

	//  First determine if bytDataToSend is compatible with the requested modulation mode.

	int intNumCar, intBaud, intDataLen, intRSLen, bytDataToSendLengthPtr, intEncodedDataPtr;

	int intCarDataCnt, intStartIndex;
	BOOL blnOdd;
	char strType[18];
	char strMod[16];
	BOOL blnFrameTypeOK;
	UCHAR bytQualThresh;
	int i;
	UCHAR * bytToRS = &bytEncodedBytes[2]; 

	blnFrameTypeOK = FrameInfo(bytFrameType, &blnOdd, &intNumCar, strMod, &intBaud, &intDataLen, &intRSLen, &bytQualThresh, strType);

	if (intDataLen == 0 || Length == 0 || !blnFrameTypeOK)
	{
		//Logs.Exception("[EncodeFSKFrameType] Failure to update parameters for frame type H" & Format(bytFrameType, "X") & "  DataToSend Len=" & bytDataToSend.Length.ToString)
		return 0;
	}
	
	//	Generate the 2 bytes for the frame type data:
	
	bytEncodedBytes[0] = bytFrameType;
	bytEncodedBytes[1] = bytFrameType ^ bytSessionID;

     //   Dim bytToRS(intDataLen + 3 - 1) As Byte ' Data + Count + 2 byte CRC

	bytDataToSendLengthPtr = 0;
	intEncodedDataPtr = 2;

	if (intBaud < 600 || intDataLen < 600)
	{
		// Now compute the RS frame for each carrier in sequence and move it to bytEncodedBytes 
		
		for (i = 0; i < intNumCar; i++)		//  across all carriers
		{
			intCarDataCnt = Length - bytDataToSendLengthPtr;
			
			if (intCarDataCnt >= intDataLen) // why not > ??
			{
				// Won't all fit 

				bytToRS[0] = intDataLen;
				intStartIndex = intEncodedDataPtr;
				memcpy(&bytToRS[1], &bytDataToSend[bytDataToSendLengthPtr], intDataLen);
				bytDataToSendLengthPtr += intDataLen;
			}
			else
			{
				// Last bit

				bytToRS[0] = intCarDataCnt;  // Could be 0 if insuffient data for # of carriers 

				if (intCarDataCnt > 0)
				{
					memcpy(&bytToRS[1], &bytDataToSend[bytDataToSendLengthPtr], intCarDataCnt);
                    bytDataToSendLengthPtr += intCarDataCnt;
				}	
			}
		
			GenCRC16FrameType(bytToRS, intDataLen + 1, bytFrameType); // calculate the CRC on the byte count + data bytes

			RSEncode(bytToRS, bytToRS+intDataLen+3, intDataLen + 3, intRSLen);  // Generate the RS encoding ...now 14 bytes total

 			//  Need: (2 bytes for Frame Type) +( Data + RS + 1 byte byteCount + 2 Byte CRC per carrier)

 			intEncodedDataPtr += intDataLen + 3 + intRSLen;

			bytToRS += intDataLen + 3 + intRSLen;
		}
		return intEncodedDataPtr;
	}

	// special case for 600 baud 4FSK which has 600 byte data field sent as three sequencial (200 byte + 50 byte RS) groups

	for (i = 0; i < 3; i++)		 // for three blocks of RS data
	{
		intCarDataCnt = Length - bytDataToSendLengthPtr;
			
		if (intCarDataCnt >= intDataLen /3 ) // why not > ??
		{
			// Won't all fit 

			bytToRS[0] = intDataLen / 3;
			intStartIndex = intEncodedDataPtr;
			memcpy(&bytToRS[1], &bytDataToSend[bytDataToSendLengthPtr], intDataLen / 3);
			bytDataToSendLengthPtr += intDataLen /3;
		}
		else
		{
			// Last bit

			bytToRS[0] = intCarDataCnt;  // Could be 0 if insuffient data for # of carriers 

			if (intCarDataCnt > 0)
			{
				memcpy(&bytToRS[1], &bytDataToSend[bytDataToSendLengthPtr], intCarDataCnt);
                bytDataToSendLengthPtr += intCarDataCnt;
			}	
		}
		GenCRC16FrameType(bytToRS, intDataLen / 3 + 1, bytFrameType); // calculate the CRC on the byte count + data bytes

 		RSEncode(bytToRS, bytToRS + intDataLen / 3 + 3, intDataLen / 3 + 3, intRSLen / 3);  // Generate the RS encoding ...now 14 bytes total
		intEncodedDataPtr += intDataLen / 3  + 3 + intRSLen / 3;
		bytToRS += intDataLen / 3  + 3 + intRSLen / 3;
	}		
	return intEncodedDataPtr;
}
	
//  Function to encode ConnectRequest frame 

BOOL EncodeARQConRequest(char * strMyCallsign, char * strTargetCallsign, enum _ARQBandwidth ARQBandwidth, UCHAR * bytReturn)
{
	//  Encodes a 4FSK 200 Hz BW Connect Request frame ( ~ 1950 ms with default leader/trailer)

	UCHAR * bytToRS= &bytReturn[2];

	if (strcmp(strTargetCallsign, "CQ") != 0)  // skip syntax checking for psuedo call "CQ"
	{
		if (!CheckValidCallsignSyntax(strMyCallsign))
		{
			//Logs.Exception("[EncodeModulate.EncodeARQConnectRequest] Illegal Call sign syntax. MyCallsign = " & strMyCallsign & ", TargetCallsign = " & strTargetCallsign)

			return 0;
		}
		if (!CheckValidCallsignSyntax(strTargetCallsign) || !CheckValidCallsignSyntax(strMyCallsign))
		{            
			//Logs.Exception("[EncodeModulate.EncodeARQConnectRequest] Illegal Call sign syntax. MyCallsign = " & strMyCallsign & ", TargetCallsign = " & strTargetCallsign)
			
			return 0;
		}
	}		
	if (ARQBandwidth == B200MAX)
		bytReturn[0] = 0x31;
	else if (ARQBandwidth == B500MAX)
		bytReturn[0] = 0x32;
	else if (ARQBandwidth == B1000MAX)
		bytReturn[0] = 0x33;
	else if (ARQBandwidth == B2000MAX)
		bytReturn[0] = 0x34;

	else if (ARQBandwidth == B200FORCED)
		bytReturn[0] = 0x35;
	else if (ARQBandwidth == B500FORCED)
		bytReturn[0] = 0x36;
	else if (ARQBandwidth == B1000FORCED)
		bytReturn[0] = 0x37;
	else if (ARQBandwidth == B2000FORCED)
		bytReturn[0] = 0x38;
	else
	{
		//Logs.Exception("[EncodeModulate.EncodeFSK500_1S] Bandwidth error.  Bandwidth = " & strBandwidth)
		return 0;
	}

	bytReturn[1] = bytReturn[0] ^ 0xFF;  // Connect Request always uses session ID of 0xFF

	// Modified May 24, 2015 to use RS instead of 2 byte CRC. (same as ID frame)

	CompressCallsign(strMyCallsign, &bytToRS[0]);
	CompressCallsign(strTargetCallsign, &bytToRS[6]);  //this uses compression to accept 4, 6, or 8 character Grid squares.

	RSEncode(bytToRS, &bytReturn[14], 12, 4);  // Generate the RS encoding ...now 14 bytes total
 
	return 18;
}

int EncodePing(char * strMyCallsign, char * strTargetCallsign, UCHAR * bytReturn)
{
	// Encodes a 4FSK 200 Hz BW Ping frame ( ~ 1950 ms with default leader/trailer) 

	UCHAR * bytToRS= &bytReturn[2];

//        If Not (CheckValidCallsignSyntax(strTargetCallsign) Or CheckValidCallsignSyntax(strMyCallsign)) Then
  //          Logs.Exception("[EncodeModulate.EncodePing] Illegal Call sign syntax. MyCallsign = " & strMyCallsign & ", TargetCallsign = " & strTargetCallsign)
   ////         Return Nothing
   //     End If

	bytReturn[0] = PING;
	bytReturn[1] = bytReturn[0] ^ 0xFF; // Ping always uses session ID of &HFF

	CompressCallsign(strMyCallsign, &bytToRS[0]);
	CompressCallsign(strTargetCallsign, &bytToRS[6]);  //this uses compression to accept 4, 6, or 8 character Grid squares.

	RSEncode(bytToRS, &bytReturn[14], 12, 4);  // Generate the RS encoding ...now 14 bytes total
	return 18;
}



 
int Encode4FSKIDFrame(char * Callsign, char * Square, unsigned char * bytreturn)
{
	// Function to encodes ID frame 
	// returns length of encoded message 

	UCHAR * bytToRS= &bytreturn[2];

	 if (!CheckValidCallsignSyntax(Callsign))
	 {
		//       Logs.Exception("[EncodeModulate.EncodeIDFrame] Illegal Callsign syntax or Gridsquare length. MyCallsign = " & strMyCallsign & ", Gridsquare = " & strGridSquare)
		
		 return 0;
	 }

	bytreturn[0] = 0x30;
	bytreturn[1] = 0x30 ^ 0xFF;

	// Modified May 9, 2015 to use RS instead of 2 byte CRC.
       
	CompressCallsign(Callsign, &bytToRS[0]);

    if (Square[0])
		CompressGridSquare(Square, &bytToRS[6]);  //this uses compression to accept 4, 6, or 8 character Grid squares.

	RSEncode(bytToRS, &bytreturn[14], 12, 4);  // Generate the RS encoding ...now 14 bytes total

	return 18;
}

//  Funtion to encodes a short 4FSK 50 baud Control frame  (2 bytes total) BREAK, END, DISC, IDLE, ConRejBusy, ConRegBW  

int Encode4FSKControl(UCHAR bytFrameType, UCHAR bytSessionID, UCHAR * bytreturn)
{
	// Encodes a short control frame (normal length ~320 ms with default 160 ms leader+trailer) 
    
	//If IsShortControlFrame(intFrameCode) Then
    //        Logs.Exception("[EncodeModulate.EncodeFSKControl] Illegal control frame code: H" & Format(intFrameCode, "X"))
    //        return Nothing
    //    End If

	bytreturn[0] = bytFrameType;
	bytreturn[1] = bytFrameType ^ bytSessionID;

	return 2;		// Length
}

//  Function to encode a CONACK frame with Timing data  (6 bytes total)  

int EncodeConACKwTiming(UCHAR bytFrameType, int intRcvdLeaderLenMs, UCHAR bytSessionID, UCHAR * bytreturn)
{
	// Encodes a Connect ACK with one byte Timing info. (Timing info repeated 2 times for redundancy) 

	//If intFrameCode < 0x39 Or intFrameCode > 0x3C Then
    //        Logs.Exception("[EncodeConACKwTiming] Illegal Frame code: " & Format(intFrameCode, "X"))
    //        return Nothing
    //    End If

	UCHAR bytTiming = min(255, intRcvdLeaderLenMs / 10);  // convert to 10s of ms.

	if (intRcvdLeaderLenMs > 2550 || intRcvdLeaderLenMs < 0)
	{
		// Logs.Exception("[EncodeConACKwTiming] Timing value out of range: " & intRcvdLeaderLenMs.ToString & " continue with forced value = 0")
        bytTiming = 0;
	}

	bytreturn[0] = bytFrameType;
	bytreturn[1] = bytFrameType ^ bytSessionID;

	bytreturn[2] = bytTiming;
	bytreturn[3] = bytTiming;
	bytreturn[4] = bytTiming;

	return 5;
}
//  Function to encode a PingAck frame with Quality Data  (5 bytes total)  

int EncodePingAck(int bytFrameType, int intSN, int intQuality, UCHAR * bytreturn)
{
	// Encodes a Ping ACK with one byte of S:N and Quality info ( info repeated 2 times for redundancy) 

	bytreturn[0] = bytFrameType;
	bytreturn[1] = bytFrameType ^ 0xff;

	if (intSN >= 21)
		bytreturn[2] = 0xf8;	// set to MAX level indicating >= 21dB
	else
		bytreturn[2] = ((intSN + 10) & 0x1F) << 3;		// Upper 5 bits are S:N 0-31 corresponding to -10 to 21 dB   (5 bits S:N, 3 bits Quality 

	bytreturn[2] += max(0, (intQuality - 30) / 10) & 7; // Quality is lower 3 bits value 0 to 7 representing 30-100
	bytreturn[3] = bytreturn[2];
	bytreturn[4] = bytreturn[2];
	
	return 5;
}


//	' Function to encode an ACK control frame  (2 bytes total) ...with 5 bit Quality code 

int EncodeDATAACK(int intQuality, UCHAR bytSessionID, UCHAR * bytreturn)
{
	// Encodes intQuality and DataACK frame (normal length ~320 ms with default leader/trailer)

	int intScaledQuality;

	if (intQuality > 100)
		intQuality = 100;

	intScaledQuality = max(0, (intQuality / 2) - 19); // scale quality value to fit 5 bit field of 0 represents Q <= of 38 (pretty poor)
	
	bytreturn[0] = 0xE0 + intScaledQuality;		//ACKs 0xE0 - 0xFF
	bytreturn[1] = bytreturn[0] ^ bytSessionID;

	return 2;
}

//  Function to encode a NAK frame  (2 bytes total) ...with 5 bit Quality code 

int EncodeDATANAK(int intQuality , UCHAR bytSessionID, UCHAR * bytreturn)
{
	// Encodes intQuality and DataACK frame (normal length ~320 ms with default leader/trailer)

	int intScaledQuality;

	intScaledQuality = max(0, (intQuality / 2) - 19); // scale quality value to fit 5 bit field of 0 represents Q <= of 38 (pretty poor)
	
	bytreturn[0] = intScaledQuality;		// NAKS 00 - 0x1F
	bytreturn[1] = bytreturn[0] ^ bytSessionID;

	return 2;
}

void SendID(BOOL blnEnableCWID)
{
	unsigned char bytIDSent[80];
	int Len;
	unsigned char * p;

	// Scheduler needs to ensure this isnt called if already playing

	if (SoundIsPlaying)
		return;

    if (GridSquare[0] == 0)
	{
		EncLen = Encode4FSKIDFrame(Callsign, "No GS", bytEncodedBytes);
		Len = sprintf(bytIDSent," %s:[No GS] ", Callsign);
	}
	else
	{
		EncLen = Encode4FSKIDFrame(Callsign, GridSquare, bytEncodedBytes);
		Len = sprintf(bytIDSent," %s:[%s] ", Callsign, GridSquare);
	}

	AddTagToDataAndSendToHost(bytIDSent, "IDF", Len);

	// On embedded platforms we don't have the memory to create full sound stream before playiong,
	// so this is structured differently from Rick's code


	p = bytEncodedBytes;

	Debugprintf("%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x ",
		p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15], p[16], p[17]);


	Mod4FSKDataAndPlay(0x30, &bytEncodedBytes[0], EncLen, 0);		// only returns when all sent

    if (blnEnableCWID)
		sendCWID(Callsign, FALSE);

}

// Function to generate a 5 second burst of two tone (1450 and 1550 Hz) used for setting up drive level
 
void Send5SecTwoTone()
{
	initFilter(200, 1500);
	GetTwoToneLeaderWithSync(250);
//	SampleSink(0);	// 5 secs
	SoundFlush();
}


void  ASCIIto6Bit(char * Padded, UCHAR * Compressed)
{
	// Input must be 8 bytes which will convert to 6 bytes of packed 6 bit characters and
	// inputs must be the ASCII character set values from 32 to 95....
    
	unsigned long long intSum = 0;

	int i;

	for (i=0; i<4; i++)
	{
		intSum = (64 * intSum) + Padded[i] - 32;
	}

	Compressed[0] = (UCHAR)(intSum >> 16) & 255;
	Compressed[1] = (UCHAR)(intSum >> 8) &  255;
	Compressed[2] = (UCHAR)intSum & 255;

	intSum = 0;

	for (i=4; i<8; i++)
	{
		intSum = (64 * intSum) + Padded[i] - 32;
	}

	Compressed[3] = (UCHAR)(intSum >> 16) & 255;
	Compressed[4] = (UCHAR)(intSum >> 8) &  255;
	Compressed[5] = (UCHAR)intSum & 255;
}

void Bit6ToASCII(UCHAR * Padded, UCHAR * UnCompressed)
{
	// uncompress 6 to 8

	// Input must be 6 bytes which represent packed 6 bit characters that well 
	// result will be 8 ASCII character set values from 32 to 95...

	unsigned long long intSum = 0;

	int i;

	for (i=0; i<3; i++)
	{
		intSum = (intSum << 8) + Padded[i];
	}

	UnCompressed[0] = (UCHAR)((intSum >> 18) & 63) + 32;
	UnCompressed[1] = (UCHAR)((intSum >> 12) & 63) + 32;
	UnCompressed[2] = (UCHAR)((intSum >> 6) & 63) + 32;
	UnCompressed[3] = (UCHAR)(intSum & 63) + 32;

	intSum = 0;

	for (i=3; i<6; i++)
	{
		intSum = (intSum << 8) + Padded[i] ;
	}

	UnCompressed[4] = (UCHAR)((intSum >> 18) & 63) + 32;
	UnCompressed[5] = (UCHAR)((intSum >> 12) & 63) + 32;
	UnCompressed[6] = (UCHAR)((intSum >> 6) & 63) + 32;
	UnCompressed[7] = (UCHAR)(intSum & 63) + 32;
}


// Function to compress callsign (up to 7 characters + optional "-"SSID   (-0 to -15 or -A to -Z) 
    
void CompressCallsign(char * inCallsign, UCHAR * Compressed)
{
	char Callsign[10] = "";
	char Padded[16];
	int SSID;
	char * Dash;

	memcpy(Callsign, inCallsign, 10);
	Dash = strchr(Callsign, '-');
	
	if (Dash == 0)		// if No SSID
	{
		strcpy(Padded, Callsign);
		strcat(Padded, "    ");
		Padded[7] = '0';			//  "0" indicates no SSID
	}
	else
	{
		*(Dash++) = 0;
		SSID = atoi(Dash);

		strcpy(Padded, Callsign);
		strcat(Padded, "    ");

		if (SSID >= 10)		// ' handles special case of -10 to -15 : ; < = > ? '
			Padded[7] = ':' + SSID - 10;
		else
			Padded[7] = *(Dash);
	}

	ASCIIto6Bit(Padded, Compressed); //compress to 8 6 bit characters   6 bytes total
}

// Function to compress Gridsquare (up to 8 characters)

void CompressGridSquare(char * Square, UCHAR * Compressed)
{
	char Padded[17];
        
	if (strlen(Square) > 8)
		return;

	strcpy(Padded, Square);
	strcat(Padded, "        ");

	ASCIIto6Bit(Padded, Compressed); //compress to 8 6 bit characters   6 bytes total
}

// Function to decompress 6 byte call sign to 7 characters plus optional -SSID of -0 to -15 or -A to -Z
  
void DeCompressCallsign(char * bytCallsign, char * returned)
{
	char bytTest[10] = "";
	char SSID[8] = "";
    
	Bit6ToASCII(bytCallsign, bytTest);

	memcpy(returned, bytTest, 7);
	returned[7] = 0;
	strlop(returned, ' ');		// remove trailing space

	if (bytTest[7] == '0') // Value of "0" so No SSID
		returned[6] = 0;
	else if (bytTest[7] >= 58 && bytTest[7] <= 63) //' handles special case for -10 to -15
		sprintf(SSID, "-%d", bytTest[7] - 48);
	else
		sprintf(SSID, "-%c", bytTest[7]);
	
	strcat(returned, SSID);
}


// Function to decompress 6 byte Grid square to 4, 6 or 8 characters

void DeCompressGridSquare(char * bytGS, char * returned)
{
	char bytTest[10] = "";
	Bit6ToASCII(bytGS, bytTest);

	strlop(bytTest, ' ');
	strcpy(returned, bytTest);
}

// A function to compute the parity symbol used in the frame type encoding

UCHAR ComputeTypeParity(UCHAR bytFrameType)
{
	UCHAR bytMask = 0xC0;
	UCHAR bytParitySum = 1;
	UCHAR bytSym = 0;
	int k;

	for (k = 0; k < 4; k++)
	{
		bytSym = (bytMask & bytFrameType) >> (2 * (3 - k));
		bytParitySum = bytParitySum ^ bytSym;
		bytMask = bytMask >> 2;
	}
    
	return bytParitySum & 0x3;
}

// Function to look up the byte value from the frame string name

UCHAR FrameCode(char * strFrameName)
{
	int i;

    for (i = 0; i < 256; i++)
	{
		if (strcmp(strFrameType[i], strFrameName) == 0)
		{
			return i;
		}
	}
	return 0;
}

unsigned int GenCRC16(unsigned char * Data, unsigned short length)
{
	// For  CRC-16-CCITT =    x^16 + x^12 +x^5 + 1  intPoly = 1021 Init FFFF
    // intSeed is the seed value for the shift register and must be in the range 0-0xFFFF

	int intRegister = 0xffff; //intSeed
	int i,j;
	int Bit;
	int intPoly = 0x8810;	//  This implements the CRC polynomial  x^16 + x^12 +x^5 + 1

	for (j = 0; j < length; j++)	
	{
		int Mask = 0x80;			// Top bit first

		for (i = 0; i < 8; i++)	// for each bit processing MS bit first
		{
			Bit = Data[j] & Mask;
			Mask >>= 1;

            if (intRegister & 0x8000)		//  Then ' the MSB of the register is set
			{
                // Shift left, place data bit as LSB, then divide
                // Register := shiftRegister left shift 1
                // Register := shiftRegister xor polynomial
                 
              if (Bit)
                 intRegister = 0xFFFF & (1 + (intRegister << 1));
			  else
                  intRegister = 0xFFFF & (intRegister << 1);
	
				intRegister = intRegister ^ intPoly;
			}
			else  
			{
				// the MSB is not set
                // Register is not divisible by polynomial yet.
                // Just shift left and bring current data bit onto LSB of shiftRegister
              if (Bit)
                 intRegister = 0xFFFF & (1 + (intRegister << 1));
			  else
                  intRegister = 0xFFFF & (intRegister << 1);
			}
		}
	}
 
	return intRegister;
}

BOOL checkcrc16(unsigned char * Data, unsigned short length)
{
	int intRegister = 0xffff; //intSeed
	int i,j;
	int Bit;
	int intPoly = 0x8810;	//  This implements the CRC polynomial  x^16 + x^12 +x^5 + 1

	for (j = 0; j <  (length - 2); j++)		// ' 2 bytes short of data length
	{
		int Mask = 0x80;			// Top bit first

		for (i = 0; i < 8; i++)	// for each bit processing MS bit first
		{
			Bit = Data[j] & Mask;
			Mask >>= 1;

            if (intRegister & 0x8000)		//  Then ' the MSB of the register is set
			{
                // Shift left, place data bit as LSB, then divide
                // Register := shiftRegister left shift 1
                // Register := shiftRegister xor polynomial
                 
              if (Bit)
                 intRegister = 0xFFFF & (1 + (intRegister << 1));
			  else
                  intRegister = 0xFFFF & (intRegister << 1);
	
				intRegister = intRegister ^ intPoly;
			}
			else  
			{
				// the MSB is not set
                // Register is not divisible by polynomial yet.
                // Just shift left and bring current data bit onto LSB of shiftRegister
              if (Bit)
                 intRegister = 0xFFFF & (1 + (intRegister << 1));
			  else
                  intRegister = 0xFFFF & (intRegister << 1);
			}
		}
	}

    if (Data[length - 2] == intRegister >> 8)
		if (Data[length - 1] == (intRegister & 0xFF))
			return TRUE;
   
	return FALSE;
}


//	Subroutine to compute a 16 bit CRC value and append it to the Data... With LS byte XORed by bytFrameType
    
void GenCRC16FrameType(char * Data, int Length, UCHAR bytFrameType)
{
	unsigned int CRC = GenCRC16(Data, Length);

	// Put the two CRC bytes after the stop index

	Data[Length++] = (CRC >> 8);		 // MS 8 bits of Register
	Data[Length] = (CRC & 0xFF) ^ bytFrameType;  // LS 8 bits of Register
}

// Function to compute a 16 bit CRC value and check it against the last 2 bytes of Data (the CRC) XORing LS byte with bytFrameType..
 
unsigned short int compute_crc(unsigned char *buf,int len);

BOOL  CheckCRC16FrameType(unsigned char * Data, int Length, UCHAR bytFrameType)
{
	// returns TRUE if CRC matches, else FALSE
    // For  CRC-16-CCITT =    x^16 + x^12 +x^5 + 1  intPoly = 1021 Init FFFF
    // intSeed is the seed value for the shift register and must be in the range 0-0xFFFF

	unsigned int CRC = GenCRC16(Data, Length);
	unsigned short CRC2 =  compute_crc(Data, Length);
	CRC2 ^= 0xffff;
  
	// Compare the register with the last two bytes of Data (the CRC) 
    
	if ((CRC >> 8) == Data[Length])
		if (((CRC & 0xFF) ^ bytFrameType) == Data[Length + 1])
			return TRUE;

	return FALSE;
}

// Subroutine to get intDataLen bytes from outbound queue (bytDataToSend)

void ClearDataToSend()
{
	GetSemaphore();
	bytDataToSendLength = 0;
	FreeSemaphore();

	SetLED(TRAFFICLED, FALSE);
	QueueCommandToHost("BUFFER 0");
}

void SaveQueueOnBreak()
{
	// Save data we are about to remove from TX buffer
}


extern UCHAR bytEchoData[1280];		// has to be at least max packet size (?1280)

extern int bytesEchoed;

extern UCHAR DelayedEcho;


void RemoveDataFromQueue(int Len)
{
	char HostCmd[32];

	if (Len == 0)
		return;

	// Called when ACK received, or on FEC send

	//	If using PTC Serial Interface and delayed echo requested, send it

	if (DelayedEcho == '1')
	{
		memcpy(bytEchoData, bytDataToSend, Len);
		bytesEchoed = Len;
	}

	GetSemaphore();

	if (Len > bytDataToSendLength)
		Len = bytDataToSendLength;			// Shouldn't happen, unless the Q is cleared

	bytDataToSendLength -= Len;

	if (bytDataToSendLength > 0)
		memmove(bytDataToSend, &bytDataToSend[Len], bytDataToSendLength);

	FreeSemaphore();

	if (bytDataToSendLength == 0)
		SetLED(TRAFFICLED, FALSE);
	
	sprintf(HostCmd, "BUFFER %d", bytDataToSendLength);
	QueueCommandToHost(HostCmd);
}

// Timer Rotines

void CheckTimers()
{
	//	Check for Timeout after a send that needs to be repeated

	if ((blnEnbARQRpt || blnDISCRepeating) && Now > dttNextPlay)
	{
		// No response Timeout

		if (GetNextFrame())
		{
			// I think this only returns TRUE if we have to repeat the last 

			//	Repeat mechanism for normal repeated FEC or ARQ frames
      
			WriteDebugLog(LOGDEBUG, "Repeating Last Frame");
			RemodulateLastFrame();
		}
		else
			// I think this means we have exceeded retries or had an abort

			blnEnbARQRpt = FALSE;
	}


	//  Event triggered by tmrSendTimeout elapse. Ends an ARQ session and sends a DISC frame 
    
	if (tmrSendTimeout && Now > tmrSendTimeout)
	{
		char HostCmd[80];

		// (Handles protocol rule 1.7)
       
		tmrSendTimeout = 0;

			//Dim dttStartWait As Date = Now
			//While objMain.blnLastPTT And Now.Subtract(dttStartWait).TotalSeconds < 10
			// Thread.Sleep(50)
			// End While

		WriteDebugLog(LOGDEBUG, "ARDOPprotocol.tmrSendTimeout]  ARQ Timeout from ProtocolState: %s Going to DISC state", ARDOPStates[ProtocolState]);
        
			// Confirmed proper operation of this timeout and rule 4.0 May 18, 2015
			// Send an ID frame (Handles protocol rule 4.0)
		
		EncLen = Encode4FSKIDFrame(strLocalCallsign, GridSquare, bytEncodedBytes);
		Mod4FSKDataAndPlay(0x30, &bytEncodedBytes[0], 16, 0);		// only returns when all sent
		dttLastFECIDSent = Now;
			
		if (AccumulateStats) LogStats();

		QueueCommandToHost("DISCONNECTED");
			
		sprintf(HostCmd, "STATUS ARQ Timeout from Protocol State:  %s", ARDOPStates[ProtocolState]);
		QueueCommandToHost(HostCmd);
		blnEnbARQRpt = FALSE;
		//Thread.Sleep(2000)
		ClearDataToSend();

		EncLen = Encode4FSKControl(0x29, bytSessionID, bytEncodedBytes);
		Mod4FSKDataAndPlay(0x29, &bytEncodedBytes[0], EncLen, LeaderLength);		// only returns when all sent

		intFrameRepeatInterval = 2000;
		SetARDOPProtocolState(DISC);
			
		InitializeConnection(); // reset all Connection data
				
			// Clear the mnuBusy status on the main form
			//Dim stcStatus As Status = Nothing
		    //stcStatus.ControlName = "mnuBusy"
			//stcStatus.Text = "FALSE"
		    //queTNCStatus.Enqueue(stcStatus)
        
		blnTimeoutTriggered = FALSE ;// prevents a retrigger
	}

	// Elapsed Subroutine for Pending timeout
   
	if (tmrIRSPendingTimeout && Now > tmrIRSPendingTimeout)
	{
		char HostCmd[80];
		
		tmrIRSPendingTimeout = 0;

		WriteDebugLog(LOGDEBUG, "ARDOPprotocol.tmrIRSPendingTimeout]  ARQ Timeout from ProtocolState: %s Going to DISC state",  ARDOPStates[ProtocolState]);

		QueueCommandToHost("DISCONNECTED");
		sprintf(HostCmd, "STATUS ARQ CONNECT REQUEST TIMEOUT FROM PROTOCOL STATE: %s",ARDOPStates[ProtocolState]);

		QueueCommandToHost(HostCmd);

		blnEnbARQRpt = FALSE;
		ProtocolState = DISC;
		blnPending = FALSE;
		InitializeConnection();	 // reset all Connection data

			//' Clear the mnuBusy status on the main form
		     //Dim stcStatus As Status = Nothing
			//stcStatus.ControlName = "mnuBusy"
			//stcStatus.Text = "FALSE"
			//	queTNCStatus.Enqueue(stcStatus)
	}

	// Subroutine for tmrFinalIDElapsed

	if (tmrFinalID && Now > tmrFinalID)
	{
		tmrFinalID = 0;
		
		WriteDebugLog(LOGDEBUG, "[ARDOPprotocol.tmrFinalID_Elapsed]  Send Final ID (%s, [%s])", strFinalIDCallsign, GridSquare);
   
		if (CheckValidCallsignSyntax(strFinalIDCallsign))
		{
			EncLen = Encode4FSKIDFrame(strFinalIDCallsign, GridSquare, bytEncodedBytes);
			Mod4FSKDataAndPlay(0x30, &bytEncodedBytes[0], 16, 0);		// only returns when all sent
			dttLastFECIDSent = Now;
		}
	}

	// Send Conect Request (from ARQCALL command)

	if (NeedConReq)
	{
		NeedConReq = 0;
		SendARQConnectRequest(Callsign, ConnectToCall);
	}
	if (NeedPing)
	{
		NeedPing = 0;
		SendPING(Callsign, ConnectToCall, PingCount);
	}

	// Send Async ID (from SENDID command)

	if (NeedID)
	{
		SendID(wantCWID);
		NeedID = 0;
	}

	if (NeedCWID)
	{
		sendCWID(Callsign, FALSE);
		NeedCWID = 0;
	}

	if (NeedTwoToneTest)
	{
		Send5SecTwoTone();
		NeedTwoToneTest = 0;
	}


	if (Now > tmrPollOBQueue)
	{
//		char HostCmd[32];
//		sprintf(HostCmd, "BUFFER %d", bytDataToSendLength);
//		QueueCommandToHost(HostCmd);
	
		tmrPollOBQueue = Now + 10000;		// 10 Secs
	}
}

// Main polling Function returns True or FALSE if closing 

BOOL MainPoll()
{

	//   Dim stcStatus As Status = Nothing

   //     ' Check the sound card to insure still sampling
     //   If (Now.Subtract(dttLastSoundCardSample).TotalSeconds > 10) And objProtocol.GetARDOPProtocolState() <> ProtocolState.OFFLINE Then
       //     tmrStartCODEC.Interval = 1000
         //   dttLastSoundCardSample = Now
        //    Logs.Exception("[tmrPoll_Tick] > 10 seconds with no sound card samples...Restarting Codec")
          //  tmrStartCODEC.Start() ' Start the timer to retry starting sound card
       // End If
	
	// Checks to see if frame ready for playing

	if (!SoundIsPlaying && !blnEnbARQRpt && !blnDISCRepeating)		// Idle (check playing in case we call from txSleep())
	{
		if (GetNextFrame())
		{
			// As we will send the frame if one is available, and won't return
			// till it is all sent, I don't think I have to do anything here
		}

		// Send anything on Packet Queue

		if (UseKISS)
			if (State == SearchingForLeader)	// Dont send while receiving
//				if (blnBusyStatus == FALSE)
//					PacketStartTX();		// Won't return till all sent
					PktARDOPStartTX();

	}
//            If Not SoundIsPlaying Then
//               SendIDToolStripMenuItem.Enabled = objProtocol.GetARDOPSetARDOPProtocolState(ProtocolState.DISC
//         Else
     //           SendIDToolStripMenuItem.Enabled = FALSE
       //     End If
    
		/*
		' Update any form Main display items from the TNCStatus queue
            While queTNCStatus.Count > 0
                Try
                    stcStatus = CType(queTNCStatus.Dequeue, Status)
                    Select Case stcStatus.ControlName
                        ' Receive controls:
                        Case "lblQuality"
                            lblQuality.Text = stcStatus.Text
                        Case "ConstellationPlot"
                            DisplayPlot()
                            intRepeatCnt += 0
                        Case "lblXmtFrame"
                            lblXmtFrame.Text = stcStatus.Text
                            lblXmtFrame.BackColor = stcStatus.BackColor
                        Case "lblRcvFrame"
                            lblRcvFrame.Text = stcStatus.Text
                            lblRcvFrame.BackColor = stcStatus.BackColor
                        Case "prgReceiveLevel"
                            prgReceiveLevel.Value = stcStatus.Value
                            If stcStatus.Value < 64 Then ' < 12% of Full scale (16 bit A/D)
                                prgReceiveLevel.ForeColor = Color.SkyBlue
                            ElseIf stcStatus.Value > 170 Then ' > 88% of full scale (16 bit A/D)
                                prgReceiveLevel.ForeColor = Color.LightSalmon
                            Else
                                prgReceiveLevel.ForeColor = Color.LightGreen
                            End If
                        Case "lblOffset"
                            lblOffset.Text = stcStatus.Text
                        Case "lblHost"
                            If stcStatus.Text <> "" Then lblHost.Text = stcStatus.Text
                            lblHost.BackColor = stcStatus.BackColor
                        Case "lblState"
                            lblState.Text = stcStatus.Text
                            lblState.BackColor = stcStatus.BackColor
                        Case "lblCF"
                            lblCF.Text = stcStatus.Text
                        Case "mnuBusy"
                            If stcStatus.Text.ToUpper = "TRUE" Or stcStatus.Text.ToUpper = "FALSE" Then
                                ChannelBusyToolStripMenuItem.Text = "Channel Busy"
                                ChannelBusyToolStripMenuItem.Visible = CBool(stcStatus.Text)
                            Else
                                ChannelBusyToolStripMenuItem.Text = stcStatus.Text
                                ChannelBusyToolStripMenuItem.Visible = True
                            End If
                    End Select
                Catch
                    Logs.Exception("[Main.tmrPoll.Tick] queTNCStatus Err: " & Err.Description)
                    Exit While
                End Try
            End While
        */

	
	if	(blnClosing)	// Check for closing
		return FALSE;

	return TRUE;
}

int dttLastBusy;
int dttLastClear;
int dttStartRTMeasure;

//  Functino to extract bandwidth from ARQBandwidth

int ExtractARQBandwidth()
{
	return atoi(ARQBandwidths[ARQBandwidth]);
}

//	 Function to implement a busy detector based on 1024 point FFT
 /*
BOOL BusyDetect(float * dblMag, int intStart, int intStop)
{
	// this only called while searching for leader ...once leader detected, no longer called.
	// First look at simple peak over the frequency band of  interest.
	// Status:  May 28, 2014.  Some initial encouraging results. But more work needed.
	//       1) Use of Start and Stop ranges good and appear to work well ...may need some tweaking +/_ a few bins.
	//       2) Using a Fast attack and slow decay for the dblAvgPk2BaselineRation number e.g.
	//       dblAvgPk2BaselineRatio = Max(dblPeakPwrAtFreq / dblAvgBaselineX, 0.9 * dblAvgPk2BaselineRatio)
	// Seems to work well for the narrow detector. Tested on real CW, PSK, RTTY. 
	// Still needs work on the wide band detector. (P3, P4 etc)  May be issues with AGC speed. (my initial on-air tests using AGC Fast).
	// Ideally can find a set of parameters that won't require user "tweaking"  (Busy Squelch) but that is an alternative if needed. 
	// use of technique of re initializing some parameters on a change in detection bandwidth looks good and appears to work well with 
	// scanning.  Could be expanded with properties to "read back" these parameters so they could be saved and re initialize upon each new frequency. 

	static int intBusyCountPkToBaseline = 0;
	static int intBusyCountFastToSlow = 0;
	static int intBusyCountSlowToFast = 0;
	static BOOL blnLastBusy = FALSE;

	float dblAvgBaseline = 0;
	float dblPwrAtPeakFreq = 0;
	float dblAvgBaselineX;
	float dblAlphaBaselineSlow = 0.1f; // This factor affects the filtering of baseline slow. smaller = slower filtering
	float dblAlphaBaselineFast = 0.5f; // This factor affects the filtering of baseline fast. smaller = slower filtering
	int intPkIndx = 0;
	float dblFSRatioNum, dblSFRatioNum;
	BOOL  blnFS, blnSF, blnPkBaseline;
	int i;

	// This holds off any processing of data until 100 ms after PTT release to allow receiver recovery.
      
	if (Now - dttStartRTMeasure < 100)
		return blnLastBusy;

	for (i = intStart; i <= intStop; i++)	 // cover a range that matches the bandwidth expanded (+/-) by the tuning range
	{
		if (dblMag[i] > dblPwrAtPeakFreq)
		{
			dblPwrAtPeakFreq = dblMag[i];
			intPkIndx = i;
		}
		dblAvgBaseline += dblMag[i];
	}
     
	if (intPkIndx == 0)
		return 0;
	
	// add in the 2 bins above and below the peak (about 59 Hz total bandwidth)
	// This needs refinement for FSK mods like RTTY which have near equal peaks making the Peak and baseline on strong signals near equal
	// Computer the power within a 59 Hz spectrum around the peak

	dblPwrAtPeakFreq += (dblMag[intPkIndx - 2] + dblMag[intPkIndx - 1]) + (dblMag[intPkIndx + 2] + dblMag[intPkIndx + 1]);
	dblAvgBaselineX = (dblAvgBaseline - dblPwrAtPeakFreq) / (intStop - intStart - 5);  // the avg Pwr per bin ignoring the 59 Hz area centered on the peak
	dblPwrAtPeakFreq = dblPwrAtPeakFreq / 5;  //the the average Power (per bin) in the region of the peak (peak +/- 2bins...about 59 Hz)

	if (intStart == intLastStart && intStop == intLastStop)
	{
		dblAvgPk2BaselineRatio = dblPwrAtPeakFreq / dblAvgBaselineX;
		dblAvgBaselineSlow = (1 - dblAlphaBaselineSlow) * dblAvgBaselineSlow + dblAlphaBaselineSlow * dblAvgBaseline;
		dblAvgBaselineFast = (1 - dblAlphaBaselineFast) * dblAvgBaselineFast + dblAlphaBaselineFast * dblAvgBaseline;
	}
	else
	{
		// This initializes the values after a bandwidth change

		dblAvgPk2BaselineRatio = dblPwrAtPeakFreq / dblAvgBaselineX;
		dblAvgBaselineSlow = dblAvgBaseline;
		dblAvgBaselineFast = dblAvgBaseline;
		intLastStart = intStart;
		intLastStop = intStop;
	}
	
	if (Now - dttLastBusy < 1000 ||  ProtocolState != DISC)	// Why??
		return blnLastBusy;
	
	if (dblAvgPk2BaselineRatio > 1.118f * powf(Squelch, 1.5f))   // These values appear to work OK but may need optimization April 21, 2015
	{
		blnPkBaseline = TRUE;
		dblAvgBaselineSlow = dblAvgBaseline;
		dblAvgBaselineFast = dblAvgBaseline;
	}
	else
	{
       // 'If intBusyCountPkToBaseline > 0 Then

		blnPkBaseline = FALSE;
	}
	
	// This detects wide band "pulsy" modes like Pactor 3, MFSK etc

	switch(Squelch)		 // this provides a modest adjustment to the ratio limit based on practical squelch values
	{
		//These values yield less sensiivity for F:S which minimizes trigger by static crashes but my need further optimization May 2, 2015

	case 0:
	case 1:
	case 2:
		dblFSRatioNum = 1.9f;
		dblSFRatioNum = 1.2f;
		break;
		
	case 3:
		dblFSRatioNum = 2.1f;
		dblSFRatioNum = 1.4f;
		break;
	case 4:
		dblFSRatioNum = 2.3f;
		dblSFRatioNum = 1.6f;
		break;
	case 5:
		dblFSRatioNum = 2.5f;
		dblSFRatioNum = 1.8f;
		break;
	case 6:
		dblFSRatioNum = 2.7f;
		dblSFRatioNum = 2.0f;
	case 7:
		dblFSRatioNum = 2.9f;
		dblSFRatioNum = 2.2f;
	case 8:
	case 9:
	case 10:
        dblFSRatioNum = 3.1f;
		dblSFRatioNum = 2.4f;
	}
	
	// This covers going from Modulation to no modulation e.g. End of Pactor frame

	if ((dblAvgBaselineSlow / dblAvgBaselineFast) > dblSFRatioNum)
	
		//Debug.WriteLine("     Slow to Fast")
		blnSF = TRUE;
	else
		blnSF = FALSE;
	
	//  This covers going from no modulation to Modulation e.g. Start of Pactor Frame or Static crash
	
	if ((dblAvgBaselineFast / dblAvgBaselineSlow) > dblFSRatioNum)
         //Debug.WriteLine("     Fast to Slow")
		blnFS = TRUE;
	else
		blnFS = FALSE;

	if (blnFS || blnSF || blnPkBaseline)
	{
		//'If blnFS Then Debug.WriteLine("Busy: Fast to Slow")
		//'If blnSF Then Debug.WriteLine("Busy: Slow to Fast")
		//'If blnPkBaseline Then Debug.WriteLine("Busy: Pk to Baseline")
		blnLastBusy = TRUE;
		dttLastBusy = Now;
		return TRUE;
	}
	else
	{
		blnLastBusy = FALSE;
		dttLastClear = Now;
        return FALSE;
	}
	return blnLastBusy;
}
*/
//	Subroutine to update the Busy detector when not displaying Spectrum or Waterfall (graphics disabled)
 		
int LastBusyCheck = 0;
extern UCHAR CurrentLevel;

#ifdef PLOTSPECTRUM		
float dblMagSpectrum[206];
float dblMaxScale = 0.0f;
extern UCHAR Pixels[4096];
extern UCHAR * pixelPointer;
#endif

void UpdateBusyDetector(short * bytNewSamples)
{
	float dblReF[1024];
	float dblImF[1024];
	float dblMag[206];
#ifdef PLOTSPECTRUM
	float dblMagMax = 0.0000000001f;
	float dblMagMin = 10000000000.0f;
#endif
	UCHAR Waterfall[256];			// Colour index values to send to GUI
	int clrTLC = Lime;				// Default Bandwidth lines on waterfall
	
	static BOOL blnLastBusyStatus;
	
	float dblMagAvg = 0;
	int intTuneLineLow, intTuneLineHi, intDelta;
	int i;

//	if (State != SearchingForLeader)
//		return;						// only when looking for leader

	if (ProtocolState != DISC)		// ' Only process busy when in DISC state
	{
		// Dont do busy, but may need waterfall or spectrum

		if ((WaterfallActive | SpectrumActive) == 0)
			return;					// No waterfall or spectrum 
	}

	if (Now - LastBusyCheck < 100)
		return;

	LastBusyCheck = Now;

	FourierTransform(1024, bytNewSamples, &dblReF[0], &dblImF[0], FALSE);

	for (i = 0; i < 206; i++)
	{
		//	starting at ~300 Hz to ~2700 Hz Which puts the center of the signal in the center of the window (~1500Hz)
            
		dblMag[i] = powf(dblReF[i + 25], 2) + powf(dblImF[i + 25], 2);	 // first pass 
		dblMagAvg += dblMag[i];
#ifdef PLOTSPECTRUM		
		dblMagSpectrum[i] = 0.2 * dblMag[i] + 0.8 * dblMagSpectrum[i];	
		dblMagMax = max(dblMagMax, dblMagSpectrum[i]);
		dblMagMin = min(dblMagMin, dblMagSpectrum[i]);
#endif
	}

//	LookforPacket(dblMag, dblMagAvg, 206, &dblReF[25], &dblImF[25]);
//	packet_process_samples(bytNewSamples, 1200);

	intDelta = (ExtractARQBandwidth() / 2 + TuningRange) / 11.719f;

	intTuneLineLow = max((103 - intDelta), 3);
	intTuneLineHi = min((103 + intDelta), 203);
    
	if (ProtocolState == DISC)		// ' Only process busy when in DISC state
	{
		blnBusyStatus = BusyDetect3(dblMag, intTuneLineLow, intTuneLineHi);
		
		if (blnBusyStatus && !blnLastBusyStatus)
		{
			QueueCommandToHost("BUSY TRUE");
         	newStatus = TRUE;				// report to PTC

			if (!WaterfallActive && !SpectrumActive)
			{
				UCHAR Msg[2];

				Msg[0] = blnBusyStatus;
				SendtoGUI('B', Msg, 1);
			}	    
		}
		//    stcStatus.Text = "True"
            //    queTNCStatus.Enqueue(stcStatus)
            //    'Debug.WriteLine("BUSY TRUE @ " & Format(DateTime.UtcNow, "HH:mm:ss"))
			
		else if (blnLastBusyStatus && !blnBusyStatus)
		{
			QueueCommandToHost("BUSY FALSE");
			newStatus = TRUE;				// report to PTC

			if (!WaterfallActive && !SpectrumActive)
			{
				UCHAR Msg[2];

				Msg[0] = blnBusyStatus;
				SendtoGUI('B', Msg, 1);
			}	    
		} 
		//    stcStatus.Text = "False"
        //    queTNCStatus.Enqueue(stcStatus)
        //    'Debug.WriteLine("BUSY FALSE @ " & Format(DateTime.UtcNow, "HH:mm:ss"))

		blnLastBusyStatus = blnBusyStatus;
	}
	
	if (BusyDet == 0) 
		clrTLC = Goldenrod;
	else if (blnBusyStatus)
		clrTLC = Fuchsia;

	// At the moment we only get here what seaching for leader,
	// but if we want to plot spectrum we should call
	// it always



	if (WaterfallActive)
	{
#ifdef PLOTWATERFALL
		dblMagAvg = log10f(dblMagAvg / 5000.0f);
	
		for (i = 0; i < 206; i++)
		{
			// The following provides some AGC over the waterfall to compensate for avg input level.
        
			float y1 = (0.25f + 2.5f / dblMagAvg) * log10f(0.01 + dblMag[i]);
			int objColor;

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
		
			if (i == 102)
				Waterfall[i] =  Tomato;  // 1500 Hz line (center)
			else if (i == intTuneLineLow || i == intTuneLineLow - 1 || i == intTuneLineHi || i == intTuneLineHi + 1)
				Waterfall[i] = clrTLC;
			else
				Waterfall[i] = objColor; // ' Else plot the pixel as received
		}

		// Send Signal level and Busy indicator to save extra packets

		Waterfall[206] = CurrentLevel;
		Waterfall[207] = blnBusyStatus;

		SendtoGUI('W', Waterfall, 208);
#endif
	}
	else if (SpectrumActive)
	{
#ifdef PLOTSPECTRUM
		// This performs an auto scaling mechansim with fast attack and slow release
        if (dblMagMin / dblMagMax < 0.0001) // more than 10000:1 difference Max:Min
            dblMaxScale = max(dblMagMax, dblMaxScale * 0.9f);
		else
            dblMaxScale = max(10000 * dblMagMin, dblMagMax);
   
		clearDisplay();
	
		for (i = 0; i < 206; i++)
		{
		// The following provides some AGC over the spectrum to compensate for avg input level.
        
			float y1 = -0.25f * (SpectrumHeight - 1) *  log10f((max(dblMagSpectrum[i], dblMaxScale / 10000)) / dblMaxScale); // ' range should be 0 to bmpSpectrumHeight -1
			int objColor  = Yellow;

			Waterfall[i] = y1;
		}
         
		// Send Signal level and Busy indicator to save extra packets

		Waterfall[206] = CurrentLevel;
		Waterfall[207] = blnBusyStatus;
		Waterfall[208] = intTuneLineLow;
		Waterfall[209] = intTuneLineHi;

		SendtoGUI('X', Waterfall, 210);
#endif
	}
}

void SendPING(char * strMycall, char * strTargetCall, int intRpt)
{   	
	EncLen = EncodePing(strMycall, strTargetCall, bytEncodedBytes);

	if (EncLen == 0)
		return;
	
	// generate the modulation with 2 x the default FEC leader length...Should insure reception at the target
	// Note this is sent with session ID 0xFF

	//	Set all flags before playing, as the End TX is called before we return here
	
	intFrameRepeatInterval = 2000;  // ms ' Finn reported 7/4/2015 that 1600 was too short ...need further evaluation but temporarily moved to 2000 ms
	blnEnbARQRpt = TRUE;

	Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);		// only returns when all sent
        
	blnAbort = False;
	dttTimeoutTrip = Now;
	intRepeatCount = 1;
	intPINGRepeats = intRpt;
	blnPINGrepeating = True;
	dttLastPINGSent = Now;

	WriteDebugLog(LOGDEBUG, "[SendPING] strMycall= %s strTargetCall=%s  Repeat=%d", strMycall, strTargetCall, intRpt);

	return;
}
 
// This sub processes a correctly decoded Ping frame, decodes it an passed to host for display if it doesn't duplicate the prior passed frame. 

void ProcessPingFrame(char * bytData)
{
	WriteDebugLog(LOGDEBUG, "ProcessPingFrame Protocol State = %s", ARDOPStates[ProtocolState]);
	
	if (ProtocolState == DISC)
	{
		char * strPingInfo = strlop(bytData, ' ');
		
		if (blnListen && IsPingToMe(strPingInfo) && EnablePingAck)
		{
			// Ack Ping

			EncLen = EncodePingAck(PINGACK, stcLastPingintRcvdSN, stcLastPingintQuality, bytEncodedBytes);
			Mod4FSKDataAndPlay(PINGACK, &bytEncodedBytes[0], EncLen, LeaderLength);		// only returns when all sent
               
			WriteDebugLog(LOGDEBUG, "[ProcessPingFrame] PING from %s S:N=%d Qual=%d", bytData, stcLastPingintRcvdSN, stcLastPingintQuality);
			SendCommandToHost("PINGREPLY");	
			return;
		}
	}	
	SendCommandToHost("CANCELPENDING");	
}

// HID Support Code
/*
char HIDRXBuffer[128];

int HID_Read_Block()
{
	int Len;
	unsigned char Msg[65] = "";

	// Don't try to read more than 64

#ifdef WIN32
	Len = rawhid_recv(0, Msg, 64, 100);
#else
	Len = read(PORT->hDevice, Msg, 64);
#endif

	if (Len <= 0)
		return 0;

	// First byte is actual length

	Len = Msg[0];

	if (Len > 0)
	{
		if (Len < 64)		// Max in HID Packet
		{
			memcpy(&PORT->RXBuffer[PORT->RXLen], Msg + 1, Len);
			Debugprintf("HID Read %d\n", Len);
			return Len;
		}
	}
	return 0;
}

void rawhid_close(int num);

BOOL HID_Write_Block(struct RIGPORTINFO * PORT)
{
	int n = PORT->TXLen;
	UCHAR * ptr = PORT->TXBuffer;
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
		//	n = hid_write(PORT->hDevice, PORT->TXBuffer, PORT->TXLen);
#ifdef WIN32
		ret = rawhid_send(0, Msg, 64, 100);		// Always send 64

		if (ret < 0)
		{
			Debugprintf("Rigcontrol HID Write Failed %d", errno);
			rawhid_close(0);
			PORT->hDevice = NULL;
			return FALSE;
		}
#else
		ret = write(PORT->hDevice, Msg, 64);

		if (ret != 64)
		{
			printf ("Write to %s failed, n=%d, errno=%d\n", PORT->HIDDevice, ret, errno);
			close (PORT->hDevice);
			PORT->hDevice = 0;
			return FALSE;
		}

//		printf("HID Write %d\n", i);
#endif
	}
	return TRUE;
}


BOOL OpenHIDPort(struct RIGPORTINFO * PORT, VOID * Port, int Speed)
{
#ifdef WIN32

	if (PORT->HIDDevice== NULL)
		return FALSE;

	PORT->hDevice = rawhid_open(PORT->HIDDevice);

	if (PORT->hDevice)
		Debugprintf("Rigcontrol HID Device %s opened", PORT->HIDDevice);


//	handle = hid_open_path(PORT->HIDDevice);

//	if (handle)
//	hid_set_nonblocking(handle, 1);

//	PORT->hDevice = handle;

#else
	int fd;
	unsigned int param = 1;

	if (PORT->HIDDevice== NULL)
		return FALSE;

	fd = open (PORT->HIDDevice, O_RDWR);
		
	if (fd == -1)
	{
          printf ("Could not open %s, errno=%d\n", PORT->HIDDevice, errno);
          return FALSE;
	}

	ioctl(fd, FIONBIO, &param);
	printf("Rigcontrol HID Device %s opened", PORT->HIDDevice);
	
	PORT->hDevice = fd;
#endif
	if (PORT->hDevice == 0)
		return (FALSE);

	return TRUE;
}


void CM108_set_ptt(struct RIGINFO *RIG, int PTTState)
{
	char io[5];
	hid_device *handle;
	int n;

	io[0] = 0;
	io[1] = 0;
	io[2] = 1 << (3 - 1);
	io[3] = PTTState << (3 - 1);
	io[4] = 0;

	if (RIG->CM108Device == NULL)
		return;

#ifdef WIN32
	handle = hid_open_path(RIG->CM108Device);

	if (!handle) {
		printf("unable to open device\n");
 		return;
	}

	n = hid_write(handle, io, 5);
	if (n < 0)
	{
		Debugprintf("Unable to write()\n");
		Debugprintf("Error: %ls\n", hid_error(RIG->PORT->hDevice));
	}
	
	hid_close(handle);

#else

	int fd;

	fd = open (RIG->CM108Device, O_WRONLY);
	
	if (fd == -1)
	{
          printf ("Could not open %s for write, errno=%d\n", RIG->CM108Device, errno);
          return;
	}
	
	io[0] = 0;
	io[1] = 0;
	io[2] = 1 << (3 - 1);
	io[3] = PTTState << (3 - 1);
	io[4] = 0;

	n = write (fd, io, 5);
	if (n != 5)
	{
		printf ("Write to %s failed, n=%d, errno=%d\n", RIG->CM108Device, n, errno);
	}
	
	close (fd);
#endif
	return;

}


*/
