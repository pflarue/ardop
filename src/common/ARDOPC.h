#ifndef ARDOPCHEADERDEFINED
#define ARDOPCHEADERDEFINED

#include <stdbool.h>

#include "common/log.h"
#include "common/Locator.h"
#include "common/StationId.h"

extern const char ProductName[];
extern const char ProductVersion[];

// #define USE_SOUNDMODEM

// Sound interface buffer size

#define SendSize 1200  // 100 mS for now
// #define ReceiveSize 512  // Must be 1024 for FFT (or we will need torepack frames)
#define NumberofinBuffers 4

// Host to TNC Buffer Size
#define DATABUFFERSIZE 100000

#ifndef _WIN32_WINNT  // Allow use of features specific to Windows XP or later.
#define _WIN32_WINNT 0x0501  // Change this to the appropriate value to target other versions of Windows.
#endif

#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers
#define _CRT_SECURE_NO_DEPRECATE

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

extern unsigned int pttOnTime;

#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#ifdef M_PI
#undef M_PI
#endif

#define M_PI 3.1415926f

#ifndef WIN32
#define LINUX
#endif

#ifdef __ARM_ARCH
#define ARMLINUX
#endif

#define UseGUI  // Enable GUI Front End Support

#ifdef UseGUI

// Constellation and Waterfall for GUI interface

#define PLOTCONSTELLATION
#define PLOTWATERFALL
#define PLOTSPECTRUM
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

typedef unsigned char UCHAR;

#define VOID void

#define ISSLED 1
#define IRSLED 2
#define TRAFFICLED 3
#define PKTLED 4

UCHAR FrameCode(char * strFrameName);
bool FrameInfo(UCHAR bytFrameType, bool * blnOdd, int * intNumCar, char * strMod,
int * intBaud, int * intDataLen, int * intRSLen, UCHAR * bytQualThres, char * strType);

void ClearDataToSend();
int EncodeFSKData(UCHAR bytFrameType, UCHAR * bytDataToSend, int Length, unsigned char * bytEncodedBytes);
int EncodePSKData(UCHAR bytFrameType, UCHAR * bytDataToSend, int Length, unsigned char * bytEncodedBytes);
int EncodePing(const StationId* mycall, const StationId* target, UCHAR* bytReturn);
int Encode4FSKIDFrame(const StationId* callsign, const Locator* square, unsigned char* bytreturn);
int EncodeDATAACK(int intQuality, UCHAR bytSessionID, UCHAR * bytreturn);
int EncodeDATANAK(int intQuality , UCHAR bytSessionID, UCHAR * bytreturn);
void Mod4FSKDataAndPlay(int Type, unsigned char * bytEncodedBytes, int Len, int intLeaderLen);
void Mod4FSK600BdDataAndPlay(int Type, unsigned char * bytEncodedBytes, int Len, int intLeaderLen);
void ModPSKDataAndPlay(int Type, unsigned char * bytEncodedBytes, int Len, int intLeaderLen);
bool IsDataFrame(UCHAR intFrameType);
void StartCodec(char * strFault);
void StopCodec(char * strFault);
bool SendARQConnectRequest(const StationId* mycall, const StationId* target);
void AddDataToDataToSend(UCHAR * bytNewData, int Len);
bool StartFEC(UCHAR * bytData, int Len, char * strDataMode, int intRepeats, bool blnSendID);
bool SendID(const StationId * id, char * reason);
// void SetARDOPProtocolState(int value);
unsigned int GenCRC16(unsigned char * Data, unsigned short length);
void SendCommandToHost(char * Cmd);
void TCPSendCommandToHost(char * Cmd);
void SendCommandToHostQuiet(char * Cmd);
void TCPSendCommandToHostQuiet(char * Cmd);
void UpdateBusyDetector(short * bytNewSamples);
int UpdatePhaseConstellation(short * intPhases, short * intMags, char * strMod, bool blnQAM);
void SetARDOPProtocolState(int value);
bool BusyDetect3(float * dblMag, int intStart, int intStop);

void displayState(const char * State);
void displayCall(int dirn, const char * call);

void SampleSink(short Sample);
void SoundFlush();
void StopCapture();
void DiscardOldSamples();
void ClearAllMixedSamples();

void SetFilter(void * Filter());

