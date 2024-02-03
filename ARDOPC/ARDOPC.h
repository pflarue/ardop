

#ifndef ARDOPCHEADERDEFINED
#define ARDOPCHEADERDEFINED

extern const char ProductName[];
extern const char ProductVersion[];

//#define USE_SOUNDMODEM

//	Sound interface buffer size

#define SendSize 1200		// 100 mS for now
// #define ReceiveSize 512	// Must be 1024 for FFT (or we will need torepack frames)
#define NumberofinBuffers 4

// Host to TNC Buffer Size

#ifdef TEENSY
#define DATABUFFERSIZE 4096
#else
#define DATABUFFERSIZE 100000
#endif


#ifndef _WIN32_WINNT		// Allow use of features specific to Windows XP or later.                   
#define _WIN32_WINNT 0x0501	// Change this to the appropriate value to target other versions of Windows.
#endif						

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#define _CRT_SECURE_NO_DEPRECATE
#define _USE_32BIT_TIME_T

#ifndef WIN32
#define max(x, y) ((x) > (y) ? (x) : (y))
#define min(x, y) ((x) < (y) ? (x) : (y))
#endif

#ifdef WIN32
typedef void *HANDLE;
#else
#define HANDLE int
#endif

void txSleep(int mS);

unsigned int getTicks();

extern unsigned int pttOnTime;

#define Now getTicks()

// DebugLog Severity Levels 

#define LOGEMERGENCY 0 
#define LOGALERT 1
#define LOGCRIT 2 
#define LOGERROR 3 
#define LOGWARNING 4
#define LOGNOTICE 5
#define LOGINFO 6
#define LOGDEBUG 7
#define LOGDEBUGPLUS 8

#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#ifdef M_PI
#undef M_PI
#endif

#define M_PI       3.1415926f

#ifndef TEENSY
#ifndef WIN32
#define LINUX
#endif
#endif

#ifdef __ARM_ARCH
#ifndef TEENSY
#define ARMLINUX
#endif
#endif

#define UseGUI			// Enable GUI Front End Support

#ifndef TEENSY
#ifdef UseGUI

// Constellation and Waterfall for GUI interface

#define PLOTCONSTELLATION
#define PLOTWATERFALL
#define PLOTSPECTRUM
#endif
#endif
#define ConstellationHeight 90
#define ConstellationWidth 90
#define WaterfallWidth 205
#define WaterfallHeight 64
#define SpectrumWidth 205
#define SpectrumHeight 64


#define PLOTRADIUS 42
#define WHITE 0
#define Tomato 1
#define Gold 2
#define Lime 3	
#define Yellow 4
#define Orange 5
#define Khaki 6
#define Cyan 7
#define DeepSkyBlue 8
#define RoyalBlue 9
#define Navy 10
#define Black 11 
#define Goldenrod 12
#define Fuchsia 13




#include "ecc.h"				// RS Constants

typedef int BOOL;
typedef unsigned char UCHAR;

#define VOID void

#define FALSE 0
#define TRUE 1

#define False 0
#define True 1

// TEENSY Interface board equates

#ifdef TEENSY
#ifdef PIBOARD
#define ISSLED LED0
#else
#define ISSLED LED1
#endif
#define IRSLED LED1
#define TRAFFICLED LED2
#else
#define ISSLED 1
#define IRSLED 2
#define TRAFFICLED 3
#define PKTLED 4
#endif

BOOL KeyPTT(BOOL State);

UCHAR FrameCode(char * strFrameName);
BOOL FrameInfo(UCHAR bytFrameType, int * blnOdd, int * intNumCar, char * strMod,
   int * intBaud, int * intDataLen, int * intRSLen, UCHAR * bytQualThres, char * strType);

