// ARDOP TNC ARQ Code
//

#include <stdbool.h>
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers
#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

#include <time.h>

#include "common/os_util.h"
#include "common/ARDOPC.h"

extern unsigned int PKTLEDTimer;
extern UCHAR bytData[];
extern int intLastRcvdFrameQuality;
extern int intRmtLeaderMeasure;
extern bool blnAbort;
extern int intRepeatCount;
extern unsigned int tmrSendTimeout;
extern bool blnFramePending;
extern int dttLastBusyTrip;
extern int dttPriorLastBusyTrip;
extern int dttLastBusyClear;
extern unsigned int LastIDFrameTime;

int wg_send_state(int cnum);
int wg_send_rcall(int cnum, const char *call);
int wg_send_irsled(int cnum, bool isOn);
int wg_send_issled(int cnum, bool isOn);
unsigned char *utf8_check(unsigned char *s, size_t slen);

int intLastFrameIDToHost = 0;
int	intLastFailedFrameID = 0;
int	intLastARQDataFrameToHost = -1;
extern int intARQDefaultDlyMs;  // Not sure if this really need with optimized leader length. 100 ms doesn't add much overhead.
int	intAvgQuality;  // the filtered average reported quality (0 to 100) practical range 50 to 96
int	intShiftUpDn = 0;
int intFrameTypePtr = 0;  // Pointer to the current data mode in bytFrameTypesForBW()
int	intRmtLeaderMeas = 0;
int intTrackingQuality = -1;
UCHAR bytLastARQDataFrameSent = 0;  // initialize to an improper data frame
UCHAR bytLastARQDataFrameAcked = 0;  // initialize to an improper data frame
void ClearTuningStats();
void ClearQualityStats();
void updateDisplay();
void DrawTXMode(const char * TXMode);

int bytQDataInProcessLen = 0;  // Length of frame to send/last sent

bool blnLastFrameSentData = false;

void ResetMemoryARQ();
extern int LastDataFrameType;
extern bool blnARQDisconnect;
extern const short FrameSize[256];

// ARQ State Variables

StationId AuxCalls[AUXCALLS_ALEN];
size_t AuxCallsLength = 0;

int intBW;  // Requested connect speed
int intSessionBW;  // Negotiated speed

const char ARQBandwidths[9][12] = {
	"200FORCED", "500FORCED", "1000FORCED", "2000FORCED",
	"200MAX", "500MAX", "1000MAX", "2000MAX", "UNDEFINED"
};
enum _ARQSubStates ARQState;

const char ARQSubStates[10][11] = {
	"None", "ISSConReq", "ISSConAck", "ISSData", "ISSId",
	"IRSConAck", "IRSData", "IRSBreak", "IRSfromISS", "DISCArqEnd"
};

/*
 * these globals from SoundInput store the callsign
 * pair received from the last frame, like a ConReq
 *
 * NOTE: these are cleared on every new frame!
 */
extern StationId LastDecodedStationCaller;
extern StationId LastDecodedStationTarget;

StationId ARQStationRemote;  // current connection remote callsign
StationId ARQStationLocal;   // current connection local callsign
StationId ARQStationFinalId; // post-session local IDF to send

UCHAR bytLastARQSessionID;
bool blnEnbARQRpt;
bool blnListen = true;
bool Monitor = true;
bool AutoBreak = true;
bool blnBREAKCmd = false;
bool BusyBlock = false;

UCHAR bytPendingSessionID;
UCHAR bytSessionID = 0xff;
bool blnARQConnected;

UCHAR bytCurrentFrameType = 0;  // The current frame type used for sending
UCHAR * bytFrameTypesForBW;  // Holds the byte array for Data modes for a session bandwidth. First are most robust, last are fastest
int bytFrameTypesForBWLength = 0;

UCHAR * bytShiftUpThresholds;
int bytShiftUpThresholdsLength;

bool blnPending;
int dttTimeoutTrip;
int intLastARQDataFrameToHost;
int intAvgQuality;
int intReceivedLeaderLen;
unsigned int tmrFinalID = 0;
unsigned int tmrIRSPendingTimeout = 0;
unsigned int tmrPollOBQueue;
UCHAR bytLastReceivedDataFrameType;
bool blnDISCRepeating;

int intRmtLeaderMeas;

int	intOBBytesToConfirm = 0;  // remaining bytes to confirm
int	intBytesConfirmed = 0;  // Outbound bytes confirmed by ACK and squenced
int	intReportedLeaderLen = 0;  // Zero out the Reported leader length the length reported to the remote station
bool blnLastPSNPassed = false;  // the last PSN passed true for Odd, false for even.
bool blnInitiatedConnection = false;  // flag to indicate if this station initiated the connection
short dblAvgPECreepPerCarrier = 0;  // computed phase error creep
int	intTotalSymbols = 0;  // To compute the sample rate error

extern int bytDataToSendLength;
int intFrameRepeatInterval;


extern int intLeaderRcvdMs;

int intTrackingQuality;
int intNAKctr = 0;
int intACKctr = 0;
UCHAR bytLastACKedDataFrameType;

int Encode4FSKControl(UCHAR bytFrameType, UCHAR bytSessionID, UCHAR * bytreturn);
int IRSNegotiateBW(int intConReqFrameType);
int GetNextFrameData(int * intUpDn, UCHAR * bytFrameTypeToSend, UCHAR * strMod, bool blnInitialize);
bool CheckForDisconnect();
void ProcessPingFrame(char * bytData);

void LogStats();
int ComputeInterFrameInterval(int intRequestedIntervalMS);
bool CheckForDisconnect();

// Tuning Stats

int intLeaderDetects;
int intLeaderSyncs;
int intAccumLeaderTracking;
float dblFSKTuningSNAvg;
int intGoodFSKFrameTypes;
int intFailedFSKFrameTypes;
int intAccumFSKTracking;
int intFSKSymbolCnt;
int intGoodFSKFrameDataDecodes;
int intFailedFSKFrameDataDecodes;
int intAvgFSKQuality;
int intFrameSyncs;
int intGoodPSKSummationDecodes;
int intGoodFSKSummationDecodes;
int intGoodQAMSummationDecodes;
float dblLeaderSNAvg;
int intAccumPSKLeaderTracking;
float dblAvgPSKRefErr;
int intPSKTrackAttempts;
int intAccumPSKTracking;
int intQAMTrackAttempts;
int intAccumQAMTracking;
int intPSKSymbolCnt;
int intQAMSymbolCnt;
int intGoodPSKFrameDataDecodes;
int intFailedPSKFrameDataDecodes;
int intGoodQAMFrameDataDecodes;
int intFailedQAMFrameDataDecodes;
int intAvgPSKQuality;
float dblAvgDecodeDistance;
int intDecodeDistanceCount;
int	intShiftUPs;
int intShiftDNs;
unsigned int dttStartSession;
int intLinkTurnovers;
int intEnvelopeCors;
float dblAvgCorMaxToMaxProduct;
int intConReqSN;
int intConReqQuality;


// Function to compute a 8 bit CRC value and append it to the Data...

UCHAR GenCRC8(char * Data)
{
	// For  CRC-8-CCITT = x^8 + x^7 +x^3 + x^2 + 1  intPoly = 1021 Init FFFF

	int intPoly = 0xC6;  // This implements the CRC polynomial  x^8 + x^7 +x^3 + x^2 + 1
	int intRegister  = 0xFF;
	int i;
	unsigned int j;
	bool blnBit;

	for (j = 0; j < strlen(Data); j++)
	{
		int Val = Data[j];

		for (i = 7; i >= 0; i--)  // for each bit processing MS bit first
		{
			blnBit = (Val & 0x80) != 0;
			Val = Val << 1;

			if ((intRegister & 0x80) == 0x80)  // the MSB of the register is set
			{
				// Shift left, place data bit as LSB, then divide
				// Register := shiftRegister left shift 1
				// Register := shiftRegister xor polynomial

				if (blnBit)
					intRegister = 0xFF & (1 + 2 * intRegister);
				else
					intRegister = 0xFF & (2 * intRegister);

				intRegister = intRegister ^ intPoly;
			}
			else
			{
				// the MSB is not set
				// Register is not divisible by polynomial yet.
				// Just shift left and bring current data bit onto LSB of shiftRegister

				if (blnBit)
					intRegister = 0xFF & (1 + 2 * intRegister);
				else
					intRegister = 0xFF & (2 * intRegister);
			}
		}
	}
	return intRegister & 0xFF;  // LS 8 bits of Register
}

int ComputeInterFrameInterval(int intRequestedIntervalMS)
{
	return max(1000, intRequestedIntervalMS + intRmtLeaderMeas);
}


// Function to Set the protocol state

void SetARDOPProtocolState(int value)
{
	char HostCmd[24];

	if (ProtocolState == value)
		return;

	ProtocolState = value;

	displayState(ARDOPStates[ProtocolState]);

	newStatus = true;  // report to PTC

	// Dim stcStatus As Status
	// stcStatus.ControlName = "lblState"
	// stcStatus.Text = ARDOPState.ToString

	switch(ProtocolState)
	{
	case DISC:

		blnARQDisconnect = false;  // always clear the ARQ Disconnect Flag from host.
		// stcStatus.BackColor = System.Drawing.Color.White
		blnARQConnected = false;
		blnPending = false;
		ClearDataToSend();
		SetLED(ISSLED, false);
		SetLED(IRSLED, false);
		displayCall(0x20, "");
		wg_send_issled(0, false);
		wg_send_irsled(0, false);
		wg_send_rcall(0, "");
		break;

	case FECRcv:
		// stcStatus.BackColor = System.Drawing.Color.PowderBlue
		break;

	case FECSend:

		InitializeConnection();
		intLastFrameIDToHost = -1;
		intLastFailedFrameID = -1;
		// ReDim bytFailedData(-1)
		// stcStatus.BackColor = System.Drawing.Color.Orange
		break;

	// Case ProtocolState.IRS
	// stcStatus.BackColor = System.Drawing.Color.LightGreen

	case ISS:
	case IDLE:

		blnFramePending = false;  // Added 0.6.4 to insure any prior repeating frame is cancelled before new data.
		blnEnbARQRpt = false;
		SetLED(ISSLED, true);
		SetLED(IRSLED, false);
		wg_send_irsled(0, false);
		wg_send_issled(0, true);
		// stcStatus.BackColor = System.Drawing.Color.LightSalmon

		break;

	case IRS:
	case IRStoISS:

		SetLED(IRSLED, true);
		SetLED(ISSLED, false);
		wg_send_issled(0, false);
		wg_send_irsled(0, true);
		bytLastACKedDataFrameType = 0;  // Clear on entry to IRS or IRS to ISS states. 3/15/2018

		break;


	// Case ProtocolState.IDLE
	// stcStatus.BackColor = System.Drawing.Color.NavajoWhite
	// Case ProtocolState.OFFLINE
	// stcStatus.BackColor = System.Drawing.Color.Silver
	}
	// queTNCStatus.Enqueue(stcStatus)

	snprintf(HostCmd, sizeof(HostCmd), "NEWSTATE %s ", ARDOPStates[ProtocolState]);
	QueueCommandToHost(HostCmd);
	wg_send_state(0);
}



// Function to Get the next ARQ frame returns true if frame repeating is enable

