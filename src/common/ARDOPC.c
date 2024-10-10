// ARDOPC.cpp : Defines the entry point for the console application.
//

// ardopcf is a fork by pflarue of ardopc by John Wiseman
const char ProductName[] = "ardopcf";

// Version k  Fix conflicting definitions of bytDataToSend
// Version m  Add CM108 PTT (Sept 2021)

// On Linux Some cards seem to stop sending but not report not running, so also check that avail is decreasing
//     (Debug 1 to 5)

// Version n Add Hamlib PTT (incomplete) (July 2022)

// Version p Leader timing changes (Jan 23)
// Version q Fix extradelay (Jan 23)

#include <stdbool.h>
#include <sys/time.h>
#ifdef WIN32
#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#else
#include <unistd.h>
#define SOCKET int
#define closesocket close
#endif

#include "common/ARDOPC.h"
#include "common/Locator.h"
#include "common/StationId.h"
#include "rockliff/rrs.h"

UCHAR bytDataToSend[DATABUFFERSIZE];
int bytDataToSendLength = 0;

/*
 * these globals from SoundInput store the callsign
 * pair received from the last frame, like a ConReq
 *
 * NOTE: these are cleared on every new frame!
 */
extern StationId LastDecodedStationCaller;
extern StationId LastDecodedStationTarget;

void GetTwoToneLeaderWithSync(int intSymLen);
bool SendID(const StationId * id, char * reason);
void PollReceivedSamples();
void CheckTimers();
BOOL GetNextARQFrame();
BOOL TCPHostInit();
BOOL SerialHostInit();
void SerialHostPoll();
void TCPHostPoll();
BOOL MainPoll();
void PlatformSleep();
const char* PlatformSignalAbbreviation(int signal);
BOOL BusyDetect2(float * dblMag, int intStart, int intStop);
BOOL IsPingToMe(const StationId* caller, const StationId* target);

void ResetMemoryARQ();

void WebguiInit();
void WebguiPoll();
int wg_send_fftdata(float *mags, int magsLen);
int wg_send_busy(int cnum, bool isBusy);
int wg_send_protocolmode(int cnum);
extern int WebGuiNumConnected;
extern char HostCommands[2048];
void ProcessCommandFromHost(char * strCMD);

// Config parameters

Locator GridSquare;
StationId Callsign;
BOOL wantCWID = FALSE;
BOOL CWOnOff = FALSE;
BOOL NeedID = FALSE;  // SENDID Command Flag
BOOL NeedCWID = FALSE;  // SENDCWID Command Flag
BOOL NeedConReq = FALSE;  // ARQCALL Command Flag
BOOL NeedPing = FALSE;  // PING Command Flag
BOOL NeedTwoToneTest = FALSE;
enum _ARQBandwidth CallBandwidth = UNDEFINED;
int PingCount;

BOOL blnPINGrepeating = False;
BOOL blnFramePending = False;  // Cancels last repeat
int intPINGRepeats = 0;

int WaterfallActive = 1;  // Waterfall display on
int SpectrumActive = 0;  // Spectrum display off

StationId ConnectToCall;

int LeaderLength = 240;
unsigned int ARQTimeout = 120;
int TuningRange = 100;
int ARQConReqRepeats = 5;
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
BOOL RadioControl = FALSE;
BOOL SlowCPU = FALSE;
BOOL AccumulateStats = TRUE;
BOOL Use600Modes = FALSE;
BOOL FSKOnly = FALSE;
BOOL fastStart = TRUE;
BOOL EnablePingAck = TRUE;

BOOL gotGPIO = FALSE;
BOOL useGPIO = FALSE;

int pttGPIOPin = -1;
BOOL pttGPIOInvert = FALSE;

HANDLE hCATDevice = 0;
char CATPort[80] = "";  // Port for CAT.
int CATBAUD = 19200;
int EnableHostCATRX = FALSE;  // Set when host sends RADIOHEX command

HANDLE hPTTDevice = 0;
char PTTPort[80] = "";  // Port for Hardware PTT - may be same as control port.
int PTTBAUD = 19200;

UCHAR PTTOnCmd[64];
UCHAR PTTOnCmdLen = 0;

UCHAR PTTOffCmd[64];
UCHAR PTTOffCmdLen = 0;

// Stats

// Public Structure QualityStats

int int4FSKQuality;
int int4FSKQualityCnts;
int intFSKSymbolsDecoded;
int intPSKQuality[2];
int intPSKQualityCnts[2];
int intPSKSymbolsDecoded;

int intQAMQuality;
int intQAMQualityCnts;
int intQAMSymbolsDecoded;


int stcLastPingintRcvdSN;
int stcLastPingintQuality;
time_t stcLastPingdttTimeReceived;

BOOL blnInitializing = FALSE;

BOOL blnLastPTT = FALSE;

BOOL PlayComplete = FALSE;

BOOL blnBusyStatus = FALSE;
BOOL newStatus;

unsigned int tmrSendTimeout;

int intCalcLeader;        // the computed leader to use based on the reported Leader Length
int intRmtLeaderMeasure = 0;

int dttCodecStarted;

enum _ReceiveState State;
enum _ARDOPState ProtocolState;

const char ARDOPStates[8][9] = {
	"OFFLINE", "DISC", "ISS", "IRS", "IDLE", "IRStoISS", "FECSEND", "FECRCV"
};

struct SEM Semaphore = {0, 0, 0, 0};

BOOL SoundIsPlaying = FALSE;
BOOL Capturing = TRUE;

int DecodeCompleteTime;

BOOL blnAbort = FALSE;
int intRepeatCount;
BOOL blnARQDisconnect = FALSE;

int dttLastPINGSent;

unsigned int LastIDFrameTime = 0;
enum _ProtocolMode ProtocolMode = FEC;

extern BOOL blnEnbARQRpt;
extern BOOL blnDISCRepeating;
extern StationId ARQStationRemote;  // current connection remote callsign
extern StationId ARQStationLocal;   // current connection local callsign
extern StationId ARQStationFinalId; // post-session local IDF to send
extern int dttTimeoutTrip;
extern int intFrameRepeatInterval;
extern BOOL blnPending;
extern unsigned int tmrIRSPendingTimeout;
extern unsigned int tmrFinalID;
extern unsigned int tmrPollOBQueue;
int Encode4FSKControl(UCHAR bytFrameType, UCHAR bytSessionID, UCHAR * bytreturn);
void SendPING(const StationId* mycall, const StationId* target, int intRpt);

int intRepeatCnt;

extern SOCKET TCPControlSock, TCPDataSock;

BOOL blnClosing = FALSE;
int closedByPosixSignal = 0;
BOOL blnCodecStarted = FALSE;

unsigned int dttNextPlay = 0;

extern BOOL InitRXO;
extern bool DeprecationWarningsIssued;

const UCHAR bytValidFrameTypesALL[] = {
	DataNAKmin, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, DataNAKmax,
	BREAK, IDLEFRAME, DISCFRAME, END, ConRejBusy, ConRejBW, IDFRAME,
	ConReq200M, ConReq500M, ConReq1000M, ConReq2000M,
	ConReq200F, ConReq500F, ConReq1000F, ConReq2000F,
	ConAck200, ConAck500, ConAck1000, ConAck2000, PINGACK, PING,

	DataFRAMEmin, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,0x47,0x48,0x49,  // 40 - 4F
	0x4A, 0x4B, 0x4C, 0x4D,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55,  // 50 - 5F
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65,  // 60 - 6F
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x7A, 0x7B, 0x7C, DataFRAMEmax,  // 70 - 7F

	DataACKmin, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
	0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
	0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
	0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, DataACKmax
};