void ClearDataToSend();
int EncodeFSKData(UCHAR bytFrameType, UCHAR * bytDataToSend, int Length, unsigned char * bytEncodedBytes);
int EncodePSKData(UCHAR bytFrameType, UCHAR * bytDataToSend, int Length, unsigned char * bytEncodedBytes);
int Encode4FSKIDFrame(char * Callsign, char * Square, unsigned char * bytreturn);
int EncodeDATAACK(int intQuality, UCHAR bytSessionID, UCHAR * bytreturn);
int EncodeDATANAK(int intQuality , UCHAR bytSessionID, UCHAR * bytreturn);
void Mod4FSKDataAndPlay(int Type, unsigned char * bytEncodedBytes, int Len, int intLeaderLen);
void Mod4FSK600BdDataAndPlay(int Type, unsigned char * bytEncodedBytes, int Len, int intLeaderLen);
void Mod16FSKDataAndPlay(int Type, unsigned char * bytEncodedBytes, int Len, int intLeaderLen);
void Mod8FSKDataAndPlay(int Type, unsigned char * bytEncodedBytes, int Len, int intLeaderLen);
void ModPSKDataAndPlay(int Type, unsigned char * bytEncodedBytes, int Len, int intLeaderLen);
BOOL IsDataFrame(UCHAR intFrameType);
BOOL CheckValidCallsignSyntax(char * strTargetCallsign);
void StartCodec(char * strFault);
void StopCodec(char * strFault);
BOOL SendARQConnectRequest(char * strMycall, char * strTargetCall);
void AddDataToDataToSend(UCHAR * bytNewData, int Len);
BOOL StartFEC(UCHAR * bytData, int Len, char * strDataMode, int intRepeats, BOOL blnSendID);
void SendID(BOOL blnEnableCWID);
BOOL CheckGSSyntax(char * GS);
//void SetARDOPProtocolState(int value);
unsigned int GenCRC16(unsigned char * Data, unsigned short length);
void SendCommandToHost(char * Cmd);
void TCPSendCommandToHost(char * Cmd);
void SCSSendCommandToHost(char * Cmd);
void SendCommandToHostQuiet(char * Cmd);
void TCPSendCommandToHostQuiet(char * Cmd);
void SCSSendCommandToHostQuiet(char * Cmd);
void UpdateBusyDetector(short * bytNewSamples);
int UpdatePhaseConstellation(short * intPhases, short * intMags, char * strMod, BOOL blnQAM);
void SetARDOPProtocolState(int value);
BOOL BusyDetect3(float * dblMag, int intStart, int intStop);
void SendLogToHost(char * Msg, int len);

void displayState(const char * State);
void displayCall(int dirn, char * call);

void SampleSink(short Sample);
void SoundFlush();
void StopCapture();
void StartCapture();
void DiscardOldSamples();
void ClearAllMixedSamples();

void SetFilter(void * Filter());

void AddTrailer();
void CWID(char * strID, short * intSamples, BOOL blnPlay);
void sendCWID(char * Call, BOOL Play);
UCHAR ComputeTypeParity(UCHAR bytFrameType);
void GenCRC16FrameType(char * Data, int Length, UCHAR bytFrameType);
BOOL CheckCRC16FrameType(unsigned char * Data, int Length, UCHAR bytFrameType);
char * strlop(char * buf, char delim);
void QueueCommandToHost(char * Cmd);
void SCSQueueCommandToHost(char * Cmd);
void TCPQueueCommandToHost(char * Cmd);
void SendReplyToHost(char * strText);
void TCPSendReplyToHost(char * strText);
void SCSSendReplyToHost(char * strText);
void LogStats();
int GetNextFrameData(int * intUpDn, UCHAR * bytFrameTypeToSend, UCHAR * strMod, BOOL blnInitialize);
void SendData();
int ComputeInterFrameInterval(int intRequestedIntervalMS);
int Encode4FSKControl(UCHAR bytFrameType, UCHAR bytSessionID, UCHAR * bytreturn);
VOID WriteExceptionLog(const char * format, ...);
void SaveQueueOnBreak();
VOID Statsprintf(const char * format, ...);
VOID CloseDebugLog();
VOID CloseStatsLog();
void Abort();
void SetLED(int LED, int State);
VOID ClearBusy();
VOID CloseCOMPort(HANDLE fd);
VOID COMClearRTS(HANDLE fd);
VOID COMClearDTR(HANDLE fd);

//#ifdef WIN32
void ProcessNewSamples(short * Samples, int nSamples);
VOID Debugprintf(const char * format, ...);
VOID WriteDebugLog(int LogLevel, const char * format, ...);
void ardopmain();
BOOL GetNextFECFrame();
void GenerateFSKTemplates();
void printtick(char * msg);
void InitValidFrameTypes();
//#endif

extern void Generate50BaudTwoToneLeaderTemplate();
extern BOOL blnDISCRepeating;