void AddTrailer();
void CWID(char * strID, short * intSamples, bool blnPlay);
void sendCWID(const StationId * id);
UCHAR ComputeTypeParity(UCHAR bytFrameType);
void GenCRC16FrameType(char * Data, int Length, UCHAR bytFrameType);
bool CheckCRC16FrameType(unsigned char * Data, int Length, UCHAR bytFrameType);
char * strlop(char * buf, char delim);
void QueueCommandToHost(char * Cmd);
void TCPQueueCommandToHost(char * Cmd);
void SendReplyToHost(char * strText);
void TCPSendReplyToHost(char * strText);
void LogStats();
int GetNextFrameData(int * intUpDn, UCHAR * bytFrameTypeToSend, UCHAR * strMod, bool blnInitialize);
void SendData();
int ComputeInterFrameInterval(int intRequestedIntervalMS);
int Encode4FSKControl(UCHAR bytFrameType, UCHAR bytSessionID, UCHAR * bytreturn);
int EncodeConACKwTiming(UCHAR bytFrameType, int intRcvdLeaderLenMs, UCHAR bytSessionID, UCHAR * bytreturn);
int EncodePingAck(int bytFrameType, int intSN, int intQuality, UCHAR * bytreturn);
void SaveQueueOnBreak();
void Abort();
void SetLED(int LED, int State);
VOID ClearBusy();
VOID CloseCOMPort(HANDLE fd);

// #ifdef WIN32
void ProcessNewSamples(short * Samples, int nSamples);
void ardopmain();
bool GetNextFECFrame();
void GenerateFSKTemplates();
void InitValidFrameTypes();
// #endif

extern void Generate50BaudTwoToneLeaderTemplate();
extern bool blnDISCRepeating;

void ProcessRcvdFECDataFrame(int intFrameType, UCHAR * bytData, bool blnFrameDecodedOK);
void ProcessUnconnectedConReqFrame(int intFrameType, UCHAR * bytData);
void ProcessRcvdARQFrame(UCHAR intFrameType, UCHAR * bytData, int DataLen, bool blnFrameDecodedOK);
void InitializeConnection();

void AddTagToDataAndSendToHost(UCHAR * Msg, char * Type, int Len);
void TCPAddTagToDataAndSendToHost(UCHAR * Msg, char * Type, int Len);

void RemoveDataFromQueue(int Len);
void RemodulateLastFrame();

void GetSemaphore();
void FreeSemaphore();
const char * Name(UCHAR bytID);
const char * shortName(UCHAR bytID);
bool InitSound();
void initFilter(int Width, int centerFreq);
void FourierTransform(int NumSamples, float * RealIn, float * RealOut, float * ImagOut, int InverseTransform);
VOID LostHost();
VOID ProcessDEDModeFrame(UCHAR * rxbuffer, unsigned int Length);

int SendtoGUI(char Type, unsigned char * Msg, int Len);
void DrawTXFrame(const char * Frame);
void DrawRXFrame(int State, const char * Frame);
void mySetPixel(unsigned char x, unsigned char y, unsigned int Colour);

void clearDisplay();

extern int WaterfallActive;
extern int SpectrumActive;
extern unsigned int PKTLEDTimer;

extern int stcLastPingintRcvdSN;
extern int stcLastPingintQuality;
extern time_t stcLastPingdttTimeReceived;

enum _ReceiveState  // used for initial receive testing...later put in correct protocol states
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
	IDLE,  // ISS in quiet state ...no transmissions)
	IRStoISS,  // IRS during transition to ISS waiting for ISS's ACK from IRS's BREAK
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

// FRAME is appended to some frame types where the frame type
// is also used to define a state (DISC, IDLE)
#define DataNAKmin 0x00
#define DataNAKmax 0x1F
// 0x20 - 0x22 Unused
#define BREAK 0x23
#define IDLEFRAME 0x24
// 0x25 - 0x28 Unused
#define DISCFRAME 0x29
// 0x2A, 0x2B Unused
#define END 0x2C
#define ConRejBusy 0x2D
#define ConRejBW 0x2E
// 0x2F Unused
#define IDFRAME 0x30
#define ConReqmin 0x31
#define ConReq200M 0x31
#define ConReq500M 0x32
#define ConReq1000M 0x33
#define ConReq2000M 0x34
#define ConReq200F 0x35
#define ConReq500F 0x36
#define ConReq1000F 0x37
#define ConReq2000F 0x38
#define ConReqmax 0x38
#define ConAckmin 0x39
#define ConAck200 0x39
#define ConAck500 0x3A
#define ConAck1000 0x3B
#define ConAck2000 0x3C
#define ConAckmax 0x3C
#define PINGACK 0x3D
#define PING 0x3E
// 0x3F Unused
#define DataFRAMEmin 0x40
// Some values between DataFRAMEmin and DataFRAMEmax are Unused
#define DataFRAMEmax 0x7D
// 0x7E - 0xDF Unused
#define DataACKmin 0xE0
#define DataACKmax 0xFF