const UCHAR bytValidFrameTypesISS[] = {  // ACKs, NAKs, END, DISC, BREAK
	// NAK
	DataNAKmin, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, DataNAKmax,
	// BREAK, DISC, or END
	BREAK, DISCFRAME, END, ConRejBusy, ConRejBW,
	// Con req and Con ACK
	ConReq200M, ConReq500M, ConReq1000M, ConReq2000M,
	ConReq200F, ConReq500F, ConReq1000F, ConReq2000F,
	ConAck200, ConAck500, ConAck1000, ConAck2000,
	// ACK
	DataACKmin, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
	0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
	0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
	0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, DataACKmax
};


const UCHAR * bytValidFrameTypes;

int bytValidFrameTypesLengthISS = sizeof(bytValidFrameTypesISS);
int bytValidFrameTypesLengthALL = sizeof(bytValidFrameTypesALL);
int bytValidFrameTypesLength;

BOOL blnTimeoutTriggered = FALSE;

//	We can't keep the audio samples for retry, but we can keep the
//	encoded data

unsigned char bytEncodedBytes[1800] = "";  // I think the biggest is 600 bd 768 + overhead
int EncLen;


extern UCHAR bytSessionID;

int intLastRcvdFrameQuality;

int intAmp = 26000;	   // Selected to have some margin in calculations with 16 bit values (< 32767) this must apply to all filters as well.

const char strAllDataModes[18][15] = {
	"4FSK.200.50S", "4PSK.200.100S",
	"4PSK.200.100", "8PSK.200.100", "16QAM.200.100",
	"4FSK.500.100S", "4FSK.500.100",
	"4PSK.500.100", "8PSK.500.100", "16QAM.500.100",
	"4PSK.1000.100", "8PSK.1000.100", "16QAM.1000.100",
	"4PSK.2000.100", "8PSK.2000.100", "16QAM.2000.100",
	"4FSK.2000.600", "4FSK.2000.600S"
};

int strAllDataModesLen = 18;

const short FrameSize[256] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 00 - 0F
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 10 - 1F
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 20 - 2F
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 30 - 3F
	64, 64, 16, 16, 108, 108, 128, 128, 16, 16, 64, 64, 32, 32, 0, 0,  // 40 - 4F
	128, 128, 216, 216, 256, 256, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 50 - 5F
	256, 256, 432, 432, 512, 512, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 60 - 6F
	512, 512, 864, 864, 1024, 1024, 0, 0, 0, 0, 600, 600, 200, 200, 0, 0,  // 70 - 7F
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 80 - 8F
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 90 - 9F
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // a0 - AF
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // b0 - BF
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // c0 - CF
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // d0 - DF
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // e0 - EF
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  // f0 - FF
};


const char strFrameType[256][18] = {
	"DataNAK",  // Range 0x00 to 0x1F includes 5 bits for quality 1 Car, 200Hz,4FSK
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
	"", "",

	// Short Control Frames 1 Car, 200Hz,4FSK.  Reassigned May 22, 2015 for maximum "distance"

	"BREAK", "IDLE", "",
	"", "", "",
	"DISC", "", "",
	"END",
	"ConRejBusy",
	"ConRejBW",
	"",

	// Special frames 1 Car, 200Hz,4FSK 0x30 +

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

	"4PSK.200.100.E",  // 0x40
	"4PSK.200.100.O",
	"4PSK.200.100S.E",
	"4PSK.200.100S.O",
	"8PSK.200.100.E",
	"8PSK.200.100.O",

	// 1 Car 16QAM Data mode 200 Hz BW, 100 baud

	"16QAM.200.100.E",  // 46
	"16QAM.200.100.O",  // 47

	// 1 Car 4FSK Data mode 200 HzBW, 50 baud

	"4FSK.200.50S.E",  // 48
	"4FSK.200.50S.O",
	"4FSK.500.100.E",
	"4FSK.500.100.O",
	"4FSK.500.100S.E",
	"4FSK.500.100S.O",
	"",
	"",

	// 2 Car PSK Data Modes 100 baud

	"4PSK.500.100.E",  // 50
	"4PSK.500.100.O",
	"8PSK.500.100.E",
	"8PSK.500.100.O",

	// 2 Car Data modes 16 QAM baud

	"16QAM.500.100.E",  // 54
	"16QAM.500.100.O",
	"", "",  // 56, 57 were 500 167 modes

	"", "", "", "",  // 58 -5B
	"", "", "", "",  // 5C-5F

	// 1 Khz Bandwidth Data Modes
	// 4 Car 100 baud PSK
	"4PSK.1000.100.E",  // 60
	"4PSK.1000.100.O",
	"8PSK.1000.100.E",
	"8PSK.1000.100.O",
	"16QAM.1000.100.E",
	"16QAM.1000.100.O",
	"",
	"",
	"",  // 68
	"", "", "", "", "", "", "",

	// 2Khz Bandwidth Data Modes
	// 8 Car 100 baud PSK
	"4PSK.2000.100.E",  // 70
	"4PSK.2000.100.O",
	"8PSK.2000.100.E",
	"8PSK.2000.100.O",
	"16QAM.2000.100.E",
	"16QAM.2000.100.O",  // 75
	"", "", "", "",  // 76-79
	// 1 Car 4FSK 600 baud (FM only)
	"4FSK.2000.600.E",  // Experimental  // 7A
	"4FSK.2000.600.O",  // Experimental
	"4FSK.2000.600S.E",  // Experimental
	"4FSK.2000.600S.O",  // Experimental  // 7D
	"", "",  // 7e-7f
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",  // 80 - 8F
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",  // 90 - 9F
	// Frame Types 0xA0 to 0xDF reserved for experimentation
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",  // A0 - AF
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",  // B0 - BF
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",  // C0 - CF
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",  // D0 - DF

	// Data ACK  1 Car, 200Hz,4FSK
	"DataACK"  // Range 0xE0 to 0xFF includes 5 bits for quality
};

const char shortFrameType[256][12] = {
	"DataNAK",  // Range 0x00 to 0x1F includes 5 bits for quality 1 Car, 200Hz,4F
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
	"", "",

	// Short Control Frames 1 Car, 200Hz,4F. Reassigned May 22, 2015 for maximum "distance"

	"BREAK", "IDLE", "",
	"", "", "",
	"DISC", "", "",
	"END",
	"ConRejBusy",
	"ConRejBW",
	"",

	// Special frames 1 Car, 200Hz,4F 0x30 +

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
	"PingAck", "Ping", "",

	// 200 Hz Bandwidth Data
	// 1 Car P Data Modes 200 HzBW  100 baud

	"4P.200.100",  // 0x40
	"4P.200.100",
	"4P.200.100S",
	"4P.200.100S",
	"8P.200.100",
	"8P.200.100",

	// 1 Car 16QAM Data mode 200 Hz BW, 100 baud

	"16Q.200.100",  // 46
	"16Q.200.100",  // 47

	"4F.200.50S",  // 48
	"4F.200.50S",
	"4F.500.100",
	"4F.500.100",
	"4F.500.100S",
	"4F.500.100S",
	"",
	"",

	// 2 Car P Data Modes 100 baud
	"4P.500.100",  // 50
	"4P.500.100",
	"8P.500.100",
	"8P.500.100",

	// 2 Car Data modes 16 Q baud

	"16Q.500.100",  // 54
	"16Q.500.100",
	"", "",  // 56, 57 were 500 167 modes

	"",  // 58
	"",
	"",
	"",
	"",  // 5C
	"",  // 5D
	"", "",  // 5E/F

	// 1 Khz Bandwidth Data Modes
	// 4 Car 100 baud P
	"4P.1K.100",  // 60
	"4P.1K.100",
	"8P.1K.100",
	"8P.1K.100",
	"16Q.1K.100",
	"16Q.1K.100",
	"",
	"",
	// 2 Car 4F 100 baud
	"",  // 68
	"", "", "", "", "", "", "",

	// 2Khz Bandwidth Data Modes
	// 8 Car 100 baud P
	"4P.2K.100",  // 70
	"4P.2K.100",
	"8P.2K.100",
	"8P.2K.100",
	"16Q.2K.100",
	"16Q.2K.100",
	"",
	"",
	// 4 Car 4F 100 baud
	"",
	"",
	// 1 Car 4F 600 baud (FM only)
	"4F.2K.600",  // Experimental
	"4F.2K.600",  // Experimental
	"4F.2K.600S",  // Experimental
	"4F.2K.600S",  // Experimental  // 7d
	"", "",  // 7e-7f
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",  // 80-8F
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",  // 90-9F
	// Frame Types 0xA0 to 0xDF reserved for experimentation
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",  // A0-AF
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",  // B0-BF
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",  // C0-CF
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",  // D0-DF
	// Data ACK  1 Car, 200Hz,4F
	"DataACK"  // Range 0xE0 to 0xFF includes 5 bits for quality
};