BOOL DemodDecode4FSKID(UCHAR bytFrameType, char * strCallID, char * strGridSquare);
void DeCompressCallsign(char * bytCallsign, char * returned);
void DeCompressGridSquare(char * bytGS, char * returned);

int RSEncode(UCHAR * bytToRS, UCHAR * bytRSEncoded, int MaxErr, int Len);
BOOL RSDecode(UCHAR * bytRcv, int Length, int CheckLen, BOOL * blnRSOK);

void ProcessRcvdFECDataFrame(int intFrameType, UCHAR * bytData, BOOL blnFrameDecodedOK);
void ProcessUnconnectedConReqFrame(int intFrameType, UCHAR * bytData);
void ProcessRcvdARQFrame(UCHAR intFrameType, UCHAR * bytData, int DataLen, BOOL blnFrameDecodedOK);
void InitializeConnection();

void AddTagToDataAndSendToHost(UCHAR * Msg, char * Type, int Len);
void TCPAddTagToDataAndSendToHost(UCHAR * Msg, char * Type, int Len);
void SCSAddTagToDataAndSendToHost(UCHAR * Msg, char * Type, int Len);

void RemoveDataFromQueue(int Len);
void RemodulateLastFrame();

void GetSemaphore();
void FreeSemaphore();
const char * Name(UCHAR bytID);
const char * shortName(UCHAR bytID);
void InitSound();
void initFilter(int Width, int centerFreq);
void FourierTransform(int NumSamples, short * RealIn, float * RealOut, float * ImagOut, int InverseTransform);
VOID ClosePacketSessions();
VOID LostHost();
VOID ProcessPacketHostBytes(UCHAR * RXBuffer, int Len);
int ReadCOMBlock(HANDLE fd, char * Block, int MaxLength);
VOID ProcessDEDModeFrame(UCHAR * rxbuffer, unsigned int Length);
BOOL CheckForPktMon();
BOOL CheckForPktData();

int SendtoGUI(char Type, unsigned char * Msg, int Len);	
void DrawTXFrame(const char * Frame);
void DrawRXFrame(int State, const char * Frame);
#ifdef TEENSY
void mySetPixel(int16_t x, int16_t y, int16_t Colour);
#else
void mySetPixel(unsigned char x, unsigned char y, unsigned int Colour);
#endif

void clearDisplay();

extern int WaterfallActive;
extern int SpectrumActive;
extern unsigned int PKTLEDTimer;

extern char stcLastPingstrSender[10];
extern char stcLastPingstrTarget[10];
extern int stcLastPingintRcvdSN;
extern int stcLastPingintQuality;
extern time_t stcLastPingdttTimeReceived;

enum _ReceiveState		// used for initial receive testing...later put in correct protocol states
{
	SearchingForLeader,
	AcquireSymbolSync,
	AcquireFrameSync,
	AcquireFrameType,
	DecodeFrameType,
	AcquireFrame,
	DecodeFramestate
};

extern enum _ReceiveState State;

enum _ARQBandwidth
{
	B200FORCED,
	B500FORCED,
	B1000FORCED,
	B2000FORCED,
	B200MAX,
	B500MAX,
	B1000MAX,
	B2000MAX,
	UNDEFINED
};

extern enum _ARQBandwidth ARQBandwidth;
extern enum _ARQBandwidth CallBandwidth;
extern const char ARQBandwidths[9][12];

enum _ARDOPState
{
	OFFLINE,
	DISC,
	ISS,
	IRS,
	IDLE,     // ISS in quiet state ...no transmissions)
	IRStoISS, // IRS during transition to ISS waiting for ISS's ACK from IRS's BREAK
 	FECSend,
	FECRcv
};

extern enum _ARDOPState ProtocolState;

extern const char ARDOPStates[8][9];


// Enum of ARQ Substates

enum _ARQSubStates
{
	None,
	ISSConReq,
	ISSConAck,
	ISSData,
	ISSId,
	IRSConAck,
	IRSData,
	IRSBreak,
	IRSfromISS,
	DISCArqEnd
};

extern enum _ARQSubStates ARQState;

enum _ProtocolMode
{
	Undef,
	FEC,
	ARQ,
	RXO  // Receive Only.  Decode all possible frames while recovering SessionID.  ProtocolState should always be DISC
};

extern enum _ProtocolMode ProtocolMode;

extern enum _ARQSubStates ARQState;