bool GetNextARQFrame()
{
	// Dim bytToMod(-1) As Byte

	char HostCmd[80];

	if (blnAbort)  // handles ABORT (aka Dirty Disconnect)
	{
		// if (DebugLog) ;(("[ARDOPprotocol.GetNextARQFrame] ABORT...going to ProtocolState DISC, return false")

		ClearDataToSend();

		SetARDOPProtocolState(DISC);
		InitializeConnection();
		blnAbort = false;
		blnEnbARQRpt = false;
		blnDISCRepeating = false;
		intRepeatCount = 0;

		return false;
	}

	if (blnDISCRepeating)  // handle the repeating DISC reply
	{
		intRepeatCount += 1;
		blnEnbARQRpt = false;

		if (intRepeatCount > 5)  // do 5 tries then force disconnect
		{
			QueueCommandToHost("DISCONNECTED");
			ZF_LOGI("[STATUS: END NOT RECEIVED CLOSING ARQ SESSION WITH %s]", ARQStationRemote.str);
			snprintf(HostCmd, sizeof(HostCmd), "STATUS END NOT RECEIVED CLOSING ARQ SESSION WITH %s", ARQStationRemote.str);
			QueueCommandToHost(HostCmd);
			blnDISCRepeating = false;
			blnEnbARQRpt = false;
			ClearDataToSend();
			SetARDOPProtocolState(DISC);
			intRepeatCount = 0;
			InitializeConnection();
			return false;  // indicates end repeat
		}
		ZF_LOGI("Repeating DISC %d", intRepeatCount);
		if ((EncLen = Encode4FSKControl(DISCFRAME, bytSessionID, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In GetNextARQFrame() Invalid EncLen (%d).", EncLen);
			return false;
		}

		return true;  // continue with DISC repeats
	}

	if (ProtocolState == ISS || ProtocolState == IDLE)
		if (CheckForDisconnect())
			return false;

	if (ProtocolState == ISS && ARQState == ISSConReq)  // Handles Repeating ConReq frames
	{
		intRepeatCount++;
		if (intRepeatCount > ARQConReqRepeats)
		{
			ClearDataToSend();
			SetARDOPProtocolState(DISC);
			intRepeatCount = 0;
			blnPending = false;
			displayCall(0x20, "");
			wg_send_rcall(0, "");

			if (stationid_ok(&ARQStationRemote))
			{
				ZF_LOGI("[STATUS: CONNECT TO %s FAILED]", ARQStationRemote.str);
				snprintf(HostCmd, sizeof(HostCmd), "STATUS CONNECT TO %s FAILED!", ARQStationRemote.str);
				QueueCommandToHost(HostCmd);
				InitializeConnection();
				return false;  // indicates end repeat
			}
			else
			{
				ZF_LOGI("[STATUS: END ARQ CALL]");
				QueueCommandToHost("STATUS END ARQ CALL");
				InitializeConnection();
				return false;  // indicates end repeat
			}


			// Clear the mnuBusy status on the main form
			// Dim stcStatus As Status = Nothing
			// stcStatus.ControlName = "mnuBusy"
			// queTNCStatus.Enqueue(stcStatus)
		}

		return true;  // continue with repeats
	}

	if (ProtocolState == ISS && ARQState == IRSConAck)
	{
		// Handles ISS repeat of ConAck

		intRepeatCount += 1;
		if (intRepeatCount <= ARQConReqRepeats)
			return true;
		else
		{
			SetARDOPProtocolState(DISC);
			ARQState = DISCArqEnd;
			ZF_LOGI("[STATUS: CONNECT TO %s FAILED]", ARQStationRemote.str);
			snprintf(HostCmd, sizeof(HostCmd), "STATUS CONNECT TO %s FAILED!", ARQStationRemote.str);
			QueueCommandToHost(HostCmd);
			intRepeatCount = 0;
			InitializeConnection();
			return false;
		}
	}
	// Handles a timeout from an ARQ connected State

	if (ProtocolState == ISS || ProtocolState == IDLE || ProtocolState == IRS || ProtocolState == IRStoISS)
	{
		if ((Now - dttTimeoutTrip) / 1000 > ARQTimeout)  // (Handles protocol rule 1.7)
		{
			if (!blnTimeoutTriggered)
			{
				ZF_LOGD("[ARDOPprotocol.GetNexARQFrame] Timeout setting SendTimeout timer to start.");

				blnEnbARQRpt = false;
				blnTimeoutTriggered = true;  // prevents a retrigger
				tmrSendTimeout = Now + 1000;
				return false;
			}
		}
	}

	if (ProtocolState == DISC && intPINGRepeats > 0)
	{
		intRepeatCount++;
		if (intRepeatCount <= intPINGRepeats && blnPINGrepeating)
		{
			dttLastPINGSent = Now;
			return true;  // continue PING
		}

		intPINGRepeats = 0;
		blnPINGrepeating = false;
		return false;
	}

	// Handles the DISC state (no repeats)

	if (ProtocolState == DISC)  // never repeat in DISC state
	{
		blnARQDisconnect = false;
		intRepeatCount = 0;
		return false;
	}

	// Handles all other possibly repeated Frames

	return blnEnbARQRpt;  // not all frame types repeat...blnEnbARQRpt is set/cleared in ProcessRcvdARQFrame
}


// function to generate 8 bit session ID

UCHAR GenerateSessionID(const char * strCallingCallSign, const char *strTargetCallsign)
{
	char bytToCRC[STATIONID_BUF_SIZE*2 + 1];

	if (snprintf(bytToCRC, sizeof(bytToCRC), "%s%s", strCallingCallSign, strTargetCallsign) >= (int)sizeof(bytToCRC))
		ZF_LOGW(
			"WARNING: Excessive length of callsigns ('%s', '%s') passed to"
			" GenerateSessionID() has probably resulted in an invalid SessionID.",
			strCallingCallSign, strTargetCallsign);

	UCHAR ID = GenCRC8(bytToCRC);

	if (ID == 255)

		// rare case where the computed session ID would be FF
		// Remap a SessionID of FF to 0...FF reserved for FEC mode

		return 0;

	return ID;
}

// Function to compute the optimum leader based on the Leader sent and the reported Received leader

void CalculateOptimumLeader(int intReportedReceivedLeaderMS,int  intLeaderSentMS)
{
//	intCalcLeader = max(200, 120 + intLeaderSentMS - intReportedReceivedLeaderMS);  // This appears to work well on HF sim tests May 31, 2015
//	ZF_LOGD("CalcualteOptimumLeader Leader Sent= %d   ReportedReceived= %d  Calculated= %d", intLeaderSentMS, intReportedReceivedLeaderMS, intCalcLeader);
}



// Function to determine if call is to Callsign or one of the AuxCalls

bool IsCallToMe(const StationId* caller, const StationId* target, UCHAR* bytReplySessionID)
{
	// returns true and sets bytReplySessionID if is to me.

	if (stationid_eq(target, &Callsign))
	{
		*bytReplySessionID = GenerateSessionID(caller->str, Callsign.str);
		return true;
	}

	for (size_t i = 0; i < AuxCallsLength; i++)
	{
		if (stationid_eq(target, &AuxCalls[i]))
		{
			*bytReplySessionID = GenerateSessionID(caller->str, AuxCalls[i].str);
			return true;
		}
	}

	return false;
}

bool IsPingToMe(const StationId* caller, const StationId* target)
{
	UCHAR _ign = 0;
	return IsCallToMe(caller, target, &_ign);
}

// 4FSK.200.50S, 4PSK.200.100S, 4PSK.200.100, 8PSK.200.100, 16QAM.200.100

static UCHAR DataModes200[] = {0x48, 0x42, 0x40, 0x44, 0x46};
static UCHAR DataModes200FSK[] = {0x48};  // 4FSK.200.50S

// 4FSK.200.50S, 4PSK.200.100S, 4PSK.200.100, 4PSK.500.100, 8PSK.500.100, 16QAM.500.100)
// (310, 436, 756, 1509, 2566, 3024 bytes/min)
// Dim byt500 As Byte() = {&H48, &H42, &H40, &H50, &H52, &H54}

static UCHAR DataModes500[] = {0x48, 0x42, 0x40, 0x50, 0x52, 0x54};
static UCHAR DataModes500FSK[] = {0x48, 0x4C, 0x4A};  // 4FSK.200.50S, 4FSK.500.100S, 4FSK.500.100


// 4FSK500.100S, 4FSK500.100, 4PSK500.100, 4PSK1000.100, 8PSK.1000.100
// (701, 865, 1509, 3018, 5133 bytes/min)

static UCHAR DataModes1000[] = {0x4C, 0x4A, 0x50, 0x60, 0x62, 0x64};
static UCHAR DataModes1000FSK[] = {0x48, 0x4C, 0x4A};  // 4FSK.200.50S, 4FSK.500.100S, 4FSK.500.100

// 2000 Non-FM

// 4FSK500.100S, 4FSK500.100, 4PSK500.100, 4PSK1000.100, 4PSK2000.100, 8PSK.2000.100, 16QAM.2000.100
// (701, 865, 1509, 3018, 6144, 10386 bytes/min)
// Dim byt2000 As Byte() = {&H4C, &H4A, &H50, &H60, &H70, &H72, &H74}. Note addtion of 16QAM 8 carrier mode 16QAM2000.100.E/O

static UCHAR DataModes2000[] = {0x4C, 0x4A, 0x50, 0x60, 0x70, 0x72, 0x74};
static UCHAR DataModes2000FSK[] = {0x48, 0x4C, 0x4A};  // 4FSK.200.50S, 4FSK.500.100S, 4FSK.500.100

// 2000 FM
// These include the 600 baud modes for FM only.
// The following is temporary, Plan to replace 8PSK 8 carrier modes with high baud 4PSK and 8PSK.

// 4FSK.500.100S, 4FSK.500.100, 4FSK.2000.600S, 4FSK.2000.600)
// (701, 865, 4338, 5853 bytes/min)
// Dim byt2000 As Byte() = {&H4C, &H4A, &H7C, &H7A}

static UCHAR DataModes2000FM[] = {0x4C, 0x4A, 0x7C, 0x7A};
// 4FSK.200.50S, 4FSK.500.100S, 4FSK.500.100, 4FSK.2000.600S, 4FSK.2000.600
static UCHAR DataModes2000FMFSK[] = {0x48, 0x4C, 0x4A, 0x7C, 0x7A};

static UCHAR NoDataModes[1] = {0};

UCHAR  * GetDataModes(int intBW)
{
	// Revised version 0.3.5
	// idea is to use this list in the gear shift algorithm to select modulation mode based on bandwidth and robustness.
	// Sequence modes in approximate order of robustness ...most robust first, shorter frames of same modulation first

	if (intBW == 200)
	{
		if (FSKOnly)
		{
			bytFrameTypesForBWLength = sizeof(DataModes200FSK);
			return DataModes200FSK;
		}

		bytFrameTypesForBWLength = sizeof(DataModes200);
		return DataModes200;
	}
	if (intBW == 500)
	{
		if (FSKOnly)
		{
			bytFrameTypesForBWLength = sizeof(DataModes500FSK);
			return DataModes500FSK;
		}

		bytFrameTypesForBWLength = sizeof(DataModes500);
		return DataModes500;
	}
	if (intBW == 1000)
	{
		if (FSKOnly)
		{
			bytFrameTypesForBWLength = sizeof(DataModes1000FSK);
			return DataModes1000FSK;
		}

		bytFrameTypesForBWLength = sizeof(DataModes1000);
		return DataModes1000;
	}
	if (intBW == 2000)
	{
		if (TuningRange > 0  && !Use600Modes)
		{
			if (FSKOnly)
			{
				bytFrameTypesForBWLength = sizeof(DataModes2000FSK);
				return DataModes2000FSK;
			}
			bytFrameTypesForBWLength = sizeof(DataModes2000);
			return DataModes2000;
		}
		else
		{
			if (FSKOnly)
			{
				bytFrameTypesForBWLength = sizeof(DataModes2000FMFSK);
				return DataModes2000FMFSK;
			}
			bytFrameTypesForBWLength = sizeof(DataModes2000FM);
			return DataModes2000FM;
		}
	}
	bytFrameTypesForBWLength = 0;
	return NoDataModes;
}

// Function to get Shift up thresholds by bandwidth for ARQ sessions

static UCHAR byt200[] = {82, 84, 84, 85, 0};
static UCHAR byt500[] = {80, 84, 84, 75, 79, 0};
static UCHAR byt1000[] = {80, 80, 80, 80, 75, 0};
static UCHAR byt2000[] = {80, 80, 80, 76, 85, 75, 0};  // Threshold for 8PSK 167 baud changed from 73 to 80 on rev 0.7.2.3
static UCHAR byt2000FM[] = {60, 85, 85, 0};

UCHAR * GetShiftUpThresholds(int intBW)
{
	// Initial values determined by finding the following process: (all using Pink Gaussian Noise channel 0 to 3 KHz)
	//  1) Find Min S:N that will give reliable (at least 4/5 tries) decoding at the fastest mode for the bandwidth.
	//  2) At that SAME S:N use the next fastest (more robust mode for the bandwidth)
	//  3) Over several frames average the Quality of the decoded frame in 2) above That quality value is the one that
	//     is then used as the shift up threshold for that mode. (note the top mode will never use a value to shift up).
	//     This might be adjusted some but should along with a requirement for two successive ACKs make a good algorithm

	if (intBW == 200)
		return byt200;

	if (intBW == 500)
		return byt500;

	if (intBW == 1000)
		return byt1000;

	// default to 2000

	if (TuningRange > 0  && !Use600Modes)
		return byt2000;
	else
		return byt2000FM;
}


unsigned short  ModeHasWorked[16] = {0};  // used to attempt to make gear shift more stable.
unsigned short  ModeHasBeenTried[16] = {0};
unsigned short  ModeNAKS[16] = {0};

// Function to shift up to the next higher throughput or down to the next more robust data modes based on average reported quality

void Gearshift_9()
{
	// More complex mechanism to gear shift based on intAvgQuality, current state and bytes remaining.
	// This can be refined later with different or dynamic Trip points etc.
	// Revised Oct 8, 2016  Rev 0.7.2.2 to use intACKctr as well as intNAKctr and bytShiftUpThresholds using FrameInfo.GetShiftUpThresholds

	char strOldMode[18] = "";
	char strNewMode[18] = "";
	int DownNAKS = 2;  // Normal (changed from 3 Nov 17)

	int intBytesRemaining = bytDataToSendLength;

	if (ModeHasWorked[intFrameTypePtr] == 0)  // This mode has never worked
		DownNAKS = 1;  // Revert immediately

	if (intACKctr)
		ModeHasWorked[intFrameTypePtr]++;
	else if (intNAKctr)
		ModeNAKS[intFrameTypePtr]++;

	if (intFrameTypePtr > 0 && intNAKctr >= DownNAKS)
	{
		strcpy(strOldMode, Name(bytFrameTypesForBW[intFrameTypePtr]));
		strOldMode[strlen(strOldMode) - 2] = 0;
		strcpy(strNewMode, Name(bytFrameTypesForBW[intFrameTypePtr - 1]));
		strNewMode[strlen(strNewMode) - 2] = 0;

		ZF_LOGI("[ARDOPprotocol.Gearshift_9] intNAKCtr= %d Shift down from Frame type %s New Mode: %s", intNAKctr, strOldMode, strNewMode);
		intShiftUpDn = -1;

		intAvgQuality = 0;  // Clear intAvgQuality causing the first received Quality to become the new average
		intNAKctr = 0;
		intACKctr = 0;
		intShiftDNs++;
	}
	else if (intAvgQuality > bytShiftUpThresholds[intFrameTypePtr] && intFrameTypePtr < (bytFrameTypesForBWLength - 1) && intACKctr >= 2)
	{
		// if above Hi Trip setup so next call of GetNextFrameData will select a faster mode if one is available

		// But don't shift if we can send remaining data in current mode

		if (intBytesRemaining <= FrameSize[bytFrameTypesForBW[intFrameTypePtr]])
		{
			intShiftUpDn = 0;
			return;
		}

		// if the new mode has been tried before, and immediately failed, don't try again
		// till we get at least 5 sucessive acks

		if (ModeHasBeenTried[intFrameTypePtr + 1] && ModeHasWorked[intFrameTypePtr + 1] == 0 && intACKctr < 5)
		{
			intShiftUpDn = 0;
			return;
		}

		intShiftUpDn = 1;

		ModeHasBeenTried[intFrameTypePtr + intShiftUpDn] = 1;

		strcpy(strNewMode, Name(bytFrameTypesForBW[intFrameTypePtr + intShiftUpDn]));
		strNewMode[strlen(strNewMode) - 2] = 0;

		ZF_LOGI("[ARDOPprotocol.Gearshift_9] ShiftUpDn = %d, AvgQuality=%d New Mode: %s",
			intShiftUpDn, intAvgQuality, strNewMode);

		intAvgQuality = 0;  // Clear intAvgQuality causing the first received Quality to become the new average
		intNAKctr = 0;
		intACKctr = 0;

		intShiftUPs++;
	}
}

// Function to provide exponential averaging for reported received quality from ACK/NAK to data frame.

void ComputeQualityAvg(int intReportedQuality)
{
	float dblAlpha = 0.5f;  // adjust this for exponential averaging speed.  smaller alpha = slower response & smoother averages but less rapid shifting.

	if (intAvgQuality == 0)
	{
		intAvgQuality = intReportedQuality;
		ZF_LOGD("[ARDOPprotocol.ComputeQualityAvg] Initialize AvgQuality= %d", intAvgQuality);
	}
	else
	{
		intAvgQuality = intAvgQuality * (1 - dblAlpha) + (dblAlpha * intReportedQuality) + 0.5f;  // exponential averager
		ZF_LOGD("[ARDOPprotocol.ComputeQualityAvg] Reported Quality= %d  New Avg Quality= %d", intReportedQuality, intAvgQuality);
	}
}

// a function to get then number of carriers from the frame type

int GetNumCarriers(UCHAR bytFrameType)
{
	bool bdummy;
	int intNumCar, dummy;
	char strType[18];
	char strMod[16];

	if (FrameInfo(bytFrameType, &bdummy, &intNumCar, strMod, &dummy, &dummy, &dummy, (UCHAR *)&dummy, strType))
		return intNumCar;

	return 0;
}

// Function to determine the next data frame to send (or IDLE if none)

void SendData()
{
	char strMod[16];
	int Len;

	// Check for ID frame required (every 10 minutes)

	if (blnDISCRepeating)
		return;

	switch (ProtocolState)
	{
	case IDLE:

		ZF_LOGI("[ARDOPProtocol.SendData] Sending Data from IDLE state! Exit SendData");
		return;

	case ISS:

		if (CheckForDisconnect())
			return;

		// In an active ARQ Connection, only send an IDFrame when ProtocolState
		// is ISS or IDLE.  If ProtocolState is IRS and an IDLEFRAME is
		// received when an IDFrame is needed, BREAK to become ISS or IDLE.
		// This leaves the possibility that a station that is IRS and receiving
		// data (not IDLE) may be delayed in sending an IDFrame.  However, it is
		// assumed that usually there will be enough back and forth between IRS
		// and ISS that this is unlikely to last long.  Because some delay is
		// possible, begin trying to send an IDFrame after 9 minutes rather than
		// waiting for a full 10 minutes.
		if(LastIDFrameTime != 0 && Now - LastIDFrameTime > 540000) {  // 9 minutes
			blnEnbARQRpt = false;  // Only send once.
			SendID(&ARQStationLocal, "ARQ 10 minute ID (while ISS)");
		}

		if (bytDataToSendLength > 0)
		{
			ZF_LOGD("[ARDOPprotocol.SendData] DataToSend = %d bytes, In ProtocolState ISS", bytDataToSendLength);

			// Get the data from the buffer here based on current data frame type
			// (Handles protocol Rule 2.1)

			Len = bytQDataInProcessLen = GetNextFrameData(&intShiftUpDn, &bytCurrentFrameType, strMod, false);

			blnLastFrameSentData = true;

			// This mechanism lengthens the intFrameRepeatInterval for multiple carriers (to provide additional decoding time at remote end)
			// This does not slow down the throughput significantly since if an ACK or NAK is received by the sending station
			// the repeat interval does not come into play.

			switch(GetNumCarriers(bytCurrentFrameType))
			{
			case 1:
				intFrameRepeatInterval = ComputeInterFrameInterval(1500);  // fairly conservative based on measured leader from remote end
				break;

			case 2:
				intFrameRepeatInterval = ComputeInterFrameInterval(1700);  // fairly conservative based on measured leader from remote end
				break;

			case 4:
				intFrameRepeatInterval = ComputeInterFrameInterval(1900);  // fairly conservative based on measured leader from remote end
				break;

			case 8:
				intFrameRepeatInterval = ComputeInterFrameInterval(2100);  // fairly conservative based on measured leader from remote end
				break;

			default:
				intFrameRepeatInterval = 2000;  // shouldn't get here
			}

			dttTimeoutTrip = Now;
			blnEnbARQRpt = true;
			ARQState = ISSData;  // Should not be necessary


			char Msg[3000] = "";

			snprintf(Msg, sizeof(Msg), "[Encoding Data to TX] %d bytes as hex values: ", Len);
			for (int i = 0; i < Len; i++)
				snprintf(Msg + strlen(Msg), sizeof(Msg) - strlen(Msg) - 1, "%02X ", bytDataToSend[i]);
			ZF_LOGV("%s", Msg);

			if (utf8_check(bytDataToSend, Len) == NULL)
				ZF_LOGV("[Encoding Data to TX] %d bytes as utf8 text: '%.*s'", Len, Len, bytDataToSend);


			if (strcmp(strMod, "4FSK") == 0)
			{
				if ((EncLen = EncodeFSKData(bytCurrentFrameType, bytDataToSend, Len, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In SendData() for 4FSK Invalid EncLen (%d).", EncLen);
					return;
				}
				if (bytCurrentFrameType >= 0x7A && bytCurrentFrameType <= 0x7D)
					Mod4FSK600BdDataAndPlay(bytEncodedBytes[0], bytEncodedBytes, EncLen, intCalcLeader);  // Modulate Data frame
				else
					Mod4FSKDataAndPlay(bytEncodedBytes[0], bytEncodedBytes, EncLen, intCalcLeader);  // Modulate Data frame
			}
			else  // This handles PSK and QAM
			{
				if ((EncLen = EncodePSKData(bytCurrentFrameType, bytDataToSend, Len, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In SendData() for PSK and QAM Invalid EncLen (%d).", EncLen);
					return;
				}
				ModPSKDataAndPlay(bytEncodedBytes[0], bytEncodedBytes, EncLen, intCalcLeader);  // Modulate Data frame
			}
			return;
		}
		else
		{
			// Nothing to send - set IDLE

			// ReDim bytQDataInProcess(-1)  // added 0.3.1.3
			SetARDOPProtocolState(IDLE);

			blnEnbARQRpt = true;
			dttTimeoutTrip = Now;

			blnLastFrameSentData = false;

			intFrameRepeatInterval = ComputeInterFrameInterval(2000);  // keep IDLE repeats at 2 sec
			ClearDataToSend();  // 0.6.4.2 This insures new OUTOUND queue is updated (to value = 0)

			if ((EncLen = Encode4FSKControl(IDLEFRAME, bytSessionID, bytEncodedBytes)) <= 0) {
				ZF_LOGE("ERROR: In SendData() for IDLE Invalid EncLen (%d).", EncLen);
				return;
			}
			Mod4FSKDataAndPlay(IDLEFRAME, &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent

			ZF_LOGI("[ARDOPprotocol.SendData]  Send IDLE with Repeat, Set ProtocolState=IDLE ");
			return;
		}
	}
}




// a simple function to get an available frame type for the session bandwidth.

int GetNextFrameData(int * intUpDn, UCHAR * bytFrameTypeToSend, UCHAR * strMod, bool blnInitialize)
{
	// Initialize if blnInitialize = true
	// Then call with intUpDn and blnInitialize = false:
	//   intUpDn = 0  // use the current mode pointed to by intFrameTypePtr
	//   intUpdn < 0  // Go to a more robust mode if available limited to the most robust mode for the bandwidth
	//   intUpDn > 0  // Go to a less robust (faster) mode if avaialble, limited to the fastest mode for the bandwidth

	bool blnOdd;
	int intNumCar, intBaud, intDataLen, intRSLen;
	UCHAR bytQualThresh;
	char strType[18];
	char * strShift = NULL;
	int MaxLen;

	if (blnInitialize)  // Get the array of supported frame types in order of Most robust to least robust
	{
		bytFrameTypesForBW = GetDataModes(intSessionBW);
		bytShiftUpThresholds = GetShiftUpThresholds(intSessionBW);

		if (fastStart)
			intFrameTypePtr = (bytFrameTypesForBWLength / 2);  // Start mid way
		else
			intFrameTypePtr = 0;

		bytCurrentFrameType = bytFrameTypesForBW[intFrameTypePtr];

		DrawTXMode(shortName(bytCurrentFrameType));
		updateDisplay();

		ZF_LOGD("[ARDOPprotocol.GetNextFrameData] Initial Frame Type: %s", Name(bytCurrentFrameType));
		*intUpDn = 0;
		return 0;
	}
	if (*intUpDn < 0)  // go to a more robust mode
	{
		if (intFrameTypePtr > 0)
		{
			intFrameTypePtr = max(0, intFrameTypePtr + *intUpDn);
			bytCurrentFrameType = bytFrameTypesForBW[intFrameTypePtr];

			DrawTXMode(shortName(bytCurrentFrameType));
			updateDisplay();

			strShift = "Shift Down";
		}
		*intUpDn = 0;
	}
	else if (*intUpDn > 0)  // go to a faster mode
	{
		if (intFrameTypePtr < bytFrameTypesForBWLength)
		{
			intFrameTypePtr = min(bytFrameTypesForBWLength, intFrameTypePtr + *intUpDn);
			bytCurrentFrameType = bytFrameTypesForBW[intFrameTypePtr];

			DrawTXMode(shortName(bytCurrentFrameType));
			updateDisplay();

			strShift = "Shift Up";
		}
		*intUpDn = 0;
	}
	// If Not objFrameInfo.IsDataFrame(bytCurrentFrameType) Then
	//   Logs.Exception("[ARDOPprotocol.GetNextFrameData] Frame Type " & Format(bytCurrentFrameType, "X") & " not a data type.")
	//   Return Noth

	if ((bytCurrentFrameType & 1) == (bytLastARQDataFrameAcked & 1))
	{
		*bytFrameTypeToSend = bytCurrentFrameType ^ 1;  // This insures toggle of  Odd and Even
		bytLastARQDataFrameSent = *bytFrameTypeToSend;
	}
	else
	{
		*bytFrameTypeToSend = bytCurrentFrameType;
		bytLastARQDataFrameSent = *bytFrameTypeToSend;
	}

	if (strShift == 0)
		ZF_LOGD("[ARDOPprotocol.GetNextFrameData] No shift, Frame Type: %s", Name(bytCurrentFrameType));
	else
		ZF_LOGD("[ARDOPprotocol.GetNextFrameData] %s, Frame Type: %s", strShift, Name(bytCurrentFrameType));

	FrameInfo(bytCurrentFrameType, &blnOdd, &intNumCar, strMod, &intBaud, &intDataLen, &intRSLen, &bytQualThresh, strType);

	MaxLen = intDataLen * intNumCar;

	if (MaxLen > bytDataToSendLength)
		MaxLen = bytDataToSendLength;

	return MaxLen;
}


void InitializeConnection()
{
	// Sub to Initialize before a new Connection

	stationid_init(&ARQStationLocal);   // this stations call sign
	stationid_init(&ARQStationRemote);  // remote station call sign

	intOBBytesToConfirm = 0;  // remaining bytes to confirm
	intBytesConfirmed = 0;  // Outbound bytes confirmed by ACK and squenced
	intReceivedLeaderLen = 0;  // Zero out received leader length (the length of the leader as received by the local station
	intReportedLeaderLen = 0;  // Zero out the Reported leader length the length reported to the remote station
	bytSessionID = 0xFF;  // Session ID
	blnLastPSNPassed = false;  // the last PSN passed true for Odd, false for even.
	blnInitiatedConnection = false;  // flag to indicate if this station initiated the connection
	dblAvgPECreepPerCarrier = 0;  // computed phase error creep
	intTotalSymbols = 0;  // To compute the sample rate error
	intSessionBW = 0;
	bytLastACKedDataFrameType = 0;

	intCalcLeader = LeaderLength;

	ClearQualityStats();
	ClearTuningStats();

	memset(ModeHasWorked, 0, sizeof(ModeHasWorked));
	memset(ModeHasBeenTried, 0, sizeof(ModeHasBeenTried));
	memset(ModeNAKS, 0, sizeof(ModeNAKS));
}

// This sub processes a correctly decoded ConReq frame, decodes it an passed to host for display if it doesn't duplicate the prior passed frame.

void ProcessUnconnectedConReqFrame(int intFrameType, UCHAR * bytData)
{
	static char strLastStringPassedToHost[80] = "";
	char strDisplay[128];
	char * ToCall = strlop(bytData, ' ');
	int Len;

	if (!(intFrameType >= ConReqmin && intFrameType <= ConReqmax))
		return;

	if (ToCall == NULL)  // messed up by COn Req processing
		ToCall = bytData + strlen(bytData) + 1;

	Len = sprintf(strDisplay, " [%s: %s > %s]", Name(intFrameType), bytData, ToCall);
	AddTagToDataAndSendToHost(strDisplay, "ARQ", Len);

}

// This is the main subroutine for processing ARQ frames

void ProcessRcvdARQFrame(UCHAR intFrameType, UCHAR * bytData, int DataLen, bool blnFrameDecodedOK)
{
	// blnFrameDecodedOK should always be true except in the case of a failed data frame ...Which is then NAK'ed if in IRS Data state

	int intReply;
	static UCHAR * strCallsign;
	int intReportedLeaderMS = 0;
	char HostCmd[80];

	// Sleep() was formerly used here to ensure that there was a sufficient
	// minimum delay between a received frame and the transmitted response.
	// This delay is now handled with txSleep() in initFilter(), which is called
	// while preparing to transmit.

	// Note this is called as part of the RX sample poll routine

	switch (ProtocolState)
	{
	case DISC:

		// DISC State *******************************************************************************************

		if (blnFrameDecodedOK && intFrameType == DISCFRAME)
		{
			// Special case to process DISC from previous connection (Ending station must have missed END reply to DISC) Handles protocol rule 1.5

			ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame]  DISC frame received in ProtocolState DISC, Send END with SessionID= %XX Stay in DISC state", bytLastARQSessionID);

			// Send END.  The other station should respond by immediately
			// sending an IDFrame.  tmrFinalID should be set so that IDFrame
			// will not be sent until after the other station's IDFrame, which
			// may or may not include a CW ID, is finished.  Now + 3000 (3
			// seconds) would be sufficient time for END plus the other
			// station's IDFrame.  A CW ID (assumed worst case QQQ0QQQ) could
			// take as long as 10 additional seconds, but is likely to be much
			// less.  Rather than add a lot of extra delay, use Now + 3000, but
			// also require handling of tmpFinalID to wait for the busy detector
			// to indicate that the frequency is not in use.  Even with the
			// delay used by the busy detector, this will normally be a lesser
			// delay, especially when the other station does not send a CW ID.
			tmrFinalID = Now + 3000;
			blnEnbARQRpt = false;

			if ((EncLen = Encode4FSKControl(END, bytLastARQSessionID, bytEncodedBytes)) <= 0) {
				ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for DISC->END Invalid EncLen (%d).", EncLen);
				return;
			}
			Mod4FSKDataAndPlay(END, &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent
			return;
		}

		if (intFrameType == PING && blnFrameDecodedOK)
		{
			ProcessPingFrame(bytData);
			return;
		}


		// Process Connect request to MyCallsign or Aux Call signs  (Handles protocol rule 1.2)

		if (!blnFrameDecodedOK || intFrameType < ConReqmin || intFrameType > ConReqmax)
			return;  // No decode or not a ConReq

		ZF_LOGI("CONREQ From %s to %s Listen = %d", LastDecodedStationCaller.str, LastDecodedStationTarget.str, blnListen);

		if (!blnListen)
			return;  // ignore connect request if not blnListen

		// see if connect request is to MyCallsign or any Aux call sign

		if (IsCallToMe(&LastDecodedStationCaller, &LastDecodedStationTarget, & bytPendingSessionID))  // (Handles protocol rules 1.2, 1.3)
		{
			bool blnLeaderTrippedBusy;

			// This logic works like this:
			// The Actual leader for this received frame should have tripped the busy detector making the last Busy trip very close
			// (usually within 100 ms) of the leader detect time. So the following requires that there be a Busy clear (last busy clear) following
			// the Prior busy Trip AND at least 600 ms of clear time (may need adjustment) prior to the Leader detect and the Last Busy Clear
			// after the Prior Busy Trip. The initialization of times on objBusy.ClearBusy should allow for passing the following test IF there
			// was no Busy detection after the last clear and before the actual reception of the next frame.

			blnLeaderTrippedBusy = (dttLastLeaderDetect - dttLastBusyTrip) < 300;

			if (BusyBlock)
			{
				if ((blnLeaderTrippedBusy && dttLastBusyClear - dttPriorLastBusyTrip < 600)
					|| (!blnLeaderTrippedBusy && dttLastBusyClear - dttLastBusyTrip < 600))
				{
					ZF_LOGI("[ProcessRcvdARQFrame] Con Req Blocked by BUSY!  LeaderTrippedBusy=%d, Prior Last Busy Trip=%d, Last Busy Clear=%d,  Last Leader Detect=%d",
						blnLeaderTrippedBusy, Now - dttPriorLastBusyTrip, Now - dttLastBusyClear, Now - dttLastLeaderDetect);

					ClearBusy();

					// Clear out the busy detector. This necessary to keep the received frame and hold time from causing
					// a continuous busy condition.

					if ((EncLen = Encode4FSKControl(ConRejBusy, bytPendingSessionID, bytEncodedBytes)) <= 0) {
						ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for ConRejBusy Invalid EncLen (%d).", EncLen);
						return;
					}
					Mod4FSKDataAndPlay(ConRejBusy, &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent

					snprintf(HostCmd, sizeof(HostCmd), "REJECTEDBUSY %s", LastDecodedStationCaller.str);
					QueueCommandToHost(HostCmd);
					ZF_LOGI("[STATUS: ARQ CONNECTION REQUEST FROM %s REJECTED, CHANNEL BUSY.]", LastDecodedStationCaller.str);
					snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ CONNECTION REQUEST FROM %s REJECTED, CHANNEL BUSY.", LastDecodedStationCaller.str);
					QueueCommandToHost(HostCmd);

					return;
				}
			}

			intReply = IRSNegotiateBW(intFrameType);  // NegotiateBandwidth

			if (intReply != ConRejBW)  // If not ConRejBW the bandwidth is compatible so answer with correct ConAck frame
			{
				// report oncoming connection
				snprintf(HostCmd, sizeof(HostCmd), "TARGET %s", LastDecodedStationTarget.str);
				QueueCommandToHost(HostCmd);
				InitializeConnection();
				bytDataToSendLength = 0;
				displayCall('<', LastDecodedStationCaller.str);
				blnPending = true;
				blnEnbARQRpt = false;

				tmrIRSPendingTimeout = Now + 10000;  // Triggers a 10 second timeout before auto abort from pending

				// (Handles protocol rule 1.2)

				dttTimeoutTrip = Now;

				SetARDOPProtocolState(IRS);
				ARQState = IRSConAck;  // now connected

				intLastARQDataFrameToHost = -1;  // precondition to an illegal frame type
				// CLear MEM ARQ Stuff
				ResetMemoryARQ();

				// latch the ConReq callsign pair from the decoder
				memcpy(&ARQStationRemote, &LastDecodedStationCaller, sizeof(ARQStationRemote));
				memcpy(&ARQStationLocal, &LastDecodedStationTarget, sizeof(ARQStationLocal));
				memcpy(&ARQStationFinalId, &LastDecodedStationTarget, sizeof(ARQStationFinalId));
				wg_send_rcall(0, ARQStationRemote.str);

				intAvgQuality = 0;  // initialize avg quality
				intReceivedLeaderLen = intLeaderRcvdMs;  // capture the received leader from the remote ISS's ConReq (used for timing optimization)

				if ((EncLen = EncodeConACKwTiming(intReply, intLeaderRcvdMs, bytPendingSessionID, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for ConAck Invalid EncLen (%d).", EncLen);
					return;
				}
				Mod4FSKDataAndPlay(intReply, &bytEncodedBytes[0], EncLen, intARQDefaultDlyMs);  // only returns when all sent
			}
			else
			{
				// ConRejBW  (Incompatible bandwidths)

				// (Handles protocol rule 1.3)

				snprintf(HostCmd, sizeof(HostCmd), "REJECTEDBW %s", ARQStationRemote.str);
				QueueCommandToHost(HostCmd);
				ZF_LOGI("[STATUS: ARQ CONNECTION REJECTED BY %s]", ARQStationRemote.str);
				snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ CONNECTION REJECTED BY %s", ARQStationRemote.str);
				QueueCommandToHost(HostCmd);

				if ((EncLen = Encode4FSKControl(intReply, bytPendingSessionID, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for ConRejBW Invalid EncLen (%d).", EncLen);
					return;
				}
				Mod4FSKDataAndPlay(intReply, &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent
			}
		}
		else
		{
			// Not for us - cancel pending

			QueueCommandToHost("CANCELPENDING");
//			ProcessUnconnectedConReqFrame(intFrameType, bytData);  // displays data if not connnected.
		}
		blnEnbARQRpt = false;
		return;


	case IRS:

		// IRS State ****************************************************************************************
		// Process ConReq, ConAck, DISC, END, Host DISCONNECT, DATA, IDLE, BREAK

		if (ARQState == IRSConAck)  // Process ConAck or ConReq if reply ConAck sent above in Case ProtocolState.DISC was missed by ISS
		{
			if (!blnFrameDecodedOK)
				return;  // no reply if no correct decode

			// ConReq processing (to handle case of ISS missing initial ConAck from IRS)

			if (intFrameType >= ConReqmin && intFrameType <= ConReqmax)  // Process Connect request to MyCallsign or Aux Call signs as for DISC state above (ISS must have missed initial ConACK from ProtocolState.DISC state)
			{
				if (!blnListen)
					return;

				// see if connect request is to MyCallsign or any Aux call sign
				if (IsCallToMe(&LastDecodedStationCaller, &LastDecodedStationTarget, &bytPendingSessionID))  // (Handles protocol rules 1.2, 1.3)
				{
					intReply = IRSNegotiateBW(intFrameType);  // NegotiateBandwidth

					if (intReply != ConRejBW)  // If not ConRejBW the bandwidth is compatible so answer with correct ConAck frame
					{
						// Note: CONNECTION and STATUS notices were already sent from  Case ProtocolState.DISC above...no need to duplicate

						SetARDOPProtocolState(IRS);
						ARQState = IRSConAck;  // now connected

						intLastARQDataFrameToHost = -1;  // precondition to an illegal frame type
						// CLear MEM ARQ Stuff
						ResetMemoryARQ();

						intAvgQuality = 0;  // initialize avg quality
						intReceivedLeaderLen = intLeaderRcvdMs;  // capture the received leader from the remote ISS's ConReq (used for timing optimization)
						InitializeConnection();


						bytDataToSendLength = 0;

						dttTimeoutTrip = Now;

						// Stop and restart the Pending timer upon each ConReq received to ME
						tmrIRSPendingTimeout= Now + 10000;  // Triggers a 10 second timeout before auto abort from pending

						// latch the ConReq callsign pair from the decoder
						memcpy(&ARQStationRemote, &LastDecodedStationCaller, sizeof(ARQStationRemote));
						memcpy(&ARQStationLocal, &LastDecodedStationTarget, sizeof(ARQStationLocal));
						memcpy(&ARQStationFinalId, &LastDecodedStationTarget, sizeof(ARQStationFinalId));

						if ((EncLen = EncodeConACKwTiming(intReply, intLeaderRcvdMs, bytPendingSessionID, bytEncodedBytes)) <= 0) {
							ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for ConAck Invalid EncLen (%d).", EncLen);
							return;
						}
						Mod4FSKDataAndPlay(intReply, &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent
						// No delay to allow ISS to measure its TX>RX delay
						return;
					}

					// ConRejBW  (Incompatible bandwidths)

					// (Handles protocol rule 1.3)

					snprintf(HostCmd, sizeof(HostCmd), "REJECTEDBW %s", ARQStationRemote.str);
					QueueCommandToHost(HostCmd);
					ZF_LOGI("[STATUS: ARQ CONNECTION FROM %s REJECTED, INCOMPATIBLE BANDWIDTHS.]", ARQStationRemote.str);
					snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ CONNECTION FROM %s REJECTED, INCOMPATIBLE BANDWIDTHS.", ARQStationRemote.str);
					QueueCommandToHost(HostCmd);

					if ((EncLen = Encode4FSKControl(intReply, bytPendingSessionID, bytEncodedBytes)) <= 0) {
						ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for ConRejBW Invalid EncLen (%d).", EncLen);
						return;
					}
					Mod4FSKDataAndPlay(intReply, &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent

					return;
				}

				// this normally shouldn't happen but is put here in case another Connect request to a different station also on freq...may want to change or eliminate this

				// if (DebugLog)
					// WriteDebug("[ARDOPprotocol.ProcessRcvdARQFrame] Call to another target while in ProtocolState.IRS, ARQSubStates.IRSConAck...Ignore");

				return;
			}

			// ConAck processing from ISS

			if (intFrameType >= ConAckmin && intFrameType <= ConAckmax)  // Process ConACK frames from ISS confirming Bandwidth and providing ISS's received leader info.
			{
				switch (intFrameType)
				{
				case ConAck200:
					intSessionBW = 200;
					break;
				case ConAck500:
					intSessionBW = 500;
					break;
				case ConAck1000:
					intSessionBW = 1000;
					break;
				case ConAck2000:
					intSessionBW = 2000;
					break;
				}

				CalculateOptimumLeader(10 * bytData[0], LeaderLength);

				bytSessionID = bytPendingSessionID;  // This sets the session ID now

				blnARQConnected = true;
				blnPending = false;
				tmrIRSPendingTimeout = 0;

				dttTimeoutTrip = Now;
				ARQState = IRSData;
				intLastARQDataFrameToHost = -1;
				intTrackingQuality = -1;
				intNAKctr = 0;

				blnEnbARQRpt = false;
				snprintf(HostCmd, sizeof(HostCmd), "CONNECTED %s %d", ARQStationRemote.str, intSessionBW);
				QueueCommandToHost(HostCmd);

				ZF_LOGI("[STATUS: ARQ CONNECTION FROM %s: SESSION BW = %d HZ]", ARQStationRemote.str, intSessionBW);
				snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ CONNECTION FROM %s: SESSION BW = %d HZ", ARQStationRemote.str, intSessionBW);
				QueueCommandToHost(HostCmd);

				// Send ACK
				if ((EncLen = EncodeDATAACK(intLastRcvdFrameQuality, bytSessionID, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for ConAck->DataACK Invalid EncLen (%d).", EncLen);
					return;
				}
				Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent

				// Initialize the frame type and pointer based on bandwidth (added 0.3.1.3)

				GetNextFrameData(&intShiftUpDn, 0, NULL, true);  // just sets the initial data, frame type, and sets intShiftUpDn= 0

				// Update the main form menu status lable
				// Dim stcStatus As Status = Nothing
				// stcStatus.ControlName = "mnuBusy"
				// stcStatus.Text = "Connected " & .strRemoteCallsign
				// queTNCStatus.Enqueue(stcStatus)

				return;
			}
			ZF_LOGD(
				"Received correctly decoded frame of unexpected FrameType=%s"
				" while ARQState == IRSConAck.  Ignoring.",
				Name(intFrameType));
			return;
		}

		if (ARQState == IRSData || ARQState == IRSfromISS)  // Process Data or ConAck if ISS failed to receive ACK confirming bandwidth so ISS repeated ConAck
		{
			// ConAck processing from ISS

			if (intFrameType >= ConAckmin && intFrameType <= ConAckmax)  // Process ConACK frames from ISS confirming Bandwidth and providing ISS's received leader info.
			{
				// Process ConACK frames (ISS failed to receive prior ACK confirming session bandwidth so repeated ConACK)

				switch (intFrameType)
				{
				case ConAck200:
					intSessionBW = 200;
					break;
				case ConAck500:
					intSessionBW = 500;
					break;
				case ConAck1000:
					intSessionBW = 1000;
					break;
				case ConAck2000:
					intSessionBW = 2000;
					break;
				}

				dttTimeoutTrip = Now;

				// Send ACK
				if ((EncLen = EncodeDATAACK(intLastRcvdFrameQuality, bytSessionID, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for repeat ConAck->DataACK Invalid EncLen (%d).", EncLen);
					return;
				}
				Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent
				return;
			}

			// handles DISC from ISS

			if (blnFrameDecodedOK && intFrameType == DISCFRAME)  // IF DISC received from ISS Handles protocol rule 1.5
			{
				ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame]  DISC frame received in ProtocolState IRS, IRSData...going to DISC state");
				if (AccumulateStats)
					LogStats();

				QueueCommandToHost("DISCONNECTED");  // Send END
				ZF_LOGI("[STATUS: ARQ CONNECTION ENDED WITH %s]", ARQStationRemote.str);
				snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ CONNECTION ENDED WITH %s", ARQStationRemote.str);
				QueueCommandToHost(HostCmd);

				bytLastARQSessionID = bytSessionID;  // capture this session ID to allow answering DISC from DISC state if ISS missed Sent END

				ClearDataToSend();
				// Send END.  The other station should respond by immediately
				// sending an IDFrame.  tmrFinalID should be set so that IDFrame
				// will not be sent until after the other station's IDFrame, which
				// may or may not include a CW ID, is finished.  Now + 3000 (3
				// seconds) would be sufficient time for END plus the other
				// station's IDFrame.  A CW ID (assumed worst case QQQ0QQQ) could
				// take as long as 10 additional seconds, but is likely to be much
				// less.  Rather than add a lot of extra delay, use Now + 3000, but
				// also require handling of tmpFinalID to wait for the busy detector
				// to indicate that the frequency is not in use.  Even with the
				// delay used by the busy detector, this will normally be a lesser
				// delay, especially when the other station does not send a CW ID.
				tmrFinalID = Now + 3000;
				blnDISCRepeating = false;

				SetARDOPProtocolState(DISC);
				InitializeConnection();
				blnEnbARQRpt = false;

				if ((EncLen = Encode4FSKControl(END, bytSessionID, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for END Invalid EncLen (%d).", EncLen);
					return;
				}
				Mod4FSKDataAndPlay(END, &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent
				return;
			}

			// handles END from ISS

			if (blnFrameDecodedOK && intFrameType == END)  // IF END received from ISS
			{
				ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame]  END frame received in ProtocolState IRS, IRSData...going to DISC state");
				if (AccumulateStats)
					LogStats();

				QueueCommandToHost("DISCONNECTED");
				ZF_LOGI("[STATUS: ARQ CONNECTION ENDED WITH %s]", ARQStationRemote.str);
				snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ CONNECTION ENDED WITH %s", ARQStationRemote.str);
				QueueCommandToHost(HostCmd);
				blnDISCRepeating = false;
				ClearDataToSend();

				SetARDOPProtocolState(DISC);

				// SendID will default to Callsign if ARQStationLocal is not valid.
				SendID(&ARQStationLocal, "Rcvd END in IRSData or IRSfromISS ARQState");

				InitializeConnection();
				blnEnbARQRpt = false;
				return;
			}

			// handles BREAK from remote IRS that failed to receive ACK

			if (blnFrameDecodedOK && intFrameType == BREAK)
			{
				ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame]  BREAK received in ProtocolState %s , IRSData. Sending ACK", ARDOPStates[ProtocolState]);

				blnEnbARQRpt = false;  // setup for no repeats

				// Send ACK
				if ((EncLen = EncodeDATAACK(100, bytSessionID, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for repeat BREAK->DataACK Invalid EncLen (%d).", EncLen);
					return;
				}
				Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent
				dttTimeoutTrip = Now;
				return;
			}

			if (blnFrameDecodedOK && intFrameType == IDLEFRAME)  // IF IDLE received from ISS
			{
				ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame]  IDLE received in ProtocolState %s substate %s", ARDOPStates[ProtocolState], ARQSubStates[ARQState]);

				blnEnbARQRpt = false;  // setup for no repeats

				if (CheckForDisconnect())
					return;

				if ((AutoBreak && bytDataToSendLength > 0) || blnBREAKCmd
					|| (LastIDFrameTime != 0 && Now - LastIDFrameTime > 540000)  // 9 minutes, so BREAK
				) {
					// In an active ARQ Connection, only send an IDFrame when ProtocolState
					// is ISS or IDLE.  If ProtocolState is IRS and an IDLEFRAME is
					// received when an IDFrame is needed, BREAK to become ISS or IDLE.
					// This leaves the possibility that a station that is IRS and receiving
					// data (not IDLE) may be delayed in sending an IDFrame.  However, it is
					// assumed that usually there will be enough back and forth between IRS
					// and ISS that this is unlikely to last long.  Because some delay is
					// possible, begin trying to send an IDFrame after 9 minutes rather than
					// waiting for a full 10 minutes.
					if(LastIDFrameTime != 0 && Now - LastIDFrameTime > 540000)  // 9 minutes
						ZF_LOGD("Need 10 minute ID, but IRS, so BREAK on receiving IDLEFRAME");

					// keep BREAK Repeats fairly short (preliminary value 1 - 3 seconds)
					intFrameRepeatInterval = ComputeInterFrameInterval(1000 + rand() % 2000);

					SetARDOPProtocolState(IRStoISS);  // (ONLY IRS State where repeats are used)
					ZF_LOGI("[STATUS: QUEUE BREAK new Protocol State IRStoISS]");
					SendCommandToHost("STATUS QUEUE BREAK new Protocol State IRStoISS");
					blnEnbARQRpt = true;  // setup for repeats until changeover
					if ((EncLen = Encode4FSKControl(BREAK, bytSessionID, bytEncodedBytes)) <= 0) {
						ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for IDLE->BREAK Invalid EncLen (%d).", EncLen);
						return;
					}
					Mod4FSKDataAndPlay(BREAK, &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent
				}
				else
				{
					// Send ACK
					dttTimeoutTrip = Now;
					if ((EncLen = EncodeDATAACK(100, bytSessionID, bytEncodedBytes)) <= 0) {
						ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for IDLE->DataACK Invalid EncLen (%d).", EncLen);
						return;
					}
					Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent
					dttTimeoutTrip = Now;
				}
				return;
			}

			// handles DISCONNECT command from host

//			if (CheckForDisconnect())
//				return;

			// This handles normal data frames

			if (blnFrameDecodedOK && IsDataFrame(intFrameType))  // Frame even/odd toggling will prevent duplicates in case of missed ACK
			{
				if (intRmtLeaderMeas == 0)
				{
					intRmtLeaderMeas = intRmtLeaderMeasure;  // capture the leader timing of the first ACK from IRS, use this value to help compute repeat interval.
					ZF_LOGD("[ARDOPprotocol.ProcessRcvdARQFrame] IRS (receiving data) RmtLeaderMeas=%d ms", intRmtLeaderMeas);
				}

				if (ARQState == IRSData && blnBREAKCmd && intFrameType != bytLastACKedDataFrameType)
				{
					// if BREAK Command and first time the new frame type is seen then
					// Handles Protocol Rule 3.4
					// This means IRS wishes to BREAK so start BREAK repeats and go to protocol state IRStoISS
					// This implements the  important IRS>ISS changeover...may have to adjust parameters here for reliability
					// The incorporation of intFrameType <> objMain.bytLastACKedDataFrameType insures only processing a BREAK on a frame
					// before it is ACKed to insure the ISS will correctly capture the frame being sent in its purge buffer.

					dttTimeoutTrip = Now;
					blnBREAKCmd = false;
					blnEnbARQRpt = true;  // setup for repeats until changeover
					intFrameRepeatInterval = ComputeInterFrameInterval(1000 + rand() % 2000);
					SetARDOPProtocolState(IRStoISS);  // (ONLY IRS State where repeats are used)
					ZF_LOGI("[STATUS: QUEUE BREAK new Protocol State IRStoISS]");
					SendCommandToHost("STATUS QUEUE BREAK new Protocol State IRStoISS");
					if ((EncLen = Encode4FSKControl(BREAK, bytSessionID, bytEncodedBytes)) <= 0) {
						ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for Rule 3.4 BREAK Invalid EncLen (%d).", EncLen);
						return;
					}
					Mod4FSKDataAndPlay(BREAK, &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent
					return;
				}

				if (intFrameType != intLastARQDataFrameToHost)  // protects against duplicates if ISS missed IRS's ACK and repeated the same frame
				{
					AddTagToDataAndSendToHost(bytData, "ARQ", DataLen);  // only correct data in proper squence passed to host
					intLastARQDataFrameToHost = intFrameType;
					dttTimeoutTrip = Now;
				}
				else
					ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame] Frame with same Type - Discard");

				if (ARQState == IRSfromISS)
				{
					ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame] Data Rcvd in ProtocolState=IRSData, Substate IRSfromISS Go to Substate IRSData");
					ARQState = IRSData;  // This substate change is the final completion of ISS to IRS changeover and allows the new IRS to now break if desired (Rule 3.5)
				}


				// Always ACK good data frame ...ISS may have missed last ACK
				// Send ACK
				blnEnbARQRpt = false;
				if ((EncLen = EncodeDATAACK(intLastRcvdFrameQuality, bytSessionID, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for DataACK Invalid EncLen (%d).", EncLen);
					return;
				}
				Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent
				bytLastACKedDataFrameType = intFrameType;
				return;
			}

			// handles Data frame which did not decode correctly but was previously ACKed to ISS  Rev 0.4.3.1  2/28/2016  RM
			// this to handle marginal decoding cases where ISS missed an original ACK from IRS, IRS passed that data to host, and channel has
			// deteriorated to where data decode is now now not possible.

			if ((!blnFrameDecodedOK) && intFrameType == bytLastACKedDataFrameType)
			{
				// Send ACK
				blnEnbARQRpt = false;
				if ((EncLen = EncodeDATAACK(intLastRcvdFrameQuality, bytSessionID, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for repeat DataACK Invalid EncLen (%d).", EncLen);
					return;
				}
				Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent
				ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame] Data Decode Failed but Frame Type matched last ACKed. Send ACK, data already passed to host. ");

				// handles Data frame which did not decode correctly (Failed CRC) and hasn't been acked before
			}
			else if ((!blnFrameDecodedOK) && IsDataFrame(intFrameType))  // Incorrectly decoded frame. Send NAK with Quality
			{
				if (ARQState == IRSfromISS)
				{
					ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame] Data Frame Type Rcvd in ProtocolState=IRSData, Substate IRSfromISS Go to Substate IRSData");

					ARQState = IRSData;  // This substate change is the final completion of ISS to IRS changeover and allows the new IRS to now break if desired (Rule 3.5)
				}
				// Send NAK
				blnEnbARQRpt = false;
				if ((EncLen = EncodeDATANAK(intLastRcvdFrameQuality, bytSessionID, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for DataNAK Invalid EncLen (%d).", EncLen);
					return;
				}
				Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
				return;
			}
			if (blnFrameDecodedOK)
				ZF_LOGD(
					"Received correctly decoded frame of unexpected FrameType=%s"
					" while ARQState == %s.  Ignoring.",
					Name(intFrameType),
					ARQSubStates[ARQState]);
			else
				ZF_LOGD(
					"Received (FAILED decode) frame of unexpected FrameType=%s"
					" while ARQState == %s.  Ignoring.  It may also have been"
					" ignored because of the failed decode.",
					Name(intFrameType),
					ARQSubStates[ARQState]);
			return;
		}
		ZF_LOGD(
			"Received frame of FrameType=%s, but unable to process due to"
			" unexpected ARQState == %s with ProtocolState == IRS."
			" Ignoring received frame.",
			Name(intFrameType),
			ARQSubStates[ARQState]);
		return;


		// IRStoISS State **************************************************************************************************

	case IRStoISS:  // In this state answer any data frame with a BREAK. If ACK received go to Protocol State ISS
		ZF_LOGD(
			"In ProtocolState == IRStoISS, Nothing is done in"
			" ProcessRcvdARQFrame() for a received FrameType=%s."
			" If this is anything other than a DataACK, another"
			" BREAK should have been sent by ProcessNewSamples().",
			Name(intFrameType));
		// TODO: Review why this is done in ProcessNewSamples() rather than
		// here, and verify that it is done reliably/correctly.
		return;

	case IDLE:  // The state where the ISS has no data to send and is looking for a BREAK from the IRS

		if (!blnFrameDecodedOK)
			return;  // No decode so continue to wait

		if (intFrameType >= DataACKmin) {
			if (bytDataToSendLength > 0)  // If ACK and Data to send
			{
				ZF_LOGI("[ARDOPprotocol.ProcessedRcvdARQFrame] Protocol state IDLE, ACK Received with Data to send. Go to ISS Data state.");

				SetARDOPProtocolState(ISS);
				ARQState = ISSData;
				SendData(false);
				return;
			}
			// In an active ARQ Connection, only send an IDFrame when ProtocolState
			// is ISS or IDLE.  If ProtocolState is IRS and an IDLEFRAME is
			// received when an IDFrame is needed, BREAK to become ISS or IDLE.
			// This leaves the possibility that a station that is IRS and receiving
			// data (not IDLE) may be delayed in sending an IDFrame.  However, it is
			// assumed that usually there will be enough back and forth between IRS
			// and ISS that this is unlikely to last long.  Because some delay is
			// possible, begin trying to send an IDFrame after 9 minutes rather than
			// waiting for a full 10 minutes.
			if(LastIDFrameTime != 0 && Now - LastIDFrameTime > 540000) {  // 9 minutes
				blnEnbARQRpt = false;  // Only send once.
				SendID(&ARQStationLocal, "ARQ 10 minute ID (while IDLE)");

				// Then send repeating IDLE
				// The following is copied from SendData() when there is no more data
				blnEnbARQRpt = true;
				blnLastFrameSentData = false;
				intFrameRepeatInterval = ComputeInterFrameInterval(2000);  // keep IDLE repeats at 2 sec

				if ((EncLen = Encode4FSKControl(IDLEFRAME, bytSessionID, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In SendData() for IDLE Invalid EncLen (%d).", EncLen);
					return;
				}
				Mod4FSKDataAndPlay(IDLEFRAME, &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent

				ZF_LOGI("[ARDOPprotocol.SendData]  Send IDLE with Repeat (after IDFrame), ProtocolState=IDLE ");
				return;
			}

			ZF_LOGI("[ARDOPprotocol.ProcessedRcvdARQFrame] Protocol state IDLE, ACK Received with No Data to send. Ignore, will repeat IDLE.");
			// DataACK but no data to send

			return;
		}
		// process BREAK here Send ID if over 10 min.

		if (intFrameType == BREAK)
		{
			// Initiate the transisiton to IRS

			dttTimeoutTrip = Now;
			blnEnbARQRpt = false;
			// Send ACK
			if ((EncLen = EncodeDATAACK(100, bytSessionID, bytEncodedBytes)) <= 0) {
				ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for BREAK->DataACK Invalid EncLen (%d).", EncLen);
				return;
			}
			Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);

			ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame] BREAK Rcvd from IDLE, Go to IRS, Substate IRSfromISS");
			ZF_LOGI("[STATUS: BREAK received from Protocol State IDLE, new state IRS]");
			SendCommandToHost("STATUS BREAK received from Protocol State IDLE, new state IRS");
			SetARDOPProtocolState(IRS);
			// Substate IRSfromISS enables processing Rule 3.5 later
			ARQState = IRSfromISS;

			intLinkTurnovers += 1;
			intLastARQDataFrameToHost = -1;  // precondition to an illegal frame type (insures the new IRS does not reject a frame)
			// CLear MEM ARQ Stuff
			ResetMemoryARQ();
			return;
		}
		if (intFrameType == DISCFRAME)  // IF DISC received from IRS Handles protocol rule 1.5
		{
			ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame]  DISC frame received Send END...go to DISC state");

			if (AccumulateStats)
				LogStats();

			QueueCommandToHost("DISCONNECTED");
			ZF_LOGI("[STATUS: ARQ CONNECTION ENDED WITH %s]", ARQStationRemote.str);
			snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ CONNECTION ENDED WITH %s ", ARQStationRemote.str);
			QueueCommandToHost(HostCmd);
			// Send END.  The other station should respond by immediately
			// sending an IDFrame.  tmrFinalID should be set so that IDFrame
			// will not be sent until after the other station's IDFrame, which
			// may or may not include a CW ID, is finished.  Now + 3000 (3
			// seconds) would be sufficient time for END plus the other
			// station's IDFrame.  A CW ID (assumed worst case QQQ0QQQ) could
			// take as long as 10 additional seconds, but is likely to be much
			// less.  Rather than add a lot of extra delay, use Now + 3000, but
			// also require handling of tmpFinalID to wait for the busy detector
			// to indicate that the frequency is not in use.  Even with the
			// delay used by the busy detector, this will normally be a lesser
			// delay, especially when the other station does not send a CW ID.
			tmrFinalID = Now + 3000;
			blnDISCRepeating = false;
			if ((EncLen = Encode4FSKControl(END, bytSessionID, bytEncodedBytes)) <= 0) {
				ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for DISC->END Invalid EncLen (%d).", EncLen);
				return;
			}
			Mod4FSKDataAndPlay(END, &bytEncodedBytes[0], EncLen, LeaderLength);
			bytLastARQSessionID = bytSessionID;  // capture this session ID to allow answering DISC from DISC state
			ClearDataToSend();
			SetARDOPProtocolState(DISC);
			blnEnbARQRpt = false;
			return;
		}
		if (intFrameType == END)
		{
			ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame]  END received ... going to DISC state");
			if (AccumulateStats)
				LogStats();
			QueueCommandToHost("DISCONNECTED");
			ZF_LOGI("[STATUS: ARQ CONNECTION ENDED WITH %s]", ARQStationRemote.str);
			snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ CONNECTION ENDED WITH %s ", ARQStationRemote.str);
			QueueCommandToHost(HostCmd);
			ClearDataToSend();

			// SendID will default to Callsign if ARQStationLocal is not valid.
			SendID(&ARQStationLocal, "Rcvd END in IDLE ProtocolState");

			SetARDOPProtocolState(DISC);
			blnEnbARQRpt = false;
			blnDISCRepeating = false;
			return;
		}
		// Shouldn'r get here ??
		if (blnFrameDecodedOK)
			ZF_LOGD(
				"Received correctly decoded frame of unexpected FrameType=%s"
				" while ProtocolState == IDLE and bytDataToSendLength=%d."
				" Ignoring.",
				Name(intFrameType),
				bytDataToSendLength);
		else
			ZF_LOGD(
				"Received (FAILED decode) frame of unexpected FrameType=%s"
				" while ProtocolState == IDLE and bytDataToSendLength=%d."
				" Ignoring.",
				Name(intFrameType),
				bytDataToSendLength);
		return;

	// ISS state **************************************************************************************

	case ISS:

		if (ARQState == ISSConReq)  // The ISS is sending Connect requests waiting for a ConAck from the remote IRS
		{
			// Session ID should be correct already (set by the ISS during first Con Req to IRS)
			// Process IRS Conack and capture IRS received leader for timing optimization
			// Process ConAck from IRS (Handles protocol rule 1.4)

			if (!blnFrameDecodedOK)
				return;

			if (intFrameType >= ConAckmin && intFrameType <= ConAckmax)  // Process ConACK frames from IRS confirming BW is compatible and providing received leader info.
			{
				UCHAR bytDummy = 0;

				switch (intFrameType)
				{
				case ConAck200:
					intSessionBW = 200;
					break;
				case ConAck500:
					intSessionBW = 500;
					break;
				case ConAck1000:
					intSessionBW = 1000;
					break;
				case ConAck2000:
					intSessionBW = 2000;
					break;
				}

				CalculateOptimumLeader(10 * bytData[0], LeaderLength);

				// Initialize the frame type based on bandwidth

				GetNextFrameData(&intShiftUpDn, &bytDummy, NULL, true);  // just sets the initial data frame type and sets intShiftUpDn = 0

				// prepare the ConACK answer with received leader length

				intReceivedLeaderLen = intLeaderRcvdMs;

				intFrameRepeatInterval = 2000;
				blnEnbARQRpt = true;  // Setup for repeats of the ConACK if no answer from IRS
				ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame] Compatible bandwidth received from IRS ConAck: %d Hz", intSessionBW);
				ARQState = ISSConAck;

				if ((EncLen = EncodeConACKwTiming(intFrameType, intReceivedLeaderLen, bytSessionID, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for ConAck->ConAck Invalid EncLen (%d).", EncLen);
					return;
				}
				Mod4FSKDataAndPlay(intFrameType, &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent
				return;
			}

			if (intFrameType == ConRejBusy)  // ConRejBusy Handles Protocol Rule 1.5
			{
				ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame] ConRejBusy received from %s ABORT Connect Request", ARQStationRemote.str);
				snprintf(HostCmd, sizeof(HostCmd), "REJECTEDBUSY %s", ARQStationRemote.str);
				QueueCommandToHost(HostCmd);
				ZF_LOGI("[STATUS: ARQ CONNECTION REJECTED BY %s, REMOTE STATION BUSY.]", ARQStationRemote.str);
				snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ CONNECTION REJECTED BY %s, REMOTE STATION BUSY.", ARQStationRemote.str);
				QueueCommandToHost(HostCmd);
				Abort();
				return;
			}
			if (intFrameType == ConRejBW)  // ConRejBW Handles Protocol Rule 1.3
			{
				ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame] ConRejBW received from %s ABORT Connect Request", ARQStationRemote.str);
				snprintf(HostCmd, sizeof(HostCmd), "REJECTEDBW %s", ARQStationRemote.str);
				QueueCommandToHost(HostCmd);
				ZF_LOGI("[STATUS: ARQ CONNECTION REJECTED BY %s, INCOMPATIBLE BW.]", ARQStationRemote.str);
				snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ CONNECTION REJECTED BY %s, INCOMPATIBLE BW.", ARQStationRemote.str);
				QueueCommandToHost(HostCmd);
				Abort();
				return;
			}
			if (blnFrameDecodedOK)
				ZF_LOGD(
					"Received correctly decoded frame of unexpected FrameType=%s"
					" while ARQState == ISSConReq. Ignoring.",
					Name(intFrameType));
			else
				ZF_LOGD(
					"Received (FAILED decode) frame of unexpected FrameType=%s"
					" while ARQState == ISSConReq. Ignoring.",
					Name(intFrameType));
			return;  // Shouldn't get here
		}
		if (ARQState == ISSConAck)
		{
			if ((blnFrameDecodedOK && intFrameType >= DataACKmin) || intFrameType == BREAK)  // if ACK received then IRS correctly received the ISS ConACK
			{
				// Note BREAK added per input from John W. to handle case where IRS has data to send and ISS missed the IRS's ACK from the ISS's ConACK Rev 0.5.3.1
				// Not sure about this. Not needed with AUTOBREAK but maybe with BREAK command

				if (intRmtLeaderMeas == 0)
				{
					intRmtLeaderMeas = intRmtLeaderMeasure;  // capture the leader timing of the first ACK from IRS, use this value to help compute repeat interval.
					ZF_LOGD("[ARDOPprotocol.ProcessRcvdARQFrame] ISS RmtLeaderMeas=%d ms", intRmtLeaderMeas);
				}
				intAvgQuality = 0;  // initialize avg quality
				blnEnbARQRpt = false;  // stop the repeats of ConAck and enables SendDataOrIDLE to get next IDLE or Data frame

				if (intFrameType >= DataACKmin)
					ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame] ACK received in ARQState %s ", ARQSubStates[ARQState]);

				if (intFrameType == BREAK)
					ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame] BREAK received in ARQState %s Processed as implied ACK", ARQSubStates[ARQState]);

				blnARQConnected = true;
				bytLastARQDataFrameAcked = 1;  // initialize to Odd value to start transmission frame on Even
				blnPending = false;

				snprintf(HostCmd, sizeof(HostCmd), "CONNECTED %s %d", ARQStationRemote.str, intSessionBW);
				QueueCommandToHost(HostCmd);
				ZF_LOGI("[STATUS: ARQ CONNECTION ESTABLISHED WITH %s, SESSION BW = %d HZ]", ARQStationRemote.str, intSessionBW);
				snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ CONNECTION ESTABLISHED WITH %s, SESSION BW = %d HZ", ARQStationRemote.str, intSessionBW);
				QueueCommandToHost(HostCmd);

				ARQState = ISSData;

				intTrackingQuality = -1;  // initialize tracking quality to illegal value
				intNAKctr = 0;

				if (intFrameType == BREAK && bytDataToSendLength == 0)
				{
					// Initiate the transisiton to IRS

					ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame] implied ACK, no data to send so action BREAK, send ACK");

					ClearDataToSend();
					blnEnbARQRpt = false;  // setup for no repeats

					// Send ACK
					if ((EncLen = EncodeDATAACK(100, bytSessionID, bytEncodedBytes)) <= 0) {
						ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for ConAck + BREAK->DataACK Invalid EncLen (%d).", EncLen);
						return;
					}
					Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent

					dttTimeoutTrip = Now;
					SetARDOPProtocolState(IRS);
					ARQState = IRSfromISS;  // Substate IRSfromISS allows processing of Rule 3.5 later

					intLinkTurnovers += 1;
					intLastARQDataFrameToHost = -1;  // precondition to an illegal frame type (insures the new IRS does not reject a frame)
					// CLear MEM ARQ Stuff
					ResetMemoryARQ();
					LastDataFrameType = intFrameType;
				}
				else
					SendData();  // Send new data from outbound queue and set up repeats
				return;
			}

			if (blnFrameDecodedOK && intFrameType == ConRejBusy)  // ConRejBusy
			{
				ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame] ConRejBusy received in ARQState %s. Going to Protocol State DISC", ARQSubStates[ARQState]);

				snprintf(HostCmd, sizeof(HostCmd), "REJECTEDBUSY %s", ARQStationRemote.str);
				QueueCommandToHost(HostCmd);
				ZF_LOGI("[STATUS: ARQ CONNECTION REJECTED BY %s]", ARQStationRemote.str);
				snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ CONNECTION REJECTED BY %s", ARQStationRemote.str);
				QueueCommandToHost(HostCmd);

				SetARDOPProtocolState(DISC);
				InitializeConnection();
				return;
			}
			if (blnFrameDecodedOK && intFrameType == ConRejBW)  // ConRejBW
			{
				// if (DebugLog) WriteDebug("[ARDOPprotocol.ProcessRcvdARQFrame] ConRejBW received in ARQState " & ARQState.ToString & " Going to Protocol State DISC")

				snprintf(HostCmd, sizeof(HostCmd), "REJECTEDBW %s", ARQStationRemote.str);
				QueueCommandToHost(HostCmd);
				ZF_LOGI("[STATUS: ARQ CONNECTION REJECTED BY %s]", ARQStationRemote.str);
				snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ CONNECTION REJECTED BY %s", ARQStationRemote.str);
				QueueCommandToHost(HostCmd);

				SetARDOPProtocolState(DISC);
				InitializeConnection();
				return;
			}
			if (blnFrameDecodedOK)
				ZF_LOGD(
					"Received correctly decoded frame of unexpected FrameType=%s"
					" while ARQState == ISSConAck. Ignoring.",
					Name(intFrameType));
			else
				ZF_LOGD(
					"Received (FAILED decode) frame of unexpected FrameType=%s"
					" while ARQState == ISSConAck. Ignoring.",
					Name(intFrameType));
			return;  // Shouldn't get here
		}

		if (ARQState == ISSData)
		{
			if (CheckForDisconnect())
				return;  // DISC sent

			if (!blnFrameDecodedOK)
				return;  // No decode so continue repeating either data or idle

			// process ACK, NAK, DISC, END or BREAK here. Send ID if over 10 min.

			if (intFrameType >= DataACKmin)  // if ACK
			{
				dttTimeoutTrip = Now;

				if (blnLastFrameSentData)
				{
					intACKctr++;

					SetLED(PKTLED, true);  // Flash LED
					PKTLEDTimer = Now + 200;  // for 200 mS

					bytLastARQDataFrameAcked = bytLastARQDataFrameSent;

					if (bytQDataInProcessLen)
					{
						RemoveDataFromQueue(bytQDataInProcessLen);
						bytQDataInProcessLen = 0;
					}

					ComputeQualityAvg(38 + 2 * (intFrameType - DataACKmin));  // Average ACK quality to exponential averager.
					Gearshift_9();  // gear shift based on average quality
				}
				intNAKctr = 0;
				blnEnbARQRpt = false;  // stops repeat and forces new data frame or IDLE

				SendData();  // Send new data from outbound queue and set up repeats
				return;
			}

			if (intFrameType == BREAK)
			{
				if (!blnARQConnected)
				{
					// Handles the special case of this ISS missed last Ack from the
					// IRS ConAck and remote station is now BREAKing to become ISS.
					// clean up the connection status

					blnARQConnected = true;
					blnPending = false;

					snprintf(HostCmd, sizeof(HostCmd), "CONNECTED %s %d", ARQStationRemote.str, intSessionBW);
					QueueCommandToHost(HostCmd);
					ZF_LOGI("[STATUS: ARQ CONNECTION ESTABLISHED WITH %s, SESSION BW = %d HZ]", ARQStationRemote.str, intSessionBW);
					snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ CONNECTION ESTABLISHED WITH %s, SESSION BW = %d HZ", ARQStationRemote.str, intSessionBW);
					QueueCommandToHost(HostCmd);
				}

				// Initiate the transisiton to IRS

				ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame] BREAK Rcvd from ARQState ISSData, Go to ProtocolState IRS & substate IRSfromISS , send ACK");

				// With new rules IRS can use BREAK to interrupt data from ISS. It will only
				// be sent on IDLE or changed data frame type, so we know the last sent data
				// wasn't processed by IRS

				if (bytDataToSendLength)
					SaveQueueOnBreak();  // Save the data so appl can restore it

				ClearDataToSend();
				blnEnbARQRpt = false;  // setup for no repeats
				intTrackingQuality = -1;  // initialize tracking quality to illegal value
				intNAKctr = 0;
				ZF_LOGI("[STATUS: BREAK received from Protocol State ISS, new state IRS]");
				SendCommandToHost("STATUS BREAK received from Protocol State ISS, new state IRS");

				// Send ACK
				if ((EncLen = EncodeDATAACK(100, bytSessionID, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for BREAK->DataACK Invalid EncLen (%d).", EncLen);
					return;
				}
				Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent

				dttTimeoutTrip = Now;
				SetARDOPProtocolState(IRS);
				ARQState = IRSfromISS;  // Substate IRSfromISS allows processing of Rule 3.5 later

				intLinkTurnovers += 1;
				// CLear MEM ARQ Stuff
				ResetMemoryARQ();
				LastDataFrameType = intFrameType;
				intLastARQDataFrameToHost = -1;  // precondition to an illegal frame type (insures the new IRS does not reject a frame)
				return;
			}
			if (intFrameType <= DataNAKmax)  // if NAK
			{
				if (blnLastFrameSentData)
				{
					intNAKctr++;

					ComputeQualityAvg(38 + 2 * intFrameType);  // Average in NAK quality to exponential averager.
					Gearshift_9();  // gear shift based on average quality or Shift Down if intNAKcnt >= 10

					if (intShiftUpDn != 0)
					{
						dttTimeoutTrip = Now;  // Retrigger the timeout on a shift and clear the NAK counter
						intNAKctr = 0;
						SendData();  // Added 0.3.5.2     Restore the last frames data, Send new data from outbound queue and set up repeats
					}
					intACKctr = 0;
				} else {
					ZF_LOGD(
						"Received DataNAK with ARQState == ISSData, but"
						" blnLastFrameSentData is false.  Not sure how/why"
						" this happened.  Ignoring.");
				}
				// For now don't try and change the current data frame the simple gear shift will change it on the next frame
				// add data being transmitted back to outbound queue
				return;
			}

			if (intFrameType == DISCFRAME)  // if DISC  Handles protocol rule 1.5
			{
				ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame]  DISC frame received Send END...go to DISC state");
				if (AccumulateStats)
					LogStats();

				QueueCommandToHost("DISCONNECTED");
				ZF_LOGI("[STATUS: ARQ CONNECTION ENDED WITH %s]", ARQStationRemote.str);
				snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ CONNECTION ENDED WITH %s", ARQStationRemote.str);
				QueueCommandToHost(HostCmd);

				bytLastARQSessionID = bytSessionID;  // capture this session ID to allow answering DISC from DISC state
				blnDISCRepeating = false;

				// Send END.  The other station should respond by immediately
				// sending an IDFrame.  tmrFinalID should be set so that IDFrame
				// will not be sent until after the other station's IDFrame, which
				// may or may not include a CW ID, is finished.  Now + 3000 (3
				// seconds) would be sufficient time for END plus the other
				// station's IDFrame.  A CW ID (assumed worst case QQQ0QQQ) could
				// take as long as 10 additional seconds, but is likely to be much
				// less.  Rather than add a lot of extra delay, use Now + 3000, but
				// also require handling of tmpFinalID to wait for the busy detector
				// to indicate that the frequency is not in use.  Even with the
				// delay used by the busy detector, this will normally be a lesser
				// delay, especially when the other station does not send a CW ID.
				tmrFinalID = Now + 3000;
				ClearDataToSend();
				SetARDOPProtocolState(DISC);
				InitializeConnection();
				blnEnbARQRpt = false;

				if ((EncLen = Encode4FSKControl(END, bytSessionID, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In ProcessRcvdARQFrame() for DISC->END Invalid EncLen (%d).", EncLen);
					return;
				}
				Mod4FSKDataAndPlay(END, &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent
				return;
			}

			if (intFrameType == END)  // if END
			{
				ZF_LOGI("[ARDOPprotocol.ProcessRcvdARQFrame]  END received ... going to DISC state");
				if (AccumulateStats)
					LogStats();

				QueueCommandToHost("DISCONNECTED");
				ZF_LOGI("[STATUS: ARQ CONNECTION ENDED WITH %s]", ARQStationRemote.str);
				snprintf(HostCmd, sizeof(HostCmd), "STATUS ARQ CONNECTION ENDED WITH %s", ARQStationRemote.str);
				QueueCommandToHost(HostCmd);
				ClearDataToSend();
				blnDISCRepeating = false;

				// SendID will default to Callsign if ARQStationLocal is not valid.
				SendID(&ARQStationLocal, "Rcvd END in ISSData ARQState");

				SetARDOPProtocolState(DISC);
				InitializeConnection();
				return;
			}
			if (blnFrameDecodedOK)
				ZF_LOGD(
					"Received correctly decoded frame of unexpected FrameType=%s"
					" while ARQState == ISSData. Ignoring.",
					Name(intFrameType));
			else
				ZF_LOGD(
					"Received (FAILED decode) frame of unexpected FrameType=%s"
					" while ARQState == ISSData. Ignoring.",
					Name(intFrameType));
			return;
		}

	default:
		ZF_LOGD(
			"Shouldnt get Here: ProtocolState=%s.  ARQState=%s. FrameType=%s.",
			ARDOPStates[ProtocolState],
			ARQSubStates[ARQState],
			Name(intFrameType));
		// Logs.Exception("[ARDOPprotocol.ProcessRcvdARQFrame]
		return;
	}
	// Unhandled Protocol state=" & GetARDOPProtocolState.ToString & "  ARQState=" & ARQState.ToString)
}


// Function to determine the IRS ConAck to reply based on intConReqFrameType received and local MCB.ARQBandwidth setting

int IRSNegotiateBW(int intConReqFrameType)
{
	// returns the correct ConAck frame number to establish the session bandwidth to the ISS or the ConRejBW frame number if incompatible
	// if acceptable bandwidth sets stcConnection.intSessionBW

	switch (ARQBandwidth)
	{
	case B200FORCED:

		if ((intConReqFrameType >= ConReq200M && intConReqFrameType <= ConReq2000M)|| intConReqFrameType == ConReq200F)
		{
			intSessionBW = 200;
			return ConAck200;
		}
		break;

	case B500FORCED:

		if ((intConReqFrameType >= ConReq500M && intConReqFrameType <= ConReq2000M) || intConReqFrameType == ConReq500F)
		{
			intSessionBW = 500;
			return ConAck500;
		}
		break;

	case B1000FORCED:

		if ((intConReqFrameType >= ConReq1000M && intConReqFrameType <= ConReq2000M) || intConReqFrameType == ConReq1000F)
		{
			intSessionBW = 1000;
			return ConAck1000;
		}
		break;

	case B2000FORCED:

		if (intConReqFrameType == ConReq2000M || intConReqFrameType == ConReq2000F)
		{
			intSessionBW = 2000;
			return ConAck2000;
		}
		break;

	case B200MAX:

		if (intConReqFrameType >= ConReq200M && intConReqFrameType <= ConReq200F)
		{
			intSessionBW = 200;
			return ConAck200;
		}
		break;

	case B500MAX:

		if (intConReqFrameType == ConReq200M || intConReqFrameType == ConReq200F)
		{
			intSessionBW = 200;
			return ConAck200;
		}
		if ((intConReqFrameType >= ConReq500M && intConReqFrameType <= ConReq2000M) || intConReqFrameType == ConReq500F)
		{
			intSessionBW = 500;
			return ConAck500;
		}
		break;

	case B1000MAX:

		if (intConReqFrameType == ConReq200M || intConReqFrameType == ConReq200F)
		{
			intSessionBW = 200;
			return ConAck200;
		}
		if (intConReqFrameType == ConReq500M || intConReqFrameType == ConReq500F)
		{
			intSessionBW = 500;
			return ConAck500;
		}
		if ((intConReqFrameType >= ConReq1000M && intConReqFrameType <= ConReq2000M) || intConReqFrameType == ConReq1000F)
		{
			intSessionBW = 1000;
			return ConAck1000;
		}
		break;

	case B2000MAX:

		if (intConReqFrameType == ConReq200M || intConReqFrameType == ConReq200F)
		{
			intSessionBW = 200;
			return ConAck200;
		}
		if (intConReqFrameType == ConReq500M || intConReqFrameType == ConReq500F)
		{
			intSessionBW = 500;
			return ConAck500;
		}
		if (intConReqFrameType == ConReq1000M || intConReqFrameType == ConReq1000F)
		{
			intSessionBW = 1000;
			return ConAck1000;
		}
		if (intConReqFrameType == ConReq2000M || intConReqFrameType == ConReq2000F)
		{
			intSessionBW = 2000;
			return ConAck2000;
		}
	}

	return ConRejBW;  // ConRejBW
}

// Function to send and ARQ connect request for the current MCB.ARQBandwidth

bool SendARQConnectRequest(const StationId* mycall, const StationId* target)
{
	// Psuedo Code:
	// Determine the proper bandwidth and target call
	// Go to the ISS State and ISSConREq sub state
	// Encode the connect frame with extended Leader
	// initialize the ConReqCount and set the Frame repeat interval
	// (Handles protocol rule 1.1)

	InitializeConnection();
	intRmtLeaderMeas = 0;
	memcpy(&ARQStationRemote, target, sizeof(ARQStationRemote));
	memcpy(&ARQStationLocal, mycall, sizeof(ARQStationLocal));
	memcpy(&ARQStationFinalId, mycall, sizeof(ARQStationFinalId));

	if (CallBandwidth == UNDEFINED) {
		if ((EncLen = EncodeARQConRequest(mycall, target, ARQBandwidth, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In SendARQConnectRequest() with UNDEFINED BW Invalid EncLen (%d).", EncLen);
			return false;
		}
	} else {
		if ((EncLen = EncodeARQConRequest(mycall, target, CallBandwidth, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In SendARQConnectRequest() Invalid EncLen (%d).", EncLen);
			return false;
		}
	}

	// generate the modulation with 2 x the default FEC leader length...Should insure reception at the target
	// Note this is sent with session ID 0xFF

	// Set all flags before playing, as the End TX is called before we return here
	blnAbort = false;
	dttTimeoutTrip = Now;
	SetARDOPProtocolState(ISS);
	ARQState = ISSConReq;
	intRepeatCount = 1;

	displayCall('>', target->str);
	bytSessionID = GenerateSessionID(mycall->str, target->str);  // Now set bytSessionID to receive ConAck (note the calling staton is the first entry in GenerateSessionID)
	bytPendingSessionID = bytSessionID;

	ZF_LOGI("[SendARQConnectRequest] MYCALL=%s  TARGET=%s bytPendingSessionID=%x", mycall->str, target->str, bytPendingSessionID);
	blnPending = true;
	blnARQConnected = false;
	wg_send_rcall(0, target->str);

	intFrameRepeatInterval = 2000;  // ms Finn reported 7/4/2015 that 1600 was too short ...need further evaluation but temporarily moved to 2000 ms
	blnEnbARQRpt = true;

	Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent

	// Update the main form menu status lable
	// Dim stcStatus As Status = Nothing
	// stcStatus.ControlName = "mnuBusy"
	// stcStatus.Text = "Calling " & strTargetCall
	// queTNCStatus.Enqueue(stcStatus)

	return true;
}


// Function to check for and initiate disconnect from a Host DISCONNECT command

bool CheckForDisconnect()
{
	if (blnARQDisconnect)
	{
		ZF_LOGD("[ARDOPprotocol.CheckForDisconnect]  ARQ Disconnect ...Sending DISC (repeat)");

		ZF_LOGI("[STATUS: INITIATING ARQ DISCONNECT]");
		QueueCommandToHost("STATUS INITIATING ARQ DISCONNECT");

		intFrameRepeatInterval = 2000;
		intRepeatCount = 1;
		blnARQDisconnect = false;
		blnDISCRepeating = true;
		blnEnbARQRpt = false;

		// We could get here while sending an ACK (if host received a diconnect (bye) resuest
		// if so, don't send the DISC. ISS should go to Quiet, and we will repeat DISC

		if (SoundIsPlaying)
			return true;

		if ((EncLen = Encode4FSKControl(DISCFRAME, bytSessionID, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In CheckForDisconnect() Invalid EncLen (%d).", EncLen);
			return false;
		}
		Mod4FSKDataAndPlay(DISCFRAME, &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent
		return true;
	}
	return false;
}

// subroutine to implement Host Command BREAK

void Break()
{
	time_t dttStartWait = Now;

	if (ProtocolState != IRS)
	{
		ZF_LOGI("[ARDOPprotocol.Break] |BREAK command received in ProtocolState: %s :", ARDOPStates[ProtocolState]);
		return;
	}

	ZF_LOGD("[ARDOPprotocol.Break] BREAK command received with AutoBreak = %d", AutoBreak);
	blnBREAKCmd = true;  // Set flag to process pending BREAK
}

// Function to abort an FEC or ARQ transmission

void Abort()
{
	blnAbort = true;

	if (ProtocolState == IDLE || ProtocolState == IRS || ProtocolState == IRStoISS)
		GetNextARQFrame();
}

void ClearTuningStats()
{
	intLeaderDetects = 0;
	intLeaderSyncs = 0;
	intFrameSyncs = 0;
	intAccumFSKTracking = 0;
	intAccumPSKTracking = 0;
	intAccumQAMTracking = 0;
	intFSKSymbolCnt = 0;
	intPSKSymbolCnt = 0;
	intQAMSymbolCnt = 0;
	intGoodFSKFrameTypes = 0;
	intFailedFSKFrameTypes = 0;
	intGoodFSKFrameDataDecodes = 0;
	intFailedFSKFrameDataDecodes = 0;
	intGoodFSKSummationDecodes = 0;

	intGoodPSKFrameDataDecodes = 0;
	intGoodPSKSummationDecodes = 0;
	intFailedPSKFrameDataDecodes = 0;
	intGoodQAMFrameDataDecodes = 0;
	intGoodQAMSummationDecodes = 0;
	intFailedQAMFrameDataDecodes = 0;
	intAvgFSKQuality = 0;
	intAvgPSKQuality = 0;
	dblFSKTuningSNAvg = 0;
	dblLeaderSNAvg = 0;
	dblAvgPSKRefErr = 0;
	intPSKTrackAttempts = 0;
	intQAMTrackAttempts = 0;
	dblAvgDecodeDistance = 0;
	intDecodeDistanceCount = 0;
	intShiftDNs = 0;
	intShiftUPs = 0;
	dttStartSession = Now;
	intLinkTurnovers = 0;
	intEnvelopeCors = 0;
	dblAvgCorMaxToMaxProduct = 0;
	intConReqSN = 0;
	intConReqQuality = 0;
}

void ClearQualityStats()
{
	int4FSKQuality = 0;
	int4FSKQualityCnts = 0;
	intPSKQuality[0] = 0;
	intPSKQuality[1] = 0;
	intPSKQualityCnts[0] = 0;
	intPSKQualityCnts[1] = 0;  // Counts for 4PSK, 8PSK modulation modes
	intFSKSymbolsDecoded = 0;
	intPSKSymbolsDecoded = 0;

	intQAMQuality = 0;
	intQAMQualityCnts = 0;
	intQAMSymbolsDecoded = 0;
}

// Sub to Write Tuning Stats to the Debug Log

void LogStats()
{
	int intTotFSKDecodes = intGoodFSKFrameDataDecodes + intFailedFSKFrameDataDecodes;
	int intTotPSKDecodes = intGoodPSKFrameDataDecodes + intFailedPSKFrameDataDecodes;
	int i;

	ardop_log_session_header(ARQStationRemote.str,(Now - dttStartSession) / 60000);

	ardop_log_session_info("     LeaderDetects= %d   AvgLeader S+N:N(3KHz noise BW)= %f dB  LeaderSyncs= %d", intLeaderDetects, dblLeaderSNAvg - 23.8, intLeaderSyncs);
	ardop_log_session_info("     AvgCorrelationMax:MaxProd= %f over %d  correlations", dblAvgCorMaxToMaxProduct, intEnvelopeCors);
	ardop_log_session_info("     FrameSyncs=%d  Good Frame Type Decodes=%d  Failed Frame Type Decodes =%d", intFrameSyncs, intGoodFSKFrameTypes, intFailedFSKFrameTypes);
	ardop_log_session_info("     Avg Frame Type decode distance= %f over %d decodes", dblAvgDecodeDistance, intDecodeDistanceCount);

	if (intGoodFSKFrameDataDecodes + intFailedFSKFrameDataDecodes + intGoodFSKSummationDecodes > 0)
	{
		ardop_log_session_info("%s","");
		ardop_log_session_info("  FSK:");
		ardop_log_session_info("     Good FSK Data Frame Decodes= %d  RecoveredFSKCarriers with Summation=%d  Failed FSK Data Frame Decodes=%d", intGoodFSKFrameDataDecodes, intGoodFSKSummationDecodes, intFailedFSKFrameDataDecodes);
		ardop_log_session_info("     AccumFSKTracking= %d   over %d symbols   Good Data Frame Decodes= %d   Failed Data Frame Decodes=%d\n", intAccumFSKTracking, intFSKSymbolCnt, intGoodFSKFrameDataDecodes, intFailedFSKFrameDataDecodes);
	}
	if (intGoodPSKFrameDataDecodes + intFailedPSKFrameDataDecodes + intGoodPSKSummationDecodes > 0)
	{
		ardop_log_session_info("%s", "");
		ardop_log_session_info("  PSK:");
		ardop_log_session_info("     Good PSK Data Frame Decodes=%d  RecoveredPSKCarriers with Summation=%d  Failed PSK Data Frame Decodes=%d", intGoodPSKFrameDataDecodes, intGoodPSKSummationDecodes, intFailedPSKFrameDataDecodes);
		ardop_log_session_info("     AccumPSKTracking=%d  %d attempts over %d total PSK Symbols\n", intAccumPSKTracking, intPSKTrackAttempts, intPSKSymbolCnt);
	}
	if (intGoodQAMFrameDataDecodes + intFailedQAMFrameDataDecodes + intGoodQAMSummationDecodes > 0)
	{
		ardop_log_session_info("%s", "");
		ardop_log_session_info("  QAM:");
		ardop_log_session_info("     Good QAM Data Frame Decodes=%d  RecoveredQAMCarriers with Summation=%d  Failed QAM Data Frame Decodes=%d", intGoodQAMFrameDataDecodes, intGoodQAMSummationDecodes, intFailedQAMFrameDataDecodes);
		ardop_log_session_info("     AccumQAMTracking=%d  %d attempts over %d total QAM Symbols\n", intAccumQAMTracking, intQAMTrackAttempts, intQAMSymbolCnt);
	}

	ardop_log_session_info("  Squelch= %d BusyDet= %d Mode Shift UPs= %d   Mode Shift DOWNs= %d  Link Turnovers= %d\n",
		Squelch, BusyDet, intShiftUPs, intShiftDNs, intLinkTurnovers);
	ardop_log_session_info("  Received Frame Quality:");

	if (int4FSKQualityCnts > 0)
		ardop_log_session_info("     Avg 4FSK Quality=%d on %d frame(s)", int4FSKQuality / int4FSKQualityCnts, int4FSKQualityCnts);

	if (intPSKQualityCnts[0] > 0)
		ardop_log_session_info("     Avg 4PSK Quality=%d on %d frame(s)", intPSKQuality[0] / intPSKQualityCnts[0], intPSKQualityCnts[0]);

	if (intPSKQualityCnts[1] > 0)
		ardop_log_session_info("     Avg 8PSK Quality=%d on %d frame(s)", intPSKQuality[1] / intPSKQualityCnts[1], intPSKQualityCnts[1]);

	if (intQAMQualityCnts > 0)
		ardop_log_session_info("     Avg QAM Quality=%d on %d frame(s)", intQAMQuality / intQAMQualityCnts, intQAMQualityCnts);

	// Experimental logging of Frame Type ACK and NAK counts
	ardop_log_session_info("\nType              ACKS  NAKS");

	for (i = 0; i < bytFrameTypesForBWLength; i++)
	{
		ardop_log_session_info("%-16s %5d %5d", Name(bytFrameTypesForBW[i]), ModeHasWorked[i], ModeNAKS[i]);
	}

	ardop_log_session_footer();
}