char * strlop(char * buf, char delim)
{
	// Terminate buf at delim, and return rest of string
	char * ptr = strchr(buf, delim);

	if (ptr == NULL)
		return NULL;
	*(ptr)++=0;
	return ptr;
}

void GetSemaphore()
{
}

void FreeSemaphore()
{
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

void setProtocolMode(char* strMode)
{
	if (strcmp(strMode, "ARQ") == 0)
	{
		ZF_LOGI("Setting ProtocolMode to ARQ.");
		ProtocolMode = ARQ;
	}
	else
	if (strcmp(strMode, "RXO") == 0)
	{
		ZF_LOGI("Setting ProtocolMode to RXO.");
		ProtocolMode = RXO;
	}
	else
	if (strcmp(strMode, "FEC") == 0)
	{
		ZF_LOGI("Setting ProtocolMode to FEC.");
		ProtocolMode = FEC;
	}
	else
	{
		ZF_LOGW("WARNING: Invalid argument to setProtocolMode.  %s given, but expected one of ARQ, RXO, or FEC.  Setting ProtocolMode to ARQ as a default.", strMode);
		ProtocolMode = ARQ;
		return;
	}
	// CLear MEM ARQ Stuff
	ResetMemoryARQ();

	wg_send_protocolmode(0);
}

void ardopmain()
{
	char *nextHostCommand = HostCommands;

	// For testing purposes, it may be useful to run ardopcf repeatedly (and
	// very quickly) to generate recordings of "random" test frames.  For
	// these test frames to differ from each other, it is necessary to seed
	// the pseudo-random number generator.  Typically, this is done using
	// srand(time(0)).  However, this value changes only once per second, so
	// running ardopcf more than once per second would produce duplicate
	// values.  So, the following is used to for a more rapidly changing
	// seed value.
	struct timeval t1;
	gettimeofday(&t1, NULL);
	srand(t1.tv_usec + t1.tv_sec);

	blnTimeoutTriggered = FALSE;
	SetARDOPProtocolState(DISC);

	if (!InitSound())
	{
		ZF_LOGF("Error in InitSound().  Stopping ardop.");
		return;
	}

	TCPHostInit();
	WebguiInit();

	tmrPollOBQueue = Now + 10000;

	if (InitRXO)
		setProtocolMode("RXO");
	else
		setProtocolMode("ARQ");

	if (DeprecationWarningsIssued) {
		ZF_LOGE(
			"*********************************************************************\n"
			"* WARNING: DEPRECATED command line parameters used.  Details shown  *\n"
			"* above.  You may need to scroll up or review the Debug Log file to *\n"
			"* see those details                                                 *\n"
			"*********************************************************************\n");
	}
	while(!blnClosing)
	{
		if (nextHostCommand != NULL) {
			// Process the next host command from the --hostcommands
			// command line argument.
			char *thisHostCommand = nextHostCommand;
			nextHostCommand = strlop(nextHostCommand, ';');
			if (thisHostCommand[0] != 0x00)
				// not an empty string
				ProcessCommandFromHost(thisHostCommand);
		}
		PollReceivedSamples();
		WebguiPoll();
		if (ProtocolMode != RXO)
		{
			CheckTimers();
			TCPHostPoll();
			MainPoll();
		}
		PlatformSleep(10);
	}

	if (closedByPosixSignal) {
		ZF_LOGI(
			"Terminating on signal: %s",
			PlatformSignalAbbreviation(closedByPosixSignal)
		);
	}

	closesocket(TCPControlSock);
	closesocket(TCPDataSock);
	return;
}

// Subroutine to generate 1 symbol of leader

// returns pointer to Frame Type Name

const char * Name(UCHAR bytID)
{
	if (bytID <= DataNAKmax)
		return strFrameType[0];
	else if (bytID >= DataACKmin)
		return strFrameType[DataACKmin];
	else
		return strFrameType[bytID];
}

// returns pointer to Frame Type Name

const char * shortName(UCHAR bytID)
{
	if (bytID <= DataNAKmax)
		return shortFrameType[0];
	else if (bytID >= DataACKmin)
		return shortFrameType[DataACKmin];
	else
		return shortFrameType[bytID];
}

// Function to look up frame info from bytFrameType

BOOL FrameInfo(UCHAR bytFrameType, int * blnOdd, int * intNumCar, char * strMod,
	int * intBaud, int * intDataLen, int * intRSLen, UCHAR * bytQualThres, char * strType)
{
	// Used to "lookup" all parameters by frame Type.
	// returns TRUE if all fields updated otherwise FALSE (improper bytFrameType)

	// 1 Carrier 4FSK control frames

	if ((bytFrameType >= DataNAKmin &&  bytFrameType <= DataNAKmax) || bytFrameType >= DataACKmin)
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
		case BREAK:  // 0x23
		case IDLEFRAME:  // 0x24
		case DISCFRAME:  // 0x29

			*blnOdd = (1 & bytFrameType) != 0;
			*intNumCar = 1;
			*intDataLen = 0;
			*intRSLen = 0;
			strcpy(strMod, "4FSK");
			*intBaud = 50;
			*bytQualThres = 60;
			break;

		case END:  // 0x2C
		case ConRejBusy:  // 0x2D
		case ConRejBW:  // 0x2E

			*blnOdd = (1 & bytFrameType) != 0;
			*intNumCar = 1;
			*intDataLen = 0;
			*intRSLen = 0;
			strcpy(strMod, "4FSK");
			*intBaud = 50;
			*bytQualThres = 60;
			break;

		case IDFRAME:  // 0x30
		case ConReq200M:  // 0x31
		case ConReq500M:
		case ConReq1000M:
		case ConReq2000M:
		case ConReq200F:
		case ConReq500F:
		case ConReq1000F:
		case ConReq2000F:  // 0x36
		case PING:  // 0x3E

			*blnOdd = (1 & bytFrameType) != 0;
			*intNumCar = 1;
			*intDataLen = 12;
			*intRSLen = 4;  // changed 0.8.0
			strcpy(strMod, "4FSK");
			*intBaud = 50;
			*bytQualThres = 50;
			break;

		case ConAck200:  // 0x39
		case ConAck500:
		case ConAck1000:
		case ConAck2000:  // 0x3C
		case PINGACK:  // 0x3D

			*blnOdd = (1 & bytFrameType) != 0;
			*intNumCar = 1;
			*intDataLen = 3;
			*intRSLen = 0;
			strcpy(strMod, "4FSK");
			*intBaud = 50;
			*bytQualThres = 50;
			break;

		case DataACKmin:  // 0xE0

			*blnOdd = (1 & bytFrameType) != 0;
			*intNumCar = 1;
			*intDataLen = 0;
			*intRSLen = 0;
			strcpy(strMod, "4FSK");
			*intBaud = 50;
			*bytQualThres = 60;
			break;

		// 1 Carrier Data modes
		// 100 baud PSK (Note 1 carrier modes Qual Threshold reduced to 30 (was 50) for testing April 20, 2015

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

		// 4 Carrier Data Modes
		// 100 baud

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
		// 100 baud

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

		default:
			ZF_LOGE("No data for frame type = 0x%02hhx", bytFrameType);
			return FALSE;
		}
	}

	if (bytFrameType >= DataNAKmin && bytFrameType <= DataNAKmax)
		strcpy(strType,strFrameType[DataNAKmin]);
	else
		if (bytFrameType >= DataACKmin)
			strcpy(strType,strFrameType[DataACKmin]);
		else
			strcpy(strType,strFrameType[bytFrameType]);

	return TRUE;
}