struct SEM
{
	unsigned int Flag;
	int Clashes;
	int	Gets;
	int Rels;
};

extern struct SEM Semaphore;


#define BREAK 0x23
#define IDLEFRAME 0x24
#define DISCFRAME 0x29
#define END 0x2C
#define ConRejBusy 0x2D
#define ConRejBW 0x2E

#define ConAck200 0x39
#define ConAck500 0x3A
#define ConAck1000 0x3B
#define ConAck2000 0x3C
#define PINGACK 0x3D
#define PING 0x3E
#define PktFrameHeader 0xC0		// Variable length frame Header
#define PktFrameData 0xC1		// Variable length frame Data (Virtual Frsme Type)


extern const short intTwoToneLeaderTemplate[120];  // holds just 1 symbol (0 ms) of the leader
extern const short int50BaudTwoToneLeaderTemplate[240];  // holds just 1 symbol (20 ms) of the leader

extern const short intPSK100bdCarTemplate[9][4][120];	// The actual templates over 9 carriers for 4 phase values and 120 samples
    //   (only positive Phase values are in the table, sign reversal is used to get the negative phase values) This reduces the table size from 7680 to 3840 integers
extern const short intPSK200bdCarTemplate[9][4][72];		// Templates for 200 bd with cyclic prefix
extern const short intFSK25bdCarTemplate[16][480];		// Template for 16FSK carriers spaced at 25 Hz, 25 baud
extern const short intFSK50bdCarTemplate[4][240];		// Template for 4FSK carriers spaced at 50 Hz, 50 baud
extern const short intFSK100bdCarTemplate[20][120];		// Template for 4FSK carriers spaced at 100 Hz, 100 baud
extern const short intFSK600bdCarTemplate[4][20];		// Template for 4FSK carriers spaced at 600 Hz, 600 baud  (used for FM only)

// Config Params
extern char GridSquare[9];
extern char Callsign[10];
extern BOOL wantCWID;
extern BOOL CWOnOff;
extern int LeaderLength;
extern int TrailerLength;
extern unsigned int ARQTimeout;
extern int TuningRange;
extern int TXLevel;
extern int RXLevel;
extern int autoRXLevel;
extern BOOL DebugLog;
extern int ARQConReqRepeats;
extern BOOL CommandTrace;
extern char strFECMode[];
extern char CaptureDevice[];
extern char PlaybackDevice[];
extern int port;
extern char HostPort[80];
extern int pktport;
extern BOOL RadioControl;
extern BOOL SlowCPU;
extern BOOL AccumulateStats;
extern BOOL Use600Modes;
extern BOOL FSKOnly;
extern BOOL fastStart;
extern BOOL ConsoleLogLevel;
extern BOOL FileLogLevel;
extern BOOL EnablePingAck;

extern int dttLastPINGSent;

extern BOOL blnPINGrepeating;
extern BOOL blnFramePending;
extern int intPINGRepeats;

extern BOOL gotGPIO;
extern BOOL useGPIO;

extern int pttGPIOPin;
extern BOOL pttGPIOInvert;

extern HANDLE hCATDevice;		// port for Rig Control
extern char CATPort[80];
extern int CATBAUD;
extern int EnableHostCATRX;

extern HANDLE hPTTDevice;			// port for PTT
extern char PTTPort[80];			// Port for Hardware PTT - may be same as control port.
extern int PTTBAUD;

#define PTTRTS		1
#define PTTDTR		2
#define PTTCI_V		4

extern UCHAR PTTOnCmd[];
extern UCHAR PTTOnCmdLen;

extern UCHAR PTTOffCmd[];
extern UCHAR PTTOffCmdLen;

extern int PTTMode;				// PTT Control Flags.




extern char * CaptureDevices;
extern char * PlaybackDevices;

extern int dttCodecStarted;
extern int dttStartRTMeasure;

extern int intCalcLeader;        // the computed leader to use based on the reported Leader Length

extern const char strFrameType[256][18];
extern const char shortFrameType[256][12];
extern BOOL Capturing;
extern BOOL SoundIsPlaying;
extern int blnLastPTT;
extern BOOL blnAbort;
extern BOOL blnClosing;
extern BOOL blnCodecStarted;
extern BOOL blnInitializing;
extern BOOL blnARQDisconnect;
extern int DriveLevel;
extern int FECRepeats;
extern BOOL FECId;
extern int Squelch;
extern int BusyDet;
extern BOOL blnEnbARQRpt;
extern unsigned int dttNextPlay;