extern const short intTwoToneLeaderTemplate[120];  // holds just 1 symbol (0 ms) of the leader
extern const short int50BaudTwoToneLeaderTemplate[240];  // holds just 1 symbol (20 ms) of the leader

extern const short intPSK100bdCarTemplate[9][4][120];  // The actual templates over 9 carriers for 4 phase values and 120 samples
// (only positive Phase values are in the table, sign reversal is used to get the negative phase values) This reduces the table size from 7680 to 3840 integers
extern const short intFSK50bdCarTemplate[4][240];  // Template for 4FSK carriers spaced at 50 Hz, 50 baud
extern const short intFSK100bdCarTemplate[20][120];  // Template for 4FSK carriers spaced at 100 Hz, 100 baud
extern const short intFSK600bdCarTemplate[4][20];  // Template for 4FSK carriers spaced at 600 Hz, 600 baud  (used for FM only)

#define AUXCALLS_ALEN 10  // length of AuxCalls array
#define COMP_SIZE 6  // size of compressed callsign or gridsquare
// Config Params
extern Locator GridSquare;
extern StationId Callsign;
extern bool wantCWID;
extern bool CWOnOff;
extern int LeaderLength;
extern int TrailerLength;
extern unsigned int ARQTimeout;
extern int TuningRange;
extern int ARQConReqRepeats;
extern bool CommandTrace;
extern char strFECMode[];
extern int host_port;
extern bool SlowCPU;
extern bool AccumulateStats;
extern bool Use600Modes;
extern bool FSKOnly;
extern bool fastStart;
extern bool EnablePingAck;

extern int dttLastPINGSent;

extern bool blnPINGrepeating;
extern bool blnFramePending;
extern int intPINGRepeats;

extern int dttCodecStarted;
extern int dttStartRTMeasure;

extern int intCalcLeader;  // the computed leader to use based on the reported Leader Length

extern const char strFrameType[256][18];
extern const char shortFrameType[256][12];
extern bool Capturing;
extern bool SoundIsPlaying;
extern bool blnAbort;
extern bool blnClosing;
extern bool blnCodecStarted;
extern bool blnInitializing;
extern bool blnARQDisconnect;
extern int DriveLevel;
extern int FECRepeats;
extern bool FECId;
extern int Squelch;
extern int BusyDet;
extern bool blnEnbARQRpt;
extern unsigned int dttNextPlay;

extern UCHAR bytDataToSend[];
extern int bytDataToSendLength;

extern bool blnListen;
extern bool Monitor;
extern bool AutoBreak;
extern bool BusyBlock;

extern int DecodeCompleteTime;

extern bool AccumulateStats;

extern unsigned char bytEncodedBytes[1800];
extern int EncLen;

extern StationId AuxCalls[AUXCALLS_ALEN];
extern size_t AuxCallsLength;

extern int bytValidFrameTypesLength;
extern int bytValidFrameTypesLengthALL;
extern int bytValidFrameTypesLengthISS;

extern bool blnTimeoutTriggered;
extern int intFrameRepeatInterval;
extern bool PlayComplete;

extern const UCHAR bytValidFrameTypesALL[];
extern const UCHAR bytValidFrameTypesISS[];
extern const UCHAR * bytValidFrameTypes;

extern const char strAllDataModes[][15];
extern int strAllDataModesLen;


extern bool newStatus;

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

extern int initMode;  // 0 - 4PSK 1 - 8PSK 2 = 16QAM

// Has to follow enum defs

int EncodeARQConRequest(const StationId* mycall, const StationId* target, enum _ARQBandwidth ARQBandwidth, UCHAR* bytReturn);

#endif