int MaxErrors = 0;

//	Main RS decode function

extern int index_of[];
extern int recd[];
int Corrected[256];
extern int tt;  // number of errors that can be corrected
extern int kk;  // Info Symbols

BOOL blnErrorsCorrected;


// Function to encode data for all PSK frame types

int EncodePSKData(UCHAR bytFrameType, UCHAR * bytDataToSend, int Length, unsigned char * bytEncodedBytes)
{
	// Objective is to use this to use this to send all PSK data frames
	// Output is a byte array which includes:
	//  1) A 2 byte Header which include the Frame ID.
	//    This will be sent using 4FSK at 50 baud.
	//    It will include the Frame ID and ID Xored by the Session bytID.
	//  2) n sections one for each carrier that will inlcude all data (with FEC appended) for the entire frame.
	//    Each block will be identical in length.
	//  Initial implementation:
	//    intNum Car may be 1, 2, 4 or 8
	//    intBaud may be 100, 167
	//    intPSKMode may be 4 (4PSK) or 8 (8PSK)
	//    bytDataToSend must be equal to or less than max data supported by the frame or a exception will be logged and an empty array returned

	// First determine if bytDataToSend is compatible with the requested modulation mode.

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
		// Logs.Exception("[EncodeFSKFrameType] Failure to update parameters for frame type H" & Format(bytFrameType, "X") & "  DataToSend Len=" & bytDataToSend.Length.ToString)
		return 0;
	}

	// Generate the 2 bytes for the frame type data:

	bytEncodedBytes[0] = bytFrameType;
	bytEncodedBytes[1] = bytFrameType ^ bytSessionID;

	bytDataToSendLengthPtr = 0;
	intEncodedDataPtr = 2;

	// Now compute the RS frame for each carrier in sequence and move it to bytEncodedBytes

	for (i = 0; i < intNumCar; i++)  // across all carriers
	{
		memset(bytToRS, 0x00, intDataLen + 3 + intRSLen);  // prefill with zeros
		intCarDataCnt = Length - bytDataToSendLengthPtr;

		if (intCarDataCnt > intDataLen)  // why not > ??
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

		GenCRC16FrameType(bytToRS, intDataLen + 1, bytFrameType);  // calculate the CRC on the byte count + data bytes

		// Append Reed Solomon codes to end of frame data
		if (rs_append(bytToRS, intDataLen + 3, intRSLen) != 0) {
			ZF_LOGE("ERROR in EncodePSKData(): rs_append() failed.");
			return (-1);
		}

		// Need: (2 bytes for Frame Type) +( Data + RS + 1 byte byteCount + 2 Byte CRC per carrier)

		intEncodedDataPtr += intDataLen + 3 + intRSLen;

		bytToRS += intDataLen + 3 + intRSLen;
	}
	return intEncodedDataPtr;
}


// Function to encode data for all FSK frame types

int EncodeFSKData(UCHAR bytFrameType, UCHAR * bytDataToSend, int Length, unsigned char * bytEncodedBytes)
{
	// Objective is to use this to use this to send all 4FSK data frames
	// Output is a byte array which includes:
	//  1) A 2 byte Header which include the Frame ID.
	//    This will be sent using 4FSK at 50 baud.
	//    It will include the Frame ID and ID Xored by the Session bytID.
	//  2) n sections one for each carrier that will inlcude all data (with FEC appended) for the entire frame.
	//    Each block will be identical in length.
	//  Initial implementation:
	//    intNum Car may be 1, 2, 4 or 8
	//    intBaud may be 50, 100
	//    strMod is 4FSK)
	//    bytDataToSend must be equal to or less than max data supported by the frame or a exception will be logged and an empty array returned

	// First determine if bytDataToSend is compatible with the requested modulation mode.

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
		// Logs.Exception("[EncodeFSKFrameType] Failure to update parameters for frame type H" & Format(bytFrameType, "X") & "  DataToSend Len=" & bytDataToSend.Length.ToString)
		return 0;
	}

	// Generate the 2 bytes for the frame type data:

	bytEncodedBytes[0] = bytFrameType;
	bytEncodedBytes[1] = bytFrameType ^ bytSessionID;

	// Dim bytToRS(intDataLen + 3 - 1) As Byte Data + Count + 2 byte CRC

	bytDataToSendLengthPtr = 0;
	intEncodedDataPtr = 2;

	if (intBaud < 600 || intDataLen < 600)
	{
		// Now compute the RS frame for each carrier in sequence and move it to bytEncodedBytes

		for (i = 0; i < intNumCar; i++)  // across all carriers
		{
			memset(bytToRS, 0x00, intDataLen + 3 + intRSLen);  // prefill with zeros
			intCarDataCnt = Length - bytDataToSendLengthPtr;

			if (intCarDataCnt >= intDataLen)  // why not > ??
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

			GenCRC16FrameType(bytToRS, intDataLen + 1, bytFrameType);  // calculate the CRC on the byte count + data bytes

			// Append Reed Solomon codes to end of frame data
			if (rs_append(bytToRS, intDataLen + 3, intRSLen) != 0) {
				ZF_LOGE("ERROR in EncodeFSKData(): rs_append() failed.");
				return (-1);
			}

			// Need: (2 bytes for Frame Type) +( Data + RS + 1 byte byteCount + 2 Byte CRC per carrier)

			intEncodedDataPtr += intDataLen + 3 + intRSLen;

			bytToRS += intDataLen + 3 + intRSLen;
		}
		return intEncodedDataPtr;
	}

	// special case for 600 baud 4FSK which has 600 byte data field sent as three sequencial (200 byte + 50 byte RS) groups

	for (i = 0; i < 3; i++)		 // for three blocks of RS data
	{
		memset(bytToRS, 0x00, intDataLen / 3  + 3 + intRSLen / 3);  // prefill with zeros
		intCarDataCnt = Length - bytDataToSendLengthPtr;

		if (intCarDataCnt >= intDataLen /3 )  // why not > ??
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
		GenCRC16FrameType(bytToRS, intDataLen / 3 + 1, bytFrameType);  // calculate the CRC on the byte count + data bytes

		// Append Reed Solomon codes to end of frame data
		if (rs_append(bytToRS, intDataLen / 3 + 3, intRSLen / 3) != 0) {
			ZF_LOGE("ERROR in EncodePSKData() for 600 baud frame: rs_append() failed.");
			return (-1);
		}

		intEncodedDataPtr += intDataLen / 3  + 3 + intRSLen / 3;
		bytToRS += intDataLen / 3  + 3 + intRSLen / 3;
	}
	return intEncodedDataPtr;
}

// Function to encode ConnectRequest frame