extern UCHAR bytDataToSend[];
extern int bytDataToSendLength;

extern BOOL blnListen;
extern BOOL Monitor;
extern BOOL AutoBreak;
extern BOOL BusyBlock;

extern int DecodeCompleteTime;

extern BOOL AccumulateStats;

extern unsigned char bytEncodedBytes[1800];
extern int EncLen;

extern char AuxCalls[10][10];
extern int AuxCallsLength;

extern int bytValidFrameTypesLength;
extern int bytValidFrameTypesLengthALL;
extern int bytValidFrameTypesLengthISS;

extern BOOL blnTimeoutTriggered;
extern int intFrameRepeatInterval;
extern BOOL PlayComplete;

extern const UCHAR bytValidFrameTypesALL[];
extern const UCHAR bytValidFrameTypesISS[];
extern const UCHAR * bytValidFrameTypes;

extern const char strAllDataModes[][15];
extern int strAllDataModesLen;

extern const short Rate[256];		// Data Rate (in bits/sec) by Frame Type


extern BOOL newStatus;

// RS Variables

extern int MaxCorrections;

// Stats counters

extern int intLeaderDetects;
extern int intLeaderSyncs;
extern int intAccumLeaderTracking;
extern float dblFSKTuningSNAvg;
extern int intGoodFSKFrameTypes;
extern int intFailedFSKFrameTypes;
extern int intAccumFSKTracking;
extern int intFSKSymbolCnt;
extern int intGoodFSKFrameDataDecodes;
extern int intFailedFSKFrameDataDecodes;
extern int intAvgFSKQuality;
extern int intFrameSyncs;
extern int intGoodPSKSummationDecodes;
extern int intGoodFSKSummationDecodes;
extern float dblLeaderSNAvg;
extern int intAccumPSKLeaderTracking;
extern float dblAvgPSKRefErr;
extern int intPSKTrackAttempts;
extern int intAccumPSKTracking;
extern int intQAMTrackAttempts;
extern int intAccumQAMTracking;
extern int intPSKSymbolCnt;
extern int intGoodPSKFrameDataDecodes;
extern int intFailedPSKFrameDataDecodes;
extern int intAvgPSKQuality;
extern float dblAvgDecodeDistance;
extern int intDecodeDistanceCount;
extern int intShiftUPs;
extern int intShiftDNs;
extern unsigned int dttStartSession;
extern int intLinkTurnovers;
extern int intEnvelopeCors;
extern float dblAvgCorMaxToMaxProduct;
extern int intConReqSN;
extern int intConReqQuality;



extern int int4FSKQuality;
extern int int4FSKQualityCnts;
extern int int8FSKQuality;
extern int int8FSKQualityCnts;
extern int int16FSKQuality;
extern int int16FSKQualityCnts;
extern int intFSKSymbolsDecoded;
extern int intPSKQuality[2];
extern int intPSKQualityCnts[2];
extern int intPSKSymbolsDecoded; 

extern int intQAMQuality;
extern int intQAMQualityCnts;
extern int intQAMSymbolsDecoded;
extern int intQAMSymbolCnt;
extern int intGoodQAMFrameDataDecodes;
extern int intFailedQAMFrameDataDecodes;
extern int intGoodQAMSummationDecodes;

extern int dttLastBusyOn;
extern int dttLastBusyOff;
extern int dttLastLeaderDetect;

extern int LastBusyOn;
extern int LastBusyOff;
extern int dttLastLeaderDetect;

extern int pktDataLen;
extern int pktRSLen;
extern const char pktMod[16][12];
extern int pktMode;
extern int pktModeLen;
extern const int pktBW[16];
extern const int pktCarriers[16];
extern const int defaultPacLen[16];
extern const BOOL pktFSK[16];

extern int pktMaxFrame;
extern int pktMaxBandwidth;
extern int pktPacLen;
extern int initMode;		 // 0 - 4PSK 1 - 8PSK 2 = 16QAM

extern BOOL SerialMode;			// Set if using SCS Mode, Unset for TCP Mode

// Has to follow enum defs

BOOL EncodeARQConRequest(char * strMyCallsign, char * strTargetCallsign, enum _ARQBandwidth ARQBandwidth, UCHAR * bytReturn);

#endif