int EncodeARQConRequest(const StationId* mycall, const StationId* target, enum _ARQBandwidth ARQBandwidth, UCHAR * bytReturn)
{
	// Encodes a 4FSK 200 Hz BW Connect Request frame ( ~ 1950 ms with default leader/trailer)

	if (!stationid_ok(mycall)) {
		ZF_LOGE("Unable to send connection request: MYCALL is unset");
		return 0;
	}

	if (!stationid_ok(target)) {
		ZF_LOGE("Unable to send connection request: TARGET is unset");
		return 0;
	}

	UCHAR * bytToRS= &bytReturn[2];

	if (ARQBandwidth == B200MAX)
		bytReturn[0] = ConReq200M;
	else if (ARQBandwidth == B500MAX)
		bytReturn[0] = ConReq500M;
	else if (ARQBandwidth == B1000MAX)
		bytReturn[0] = ConReq1000M;
	else if (ARQBandwidth == B2000MAX)
		bytReturn[0] = ConReq2000M;

	else if (ARQBandwidth == B200FORCED)
		bytReturn[0] = ConReq200F;
	else if (ARQBandwidth == B500FORCED)
		bytReturn[0] = ConReq500F;
	else if (ARQBandwidth == B1000FORCED)
		bytReturn[0] = ConReq1000F;
	else if (ARQBandwidth == B2000FORCED)
		bytReturn[0] = ConReq2000F;
	else
	{
		// Logs.Exception("[EncodeModulate.EncodeFSK500_1S] Bandwidth error.  Bandwidth = " & strBandwidth)
		return 0;
	}

	bytReturn[1] = bytReturn[0] ^ 0xFF;  // Connect Request always uses session ID of 0xFF

	// Modified May 24, 2015 to use RS instead of 2 byte CRC. (same as ID frame)

	bool ok = true;
	ok &= stationid_to_buffer(mycall, &bytToRS[0]);
	ok &= stationid_to_buffer(target, &bytToRS[sizeof(Packed6)]);
	if (! ok)
		return 0;

	// Append Reed Solomon codes to end of frame data
	if (rs_append(bytToRS, 12, 4) != 0) {
		ZF_LOGE("ERROR in EncodeARQConRequest(): rs_append() failed.");
		return (-1);
	}

	return 18;  // 2 bytes for FrameType + 12 bytes data + 4 bytes RS
}

int EncodePing(const StationId* mycall, const StationId* target, UCHAR * bytReturn)
{
	// Encodes a 4FSK 200 Hz BW Ping frame ( ~ 1950 ms with default leader/trailer)

	UCHAR * bytToRS= &bytReturn[2];

	if (!stationid_ok(mycall)) {
		ZF_LOGE("Unable to send ping: MYCALL is unset");
		return 0;
	}

	if (!stationid_ok(target)) {
		ZF_LOGE("Unable to send ping: TARGET is unset");
		return 0;
	}

	bytReturn[0] = PING;
	bytReturn[1] = bytReturn[0] ^ 0xFF;  // Ping always uses session ID of &HFF

	bool ok = true;
	ok &= stationid_to_buffer(mycall, &bytToRS[0]);
	ok &= stationid_to_buffer(target, &bytToRS[sizeof(Packed6)]);
	if (!ok)
		return 0;

	// Append Reed Solomon codes to end of frame data
	if (rs_append(bytToRS, 12, 4) != 0) {
		ZF_LOGE("ERROR in EncodePing(): rs_append() failed.");
		return (-1);
	}

	return 18;  // 2 bytes for FrameType + 12 bytes data + 4 bytes RS
}



int Encode4FSKIDFrame(const StationId* callsign, const Locator* square, unsigned char * bytreturn)
{
	// Function to encodes ID frame
	// returns length of encoded message

	UCHAR * bytToRS= &bytreturn[2];

	if (!stationid_ok(callsign))
	{
		ZF_LOGE("Unable to send ID frame: MYCALL is unset");
		return 0;
	}

	bytreturn[0] = IDFRAME;
	bytreturn[1] = IDFRAME ^ 0xFF;

	// Modified May 9, 2015 to use RS instead of 2 byte CRC.
	if (!stationid_to_buffer(callsign, &bytToRS[0]))
		return 0;

	memcpy(&bytToRS[PACKED6_SIZE], locator_as_bytes(square), PACKED6_SIZE);

	// Append Reed Solomon codes to end of frame data
	if (rs_append(bytToRS, 12, 4) != 0) {
		ZF_LOGE("ERROR in Encode4FSKIDFrame(): rs_append() failed.");
		return (-1);
	}

	return 18;  // 2 bytes for FrameType + 12 bytes data + 4 bytes RS
}

// Funtion to encodes a short 4FSK 50 baud Control frame  (2 bytes total) BREAK, END, DISC, IDLE, ConRejBusy, ConRegBW

int Encode4FSKControl(UCHAR bytFrameType, UCHAR bytSessionID, UCHAR * bytreturn)
{
	// Encodes a short control frame (normal length ~320 ms with default 160 ms leader+trailer)

	// If IsShortControlFrame(intFrameCode) Then
	//        Logs.Exception("[EncodeModulate.EncodeFSKControl] Illegal control frame code: H" & Format(intFrameCode, "X"))
	//        return Nothing
	//    End If

	bytreturn[0] = bytFrameType;
	bytreturn[1] = bytFrameType ^ bytSessionID;

	return 2;  // Length
}

// Function to encode a CONACK frame with Timing data  (6 bytes total)

int EncodeConACKwTiming(UCHAR bytFrameType, int intRcvdLeaderLenMs, UCHAR bytSessionID, UCHAR * bytreturn)
{
	// Encodes a Connect ACK with one byte Timing info. (Timing info repeated 2 times for redundancy)

	// If intFrameCode < ConAckmin Or intFrameCode > ConAckmax Then
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

// Function to encode a PingAck frame with Quality Data  (5 bytes total)

int EncodePingAck(int bytFrameType, int intSN, int intQuality, UCHAR * bytreturn)
{
	// Encodes a Ping ACK with one byte of S:N and Quality info ( info repeated 2 times for redundancy)

	bytreturn[0] = bytFrameType;
	bytreturn[1] = bytFrameType ^ 0xff;

	if (intSN >= 21)
		bytreturn[2] = 0xf8;  // set to MAX level indicating >= 21dB
	else
		bytreturn[2] = ((intSN + 10) & 0x1F) << 3;  // Upper 5 bits are S:N 0-31 corresponding to -10 to 21 dB   (5 bits S:N, 3 bits Quality

	bytreturn[2] += max(0, (intQuality - 30) / 10) & 7;  // Quality is lower 3 bits value 0 to 7 representing 30-100
	bytreturn[3] = bytreturn[2];
	bytreturn[4] = bytreturn[2];

	return 5;
}


// Function to encode an ACK control frame  (2 bytes total) ...with 5 bit Quality code

int EncodeDATAACK(int intQuality, UCHAR bytSessionID, UCHAR * bytreturn)
{
	// Encodes intQuality and DataACK frame (normal length ~320 ms with default leader/trailer)

	int intScaledQuality;

	if (intQuality > 100)
		intQuality = 100;

	intScaledQuality = max(0, (intQuality / 2) - 19);  // scale quality value to fit 5 bit field of 0 represents Q <= of 38 (pretty poor)

	bytreturn[0] = DataACKmin + intScaledQuality;  // ACKs 0xE0 - 0xFF
	bytreturn[1] = bytreturn[0] ^ bytSessionID;

	return 2;
}

// Function to encode a NAK frame  (2 bytes total) ...with 5 bit Quality code

int EncodeDATANAK(int intQuality , UCHAR bytSessionID, UCHAR * bytreturn)
{
	// Encodes intQuality and DataACK frame (normal length ~320 ms with default leader/trailer)

	int intScaledQuality;

	intScaledQuality = max(0, (intQuality / 2) - 19);  // scale quality value to fit 5 bit field of 0 represents Q <= of 38 (pretty poor)

	bytreturn[0] = intScaledQuality;  // NAKS 00 - 0x1F
	bytreturn[1] = bytreturn[0] ^ bytSessionID;

	return 2;
}

// SendID() should be the only function that sends an IDFrame.
// If id is not null, send it, else use global Callsign.
// always use global GridSquare.
// always use global wantCWID to determine whether CWID is also sent.
// reason is used to log info about why SendID is being called.
// Return true on success or false if no ID was sent.
bool SendID(const StationId * id, char * reason)
{
	const StationId *id_to_send = &Callsign;  // default to Callsign if id not provided
	unsigned char bytIDSent[80];
	int Len;

	// Scheduler needs to ensure this isnt called if already playing
	if (SoundIsPlaying) {
		// LastIDFrameTime is not reset.  So, this doesn't cancel sending the
		// IDFrame, it only delays it.
		ZF_LOGD("Don't send ID now because SoundIsPlaying");
		return false;
	}

	if (id != NULL && stationid_ok(id))
		id_to_send = id;

	if (!stationid_ok(id_to_send)) {
		// This should not happen (so it is logged as an ERROR).  Host/Gui
		// commands to transmit are designed to fail if Callsign is not set and
		// automatic transmissions in response to received frames (ConReq ->
		// ConAck, Ping -> PingAck) only respond to frames whose target callsign
		// matches Callsign or a value in AuxCalls.
		// If this occurs, it could result in a bad loop condition as repeated
		// attempts to send an IDFrame fail.  So, reset tmrFinalID and
		// LastIDFrameTime to prevent that from happening.
		ZF_LOGE(
			"SendID() called with an invalid StationId and fallback to MYCALL"
			" is also invalid!");
		tmrFinalID = 0;
		LastIDFrameTime = 0;
		return false;
	}
	Len = snprintf(bytIDSent, sizeof(bytIDSent), " %s:[%s] ", id_to_send->str, GridSquare.grid);
	ZF_LOGD("SendID %s %s", bytIDSent, reason);

	if ((EncLen = Encode4FSKIDFrame(id_to_send, &GridSquare, bytEncodedBytes)) <= 0) {
		ZF_LOGE("ERROR: In SendID() Invalid EncLen (%d).", EncLen);
		return false;
	}

	AddTagToDataAndSendToHost(bytIDSent, "IDF", Len);

	Mod4FSKDataAndPlay(IDFRAME, &bytEncodedBytes[0], EncLen, 0);  // only returns when all sent

	if (wantCWID)
		sendCWID(id_to_send);

	// tmrFinalID and LastIDFrameTime are reset after Mod4FSKDataAndPlay and
	// sendCWID() are done.  Setting them to zero indicates that no
	// transmissions have been made since the last IDFrame was sent.
	tmrFinalID = 0;
	LastIDFrameTime = 0;

	return true;
}

// Function to generate a 5 second burst of two tone (1450 and 1550 Hz) used for setting up drive level

void Send5SecTwoTone()
{
	initFilter(200, 1500);
	GetTwoToneLeaderWithSync(250);
//	SampleSink(0);  // 5 secs
	SoundFlush();
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

	int intRegister = 0xffff;  // intSeed
	int i, j;
	int Bit;
	int intPoly = 0x8810;  // This implements the CRC polynomial  x^16 + x^12 +x^5 + 1

	for (j = 0; j < length; j++)
	{
		int Mask = 0x80;  // Top bit first

		for (i = 0; i < 8; i++)  // for each bit processing MS bit first
		{
			Bit = Data[j] & Mask;
			Mask >>= 1;

			if (intRegister & 0x8000)  // Then the MSB of the register is set
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

// Function to compute a 16 bit CRC value and append it to the Data... With LS byte XORed by bytFrameType

void GenCRC16FrameType(char * Data, int Length, UCHAR bytFrameType)
{
	unsigned int CRC = GenCRC16(Data, Length);

	// Put the two CRC bytes after the stop index

	Data[Length++] = (CRC >> 8);  // MS 8 bits of Register
	Data[Length] = (CRC & 0xFF) ^ bytFrameType;  // LS 8 bits of Register
}

// Function to compute a 16 bit CRC value and check it against the last 2 bytes of Data (the CRC) XORing LS byte with bytFrameType..

BOOL  CheckCRC16FrameType(unsigned char * Data, int Length, UCHAR bytFrameType)
{
	// returns TRUE if CRC matches, else FALSE
	// For  CRC-16-CCITT =    x^16 + x^12 +x^5 + 1  intPoly = 1021 Init FFFF
	// intSeed is the seed value for the shift register and must be in the range 0-0xFFFF

	unsigned int CRC = GenCRC16(Data, Length);

	// Compare the register with the last two bytes of Data (the CRC)

	if ((CRC >> 8) == Data[Length])
		if (((CRC & 0xFF) ^ bytFrameType) == Data[Length + 1])
			return TRUE;

	return FALSE;
}

// Function to get intDataLen bytes from outbound queue (bytDataToSend)

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


void RemoveDataFromQueue(int Len)
{
	char HostCmd[32];

	if (Len == 0)
		return;

	// Called when ACK received, or on FEC send

	GetSemaphore();

	if (Len > bytDataToSendLength)
		Len = bytDataToSendLength;  // Shouldn't happen, unless the Q is cleared

	bytDataToSendLength -= Len;

	if (bytDataToSendLength > 0)
		memmove(bytDataToSend, &bytDataToSend[Len], bytDataToSendLength);

	FreeSemaphore();

	if (bytDataToSendLength == 0)
		SetLED(TRAFFICLED, FALSE);

	snprintf(HostCmd, sizeof(HostCmd), "BUFFER %d", bytDataToSendLength);
	QueueCommandToHost(HostCmd);
}

// Timer Rotines

void CheckTimers()
{
	// Check for Timeout after a send that needs to be repeated

	if ((blnEnbARQRpt || blnDISCRepeating) && Now > dttNextPlay)
	{
		// No response Timeout

		if (GetNextFrame())
		{
			// I think this only returns TRUE if we have to repeat the last

			//	Repeat mechanism for normal repeated FEC or ARQ frames

			ZF_LOGI("[Repeating Last Frame]");
			RemodulateLastFrame();
		}
		else
			// I think this means we have exceeded retries or had an abort

			blnEnbARQRpt = FALSE;
	}


	// Event triggered by tmrSendTimeout elapse. Ends an ARQ session and sends a DISC frame

	if (tmrSendTimeout && Now > tmrSendTimeout)
	{
		char HostCmd[80];

		// (Handles protocol rule 1.7)

		tmrSendTimeout = 0;

		// Dim dttStartWait As Date = Now
		// While objMain.blnLastPTT And Now.Subtract(dttStartWait).TotalSeconds < 10
		//  Thread.Sleep(50)
		// End While

		ZF_LOGD("ARDOPprotocol.tmrSendTimeout]  ARQ Timeout from ProtocolState: %s Going to DISC state", ARDOPStates[ProtocolState]);

		// Confirmed proper operation of this timeout and rule 4.0 May 18, 2015
		// Send an ID frame (Handles protocol rule 4.0)

		// SendID will default to Callsign if ARQStationFinalId is not valid.
		SendID(&ARQStationFinalId, "ARQ Timeout");

		if (AccumulateStats)
			LogStats();

		QueueCommandToHost("DISCONNECTED");

		snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ Timeout from Protocol State:  %s", ARDOPStates[ProtocolState]);
		QueueCommandToHost(HostCmd);
		blnEnbARQRpt = FALSE;
		// Thread.Sleep(2000)
		ClearDataToSend();

		if ((EncLen = Encode4FSKControl(DISCFRAME, bytSessionID, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In CheckTimers() sending DISC Invalid EncLen (%d).", EncLen);
			return;
		}
		Mod4FSKDataAndPlay(DISCFRAME, &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent

		intFrameRepeatInterval = 2000;
		SetARDOPProtocolState(DISC);

		InitializeConnection();  // reset all Connection data

		// Clear the mnuBusy status on the main form
		// Dim stcStatus As Status = Nothing
		// stcStatus.ControlName = "mnuBusy"
		// stcStatus.Text = "FALSE"
		// queTNCStatus.Enqueue(stcStatus)

		blnTimeoutTriggered = FALSE;  // prevents a retrigger
	}

	// Elapsed Subroutine for Pending timeout

	if (tmrIRSPendingTimeout && Now > tmrIRSPendingTimeout)
	{
		char HostCmd[80];

		tmrIRSPendingTimeout = 0;

		ZF_LOGD("ARDOPprotocol.tmrIRSPendingTimeout]  ARQ Timeout from ProtocolState: %s Going to DISC state",  ARDOPStates[ProtocolState]);

		QueueCommandToHost("DISCONNECTED");
		snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ CONNECT REQUEST TIMEOUT FROM PROTOCOL STATE: %s",ARDOPStates[ProtocolState]);

		QueueCommandToHost(HostCmd);

		blnEnbARQRpt = FALSE;
		ProtocolState = DISC;
		blnPending = FALSE;
		InitializeConnection();	 // reset all Connection data

		// Clear the mnuBusy status on the main form
		// Dim stcStatus As Status = Nothing
		// stcStatus.ControlName = "mnuBusy"
		// stcStatus.Text = "FALSE"
		// queTNCStatus.Enqueue(stcStatus)
	}

	// Subroutine for tmrFinalIDElapsed

	if (tmrFinalID && Now > tmrFinalID && !blnBusyStatus)
	{
		// SendID will default to Callsign if ARQStationFinalId is not valid.
		SendID(&ARQStationFinalId, "ARQ FinalID");
	}

	// Send Conect Request (from ARQCALL command)

	if (NeedConReq)
	{
		NeedConReq = 0;
		SendARQConnectRequest(&Callsign, &ConnectToCall);
	}
	if (NeedPing)
	{
		NeedPing = 0;
		SendPING(&Callsign, &ConnectToCall, PingCount);
	}

	// Send Async ID (from SENDID command)
	if (NeedID)
	{
		// This occurs from SENDID Host command, from Gui, and from StartFEC()
		// with blnSendID=true (due to FECSEND TRUE host command after FECID
		// TRUE HOST COMMAND)
		SendID(NULL, "Host/User requested");
		NeedID = 0;
	}

	if (NeedCWID)
	{
		sendCWID(&Callsign);
		NeedCWID = 0;
	}

	if (NeedTwoToneTest)
	{
		Send5SecTwoTone();
		NeedTwoToneTest = 0;
	}

	// In addition to other times when an IDFrame is sent by protocol (such as
	// after receiving and END frame), an IDFrame will be sent at least once
	// every 10 minutes if any transmissions have been made.  During an active
	// ARQ connection or in the FECSend state, the LastIDFrameTime is checked
	// in the code that handles those states so that the IDFrame is sent when it
	// fits well with other frames being sent and is not too disruptive.  The
	// following handles sending an IDFrame in the DISC state.  It will defer
	// sending an IDFrame while sending a series of PINGs, while actively trying
	// to decode a signal (State != SearchingForLeader), and while the busy
	// detector indicates that the frequency is is use.  To allow for such
	// delays, it is actually set to begin trying to send an IDFrame after only
	// 9 minutes.
	if (
		ProtocolState == DISC // Not ARQ connected or FECSend
		&& State == SearchingForLeader // not receiving an incoming frame
		&& !blnBusyStatus  // no other traffic on this frequency has been detected
		&& !blnPINGrepeating  // not sending a sequence of PING frames
		&& LastIDFrameTime != 0  // Some transmission has been since last IDFrame
		&& Now - LastIDFrameTime > 540000  // more than 9 minutes elapsed
	) {
		// 9 minutes since the first transmission after the last IDFrame was sent.
		SendID(NULL, "10 minute ID");
	}

	if (Now > tmrPollOBQueue)
	{
		tmrPollOBQueue = Now + 10000;  // 10 Secs
	}
}

// Main polling Function returns True or FALSE if closing

BOOL MainPoll() {
	// Checks to see if frame ready for playing

	if (!SoundIsPlaying && !blnEnbARQRpt && !blnDISCRepeating)  // Idle (check playing in case we call from txSleep())
	{
		if (GetNextFrame())
		{
			// As we will send the frame if one is available, and won't return
			// till it is all sent, I don't think I have to do anything here
		}
	}

	if	(blnClosing)  // Check for closing
		return FALSE;

	return TRUE;
}

int dttLastBusy;
int dttLastClear;
int dttStartRTMeasure;

// Function to extract bandwidth from ARQBandwidth

int ExtractARQBandwidth()
{
	return atoi(ARQBandwidths[ARQBandwidth]);
}

// Subroutine to update the Busy detector when not displaying Spectrum or Waterfall (graphics disabled)

int LastBusyCheck = 0;
extern UCHAR CurrentLevel;

#ifdef PLOTSPECTRUM
float dblMagSpectrum[206];
float dblMaxScale = 0.0f;
extern UCHAR Pixels[4096];
extern UCHAR * pixelPointer;
#endif

float bhWindow[513];
float wS1 = 0;
void generateBH() {
	for (int i = 0; i < 513; i++) {
		bhWindow[i] = 0.35875 - 0.48829 * cos(2 * M_PI * i / 1024)
			+ 0.14128 * cos(4 * M_PI * i / 1024) - 0.01168 * cos(6 * M_PI * i / 1024);
		wS1 += 2 * bhWindow[i];
	}
	// wS1 should only include one of terms 0, 512
	wS1 -= bhWindow[0] + bhWindow[512];
}

void UpdateBusyDetector(short * bytNewSamples)
{
	float windowedSamples[1024];
	float dblReF[1024];
	float dblImF[1024];
	float dblMag[206];
#ifdef PLOTSPECTRUM
	float dblMagMax = 0.0000000001f;
	float dblMagMin = 10000000000.0f;
#endif
	UCHAR Waterfall[256];  // Colour index values to send to GUI
	int clrTLC = Lime;  // Default Bandwidth lines on waterfall

	static BOOL blnLastBusyStatus;

	float dblMagAvg = 0;
	int intTuneLineLow, intTuneLineHi, intDelta;
	int i;

//	if (State != SearchingForLeader)
//		return;  // only when looking for leader

	if (ProtocolState != DISC)  // Only process busy when in DISC state
	{
		// Dont do busy, but may need waterfall or spectrum

		if ((WaterfallActive | SpectrumActive) == 0 && WebGuiNumConnected == 0)
			return;  // No waterfall or spectrum
	}

	if (Now - LastBusyCheck < 100)
		return;

	LastBusyCheck = Now;

	// Apply a Blackman-Harris window before doing FFT
	// This will decrease spectral leakage.  It also decreases the magnitude
	// of the FFT results by wS1/1024
	if (wS1 == 0)
		generateBH();
	windowedSamples[0] = bytNewSamples[0]*bhWindow[0];
	windowedSamples[512] = bytNewSamples[512]*bhWindow[512];
	for(i = 1; i < 512; i++) {
		windowedSamples[i] = bytNewSamples[i]*bhWindow[i];
		windowedSamples[1024 - i] = bytNewSamples[1024 - i]*bhWindow[i];
	}

	FourierTransform(1024, windowedSamples, &dblReF[0], &dblImF[0], FALSE);

	for (i = 0; i < 206; i++)
	{
		// starting at ~300 Hz to ~2700 Hz Which puts the center of the signal in the center of the window (~1500Hz)
		// bytNewSamples are sampled at 12000 samples per second, and a 1024 sample FFT was
		// used.  So dblReF[j] and dblImF[j] correspond to a frequency of j * 12000 / 1024
		// for 0 <= j < 512

		dblMag[i] = powf(dblReF[i + 25], 2) + powf(dblImF[i + 25], 2);	 // first pass
		dblMagAvg += dblMag[i];
#ifdef PLOTSPECTRUM
		dblMagSpectrum[i] = 0.2 * dblMag[i] + 0.8 * dblMagSpectrum[i];
		dblMagMax = max(dblMagMax, dblMagSpectrum[i]);
		dblMagMin = min(dblMagMin, dblMagSpectrum[i]);
#endif
	}

	intDelta = (ExtractARQBandwidth() / 2 + TuningRange) / 11.719f;

	intTuneLineLow = max((103 - intDelta), 3);
	intTuneLineHi = min((103 + intDelta), 203);

	if (ProtocolState == DISC)  // Only process busy when in DISC state
	{
		blnBusyStatus = BusyDetect3(dblMag, intTuneLineLow, intTuneLineHi);

		if (blnBusyStatus && !blnLastBusyStatus)
		{
			QueueCommandToHost("BUSY TRUE");
			newStatus = TRUE;  // report to PTC

			if (!WaterfallActive && !SpectrumActive)
			{
				UCHAR Msg[2];

				Msg[0] = blnBusyStatus;
				SendtoGUI('B', Msg, 1);
			}
			wg_send_busy(0, true);
		}
		// stcStatus.Text = "True"
		// queTNCStatus.Enqueue(stcStatus)
		// Debug.WriteLine("BUSY TRUE @ " & Format(DateTime.UtcNow, "HH:mm:ss"))

		else if (blnLastBusyStatus && !blnBusyStatus)
		{
			QueueCommandToHost("BUSY FALSE");
			newStatus = TRUE;  // report to PTC

			if (!WaterfallActive && !SpectrumActive)
			{
				UCHAR Msg[2];

				Msg[0] = blnBusyStatus;
				SendtoGUI('B', Msg, 1);
			}
			wg_send_busy(0, false);
		}
		// stcStatus.Text = "False"
		// queTNCStatus.Enqueue(stcStatus)
		// Debug.WriteLine("BUSY FALSE @ " & Format(DateTime.UtcNow, "HH:mm:ss"))

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
				objColor = Orange;  // Strongest spectral line
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
				Waterfall[i] = objColor;  // Else plot the pixel as received
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
		if (dblMagMin / dblMagMax < 0.0001)  // more than 10000:1 difference Max:Min
			dblMaxScale = max(dblMagMax, dblMaxScale * 0.9f);
		else
			dblMaxScale = max(10000 * dblMagMin, dblMagMax);

		clearDisplay();

		for (i = 0; i < 206; i++)
		{
		// The following provides some AGC over the spectrum to compensate for avg input level.

			float y1 = -0.25f * (SpectrumHeight - 1) *  log10f((max(dblMagSpectrum[i], dblMaxScale / 10000)) / dblMaxScale);  // range should be 0 to bmpSpectrumHeight -1
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
	wg_send_fftdata(dblMag, 206);
}

void SendPING(const StationId* mycall, const StationId* target, int intRpt)
{
	if ((EncLen = EncodePing(mycall, target, bytEncodedBytes)) <= 0) {
		ZF_LOGE("ERROR: In SendPING() Invalid EncLen (%d).", EncLen);
		return;
	}

	// generate the modulation with 2 x the default FEC leader length...Should insure reception at the target
	// Note this is sent with session ID 0xFF

	//	Set all flags before playing, as the End TX is called before we return here

	intFrameRepeatInterval = 2000;  // ms Finn reported 7/4/2015 that 1600 was too short ...need further evaluation but temporarily moved to 2000 ms
	blnEnbARQRpt = TRUE;

	Mod4FSKDataAndPlay(PING, &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent

	blnAbort = False;
	dttTimeoutTrip = Now;
	intRepeatCount = 1;
	intPINGRepeats = intRpt;
	blnPINGrepeating = True;
	dttLastPINGSent = Now;

	ZF_LOGD("[SendPING] MYCALL= %s TARGET=%s  Repeat=%d", mycall->str, target->str, intRpt);

	return;
}

// This sub processes a correctly decoded Ping frame, decodes it an passed to host for display if it doesn't duplicate the prior passed frame.

void ProcessPingFrame(char * _bytData)
{
	ZF_LOGD("ProcessPingFrame Protocol State = %s", ARDOPStates[ProtocolState]);

	if (ProtocolState == DISC)
	{
		if (blnListen && IsPingToMe(&LastDecodedStationCaller, &LastDecodedStationTarget) && EnablePingAck)
		{
			// Ack Ping
			ZF_LOGI("[ProcessPingFrame] PING from %s>%s S:N=%d Qual=%d Reply=1", LastDecodedStationCaller.str, LastDecodedStationTarget.str, stcLastPingintRcvdSN, stcLastPingintQuality);

			// An earlier version of this function would refuse to send a
			// PingAck if Callsign was not valid.  The use of tmrFinalID to send
			// an ID frame with lastDecodedStationTarget (which may have matched
			// a value in AuxCalls instead of Callsign) eliminated that need.

			if ((EncLen = EncodePingAck(PINGACK, stcLastPingintRcvdSN, stcLastPingintQuality, bytEncodedBytes)) <= 0) {
				ZF_LOGE("ERROR: In ProcessPingFrame() Invalid EncLen (%d).", EncLen);
				return;
			}
			Mod4FSKDataAndPlay(PINGACK, &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent

			SendCommandToHost("PINGREPLY");

			// The mechanism built around tmrFinalID and ARQStationFinalId is
			// used to send a station ID frame after an ARQ session has
			// finished.  Reuse this mechanism to also send an ID frame after
			// sending a PingAck, and to use the station ID that was the Ping's
			// target for this IDFrame.  This target ID may match either
			// Callsign or a value in AuxCalls.  Because of this, if the normal
			// 10 minute ID timer was used to ID after a PingAck, it might not
			// have used the same ID as the Ping's target.
			if (tmrFinalID != 0)
				// The only way that this would disrupt other use of tmrFinalID
				// or ARQStationFinalId is if tmrFinalID is already set.  This
				// is unlikely, but possible.  If it is, then do nothing and
				// accept the station ID that will be sent for it is sufficient.
				return;
			// Set ARQStationFinalId to contain the target callsign from the Ping.
			memcpy(&ARQStationFinalId, &LastDecodedStationTarget, sizeof(ARQStationFinalId));
			// The response to tmrFinalID will be deferred if the busy detector
			// indicates that the frequency is in use.  This will prevent the
			// ID frame being sent until the sequence of repeated Pings is done.
			// The sequence of Pings normally stops in response to the first
			// PingAck, but may continue if the sender of the Pings fails to
			// decode the PingAck.  So, set tmrFinalID to provide only a short
			// initial delay.  This works well because the hold time of the busy
			// detector is longer than the delay between repeated Ping frames.
			tmrFinalID = Now + 3000;
			return;
		}
	}
	ZF_LOGI("[ProcessPingFrame] PING from %s>%s S:N=%d Qual=%d Reply=0", LastDecodedStationCaller.str, LastDecodedStationTarget.str, stcLastPingintRcvdSN, stcLastPingintQuality);
	SendCommandToHost("CANCELPENDING");
}
