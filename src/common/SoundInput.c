//	ARDOP Modem Decode Sound Samples

#ifdef WIN32
#define _CRT_SECURE_NO_DEPRECATE
#include <windows.h>
#endif

#include "common/ARDOPC.h"
#include "common/Locator.h"
#include "common/RXO.h"
#include "common/sdft.h"
#include "rockliff/rrs.h"

#pragma warning(disable : 4244)  // Code does lots of float to int

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


extern unsigned int PKTLEDTimer;

void SendFrametoHost(unsigned char *data, unsigned dlen);

void clearDisplay();
void updateDisplay();

void DrawAxes(int Qual, char * Mode);

void PassFECErrDataToHost();

extern int lastmax, lastmin;  // Sample Levels

BOOL blnLeaderFound = FALSE;

int intLeaderRcvdMs = 1000;  // Leader length??

extern int intLastRcvdFrameQuality;
extern UCHAR bytLastReceivedDataFrameType;
extern BOOL blnBREAKCmd;
extern UCHAR bytLastACKedDataFrameType;
extern int intARQDefaultDlyMs;
extern unsigned int tmrFinalID;
void DrawDecode(char * Decode);

int wg_send_rxframet(int cnum, unsigned char state, const char *frame);
int wg_send_quality(int cnum, unsigned char quality,
	unsigned int totalRSErrors, unsigned int maxRSErrors);

extern BOOL UseSDFT;
int Corrections = 0;
void Demod1Car4FSK_SDFT(int Start, BOOL blnGetFrameType);

short intPriorMixedSamples[120];  // a buffer of 120 samples to hold the prior samples used in the filter
int	intPriorMixedSamplesLength = 120;  // size of Prior sample buffer

// While searching for leader we must save unprocessed samples
// We may have up to 720 left, so need 1920

short rawSamples[2400];  // Get Frame Type need 2400 and we may add 1200
int rawSamplesLength = 0;

short intFilteredMixedSamples[5000];  // Get Frame Type need 2400 and we may add 1200
int intFilteredMixedSamplesLength = 0;

int intFrameType;  // Type we are decoding
int LastDataFrameType;  // Last data frame processed (for Memory ARQ, etc)

char strDecodeCapture[1024];

// Frame type parameters

int intCenterFreq = 1500;
float intCarFreq;  // (was int)
int intNumCar;
int intBaud;
int intDataLen;
int intRSLen;
int intSampleLen;
int DataRate = 0;  // For SCS Reporting
int intDataPtr;
int intSampPerSym;
int intDataBytesPerCar;
BOOL blnOdd;
char strType[18] = "";
char strMod[16] = "";
UCHAR bytMinQualThresh;
int intPSKMode;

// 16QAM carriers each contain 128 data bytes, 64 RS bytes, 1 length byte and 2
// CRC bytes = 195 bytes.
#define MAX_RAW_LENGTH 195
// A 4FSK.2000.600 frame contains 600 data bytes, 150 RS bytes 3 length bytes,
// and 6 CRC bytes = 759.  While this is broken up into 3 "carriers" for RS
// error correction, it is demodulated and initially decoded as one long block.
#define MAX_600B_RAW_LENGTH 759
// 16QAM.2000.100 requires 8*128.
#define MAX_DATA_LENGTH 8 * 128

// obsolete versions of this code accommodated multi-carrier FSK modes, for
// which intToneMags was intToneMags[4][16 * MAX_RAW_LENGTH]
// obsolete versions of this code also treated the three blocks of data in
// long 600 baud 4FSK frames as if they were 3 dummy carriers.
// This was changed to use only a single set of intToneMags[], but to still
// put decoded results into bytFrameData1, bytFrameData2, and bytFrameData3

// length 16 * max bytes data (2 bits per symbol, 4 samples per symbol in 4FSK.
// looks like we have 4 samples for each 2 bits, which means 16 samples per byte.

// Size intToneMags for demodulating the long 4FSK.2000.600 frames
int intToneMags[16 * MAX_600B_RAW_LENGTH] = {0};
int intToneMagsIndex = 0;

int intSumCounts[8];  // number in above arrays

int intToneMagsLength;

unsigned char goodCarriers = 0;  // Carriers we have already decoded

// We always collect all phases for PSK and QAM so we can do phase correction
// 4PSK uses 4 phase values per byte, and 1+64+32+2=99 bytes per carrier = 396 values per carrier.
// 8PSK and 16QAM use 8/3 phase values per byte, and up to 1+128+64+2=195 bytes per carrier = 520 values per carrier.
short intPhases[8][520] = {0};

// We only use Mags for QAM (see intToneMags for FSK)
short intMags[8][520] = {0};

// Keep all samples for FSK and a copy of tones or phase/amplitude

// Because intToneMagsAvg is used for Memory ARQ after RS correction has failed,
// for the long 4FSK.2000.600 frames it is applied to each of the sequential
// "carrier" blocks separately.  Thus, each part does not need to be large
// enough to hold tone magnitudes for all of a 4FSK.2000.600 frame, but it does
// need to be large enough for a third of this.  intToneMagsAvg[1] and [2] are
// used only for these long 600 baud frames.  intToneMagsAvg[0] is used for all
// FSK data frame types.
int intToneMagsAvg[3][16 * (MAX_600B_RAW_LENGTH / 3)];  // FSK Tone averages.

short intCarPhaseAvg[8][520];  // array to accumulate phases for averaging (Memory ARQ)
short intCarMagAvg[8][520];  // array to accumulate mags for averaging (Memory ARQ)

// Use MemarqTime, FECMemarqTimeout, and ARQTimeout to keep track of whether
// Memory ARQ values have been stored, and whether they have become stale.  If
// MemarqTime is 0, then no Memory ARQ values (CarrierOk, intSumCounts,
// intToneMagsAvg, intCarPhaseAvg, intCarMagAvg) are set.  Otherwise, it is the
// ms resolution time (set with Now) that they were first set.  If MemarqTime is
// not 0, and (Now - MemarqTime) > 1000 * ARQTimeout, then all Memory ARQ values
// should be considered stale and should be discarded.  This is intended to
// reduce the likelihood that Memory ARQ values will be ineffective (or even
// produce invalid results in the the case of multi-carrier frame types in FEC
// or RXO modes) when stale results are applied to a new incoming frame that is
// unrelated to the frames used to set the Memory ARQ values.  In FEC
// protocolmode, the lesser of 1000 * ARQTimeout and FECMemarqTimeout is used.
// FECMemarqTimeout is set to slightly longer than the duration of the longest
// data frame type (less than 6 seconds) times one plus the maximum value of
// FECRepeats (5).  This is less than the default value of ARQTimeout (120
// seconds), but ARQTimeout can be set with a host command to any value in the
// range of 30 to 240 seconds.  If RXO protocolmode is used to monitor FEC
// transmissions, it may be appropriate to set a reduced ARQTimeout value.
// However, when RXO protocolmode is used to monitor ARQ transmissions, a larger
// ARQTimeout may allow a frame that is repeated many times to be successfully
// decoded.  The use of a larger ARQTimeout also increases the risk that data
// from frames of the same type but carrying different data may be mistakenly
// combined and reported as correctly decoded.  The ARQ protocol prevents this
// from occuring to participants in that protocol, but cannot completely prevent
// it from occuring when a third party is monitoring the transmissions.
//
// CheckMemarqTime() is used to apply these tests, and reset the values if
// appropriate.
unsigned int MemarqTime = 0;  // ms Resolution
unsigned int FECMemarqTimeout = 36000;  // ms Resolution 36 seconds

// If we do Mem ARQ we will need a fair amount of RAM

int intPhasesLen;

// Received Frame

UCHAR bytData[MAX_DATA_LENGTH];
int frameLen;

int totalRSErrors;

// We need one raw buffer per carrier

// This can be optimized quite a bit to save space
// We can probably overlay on bytData

// bytFrameData1 must be large enough to accomodate 4FSK.2000.600 frames.  The
// remaining bytFrameDataX must be large enough to accomodate 16QAM carriers.
UCHAR bytFrameData1[MAX_600B_RAW_LENGTH];  // Received chars
UCHAR bytFrameData2[MAX_RAW_LENGTH];  // Received chars
UCHAR bytFrameData3[MAX_RAW_LENGTH];  // Received chars
UCHAR bytFrameData4[MAX_RAW_LENGTH];  // Received chars
UCHAR bytFrameData5[MAX_RAW_LENGTH];  // Received chars
UCHAR bytFrameData6[MAX_RAW_LENGTH];  // Received chars
UCHAR bytFrameData7[MAX_RAW_LENGTH];  // Received chars
UCHAR bytFrameData8[MAX_RAW_LENGTH];  // Received chars

UCHAR * bytFrameData[8] = {bytFrameData1, bytFrameData2,
		bytFrameData3, bytFrameData4, bytFrameData5,
		bytFrameData6, bytFrameData7, bytFrameData8};

char CarrierOk[8];  // RS OK Flags per carrier

int charIndex = 0;  // Index into received chars

int SymbolsLeft;  // number still to decode

BOOL PSKInitDone = FALSE;

BOOL blnSymbolSyncFound, blnFrameSyncFound;

extern UCHAR bytLastARQSessionID;
extern UCHAR bytCurrentFrameType;
extern int intShiftUpDn;
extern const char ARQSubStates[10][11];
extern int intLastARQDataFrameToHost;

// dont think I need it short intRcvdSamples[12000];  // 1 second. May need to optimise

float dblOffsetLastGoodDecode = 0;
int dttLastGoodFrameTypeDecode = -20000;

float dblOffsetHz = 0;
int dttLastLeaderDetect;

extern int intRmtLeaderMeasure;

extern BOOL blnARQConnected;


extern BOOL blnPending;
extern UCHAR bytPendingSessionID;
extern UCHAR bytSessionID;

int dttLastGoodFrameTypeDecod;
int dttStartRmtLeaderMeasure;

StationId LastDecodedStationCaller;  // most recent frame sender
StationId LastDecodedStationTarget;  // most recent frame recipient

int GotBitSyncTicks;

int intARQRTmeasuredMs;

float dbl2Pi = 2 * M_PI;

float dblSNdBPwr;
float dblNCOFreq = 3000;  // nominal NC) frequency
float dblNCOPhase = 0;
float dblNCOPhaseInc = 2 * M_PI * 3000 / 12000;  // was dblNCOFreq

int	intMFSReadPtr = 30;  // reset the MFSReadPtr offset 30 to accomodate the filter delay

int RcvdSamplesLen = 0;  // Samples in RX buffer
int cumAdvances = 0;
int symbolCnt = 0;

int intSNdB = 0;
int intQuality = 0;


BOOL Acquire2ToneLeaderSymbolFraming();
BOOL SearchFor2ToneLeader3(short * intNewSamples, int Length, float * dblOffsetHz, int * intSN);
BOOL AcquireFrameSyncRSB();
int Acquire4FSKFrameType();

void DemodulateFrame(int intFrameType);
void Demod1Car4FSKChar(int Start, UCHAR * Decoded);
VOID Track1Car4FSK(short * intSamples, int * intPtr, int intSampPerSymbol, float intSearchFreq, int intBaud, UCHAR * bytSymHistory);
VOID Decode1CarPSK(UCHAR * Decoded, int Carrier);
VOID Decode1Car4FSK(UCHAR * Decoded, int *Magnitudes, int MagsLength);
int EnvelopeCorrelator();
BOOL DecodeFrame(int intFrameType, uint8_t bytData[MAX_DATA_LENGTH]);

void Update4FSKConstellation(int * intToneMags, int * intQuality);
void ProcessPingFrame(char * bytData);
int Compute4FSKSN();

void DemodPSK();
BOOL DemodQAM();
void SaveFSKSamples(int Part, int *Magnitudes, int Length);

void ResetMemoryARQ() {
	memset(CarrierOk, 0, sizeof(CarrierOk));
	memset(intSumCounts, 0, sizeof(intSumCounts));
	memset(intToneMagsAvg, 0, sizeof(intToneMagsAvg));
	memset(intCarPhaseAvg, 0, sizeof(intCarPhaseAvg));
	memset(intCarMagAvg, 0, sizeof(intCarMagAvg));
	LastDataFrameType = -1;
	MemarqTime = 0;
}

// See comments where MemarqTime is defined
void CheckMemarqTime() {
	// Now, MemarqTime, and FECMemarqTimeout have ms resolution, but ARQTimeout
	// is in seconds.
	if (MemarqTime != 0
		&& (
			(Now - MemarqTime) > 1000 * ARQTimeout
			|| (ProtocolMode == FEC && (Now - MemarqTime) > FECMemarqTimeout)
		)
	) {
		ZF_LOGD("Resetting stale Memory ARQ values.");
		ResetMemoryARQ();  // This also sets MemarqTime = 0
		// Calling PassFECErrDataToHost() here also ensures that stale failed
		// data stored by ProcessRcvdFECDataFrame() is passed to the host tagged
		// as ERR now.  Otherwise, this would have been done on the next call to
		// ProcessRcvdFECDataFrame(), or else the data would have been discarded
		// if another failed data frame of the same type was received and
		// mistakenly assumed to be another repeat of this data.  So, this
		// ensures that such failed data, which may be partially readable, is
		// passed to the host in a timely manner.
		PassFECErrDataToHost();
	}
}

// MemarqUpdated() should be called after any of the Memory ARQ values are set.
// If this is the first of these values set, then MemarqTime will also be set.
// MemarqTime is not updated upon further changes to the Memory ARQ values
// because it is intended to indicate the oldest age of any Memory ARQ values
// that have been set so that they can be reset upon becoming stale.
void MemarqUpdated() {
	if (MemarqTime == 0)
		MemarqTime = Now;
}

// Function to determine if frame type is short control frame

BOOL IsShortControlFrame(UCHAR bytType)
{
	if (bytType <= DataNAKmax)
		return TRUE;  // NAK
	if (bytType == BREAK || bytType == IDLEFRAME || bytType == DISCFRAME || bytType == END || bytType == ConRejBusy || bytType == ConRejBW)
		return TRUE;  // BREAK, IDLE, DISC, END, ConRejBusy, ConRejBW
	if (bytType >= DataACKmin)
		return TRUE;  // ACK
	return FALSE;
}

// Function to determine if it is a data frame (Even OR Odd)

BOOL IsDataFrame(UCHAR intFrameType)
{
	const char * String = Name(intFrameType);

	if (String == NULL || String[0] == 0)
		return FALSE;

	if (strstr(String, ".E") || strstr(String, ".O"))
		return TRUE;

	return FALSE;
}

// Function to clear all mixed samples

void ClearAllMixedSamples()
{
	intFilteredMixedSamplesLength = 0;
	intMFSReadPtr = 0;
	rawSamplesLength = 0;  // Clear saved
}

// Function to Initialize mixed samples

void InitializeMixedSamples()
{
	// Measure the time from release of PTT to leader detection of reply.

	intARQRTmeasuredMs = min(10000, Now - dttStartRTMeasure);  // ?????? needs work
	intPriorMixedSamplesLength = 120;  // zero out prior samples in Prior sample buffer
	intFilteredMixedSamplesLength = 0;  // zero out the FilteredMixedSamples array
	intMFSReadPtr = 30;  // reset the MFSReadPtr offset 30 to accomodate the filter delay
}

// Function to discard all sampled prior to current intRcvdSamplesRPtr

void DiscardOldSamples()
{
	// This restructures the intRcvdSamples array discarding all samples prior to intRcvdSamplesRPtr

	// not sure why we need this !!
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

// Function to apply 2000 Hz filter to mixed samples

float xdblZin_1 = 0, xdblZin_2 = 0, xdblZComb= 0;  // Used in the comb generator

// The resonators

float xdblZout_0[27] = {0.0f};  // resonator outputs
float xdblZout_1[27] = {0.0f};  // resonator outputs delayed one sample
float xdblZout_2[27] = {0.0f};  // resonator outputs delayed two samples
float xdblCoef[27] = {0.0};  // the coefficients
float xdblR = 0.9995f;  // insures stability (must be < 1.0) (Value .9995 7/8/2013 gives good results)
int xintN = 120;  // Length of filter 12000/100


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

	float intFilteredSample = 0;  // Filtered sample

	if (intFilteredMixedSamplesLength < 0)
		ZF_LOGE(
			"Corrupt intFilteredMixedSamplesLength (%d) in FSMixFilter2000Hz().",
			intFilteredMixedSamplesLength);

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

		// Compute the Comb

		xdblZComb = dblZin - xdblZin_2 * dblR2;
		xdblZin_2 = xdblZin_1;
		xdblZin_1 = dblZin;

		// Now the resonators
		for (j = 4; j <= 26; j++)  // calculate output for 3 resonators
		{
			xdblZout_0[j] = xdblZComb + xdblCoef[j] * xdblZout_1[j] - dblR2 * xdblZout_2[j];
			xdblZout_2[j] = xdblZout_1[j];
			xdblZout_1[j] = xdblZout_0[j];

			// scale each by transition coeff and + (Even) or - (Odd)
			// Resonators 2 and 13 scaled by .389 get best shape and side lobe supression
			// Scaling also accomodates for the filter "gain" of approx 60.

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
		ZF_LOGE(
			"Corrupt intFilteredMixedSamplesLength (%d) in FSMixFilter2000Hz().",
			intFilteredMixedSamplesLength);

}

// Function to apply 75Hz filter used in Envelope correlator

void Filter75Hz(short * intFilterOut, BOOL blnInitialise, int intSamplesToFilter)
{
	// assumes sample rate of 12000
	// implements  3 50 Hz wide sections   (~75 Hz wide @ - 30dB centered on 1500 Hz)
	// FSF (Frequency Selective Filter) variables

	static float dblR = 0.9995f;  // insures stability (must be < 1.0) (Value .9995 7/8/2013 gives good results)
	static int intN = 240;  // Length of filter 12000/50 - delays output 120 samples from input
	static float dblRn;
	static float dblR2;
	static float dblCoef[3] = {0.0};  // the coefficients
	float dblZin = 0, dblZin_1 = 0, dblZin_2 = 0, dblZComb= 0;  // Used in the comb generator
	// The resonators

	float dblZout_0[3] = {0.0};  // resonator outputs
	float dblZout_1[3] = {0.0};  // resonator outputs delayed one sample
	float dblZout_2[3] = {0.0};  // resonator outputs delayed two samples

	int i, j;

	float FilterOut = 0;  // Filtered sample
	float largest = 0;

	dblRn = powf(dblR, intN);

	dblR2 = powf(dblR, 2);

	// Initialize the coefficients

	if (dblCoef[2] == 0)
	{
		for (i = 0; i < 3; i++)
		{
			dblCoef[i] = 2 * dblR * cosf(2 * M_PI * (29 + i)/ intN);  // For Frequency = bin 29, 30, 31
		}
	}

	for (i = 0; i < intSamplesToFilter; i++)
	{
		if (i < intN)
			dblZin = intFilteredMixedSamples[intMFSReadPtr + i] - dblRn * 0;  // no prior mixed samples
		else
			dblZin = intFilteredMixedSamples[intMFSReadPtr + i] - dblRn * intFilteredMixedSamples[intMFSReadPtr + i - intN];

		// Compute the Comb

		dblZComb = dblZin - dblZin_2 * dblR2;
		dblZin_2 = dblZin_1;
		dblZin_1 = dblZin;

		// Now the resonators

		for (j = 0; j < 3; j++)  // calculate output for 3 resonators
		{
			dblZout_0[j] = dblZComb + dblCoef[j] * dblZout_1[j] - dblR2 * dblZout_2[j];
			dblZout_2[j] = dblZout_1[j];
			dblZout_1[j] = dblZout_0[j];

			// scale each by transition coeff and + (Even) or - (Odd)

			// Scaling also accomodates for the filter "gain" of approx 120.
			// These transition coefficients fairly close to optimum for WGN 0db PSK4, 100 baud (yield highest average quality) 5/24/2014

			if (j == 0 || j == 2)
				FilterOut -= 0.39811f * dblZout_0[j];  // this transisiton minimizes ringing and peaks
			else
				FilterOut += dblZout_0[j];
		}
		intFilterOut[i] = (int)ceil(FilterOut * 0.0041f);  // rescales for gain of filter
	}
}

// Subroutine to Mix new samples with NCO to tune to nominal 1500 Hz center with reversed sideband and filter.

void MixNCOFilter(short * intNewSamples, int Length, float dblOffsetHz)
{
	// Correct the dimension of intPriorMixedSamples if needed (should only happen after a bandwidth setting change).

	int i;
	short intMixedSamples[2400];  // All we need at once ( I hope!)  // may need to be int
	int	intMixedSamplesLength;  // size of intMixedSamples

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
	FSMixFilter2000Hz(intMixedSamples, intMixedSamplesLength);  // filter through the FS filter (required to reject image from Local oscillator)
//	printtick("Done Filter");

	// save for analysys

//	WriteSamples(&intFilteredMixedSamples[oldlen], Length);
//	WriteSamples(intMixedSamples, Length);

}

// Calculate the number of 1 bits in a byte
// Inplementation from:
// https://stackoverflow.com/questions/9949935/calculate-number-of-bits-set-in-byte
UCHAR CountOnes(UCHAR Byte) {
	static const UCHAR NIBBLE_LOOKUP[16] = {
		0, 1, 1, 2, 1, 2, 2, 3,
		1, 2, 2, 3, 2, 3, 3, 4
	};
	return NIBBLE_LOOKUP[Byte & 0x0F] + NIBBLE_LOOKUP[Byte >> 4];
}

// Calculate and write to the log the transmission bit error rate for this
// carrier.  Optionally, also write a "map" of the error locations.
//
// For data that was successfully corrected by rs_correct(), comparison of the
// corrected data to the uncorrected data can be used to calculate the number of
// bit errors.  If the distribution of the errors is non-uniform, that pattern
// may provide useful insight.
//
// This is a transmission bit error rate since it considers all transmitted data
// (intended payload + overhead) and is calculated for uncorrected data.  This
// can only be done when the rs_correct() successfully returned the error free
// intended payload (as confirmed by the matching CRC value).  Thus, it is a
// measure of the effectiveness of the demodulator, and it provides diagnostic
// data that may be of interest when demodulation is less than perfect, but
// still within the limits of what RS correction can fix.
void CountErrors(const UCHAR * Uncorrected, const UCHAR * Corrected, int Len, int Carrier, bool LogMap) {
	int BitErrorCount = 0;
	int CharErrorCount = 0;
	UCHAR BitErrors;
	char ErrMap[300] = " [";

	for (int i = 0; i < Len; ++i) {
		BitErrors = CountOnes(Uncorrected[i] ^ Corrected[i]);
		snprintf(ErrMap + strlen(ErrMap), sizeof(ErrMap) - strlen(ErrMap), "%d", BitErrors);
		BitErrorCount += BitErrors;
		if (BitErrors)
			++CharErrorCount;
	}
	if (LogMap)
		snprintf(ErrMap + strlen(ErrMap), sizeof(ErrMap) - strlen(ErrMap), "]");
	else
		// Make ErrMap an empty string so that it won't be written to the log
		ErrMap[0] = 0x00;

	ZF_LOGV(
		"Carrier[%d] %d raw bytes. CER=%.1f%% BER=%.1f%%%s",
		Carrier,
		Len,
		100.0 * CharErrorCount / Len,
		100.0 * BitErrorCount / (8 * Len),
		ErrMap);

	// log hex string of Uncorrected and Corrected
	char HexData[1024];
	snprintf(HexData, sizeof(HexData), "Uncorrected: ");
	for (int i = 0; i < Len; ++i)
		snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %02X", Uncorrected[i]);
	ZF_LOGV("%s", HexData);
	snprintf(HexData, sizeof(HexData), "Corrected:   ");
	for (int i = 0; i < Len; ++i)
		snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %02X", Corrected[i]);
	ZF_LOGV("%s", HexData);
}

// Function to Correct Raw demodulated data with Reed Solomon FEC

int CorrectRawDataWithRS(UCHAR * bytRawData, UCHAR * bytCorrectedData, int intDataLen, int intRSLen, int bytFrameType, int Carrier)
{
	int NErrors;
	// rs_correct() attempts to correct bytRawData in place.  So, preserve a
	// copy of the uncorrected data so that it can be used for CountBitErrors().
	// This includes a single byte that indicates the length of the data
	// intended to be transmitted, that data intended to be transmitted, padding
	// bytes (should be 0x00) if that data length was less than intDataLen, the
	// RS bytes, and a 2-byte checksum.
	int CombinedLength = intDataLen + intRSLen + 3;
	UCHAR RawDataCopy[256];
	if (ZF_LOG_ON_VERBOSE)
		memcpy(RawDataCopy, bytRawData, CombinedLength);

	// Dim bytNoRS(1 + intDataLen + 2 - 1) As Byte  // 1 byte byte Count, Data, 2 byte CRC
	// Array.Copy(bytRawData, 0, bytNoRS, 0, bytNoRS.Length)

	if (CarrierOk[Carrier])  // Already decoded this carrier?
	{
		// Athough we have already checked the data, it may be in the wrong place
		// in the buffer if another carrier was decoded wrong.

		memcpy(bytCorrectedData, &bytRawData[1], bytRawData[0]);

		ZF_LOGV(
			"Carrier[%d] %d raw bytes. CER and BER not calculated (previously decoded)",
			Carrier,
			CombinedLength);
		ZF_LOGD("[CorrectRawDataWithRS] Carrier %d (%d/%d net/gross bytes) already decoded", Carrier, bytRawData[0], intDataLen);
		return bytRawData[0];  // don't do it again
	}

	// An earlier version did CheckCRC16FrameType() here, before rs_correct.
	// While unlikely, a case was encountered in which this returned True, even
	// though bytRawData was corrupted.  However, rs_correct() was able to
	// successfully correct it.  So, always do rs_correct() before
	// CheckCRC16FrameType().  The increased computational burden is relatively
	// low, and it reduces the likelyhood of an undetected error.
	/*
	if (CheckCRC16FrameType(bytRawData, intDataLen + 1, bytFrameType))  // No RS correction needed
	{
		// return the actual data

		memcpy(bytCorrectedData, &bytRawData[1], bytRawData[0]);
		ZF_LOGD("[CorrectRawDataWithRS] OK (%d bytes) without RS", intDataLen);
		CarrierOk[Carrier] = TRUE;
		MemarqUpdated();
		return bytRawData[0];
	}
	*/

	// Try correcting with RS Parity
	NErrors = rs_correct(bytRawData, CombinedLength, intRSLen, true, false);

	if (NErrors == 0)
	{
//		ZF_LOGD("RS Says OK without correction");
	}
	else if (NErrors > 0)
	{
		// Unfortunately, Reed Solomon correction will sometimes return a
		// non-negative result indicating that the data is OK when it is not.
		// This is most likely to occur if the number of errors exceeds
		// intRSLen.  However, especially for small intRSLen, it may also
		// occur even if the number of errors is less than or equal to
		// intRSLen.  The test for invalid corrections in the padding bytes
		// performed by rs_correct() can reduce the likelyhood of this,
		// occuring, but it cannot be eliminiated.
		// For frames like this with a CRC, it provides a more reliable test
		// to verify whether a frame is correctly decoded.
//		ZF_LOGD("RS Says OK after %d correction(s)", NErrors);
	}
	else  // NErrors < 0
	{
		ZF_LOGV(
			"Carrier[%d] %d raw bytes. CER and BER too high. (rs_correct() failed)",
			Carrier,
			CombinedLength);
		ZF_LOGD("[CorrectRawDataWithRS] RS Says Can't Correct (%d(?)/%d net/gross bytes) (>%d max corrections)", bytRawData[0], intDataLen, intRSLen/2);
		goto returnBad;
	}

	//
	if (
		NErrors >= 0
		&& (bytRawData[0] <= intDataLen)  // Valid reported length
		&& CheckCRC16FrameType(bytRawData, intDataLen + 1, bytFrameType)
	) {  // RS correction successful
		if (ZF_LOG_ON_VERBOSE)
			CountErrors(RawDataCopy, bytRawData, CombinedLength, Carrier, true);
		ZF_LOGD("[CorrectRawDataWithRS] OK (%d/%d net/gross bytes) with RS %d (of max %d) corrections", bytRawData[0], intDataLen, NErrors, intRSLen/2);
		totalRSErrors += NErrors;

		memcpy(bytCorrectedData, &bytRawData[1], bytRawData[0]);
		CarrierOk[Carrier] = TRUE;
		MemarqUpdated();
		return bytRawData[0];
	}
	else
		ZF_LOGD("[CorrectRawDataWithRS] RS says ok (%d(?)/%d net/gross bytes) but CRC still bad", bytRawData[0], intDataLen);

	// return uncorrected data without byte count or RS Parity

returnBad:

	memcpy(bytCorrectedData, &bytRawData[1], intDataLen);
	return intDataLen;
}

// Subroutine to process new samples as received from the sound card via Main.ProcessCapturedData
// Only called when not transmitting

double dblPhaseInc;  // in milliradians
short intNforGoertzel[8];
short intPSKPhase_1[8], intPSKPhase_0[8];
short intCP[8];  // Cyclic prefix offset
float dblFreqBin[8];

void ProcessNewSamples(short * Samples, int nSamples)
{
	BOOL blnFrameDecodedOK = FALSE;

	// Reset Memory ARQ values if they have become stale.
	// This is done here rather than only when a new frame is detected so that
	// stale failed FEC data is passed to the host in a timely manner when no
	// further transmissions are detected for an extended time.  It is done
	// here rather than in CheckTimers() because CheckTimers() is not called in
	// RXO mode.
	CheckMemarqTime();

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
		return;  // FFT needs 1024 samples
	}

	UpdateBusyDetector(Samples);

	// searchforleader runs on unmixed and unfilered samples

	// Searching for leader

	if (State == SearchingForLeader)
	{
		// Search for leader as long as 960 samples (8 symbols) available

//		printtick("Start Leader Search");

		if (nSamples >= 1200)
		{
//			printtick("Start Busy");
//			if (State == SearchingForLeader)
//				UpdateBusyDetector(Samples);
//			printtick("Done Busy");

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
//				ZF_LOGD("Got Leader");

				dttLastLeaderDetect = Now;

				nSamples -= 480;
				Samples += 480;  // !!!! needs attention !!!

				InitializeMixedSamples();
				State = AcquireSymbolSync;
			}
			else
			{
				if (SlowCPU)
				{
					nSamples -= 480;
					Samples += 480;  // advance pointer 2 symbols (40 ms)  // reduce CPU loading
				}
				else
				{
					nSamples -= 240;
					Samples += 240;  // !!!! needs attention !!!
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

	// At this point samples haven't been processed, and are in Samples, len nSamples

	// I'm going to filter all samples into intFilteredMixedSamples.

//	printtick("Start Mix");

	MixNCOFilter(Samples, nSamples, dblOffsetHz);  // Mix and filter new samples (Mixing consumes all intRcvdSamples)
	nSamples = 0;  // all used

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

	// Acquire Frame Sync

	if (State == AcquireFrameSync)
	{
		blnFrameSyncFound = AcquireFrameSyncRSB();

		if (blnFrameSyncFound)
		{
			State = AcquireFrameType;

			// Have frame Sync. Remove used samples from buffer

//			printtick("Got Frame Sync");

		}

		// Remove used samples

		intFilteredMixedSamplesLength -= intMFSReadPtr;

		if (intFilteredMixedSamplesLength < 0)
			ZF_LOGE(
				"Corrupt intFilteredMixedSamplesLength (%d) at State == AcquireFrameSync.",
				intFilteredMixedSamplesLength);


		memmove(intFilteredMixedSamples,
			&intFilteredMixedSamples[intMFSReadPtr], intFilteredMixedSamplesLength * 2);

		intMFSReadPtr = 0;

		if ((Now - dttLastLeaderDetect) > 1000)  // no Frame sync within 1000 ms (may want to make this limit a funciton of Mode and leaders)
		{
			DiscardOldSamples();
			ClearAllMixedSamples();
			State = SearchingForLeader;
//			printtick("frame sync timeout");
		}
		intToneMagsIndex = 0;
	}

	// Acquire Frame Type
	if (State == AcquireFrameType)
	{
//		printtick("getting frame type");

		intFrameType = Acquire4FSKFrameType();
		if (intFrameType == -2)
		{
//			sprintf(Msg, "not enough %d %d", intFilteredMixedSamplesLength, intMFSReadPtr);
//			printtick(Msg);
			return;  // insufficient samples
		}
		// Reset sdft in case frame data is 4FSK with baud rate different from the
		// 50 baud used for the frame type.  Alternatively, this could be done only
		// for specified frame types, which would be slightly more efficient when
		// processing frames with 50 baud 4FSK frame data.
		blnSdftInitialized = FALSE;

		if (intFrameType == -1)  // poor decode quality (large decode distance)
		{
			State = SearchingForLeader;
			ClearAllMixedSamples();
			DiscardOldSamples();
			ZF_LOGD("poor frame type decode");

			// stcStatus.BackColor = SystemColors.Control
			// stcStatus.Text = ""
			// stcStatus.ControlName = "lblRcvFrame"
			// queTNCStatus.Enqueue(stcStatus)
		}
		else
		{
			// Get Frame info and Initialise Demodulate variables

			// We've used intMFSReadPtr samples, so remove from Buffer

//			sprintf(Msg, "Got Frame Type %x", intFrameType);
//			printtick(Msg);

			intFilteredMixedSamplesLength -= intMFSReadPtr;

			if (intFilteredMixedSamplesLength < 0)
				ZF_LOGE(
					"Corrupt intFilteredMixedSamplesLength (%d) at State == AcquireFrameTypeType.",
					intFilteredMixedSamplesLength);

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
				// In addition to Name() of frame type, include quality value
				// encoded in received ACK/NAK frames
				if (intFrameType <= DataNAKmax || intFrameType >= DataACKmin) {
					char fr_info[32] = "";
					// Quality decoding per DecodeACKNAK().  Note that any value
					// less than or equal to 38 is always encoded as 38.
					int q = 38 + (2 * (intFrameType & 0x1F));
					snprintf(fr_info, sizeof(fr_info), "%s (Q%s=%d/100)",
						Name(intFrameType), q <= 38 ? "<" : "", q);
					wg_send_rxframet(0, 1, fr_info);
				} else
					wg_send_rxframet(0, 1, Name(intFrameType));


				if (ProtocolState == IRStoISS && intFrameType >= DataACKmin)
				{
					// In this state transition to ISS if  ACK frame

					txSleep(250);

					ZF_LOGI("[ARDOPprotocol.ProcessNewSamples] ProtocolState=IRStoISS, substate = %s ACK received. Cease BREAKS, NewProtocolState=ISS, substate ISSData", ARQSubStates[ARQState]);
					blnEnbARQRpt = FALSE;  // stop the BREAK repeats
					intLastARQDataFrameToHost = -1;  // initialize to illegal value to capture first new ISS frame and pass to host

					if (bytCurrentFrameType == 0)  // hasn't been initialized yet
					{
						ZF_LOGI("[ARDOPprotocol.ProcessNewSamples, ProtocolState=IRStoISS, Initializing GetNextFrameData");
						GetNextFrameData(&intShiftUpDn, 0, "", TRUE);  // just sets the initial data, frame type, and sets intShiftUpDn= 0
					}

					SetARDOPProtocolState(ISS);
					intLinkTurnovers += 1;
					ARQState = ISSData;
					SendData();  // Send new data from outbound queue and set up repeats
					goto skipDecode;
				}

				// prepare for next

				DiscardOldSamples();
				ClearAllMixedSamples();
				State = SearchingForLeader;
				blnFrameDecodedOK = TRUE;
				ZF_LOGI("[DecodeFrame] Frame: %s ", Name(intFrameType));

				DecodeCompleteTime = Now;

				goto ProcessFrame;
			}

			DrawRXFrame(0, Name(intFrameType));
			// Frame type has been decoded, but data not yet received.  So,
			// show as Pending (state=0)
			wg_send_rxframet(0, 0, Name(intFrameType));

			// obsolete versions of this code also accommodated intBaud of 25 and 167.
			if (intBaud == 50)
				intSampPerSym = 240;
			else if (intBaud == 100)
				intSampPerSym = 120;
			else if (intBaud == 600)
				intSampPerSym = 20;

			// TODO:  SymbolsLeft should really be BytesLeft.  Depending on the
			// modulation type, each of these bytes may be encoded by as many
			// as four symbols.
			if (IsDataFrame(intFrameType))
				// Data frames include 1 length byte and 2 CRC bytes
				SymbolsLeft = intDataLen + intRSLen + 3;
			else
				// Non-data frames do not have a length byte or CRC bytes
				SymbolsLeft = intDataLen + intRSLen;  // No CRC

			if (intDataLen == 600)
				// 4FSK.200.600 frames are broken into three "carriers" each of
				// which have their own length byte and 2 CRC bytes.  So, add
				// 6 to SymbolsLeft for these for these two extra "carriers".
				SymbolsLeft += 6;

			// For each of SymbolsLeft (see note above this name), there are
			// 4 FSK Tones per symbol and 4 symbols per byte = 16 values each.
			intToneMagsLength = 16 * SymbolsLeft;

			intToneMagsIndex = 0;

			charIndex = 0;
			PSKInitDone = 0;

			frameLen = 0;
			totalRSErrors = 0;  // reset totalRSErrors just before AquireFrame

			if (!IsShortControlFrame(intFrameType))
			{
				// stcStatus.BackColor = Color.Khaki
				// stcStatus.Text = strType
				// stcStatus.ControlName = "lblRcvFrame"
				// queTNCStatus.Enqueue(stcStatus)
			}

			State = AcquireFrame;

			if (ProtocolMode == FEC && IsDataFrame(intFrameType) && ProtocolState != FECSend)
				SetARDOPProtocolState(FECRcv);

			// if a data frame, and not the same frame type as last, reinitialise
			// correctly received carriers byte and memory ARQ fields

//			if (IsDataFrame(intFrameType) && LastDataFrameType != intFrameType)

			if (LastDataFrameType != intFrameType)
			{
				// Memory ARQ values are reset here upon receiving a new frame type
				// (excluding short control frames: DataNAK, BREAK, IDLE, DISC,
				// END ConRejBusy, ConRejBW, DataACK).  Thus, they are reset for
				// non-short control frames: IDFrame, ConReqXXX, ConAckXXX,
				// PingAck, Ping.
				// TODO: Is this choice of control frames appropriate, or was
				// this an accident that should be reconsidered?  If reset on
				// DataNAK is applied, be sure to exclude RXO mode.
				//
				// They are also reset by CheckMemarqTimeout() when they become
				// stale or any time a successfully decoded data frame
				// is passed to ProcessRcvFECDataFrame() or ProcessRXOFrame()
				// since in FEC and RXO protocolmodes, a repeated data frame
				// type is not always a repetition of the previous frame.
				ZF_LOGD("New frame type - MEMARQ flags reset");
				ResetMemoryARQ();
			}
			// TODO: Change name of LastDataFrameType since it may also be set
			// to some non-data frame types?.
			LastDataFrameType = intFrameType;

			ZF_LOGD("MEMARQ Flags %d %d %d %d %d %d %d %d",
				CarrierOk[0], CarrierOk[1], CarrierOk[2], CarrierOk[3],
				CarrierOk[4], CarrierOk[5], CarrierOk[6], CarrierOk[7]);
		}
	}
	// Acquire Frame

	if (State == AcquireFrame)
	{
		// Call DemodulateFrame for each set of samples

		DemodulateFrame(intFrameType);

		if (State == AcquireFrame)

			// We haven't got it all yet so wait for more samples
			return;

		//	We have the whole frame, so process it
		blnSdftInitialized = FALSE;

//		printtick("got whole frame");

		if (strncmp (Name(intFrameType), "4FSK.200.50S", 12) == 0 && UseSDFT)
		{
			float observed_baudrate = 50.0 / ((intSampPerSym + ((float) cumAdvances)/symbolCnt)/intSampPerSym);
			ZF_LOGV("Estimated %s symbol rate = %.3f. (ideal=50.000)",
				Name(intFrameType), observed_baudrate);
		}
		else if (strncmp (Name(intFrameType), "4FSK.500.100", 12) == 0 && UseSDFT)
		{
			float observed_baudrate = 100.0 / ((intSampPerSym + ((float) cumAdvances)/symbolCnt)/intSampPerSym);
			ZF_LOGV("Estimated %s symbol rate = %.3f. (ideal=100.000)",
				Name(intFrameType), observed_baudrate);
		}
		if (strcmp (strMod, "4FSK") == 0)
			Update4FSKConstellation(&intToneMags[0], &intLastRcvdFrameQuality);

		// PSK and QAM quality done in Decode routines

		ZF_LOGD("Qual = %d", intLastRcvdFrameQuality);

		// This mechanism is to skip actual decoding and reply/change state...no need to decode

		if (blnBREAKCmd && ProtocolState == IRS && ARQState == IRSData &&
			intFrameType != bytLastACKedDataFrameType)
		{
			// This to immediatly go to IRStoISS if blnBREAKCmd enabled.

			// Implements protocol rule 3.4 (allows faster break) and does not require a good frame decode.

			ZF_LOGD("[ARDOPprotocol.ProcessNewSamples] Skip Data Decoding when blnBREAKCmd and ProtcolState=IRS");
			intFrameRepeatInterval = ComputeInterFrameInterval(1000 + rand() % 2000);
			SetARDOPProtocolState(IRStoISS);  // (ONLY IRS State where repeats are used)
			SendCommandToHost("STATUS QUEUE BREAK new Protocol State IRStoISS");
			blnEnbARQRpt = TRUE;  // setup for repeats until changeover
			ZF_LOGD("[ARDOPprotocol.ProcessNewSamples] %d bytes to send in ProtocolState: %s: Send BREAK,  New state=IRStoISS (Rule 3.3)",
					bytDataToSendLength,  ARDOPStates[ProtocolState]);
			ZF_LOGD("[ARDOPprotocol.ProcessNewSamples] Skip Data Decoding when blnBREAKCmd and ProtcolState=IRS");
			blnBREAKCmd = FALSE;
			if ((EncLen = Encode4FSKControl(BREAK, bytSessionID, bytEncodedBytes)) <= 0) {
				ZF_LOGE("ERROR: In ProcessNewSamples() Invalid EncLen (%d).", EncLen);
				goto skipDecode;
			}
			Mod4FSKDataAndPlay(BREAK, &bytEncodedBytes[0], EncLen, intARQDefaultDlyMs);  // only returns when all sent

			goto skipDecode;
		}

		if (ProtocolState == IRStoISS && IsDataFrame(intFrameType))
		{
			// In this state answer any data frame with BREAK
			// not necessary to decode the frame ....just frame type

			ZF_LOGD("[ARDOPprotocol.ProcessNewSamples] Skip Data Decoding when ProtcolState=IRStoISS, Answer with BREAK");
			intFrameRepeatInterval = ComputeInterFrameInterval(1000 + rand() % 2000);
			blnEnbARQRpt = TRUE;  // setup for repeats until changeover
			if ((EncLen = Encode4FSKControl(BREAK, bytSessionID, bytEncodedBytes)) <= 0) {
				ZF_LOGE("ERROR: In ProcessNewSamples() Invalid EncLen (%d).", EncLen);
				goto skipDecode;
			}
			Mod4FSKDataAndPlay(BREAK, &bytEncodedBytes[0], EncLen, intARQDefaultDlyMs);  // only returns when all sent
			goto skipDecode;
		}

		if (ProtocolState == IRStoISS && intFrameType >= DataACKmin)
		{
			// In this state transition to ISS if  ACK frame

			ZF_LOGD("[ARDOPprotocol.ProcessNewSamples] ProtocolState=IRStoISS, substate = %s ACK received. Cease BREAKS, NewProtocolState=ISS, substate ISSData", ARQSubStates[ARQState]);
			blnEnbARQRpt = FALSE;  // stop the BREAK repeats
			intLastARQDataFrameToHost = -1;  // initialize to illegal value to capture first new ISS frame and pass to host

			if (bytCurrentFrameType == 0)  // hasn't been initialized yet
			{
				ZF_LOGD("[ARDOPprotocol.ProcessNewSamples, ProtocolState=IRStoISS, Initializing GetNextFrameData");
				GetNextFrameData(&intShiftUpDn, 0, "", TRUE);  // just sets the initial data, frame type, and sets intShiftUpDn= 0
			}

			SetARDOPProtocolState(ISS);
			intLinkTurnovers += 1;
			ARQState = ISSData;
			SendData();  // Send new data from outbound queue and set up repeats
			goto skipDecode;
		}

		blnFrameDecodedOK = DecodeFrame(intFrameType, bytData);

ProcessFrame:
		if (!blnFrameDecodedOK) {
			DrawRXFrame(2, Name(intFrameType));
			// DecodeFrame() only does wg_send_rxframet() if OK.  So do here for
			// failed decodes (state=2)
			wg_send_rxframet(0, 2, Name(intFrameType));
		}

		if (blnFrameDecodedOK)
		{
			if (AccumulateStats)
				if (IsDataFrame(intFrameType)) {
					if (strstr (strMod, "PSK"))
						intGoodPSKFrameDataDecodes++;
					else if (strstr (strMod, "QAM"))
						intGoodQAMFrameDataDecodes++;
					else
						intGoodFSKFrameDataDecodes++;
				}


			if (IsDataFrame(intFrameType))
			{
				SetLED(PKTLED, TRUE);  // Flash LED
				PKTLEDTimer = Now + 400;  // For 400 Ms
			}

			if (ProtocolMode == FEC)
			{
				if (IsDataFrame(intFrameType))	// check to see if a data frame
					ProcessRcvdFECDataFrame(intFrameType, bytData, blnFrameDecodedOK);
				else if (intFrameType == IDFRAME)
					AddTagToDataAndSendToHost(bytData, "IDF", frameLen);
				else if (intFrameType >= ConReqmin && intFrameType <= ConReqmax)
					ProcessUnconnectedConReqFrame(intFrameType, bytData);
				else if (intFrameType == PING)
					ProcessPingFrame(bytData);
				else if (intFrameType == DISCFRAME)
				{
					// Special case to process DISC from previous connection (Ending station must have missed END reply to DISC) Handles protocol rule 1.5

					ZF_LOGD("[ARDOPprotocol.ProcessNewSamples]  DISC frame received in ProtocolMode FEC, Send END with SessionID= %XX", bytLastARQSessionID);

					tmrFinalID = Now + 3000;
					blnEnbARQRpt = FALSE;

					if ((EncLen = Encode4FSKControl(END, bytLastARQSessionID, bytEncodedBytes)) <= 0) {
						ZF_LOGE("ERROR: In ProcessNewSamples() Invalid EncLen (%d).", EncLen);
						goto skipDecode;
					} else
						Mod4FSKDataAndPlay(END, &bytEncodedBytes[0], EncLen, LeaderLength);  // only returns when all sent

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

				if (ProtocolState == DISC && Monitor) {  // allows ARQ mode to operate like FEC when not connected
					if (intFrameType == IDFRAME)
						AddTagToDataAndSendToHost(bytData, "IDF", frameLen);
					else if (intFrameType >= ConReqmin && intFrameType <= ConReqmax)
						ProcessUnconnectedConReqFrame(intFrameType, bytData);
					else if (IsDataFrame(intFrameType))  // check to see if a data frame
						ProcessRcvdFECDataFrame(intFrameType, bytData, blnFrameDecodedOK);
				}
			}
			else
			{
				// Unknown Mode
				bytData[frameLen] = 0;
				ZF_LOGD("Received Data, No State %s", bytData);
			}
		}
		else
		{
			//	Bad decode

			if (AccumulateStats)
				if (IsDataFrame(intFrameType)) {
					if (strstr (strMod, "PSK"))
						intFailedPSKFrameDataDecodes++;
					else if (strstr (strMod, "QAM"))
						intFailedQAMFrameDataDecodes++;
					else
						intFailedFSKFrameDataDecodes++;
				}

			// Debug.WriteLine("[DecodePSKData2] bytPass = " & Format(bytPass, "X"))

			if (ProtocolMode == FEC)
			{
				if (IsDataFrame(intFrameType))  // check to see if a data frame
					ProcessRcvdFECDataFrame(intFrameType, bytData, blnFrameDecodedOK);
				else if (intFrameType == IDFRAME)
					AddTagToDataAndSendToHost(bytData, "ERR", frameLen);
			}
			else if (ProtocolMode == RXO)
			{
				ProcessRXOFrame(intFrameType, frameLen, bytData, FALSE);
			}
			else if (ProtocolMode == ARQ)
			{
				if (ProtocolState == DISC)  // allows ARQ mode to operate like FEC when not connected
				{
					if (intFrameType == IDFRAME)
						AddTagToDataAndSendToHost(bytData, "ERR", frameLen);

					else if (IsDataFrame(intFrameType))  // check to see if a data frame
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

// Function to compute Goertzel algorithm and return Real and Imag components for a single frequency bin

void GoertzelRealImag(short intRealIn[], int intPtr, int N, float m, float * dblReal, float * dblImag)
{
	// intRealIn is a buffer at least intPtr + N in length
	// N need not be a power of 2
	// m need not be an integer
	// Computes the Real and Imaginary Freq values for bin m
	// Verified to = FFT results for at least 10 significant digits
	// Timings for 1024 Point on Laptop (64 bit Core Duo 2.2 Ghz)
	//  GoertzelRealImag .015 ms   Normal FFT (.5 ms)
	// assuming Goertzel is proportional to N and FFT time proportional to Nlog2N
	// FFT:Goertzel time  ratio ~ 3.3 Log2(N)

	// Sanity check

	// if (intPtr < 0 Or (intRealIn.Length - intPtr) < N Then
	//	dblReal = 0 : dblImag = 0 : Exit Sub
	// End If

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

// Function to compute Goertzel algorithm and return Real and Imag components for a single frequency bin with a Hanning Window function

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
	//  GoertzelRealImag .015 ms   Normal FFT (.5 ms)
	// assuming Goertzel is proportional to N and FFT time proportional to Nlog2N
	// FFT:Goertzel time  ratio ~ 3.3 Log2(N)

	// Sanity check

	float dblZ_1 = 0.0f, dblZ_2 = 0.0f, dblW = 0.0f;
	float dblCoeff = 2 * cosf(2 * M_PI * m / N);

	int i;

	if (HanWinLen != N)  // if there is any change in N this is then recalculate the Hanning Window...this mechanism reduces use of Cos
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
	//  GoertzelRealImag .015 ms   Normal FFT (.5 ms)
	// assuming Goertzel is proportional to N and FFT time proportional to Nlog2N
	// FFT:Goertzel time  ratio ~ 3.3 Log2(N)

	// Sanity check

	float dblZ_1 = 0.0f, dblZ_2 = 0.0f, dblW = 0.0f;
	float dblCoeff = 2 * cosf(2 * M_PI * m / N);

	int i;

	if (HamWinLen != N)  // if there is any cHamge in N this is then recalculate the Hanning Window...this mechanism reduces use of Cos
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

// Function to interpolate spectrum peak using simple interpolation

float SpectralPeakLocator(float XkM1Re, float XkM1Im, float XkRe, float XkIm, float XkP1Re, float XkP1Im, float * dblCentMag)
{
	// Use this for Windowed samples instead of QuinnSpectralPeakLocator

	float dblLeftMag, dblRightMag;
	*dblCentMag = sqrtf(powf(XkRe, 2) + powf(XkIm, 2));

	dblLeftMag  = sqrtf(powf(XkM1Re, 2) + powf(XkM1Im, 2));
	dblRightMag  = sqrtf(powf(XkP1Re, 2) + powf(XkP1Im, 2));

	// Factor 1.22 empirically determine optimum for Hamming window
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
	float dblInterpretThreshold= 1.0f;  // Good results June 6, 2014 (was .4)  // Works well possibly some room for optimization
	float dblFilteredMaxPeak = 0;
	int intStartBin, intStopBin;
	float dblLeftCar, dblRightCar, dblBinInterpLeft, dblBinInterpRight, dblCtrR, dblCtrI, dblLeftP, dblRightP;
	float dblLeftR[3], dblLeftI[3], dblRightR[3], dblRightI[3];
	int i;
	int Ptr = 0;
	float dblAvgNoisePerBin, dblCoarsePwrSN, dblBinAdj1475, dblBinAdj1525, dblCoarseOffset = 1000;
	float dblTrialOffset, dblPowerEarly, dblSNdBPwrEarly;

	if ((Length) < 1200)
		return FALSE;  // ensure there are at least 1200 samples (5 symbols of 240 samples)

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
			dblMag[i] = powf(dblGoertzelReal[i], 2) + powf(dblGoertzelImag[i], 2);  // dblMag(i) in units of power (V^2)
		}

		// Search the bins to locate the max S:N in the two tone signal/avg noise.

		for (i = intStartBin + 5; i <= intStopBin - 10; i++)  // +/- MCB.TuningRange from nominal
		{
			dblPower = sqrtf(dblMag[i] * dblMag[i + 5]);  // using the product to minimize sensitivity to one strong carrier vs the two tone
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

		if (((intIatMaxPeak - 123) >= intStartBin) && ((intIatMaxPeak - 118) <= intStopBin))  // check to ensure no index errors
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
				dblBinAdj = dblBinAdj / intInterpCnt;  // average the offsets that are within 1 bin
				dblCoarseOffset = 10.0f * (intIatMaxPeak + dblBinAdj - 147);  // compute the Coarse tuning offset in Hz
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
		dblTrialOffset = *dblOffsetHz;  // use the prior offset value

	if (fabsf(dblTrialOffset) > TuningRange && TuningRange > 0)
	{
		dblPriorFineOffset = 1000.0f;
		return False;
	}

	dblLeftCar = 147.5f + dblTrialOffset / 10.0f;  // the nominal positions of the two tone carriers based on the last computerd dblOffsetHz
	dblRightCar = 152.5f + dblTrialOffset / 10.0f;

	// Calculate 4 bins total for Noise values in S/N computation (calculate average noise)  // Simple average of noise bins
	GoertzelRealImagHamming(intNewSamples, Ptr, 1200, 142.5f + dblTrialOffset / 10.0f, &dblCtrR, &dblCtrI);  // nominal center -75 Hz
	dblAvgNoisePerBin = powf(dblCtrR, 2) + powf(dblCtrI, 2);
	GoertzelRealImagHamming(intNewSamples, Ptr, 1200, 145.0f + dblTrialOffset / 10.0f, &dblCtrR, &dblCtrI);  // center - 50 Hz
	dblAvgNoisePerBin += powf(dblCtrR, 2) + powf(dblCtrI, 2);
	GoertzelRealImagHamming(intNewSamples, Ptr, 1200, 155.0 + dblTrialOffset / 10.0f, &dblCtrR, &dblCtrI);  // center + 50 Hz
	dblAvgNoisePerBin += powf(dblCtrR, 2) + powf(dblCtrI, 2);
	GoertzelRealImagHamming(intNewSamples, Ptr, 1200, 157.5 + dblTrialOffset / 10.0f, &dblCtrR, &dblCtrI);  // center + 75 Hz
	dblAvgNoisePerBin += powf(dblCtrR, 2) + powf(dblCtrI, 2);
	dblAvgNoisePerBin = dblAvgNoisePerBin * 0.25f;  // simple average,  now units of power

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
	// concept is to allow more accurate framing and sync detection and reduce false leader detects

	GoertzelRealImag(intNewSamples, Ptr, 480, 57.0f + dblTrialOffset / 25.0f, &dblCtrR, &dblCtrI);  // nominal center -75 Hz
	dblAvgNoisePerBin = powf(dblCtrR, 2) + powf(dblCtrI, 2);
	GoertzelRealImag(intNewSamples, Ptr, 480, 58.0f + dblTrialOffset / 25.0f, &dblCtrR, &dblCtrI);  // nominal center -75 Hz
	dblAvgNoisePerBin += powf(dblCtrR, 2) + powf(dblCtrI, 2);
	GoertzelRealImag(intNewSamples, Ptr, 480, 62.0f + dblTrialOffset / 25.0f, &dblCtrR, &dblCtrI);  // nominal center -75 Hz
	dblAvgNoisePerBin += powf(dblCtrR, 2) + powf(dblCtrI, 2);
	GoertzelRealImag(intNewSamples, Ptr, 480, 63.0f + dblTrialOffset / 25.0f, &dblCtrR, &dblCtrI);  // nominal center -75 Hz
	dblAvgNoisePerBin = max(1000.0f, 0.25 * (dblAvgNoisePerBin + powf(dblCtrR, 2) + powf(dblCtrI, 2)));  // average of 4 noise bins
	dblLeftCar = 59 + dblTrialOffset / 25;  // the nominal positions of the two tone carriers based on the last computerd dblOffsetHz
	dblRightCar = 61 + dblTrialOffset / 25;

	GoertzelRealImag(intNewSamples, Ptr, 480, dblLeftCar, &dblCtrR, &dblCtrI);  // LEFT carrier
	dblLeftP = powf(dblCtrR, 2) + powf(dblCtrI, 2);
	GoertzelRealImag(intNewSamples, Ptr, 480, dblRightCar, &dblCtrR, &dblCtrI);  // Right carrier
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

	if (dblSNdBPwr > (4 + Squelch) && dblSNdBPwrEarly > Squelch && (dblAvgNoisePerBin > 100.0f || dblPriorFineOffset != 1000.0f))  // making early threshold = lower (after 3 dB compensation for bandwidth)
	{
		// Calculate the interpolation based on the left of the two tones

		dblBinInterpLeft = SpectralPeakLocator(dblLeftR[0], dblLeftI[0], dblLeftR[1], dblLeftI[1], dblLeftR[2], dblLeftI[2], &dblLeftMag);

		// And the right of the two tones

		dblBinInterpRight = SpectralPeakLocator(dblRightR[0], dblRightI[0], dblRightR[1], dblRightI[1], dblRightR[2], dblRightI[2], &dblRightMag);

		// Weight the interpolated values in proportion to their magnitudes

		dblBinInterpLeft = dblBinInterpLeft * dblLeftMag / (dblLeftMag + dblRightMag);
		dblBinInterpRight = dblBinInterpRight * dblRightMag / (dblLeftMag + dblRightMag);

		if (fabsf(dblBinInterpLeft + dblBinInterpRight) < 1.0)  // sanity check for the interpolators
		{
			if (dblBinInterpLeft + dblBinInterpRight > 0)  // consider different bounding below
				*dblOffsetHz = dblTrialOffset + min((dblBinInterpLeft + dblBinInterpRight) * 10.0f, 3);  // average left and right, adjustment bounded to +/- 3Hz max
			else
				*dblOffsetHz = dblTrialOffset + max((dblBinInterpLeft + dblBinInterpRight) * 10.0f, -3);

			// Note the addition of requiring a second detect with small offset dramatically reduces false triggering even at Squelch values of 3
			// The following demonstrated good detection down to -10 dB S:N with squelch = 3 and minimal false triggering.
			// Added rev 0.8.2.2 11/6/2016 RM

			if (fabs(dblPriorFineOffset - *dblOffsetHz) < 2.9f)
			{
				ZF_LOGD("Prior-Offset= %f", (dblPriorFineOffset - *dblOffsetHz));

				// Capture power for debugging ...note: convert to 3 KHz noise bandwidth from 25Hz or 12.Hz for reporting consistancy.

				snprintf(strDecodeCapture, sizeof(strDecodeCapture),
					"Ldr; S:N(3KHz) Early= %f dB, Full %f dB, Offset= %f Hz: ",
					dblSNdBPwrEarly - 20.8f, dblSNdBPwr  - 24.77f, *dblOffsetHz);

				if (AccumulateStats)
				{
					dblLeaderSNAvg = ((dblLeaderSNAvg * intLeaderDetects) + dblSNdBPwr) / (1 + intLeaderDetects);
					intLeaderDetects++;
				}

				dblNCOFreq = 3000 + *dblOffsetHz;  // Set the NCO frequency and phase inc for mixing
				dblNCOPhaseInc = dbl2Pi * dblNCOFreq / 12000;
				dttLastLeaderDetect = dttStartRmtLeaderMeasure = Now;

				State = AcquireSymbolSync;
				*intSN = dblSNdBPwr - 24.77;  // 23.8dB accomodates ratio of 3Kz BW:10 Hz BW (10Log 3000/10 = 24.77)

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


// Function to look at the 2 tone leader and establishes the Symbol framing using envelope search and minimal phase error.

BOOL Acquire2ToneLeaderSymbolFraming()
{
	float dblCarPh;
	float dblReal, dblImag;
	int intLocalPtr = intMFSReadPtr;  // try advancing one symbol to minimize initial startup errors
	float dblAbsPhErr;
	float dblMinAbsPhErr = 5000;  // initialize to an excessive value
	int intIatMinErr;
	float dblPhaseAtMinErr;
	int intAbsPeak = 0;
	int intJatPeak = 0;
	int i;

	// Use Phase of 1500 Hz leader  to establish symbol framing. Nominal phase is 0 or 180 degrees

	if ((intFilteredMixedSamplesLength - intLocalPtr) < 860)
		return FALSE;  // not enough

	intLocalPtr = intMFSReadPtr + EnvelopeCorrelator();  // should position the pointer at the symbol boundary

	// Check 2 samples either side of the intLocalPtr for minimum phase error.(closest to Pi or -Pi)
	// Could be as much as .4 Radians (~70 degrees) depending on sampling positions.

	for (i = -2; i <= 2; i++)  // 0 To 0  // -2 To 2  // for just 5 samples
	{
		// using the full symbol seemed to work best on weak Signals (0 to -5 dB S/N) June 15, 2015

		GoertzelRealImag(intFilteredMixedSamples, intLocalPtr + i, 120, 30, &dblReal, &dblImag);   // Carrier at 1500 Hz nominal Positioning
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
	ZF_LOGD("[Acquire2ToneLeaderSymbolFraming] intIatMinError= %d", intIatMinErr);
	State = AcquireFrameSync;

	if (AccumulateStats)
		intLeaderSyncs++;

	// Debug.WriteLine("   [Acquire2ToneLeaderSymbolSync] iAtMinError = " & intIatMinErr.ToString & "   Ptr = " & intMFSReadPtr.ToString & "  MinAbsPhErr = " & Format(dblMinAbsPhErr, "#.00"))
	// Debug.WriteLine("   [Acquire2ToneLeaderSymbolSync]      Ph1500 @ MinErr = " & Format(dblPhaseAtMinErr, "#.000"))

	// strDecodeCapture &= "Framing; iAtMinErr=" & intIatMinErr.ToString & ", Ptr=" & intMFSReadPtr.ToString & ", MinAbsPhErr=" & Format(dblMinAbsPhErr, "#.00") & ": "

	return TRUE;
}

// Function to establish symbol sync

int EnvelopeCorrelator()
{
	// Compute the two symbol correlation with the Two tone leader template.
	// slide the correlation one sample and repeat up to 240 steps
	// keep the point of maximum or minimum correlation...and use this to identify the the symbol start.

	float dblCorMax  = -1000000.0f;  // Preset to excessive values
	float dblCorMin  = 1000000.0f;
	int intJatMax = 0, intJatMin = 0;
	float dblCorSum, dblCorProduct, dblCorMaxProduct = 0.0;
	int i, j;
	short int75HzFiltered[720];

	if (intFilteredMixedSamplesLength < intMFSReadPtr + 720)
		return -1;

	Filter75Hz(int75HzFiltered, TRUE, 720);  // This filter appears to help reduce avg decode distance (10 frames) by about 14%-19% at WGN-5 May 3, 2015

	for (j = 0; j < 360; j++)  // Over 1.5 symbols
	{
		dblCorSum = 0;
		for (i = 0; i < 240; i++)  // over 1 50 baud symbol (may be able to reduce to 1 symbol)
		{
			dblCorProduct = int50BaudTwoToneLeaderTemplate[i] * int75HzFiltered[120 + i + j];  // note 120 accomdates filter delay of 120 samples
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
		ZF_LOGD("EnvelopeCorrelator CorMax:MaxProd= %f  J= %d", dblCorMax / dblCorMaxProduct, intJatMax);
		return intJatMax;
	}
	else
		return -1;
}


// Function to acquire the Frame Sync for all Frames

BOOL AcquireFrameSyncRSB()
{
	// Two improvements could be incorporated into this function:
	//    1) Provide symbol tracking until the frame sync is found (small corrections should be less than 1 sample per 4 symbols ~2000 ppm)
	//    2) Ability to more accurately locate the symbol center (could be handled by symbol tracking 1) above.

	// This is for acquiring FSKFrameSync After Mixing Tones Mirrored around 1500 Hz. e.g. Reversed Sideband
	// Frequency offset should be near 0 (normally within +/- 1 Hz)
	// Locate the sync Symbol which has no phase change from the prior symbol (BPSK leader @ 1500 Hz)

	int intLocalPtr = intMFSReadPtr;
	int intAvailableSymbols = (intFilteredMixedSamplesLength - intMFSReadPtr) / 240;
	float dblPhaseSym1;  // phase of the first symbol
	float dblPhaseSym2;  // phase of the second symbol
	float dblPhaseSym3;  // phase of the third symbol

	float dblReal, dblImag;
	float dblPhaseDiff12, dblPhaseDiff23;

	int i;

	if (intAvailableSymbols < 3)
		return FALSE;  // must have at least 360 samples to search

	// Calculate the Phase for the First symbol

	GoertzelRealImag(intFilteredMixedSamples, intLocalPtr, 240, 30, &dblReal, &dblImag);  // Carrier at 1500 Hz nominal Positioning with no cyclic prefix
	dblPhaseSym1 = atan2f(dblImag, dblReal);
	intLocalPtr += 240;  // advance one symbol
	GoertzelRealImag(intFilteredMixedSamples, intLocalPtr, 240, 30, &dblReal, &dblImag);  // Carrier at 1500 Hz nominal Positioning with no cyclic prefix
	dblPhaseSym2 = atan2f(dblImag, dblReal);
	intLocalPtr += 240;  // advance one symbol

	for (i = 0; i <=  intAvailableSymbols - 3; i++)
	{
		// Compute the phase of the next symbol

		GoertzelRealImag(intFilteredMixedSamples, intLocalPtr, 240, 30, &dblReal, &dblImag);  // Carrier at 1500 Hz nominal Positioning with no cyclic prefix
		dblPhaseSym3 = atan2f(dblImag, dblReal);
		// Compute the phase differences between sym1-sym2, sym2-sym3
		dblPhaseDiff12 = dblPhaseSym1 - dblPhaseSym2;
		if (dblPhaseDiff12 > M_PI)  // bound phase diff to +/- Pi
			dblPhaseDiff12 -= dbl2Pi;
		else if (dblPhaseDiff12 < -M_PI)
			dblPhaseDiff12 += dbl2Pi;

		dblPhaseDiff23 = dblPhaseSym2 - dblPhaseSym3;
		if (dblPhaseDiff23 > M_PI)  // bound phase diff to +/- Pi
			dblPhaseDiff23 -= dbl2Pi;
		else if (dblPhaseDiff23 < -M_PI)
			dblPhaseDiff23 += dbl2Pi;

		if (fabsf(dblPhaseDiff12) > 0.6667f * M_PI && fabsf(dblPhaseDiff23) < 0.3333f * M_PI)  // Tighten the margin to 60 degrees
		{
//			intPSKRefPhase = (short)dblPhaseSym3 * 1000;

			intLeaderRcvdMs = (int)ceil((intLocalPtr - 30) / 12);  // 30 is to accomodate offset of inital pointer for filter length.
			intMFSReadPtr = intLocalPtr + 240;  // Position read pointer to start of the symbol following reference symbol

			if (AccumulateStats)
				intFrameSyncs += 1;  // accumulate tuning stats

			// strDecodeCapture &= "Sync; Phase1>2=" & Format(dblPhaseDiff12, "0.00") & " Phase2>3=" & Format(dblPhaseDiff23, "0.00") & ": "

			return TRUE;  // pointer is pointing to first 4FSK data symbol. (first symbol of frame type)
		}
		else
		{
			dblPhaseSym1 = dblPhaseSym2;
			dblPhaseSym2 = dblPhaseSym3;
			intLocalPtr += 240;  // advance one symbol
		}
	}

	intMFSReadPtr = intLocalPtr - 480;  // back up 2 symbols for next attempt (Current Sym2 will become new Sym1)
	return FALSE;
}

// Function to Demod FrameType4FSK

BOOL DemodFrameType4FSK(int intPtr, short * intSamples, int * intToneMags)
{
	float dblReal, dblImag;
	int i;
	int intMagSum;
	UCHAR bytSym;

	if ((intFilteredMixedSamplesLength - intPtr) < 2400)
		return FALSE;

	intToneMagsLength = 10;

	if (UseSDFT)
	{
		intSampPerSym = 240;  // 50 baud 4FSK frame type
		Demod1Car4FSK_SDFT(intPtr, TRUE);
		return TRUE;
	}

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

		// Include these tone values in debug log only if FileLogLevel is VERBOSE (1)
		ZF_LOGV("FrameType_bytSym : %d(%d %03.0f/%03.0f/%03.0f/%03.0f)", bytSym, intMagSum, 100.0*intToneMags[4 * i]/intMagSum, 100.0*intToneMags[1 + 4 * i]/intMagSum, 100.0*intToneMags[2 + 4 * i]/intMagSum, 100.0*intToneMags[3 + 4 * i]/intMagSum);
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

	for (j = 0; j <= 4; j++)  // over 5 symbols
	{
		int4ToneSum = 0;
		for (k = 0; k <=3; k++)
		{
			int4ToneSum += intToneMags[intTonePtr + (4 * j) + k];
		}
		if (int4ToneSum == 0)
			int4ToneSum = 1;  // protects against possible overflow
		if (j < 4)
			intToneIndex = ((bytFrameType ^ bytID) & bytMask) >> (6 - 2 * j);
		else
			intToneIndex = ComputeTypeParity(bytFrameType);

		dblDistance += 1.0f - ((1.0f * intToneMags[intTonePtr + (4 * j) + intToneIndex]) / (1.0f * int4ToneSum));
		bytMask = bytMask >> 2;
	}

	dblDistance = dblDistance / 5;  // normalize back to 0 to 1 range
	return dblDistance;
}


// Function to compute the frame type by selecting the minimal distance from all valid frame types.

int MinimalDistanceFrameType(int * intToneMags, UCHAR bytSessionID)
{
	float dblMinDistance1 = 5;  // minimal distance for the first byte initialize to large value
	float dblMinDistance2 = 5;  // minimal distance for the second byte initialize to large value
	float dblMinDistance3 = 5;  // minimal distance for the second byte under exceptional cases initialize to large value
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

	ZF_LOGD("Frame Decode type %x %x %x Dist %.2f %.2f %.2f Sess %x pend %d conn %d lastsess %d",
		intIatMinDistance1, intIatMinDistance2, intIatMinDistance3,
		dblMinDistance1, dblMinDistance2, dblMinDistance3,
		bytSessionID, blnPending, blnARQConnected, bytLastARQSessionID);

	if (bytSessionID == 0xFF)  // we are in a FEC QSO, monitoring an ARQ session or have not yet reached the ARQ Pending or Connected status
	{
		// This handles the special case of a DISC command received from the prior session (where the station sending DISC did not receive an END).

		if (intIatMinDistance1 == DISCFRAME && intIatMinDistance3 == DISCFRAME && ((dblMinDistance1 < 0.3) || (dblMinDistance3 < 0.3)))
		{
			snprintf(strDecodeCapture + strlen(strDecodeCapture), sizeof(strDecodeCapture) - strlen(strDecodeCapture),
				" MD Decode;1 ID=H%X, Type=H29: %s, D1= %.2f, D3= %.2f",
				bytLastARQSessionID, Name(intIatMinDistance1), dblMinDistance1, dblMinDistance3);

			ZF_LOGD("[Frame Type Decode OK  ] %s", strDecodeCapture);

			return intIatMinDistance1;
		}

		// no risk of damage to an existing ARQConnection with END, BREAK, DISC, or ACK frames so loosen decoding threshold

		if (intIatMinDistance1 == intIatMinDistance2 && ((dblMinDistance1 < 0.3) || (dblMinDistance2 < 0.3)))
		{
			snprintf(strDecodeCapture + strlen(strDecodeCapture),  sizeof(strDecodeCapture) - strlen(strDecodeCapture),
				" MD Decode;2 ID=H%X, Type=H%X:%s, D1= %.2f, D2= %.2f",
				bytSessionID, intIatMinDistance1, Name(intIatMinDistance1), dblMinDistance1, dblMinDistance2);
			ZF_LOGD("[Frame Type Decode OK  ] %s", strDecodeCapture);
			dblOffsetLastGoodDecode = dblOffsetHz;
			dttLastGoodFrameTypeDecode = Now;

			return intIatMinDistance1;
		}
		if ((dblMinDistance1 < 0.3) && (dblMinDistance1 < dblMinDistance2) && IsDataFrame(intIatMinDistance1) )  // this would handle the case of monitoring an ARQ connection where the SessionID is not 0xFF
		{
			snprintf(strDecodeCapture + strlen(strDecodeCapture), sizeof(strDecodeCapture) - strlen(strDecodeCapture),
				" MD Decode;3 ID=H%X, Type=H%X:%s, D1= %.2f, D2= %.2f",
				bytSessionID, intIatMinDistance1, Name(intIatMinDistance1), dblMinDistance1, dblMinDistance2);
			ZF_LOGD("[Frame Type Decode OK  ] %s", strDecodeCapture);

			return intIatMinDistance1;
		}

		if ((dblMinDistance2 < 0.3) && (dblMinDistance2 < dblMinDistance1) && IsDataFrame(intIatMinDistance2))  // this would handle the case of monitoring an FEC transmission that failed above when the session ID is = 0xFF
		{
			snprintf(strDecodeCapture + strlen(strDecodeCapture),  sizeof(strDecodeCapture) - strlen(strDecodeCapture),
				" MD Decode;4 ID=H%X, Type=H%X:%s, D1= %.2f, D2= %.2f",
				bytSessionID, intIatMinDistance1, Name(intIatMinDistance1), dblMinDistance1, dblMinDistance2);
			ZF_LOGD("[Frame Type Decode OK  ] %s", strDecodeCapture);

			return intIatMinDistance2;
		}

		snprintf(strDecodeCapture + strlen(strDecodeCapture), sizeof(strDecodeCapture) - strlen(strDecodeCapture),
			" MD Decode;5 Type1=H%X, Type2=H%X, D1= %.2f, D2= %.2f",
			intIatMinDistance1, intIatMinDistance2, dblMinDistance1, dblMinDistance2);
		ZF_LOGD("[Frame Type Decode Fail] %s", strDecodeCapture);

		return -1;  // indicates poor quality decode so  don't use

	}

	else if (blnPending)  // We have a Pending ARQ connection
	{
		// this should be a Con Ack from the ISS if we are Pending

		if (intIatMinDistance1 == intIatMinDistance2)  // matching indexes at minimal distances so high probablity of correct decode.
		{
			snprintf(strDecodeCapture + strlen(strDecodeCapture), sizeof(strDecodeCapture) - strlen(strDecodeCapture),
				" MD Decode;6 ID=H%X, Type=H%X:%s, D1= %.2f, D2= %.2f",
				bytSessionID, intIatMinDistance1, Name(intIatMinDistance1), dblMinDistance1, dblMinDistance2);


			if ((dblMinDistance1 < 0.3) || (dblMinDistance2 < 0.3))
			{
				dblOffsetLastGoodDecode = dblOffsetHz;
				dttLastGoodFrameTypeDecode = Now;  // This allows restricting tuning changes to about +/- 4Hz from last dblOffsetHz
				ZF_LOGD("[Frame Type Decode OK  ] %s", strDecodeCapture);
				return intIatMinDistance1;
			}
			else
			{
				return -1;  // indicates poor quality decode so  don't use
			}
		}

		//	handles the case of a received ConReq frame based on an ID of &HFF (ISS must have missed ConAck reply from IRS so repeated ConReq)

		else if (intIatMinDistance1 == intIatMinDistance3)  // matching indexes at minimal distances so high probablity of correct decode.
		{
			snprintf(strDecodeCapture + strlen(strDecodeCapture), sizeof(strDecodeCapture) - strlen(strDecodeCapture),
				" MD Decode;7 ID=H%X, Type=H%X:%s, D1= %.2f, D3= %.2f",
				bytSessionID, intIatMinDistance1, Name(intIatMinDistance1), dblMinDistance1, dblMinDistance3);

			if (intIatMinDistance1 >= ConReqmin && intIatMinDistance1 <= ConReqmax && ((dblMinDistance1 < 0.3) || (dblMinDistance3 < 0.3)))  // Check for ConReq (ISS must have missed previous ConAck
			{
				dblOffsetLastGoodDecode = dblOffsetHz;
				dttLastGoodFrameTypeDecode = Now;  // This allows restricting tuning changes to about +/- 4Hz from last dblOffsetHz
				ZF_LOGD("[Frame Type Decode OK  ] %s", strDecodeCapture);
				return intIatMinDistance1;
			}
			else
			{
				return -1;  // indicates poor quality decode so don't use
			}
		}
	}
	else if (blnARQConnected)  // we have an ARQ connected session.
	{
		if (AccumulateStats)
		{
			dblAvgDecodeDistance = (dblAvgDecodeDistance * intDecodeDistanceCount + 0.5f * (dblMinDistance1 + dblMinDistance2)) / (intDecodeDistanceCount + 1);
			intDecodeDistanceCount++;
		}

		if (intIatMinDistance1 == intIatMinDistance2)  // matching indexes at minimal distances so high probablity of correct decode.
		{
			if ((intIatMinDistance1 >= DataACKmin && intIatMinDistance1 <= DataACKmax) || (intIatMinDistance1 == BREAK) ||
				(intIatMinDistance1 == END) || (intIatMinDistance1 == DISCFRAME))  // Check for critical ACK, BREAK, END, or DISC frames
			{
				snprintf(strDecodeCapture + strlen(strDecodeCapture), sizeof(strDecodeCapture) - strlen(strDecodeCapture),
					" MD Decode;8 ID=H%X, Critical Type=H%X: %s, D1= %.2f, D2= %.2f",
					bytSessionID, intIatMinDistance1, Name(intIatMinDistance1), dblMinDistance1, dblMinDistance2);
				if ((dblMinDistance1 < 0.3f) || (dblMinDistance2 < 0.3f))  // use tighter limits here
				{
					dblOffsetLastGoodDecode = dblOffsetHz;
					dttLastGoodFrameTypeDecode = Now;  // This allows restricting tuning changes to about +/- 4Hz from last dblOffsetHz
					ZF_LOGD("[Frame Type Decode OK  ] %s", strDecodeCapture);
					return intIatMinDistance1;
				}
				else
				{
					return -1;  // indicates poor quality decode so don't use
				}
			}
			else  // non critical frames
			{
				snprintf(strDecodeCapture + strlen(strDecodeCapture), sizeof(strDecodeCapture) - strlen(strDecodeCapture),
					" MD Decode;9 ID=H%X, Type=H%X: %s, D1= %.2f, D2= %.2f",
					bytSessionID, intIatMinDistance1, Name(intIatMinDistance1), dblMinDistance1, dblMinDistance2);
				// use looser limits here, there is no risk of protocol damage from these frames
				if ((dblMinDistance1 < 0.4) || (dblMinDistance2 < 0.4))
				{
					ZF_LOGD("[Frame Type Decode OK  ] %s", strDecodeCapture);

					dblOffsetLastGoodDecode = dblOffsetHz;
					dttLastGoodFrameTypeDecode = Now;  // This allows restricting tuning changes to about +/- 4Hz from last dblOffsetHz
					return intIatMinDistance1;
				}
				else
				{
					return -1;  // indicates poor quality decode so don't use
				}
			}
		}
		else  // non matching indexes
		{
			snprintf(strDecodeCapture + strlen(strDecodeCapture), sizeof(strDecodeCapture) - strlen(strDecodeCapture),
				" MD Decode;10  Type1=H%X: Type2=H%X: , D1= %.2f, D2= %.2f",
				intIatMinDistance1 , intIatMinDistance2, dblMinDistance1, dblMinDistance2);
			return -1;  // indicates poor quality decode so don't use
		}
	}
	snprintf(strDecodeCapture + strlen(strDecodeCapture), sizeof(strDecodeCapture) - strlen(strDecodeCapture),
		" MD Decode;11  Type1=H%X: Type2=H%X: , D1= %.2f, D2= %.2f",
		intIatMinDistance1 , intIatMinDistance2, dblMinDistance1, dblMinDistance2);
	ZF_LOGD("[Frame Type Decode Fail] %s", strDecodeCapture);
	return -1;  // indicates poor quality decode so don't use
}


// Function to acquire the 4FSK frame type

int Acquire4FSKFrameType()
{
	// intMFSReadPtr is pointing to start of first symbol of Frame Type (total of 10 4FSK symbols in frame type (2 bytes) + 1 parity symbol per byte
	// returns -1 if minimal distance decoding is below threshold (low likelyhood of being correct)
	// returns -2 if insufficient samples
	// Else returns frame type 0-255

	int NewType = 0;
	char Offset[32];

	// Check for sufficient audio available for 10.5 4FSK Symbols.
	// Only 10 will nominally be used, but slightly more may be consumed.
	// Requiring too many excess samples might result in unacceptable
	// delays in decoding, and thus delayed response.
	if ((intFilteredMixedSamplesLength - intMFSReadPtr) < (240 * 10.5))
		return -2;  // Wait for more samples

	if (!DemodFrameType4FSK(intMFSReadPtr, intFilteredMixedSamples, &intToneMags[0]))
	{
		ZF_LOGW(
			"Unexpected return from DemodFrameType4FSK indicating insufficient"
			" audio samples availale.  intFilteredMixedSamplesLength=%d."
			" intMFSReadPtr=%d.",
			intFilteredMixedSamplesLength,
			intMFSReadPtr);
		return -1;
	}

	intRmtLeaderMeasure = (Now - dttStartRmtLeaderMeasure);

	// Now do check received  Tone array for testing minimum distance decoder

	if (ProtocolMode == RXO)  // bytSessionID is uncertain, but alternatives will be tried if unsuccessful.
		NewType = RxoMinimalDistanceFrameType(&intToneMags[0]);
	else if (blnPending)  // If we have a pending connection (btween the IRS first decode of ConReq until it receives a ConAck from the iSS)
		NewType = MinimalDistanceFrameType(&intToneMags[0], bytPendingSessionID);  // The pending session ID will become the session ID once connected)
	else if (blnARQConnected)  // If we are connected then just use the stcConnection.bytSessionID
		NewType = MinimalDistanceFrameType(&intToneMags[0], bytSessionID);
	else  // not connected and not pending so use &FF (FEC or ARQ unconnected session ID
		NewType = MinimalDistanceFrameType(&intToneMags[0], 0xFF);

	if ((NewType >= ConReqmin && NewType <= ConReqmax) || NewType == PING)
		QueueCommandToHost("PENDING");  // early pending notice to stop scanners

	sprintf(Offset, "Offset %5.1f", dblOffsetHz);
	SendtoGUI('O', Offset, strlen(Offset));

	if (NewType >= 0 &&  IsShortControlFrame(NewType))  // update the constellation if a short frame (no data to follow)
		Update4FSKConstellation(&intToneMags[0], &intLastRcvdFrameQuality);

	if (AccumulateStats) {
		if (NewType >= 0)
			intGoodFSKFrameTypes++;
		else
			intFailedFSKFrameTypes++;
	}

	intMFSReadPtr += (240 * 10) + Corrections;  // advance to read pointer to the next symbol (if there is one)
	Corrections = 0;

	return NewType;
}


// Demodulate Functions. These are called repeatedly as samples addive
// and buld a frame in static array  bytFrameData

// Function to demodulate one carrier for all low baud rate 4FSK frame types

// Is called repeatedly to decode multitone modes

BOOL Demod1Car4FSK()
{
	// obsolete versions of this code accommodated multiple 4FSK carriers
	int Start = 0;

	// We can't wait for the full frame as we don't have enough ram, so
	// we do one character at a time, until we run out or end of frame

	// Only continue if we have more than intSampPerSym * 4 chars

	while (State == AcquireFrame)
	{
		// Check for sufficient audio available for 4.5 4FSK Symbols.
		// Only 4 will nominally be used, but slightly more may be consumed.
		if (intFilteredMixedSamplesLength < (intSampPerSym * 4.5))
		{
			// Move any unprocessessed data down buffer

			// (while checking process - will use cyclic buffer eventually

			if (intFilteredMixedSamplesLength < 0)
				ZF_LOGE(
					"Corrupt intFilteredMixedSamplesLength (%d) in Demod1Car4FSK().",
					intFilteredMixedSamplesLength);

			if (intFilteredMixedSamplesLength > 0)
				memmove(intFilteredMixedSamples,
					&intFilteredMixedSamples[Start], intFilteredMixedSamplesLength * 2);

			return FALSE;  // Wait for more samples
		}

		if (UseSDFT)
		{
			// Only do Demod1Car4FSK_SDFT() if carrier has not been successfully demodulated
			//
			if (!CarrierOk[0]) {
				Demod1Car4FSK_SDFT(Start, FALSE);
			}
			else if (SymbolsLeft == 1)
			{
				// Only write this to log once per frame.
				ZF_LOGD("All carriers demodulated. Probably a repeated frame.");
			}
		}
		else
		{
			// obsolete versions of this code accommodated inNumCar > 1
			if (CarrierOk[0] == FALSE)  // Don't redo if already decoded
				Demod1Car4FSKChar(Start, bytFrameData1);
		}
		charIndex++;  // Index into received chars
		SymbolsLeft--;  // number still to decode
		Start += intSampPerSym * 4 + Corrections;  // 4 FSK bit pairs per byte
		intFilteredMixedSamplesLength -= intSampPerSym * 4 + Corrections;
		Corrections = 0;  // Reset Corrections now that they have been applied

		if (SymbolsLeft == 0)
		{
			// - prepare for next
			DecodeCompleteTime = Now;
			DiscardOldSamples();
			ClearAllMixedSamples();
			State = SearchingForLeader;
		}
	}
	return TRUE;
}

short intSdftSamples[5 * MAXDFTLEN / 2];
const short intHalfDftlenZeros[MAXDFTLEN / 2];
// Function to demodulate one carrier for all low baud rate 4FSK frame types using SDFT
void Demod1Car4FSK_SDFT(int Start, BOOL blnGetFrameType)
{
	/*
	Demodulate the 4FSK symbols from intFilteredMixedSample around center freqency
	defined by (global) intCenterFreq.  Populate intToneMags[] and, if blnGetFrameType
	is FALSE, also populate bytFrameData.

	Start, an index into intFilteredMixedSAmples, points to the first sample of the
	first symbol to be decoded.

	Updates bytData() with demodulated bytes
	Updates bytMinSymQuality with the minimum (range is 25 to 100) symbol making up each byte.
	*/

	int nSym;
	int symnum;
	int frqNum;
	int timing_advance;
	int intToneMags_sum;
	UCHAR bytSym;
	int intToneMags_max;
	UCHAR bytCharValue;
	// size of intSampPerSym/2 samples
	int intHalfSampPerSymSize = sizeof(intHalfDftlenZeros)*intSampPerSym/MAXDFTLEN;

	// If demodulating the frame type, or if this is the first call to demodulate data for a
	// 4FSK frame, then blnSdftInitialized is false, so call
	// init_sdft(Start, intCenterFrqs, intSampPerSym) before demodulating first symbol.
	if (!blnSdftInitialized)
	{
		// init_sdft() consumes the first intSampPerSym/2 samples of the first symbol.
		init_sdft(intCenterFreq, intFilteredMixedSamples + Start, intSampPerSym);
		memcpy(intSdftSamples, intHalfDftlenZeros, intHalfSampPerSymSize);
		memcpy(intSdftSamples + intSampPerSym/2, intFilteredMixedSamples + Start, intHalfSampPerSymSize);
		// Don't adjust Start or apply Corrections after init_sdft()
		cumAdvances = 0;
		symbolCnt = 0;
	}

	if (blnGetFrameType)
	{
		// Demodulating all of Frame Type
		nSym = 10;
	} else {
		// Demodulating Frame Data 4 symbols at a time
		nSym = 4;
	}

	for (symnum = 0; symnum < nSym; symnum++)
	{
		/*
		sdft() requires intSamples to begin with the intSampPerSym samples
		consumed by the prior call of sdft(), or the values passed to
		init_sdft() which were implicity preceeded by intSampPerSym/2 zeros.
		These old samples shall be followed by
		intSampPerSym new samples and some extra samples to allow for a
		positive timing_advance.  An extra intSampPerSym/2, for a total
		2.5*intSampPerSym samples, is more than enough extra.
		This requires that intFilteredMixedSamples extends
		(nSym + 1)*intSampPerSym past Start when this function was called.
		intSdftSamples already begins with intSampPerSym old samples
		*/
		memcpy(
			intSdftSamples + intSampPerSym,
			intFilteredMixedSamples + Start + intSampPerSym/2,
			3 * intHalfSampPerSymSize);
		// timing advance may be positive or negative.
		timing_advance = sdft(intSdftSamples, intToneMags, &intToneMagsIndex, intSampPerSym);
		cumAdvances += timing_advance;
		symbolCnt++;
		// Adjust Start so that the correct samples are included in
		// intSdftSamples for the loop.
		Start += timing_advance;
		// Adjust the external variable Corrections so that the correct value of
		// Start will be provided as an argument to the next call of this
		// function.
		Corrections += timing_advance;
		if (AccumulateStats)
			intAccumFSKTracking += timing_advance;
		// Update intSdftSamples to begin with the new old samples.
		// This uses Start, which has already been adjusted by
		// timing_advance, as required.
		memcpy(
			intSdftSamples,
			intFilteredMixedSamples + Start + intSampPerSym/2,
			2 * intHalfSampPerSymSize);

		// Could an indicator of confidence level of bytSym be used for improved FEC??
		/*
		sdft() set intToneMags[intToneMagsIndex] to the
		magnitude squared of the sdft_s values.
		It also incremented intToneMagsIndex to now point to the
		next free slot after the four values that must now be compared.
		*/
		// Start at 1 instead of 0 so that if audio is total silence,
		// later division by this value does not cause an error
		intToneMags_sum = 1;
		for (frqNum = 0; frqNum < FRQCNT; frqNum++)
		{
			intToneMags_sum += intToneMags[intToneMagsIndex - (frqNum + 1)];
		}
		/*
		FrameType will be computed from intToneMags[] values by
		considering minimum distance to a sparse set of possible
		values.  So, extraction of symbols by direct comparison
		of tone magnitudes is not required for that case
		*/
		bytSym = 99;  // value to write to debug log for FrameType
		if (!blnGetFrameType)
		{
			// Compare the tone magnitudes to extract bytSym values.
			bytSym = 0;
			intToneMags_max = 0;
			for (frqNum = 0; frqNum < FRQCNT; frqNum++)
			{
				if (intToneMags[intToneMagsIndex - 4 + frqNum] > intToneMags_max)
				{
					bytSym = frqNum;
					intToneMags_max = intToneMags[intToneMagsIndex - 4 + frqNum];
				}
			}
			bytCharValue = (bytCharValue << 2) + bytSym;
		}
		// Include these tone values in debug log only if FileLogLevel is VERBOSE (1)
		ZF_LOGV(
			"Demod4FSK %d(%03.0f/%03.0f/%03.0f/%03.0f) [timing_advance=%d : avg %.02f per symbol]",
			bytSym,
			100.0*intToneMags[intToneMagsIndex - 4]/intToneMags_sum,
			100.0*intToneMags[intToneMagsIndex - 3]/intToneMags_sum,
			100.0*intToneMags[intToneMagsIndex - 2]/intToneMags_sum,
			100.0*intToneMags[intToneMagsIndex - 1]/intToneMags_sum,
			timing_advance,
			1.0 * cumAdvances / symbolCnt
		);
		Start += intSampPerSym;  // Advance by one symbol.  timing adjustments already made.
	}
	if (!blnGetFrameType)
		bytFrameData1[charIndex] = bytCharValue;
	return;
}

// Function to demodulate one carrier for all low baud rate 4FSK frame types

void Demod1Car4FSKChar(int Start, UCHAR * Decoded)
{
	// Converts intSamples to an array of bytes demodulating the 4FSK symbols with center freq (global) intCenterFreq
	// intPtr should be pointing to the approximate start of the first data symbol
	// Updates bytData() with demodulated bytes
	// Updates bytMinSymQuality with the minimum (range is 25 to 100) symbol making up each byte.

	float dblReal, dblImag;
	float dblSearchFreq;
	float dblMagSum = 0;
	float  dblMag[4];  // The magnitude for each of the 4FSK frequency bins
	UCHAR bytSym;
	static UCHAR bytSymHistory[3];
	int j;
	UCHAR bytData = 0;
	char DebugMess[1024];

	int * intToneMagsptr = &intToneMags[intToneMagsIndex];

	intToneMagsIndex += 16;

	// ReDim intToneMags(4 * intNumOfSymbols - 1)
	// ReDim bytData(intNumOfSymbols \ 4 - 1)

	dblSearchFreq = intCenterFreq + (1.5f * intBaud);  // the highest freq (equiv to lowest sent freq because of sideband reversal)

	// Do one symbol
	snprintf(DebugMess, sizeof(DebugMess), "4FSK_bytSym :");
	for (j = 0; j < 4; j++)		// for each 4FSK symbol (2 bits) in a byte
	{
		// Start at 1 instead of 0 so that if audio is total silence,
		// later division by this value does not cause an error
		dblMagSum = 1;
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

		snprintf(DebugMess + strlen(DebugMess), sizeof(DebugMess) - strlen(DebugMess),
			" %d(%.0f %03.0f/%03.0f/%03.0f/%03.0f)",
			bytSym, dblMagSum, 100*dblMag[0]/dblMagSum, 100*dblMag[1]/dblMagSum, 100*dblMag[2]/dblMagSum, 100*dblMag[3]/dblMagSum);
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
		Start += intSampPerSym;  // advance the pointer one symbol
	}

	// Include these tone values in debug log only if FileLogLevel is VERBOSE (1)
	ZF_LOGV("%s", DebugMess);
	if (AccumulateStats)
		intFSKSymbolCnt += 4;

	Decoded[charIndex] = bytData;
	return;
}


void Demod1Car4FSK600Char(int Start, UCHAR * Decoded);

BOOL Demod1Car4FSK600()
{
	int Start = 0;

	// We can't wait for the full frame as we don't have enough data, so
	// we do one character at a time, until we run out or end of frame

	// Only continue if we have more than intSampPerSym * 4 chars

	while (State == AcquireFrame)
	{
		// Check for sufficient audio available for 4.5 Symbols.
		// Only 4 will nominally be used, but slightly more may be consumed.
		if (intFilteredMixedSamplesLength < (intSampPerSym * 4.5))
		{
			// Move any unprocessessed data down buffer

			// (while checking process - will use cyclic buffer eventually

			if (intFilteredMixedSamplesLength < 0)
				ZF_LOGE(
					"Corrupt intFilteredMixedSamplesLength (%d) in Demod1Car4FSK600().",
					intFilteredMixedSamplesLength);

			if (intFilteredMixedSamplesLength > 0)
				memmove(intFilteredMixedSamples,
					&intFilteredMixedSamples[Start], intFilteredMixedSamplesLength * 2);

			return FALSE;
		}

		// Since "carriers" are sequential (rather than simultaneous on adjacent
		// frequencies as in multi-carrier PSK and QAM frames), don't attempt
		// to skip demodulating individual carriers based on CarrierOk.
		Demod1Car4FSK600Char(Start, bytFrameData1);

		charIndex++;  // Index into received chars

		SymbolsLeft--;  // number still to decode
		Start += intSampPerSym * 4;  // 4 FSK bit pairs per byte
		intFilteredMixedSamplesLength -= intSampPerSym * 4;

		if (SymbolsLeft == 0)
		{
			// - prepare for next

			DecodeCompleteTime = Now;
			DiscardOldSamples();
			ClearAllMixedSamples();
			State = SearchingForLeader;
		}
	}
	return TRUE;
}

void Demod1Car4FSK600Char(int Start, UCHAR * Decoded)
{
	float dblReal, dblImag;
	float dblSearchFreq;
	float dblMagSum = 0;
//	float  dblMag[4];  // The magnitude for each of the 4FSK frequency bins
	UCHAR bytSym = 0;
	static UCHAR bytSymHistory[3];
	int j, k;
	UCHAR bytData = 0;
	int intSampPerSymbol = 12000 / intBaud;
	int intMaxMag;

	int * intToneMagsptr = &intToneMags[intToneMagsIndex];

	intToneMagsIndex += 16;

	dblSearchFreq = intCenterFreq + (1.5f * intBaud);  // the highest freq (equiv to lowest sent freq because of sideband reversal)

	// Do one symbol

	for (j = 0; j < 4; j++)  // for each 4FSK symbol (2 bits) in a byte
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

		// If (bytSymHistory(0) <> bytSymHistory(1)) And (bytSymHistory(1) <> bytSymHistory(2)) Then  // only track when adjacent symbols are different (statistically about 56% of the time)
		// this should allow tracking over 2000 ppm sampling rate error
		//   Track1Car4FSK600bd(intSamples, intPtr, intSampPerSymbol, intSearchFreq, intBaud, bytSymHistory)
		// End If

		Start += intSampPerSym;  // advance the pointer one symbol
	}

	Decoded[charIndex] = bytData;
	return;
}


//	Function to Decode 1 carrier 4FSK 50 baud Connect Request

extern int intBW;

BOOL Decode4FSKConReq(StationId* caller, StationId* target)
{
	BOOL FrameOK;
	stationid_init(caller);
	stationid_init(target);

	// Modified May 24, 2015 to use RS encoding vs CRC (similar to ID Frame)

	// Try correcting with RS Parity
	int NErrors = rs_correct(bytFrameData1, 16, 4, true, false);

	FrameOK = TRUE;
	if (NErrors > 0) {
		// Unfortunately, Reed Solomon correction will sometimes return a
		// non-negative result indicating that the data is OK when it is not.
		// This is most likely to occur if the number of errors exceeds
		// intRSLen.  However, espectially for small intRSLen, it may also
		// occur even if the number of errors is less than or equal to
		// intRSLen.  The test for invalid corrections in the padding bytes
		// performed by rs_correct() can reduce the likelyhood of this,
		// occuring, but it cannot be eliminiated.
		// Without a CRC, it is not possible to be completely confident
		// that the frame is correctly decoded.
		//
		// Check for valid callsigns?
		ZF_LOGD("CONREQ Frame Corrected by RS");
	} else if (NErrors < 0) {
		ZF_LOGD("CONREQ Still bad after RS");
		FrameOK = FALSE;
	}

	station_id_err ec = stationid_from_bytes(&bytFrameData1[0], caller);
	if (ec) {
		ZF_LOGD_MEM(&bytFrameData1[0], PACKED6_SIZE, "Failed to decode ConReq frame calling station ID: %s, bytes: ", stationid_strerror(ec));
		FrameOK = FALSE;
	}

	station_id_err et = stationid_from_bytes(&bytFrameData1[PACKED6_SIZE], target);
	if (et) {
		ZF_LOGD_MEM(&bytFrameData1[PACKED6_SIZE], PACKED6_SIZE, "Failed to decode ConReq frame target station ID: %s, bytes: ", stationid_strerror(et));
		FrameOK = FALSE;
	}

	// Recheck the returned data by reencoding

	if (intFrameType == ConReq200M)
		intBW = 200;
	else if (intFrameType == ConReq500M)
		intBW = 500;
	else if (intFrameType == ConReq1000M)
		intBW = 1000;
	else if (intFrameType == ConReq2000M)
		intBW = 2000;

	if (FrameOK) {
		snprintf(bytData, sizeof(bytData), "%s %s", caller->str, target->str);
	}
	else {
		bytData[0] = '\0';
		stationid_init(caller);
		stationid_init(target);
		SendCommandToHost("CANCELPENDING");
	}

	intConReqSN = Compute4FSKSN();
	ZF_LOGD("DemodDecode4FSKConReq:  S:N=%d Q=%d", intConReqSN, intLastRcvdFrameQuality);
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
	float dblAvgNonDominateTone;
	int i, j;
	int SNcount = 0;

	// First compute the average S:N for all symbols

	for (i = 0; i < intNumSymbols; i++)		// for each symbol
	{
		intNonDominateToneSum = 10;			// Protect divide by zero
		intDominateTones[i] = 0;

		for (j = 0; j < 4; j++)			 // for each tone
		{
			if (intToneMags[4 * i + j] > intDominateTones[i])
				intDominateTones[i] = intToneMags[4 * i + j];

			intNonDominateToneSum += intToneMags[4 * i + j];
		}

		dblAvgNonDominateTone = (intNonDominateToneSum - intDominateTones[i])/ 3;  // subtract out the Dominate Tone from the sum

		// Note subtract dblAvgNonDominateTone below to compute S:N instead of (S+N):N
		// note 10 * log used since tone values are already voltage squared (avoids SQRT)
		// the S:N calculation is limited to a Max of + 50 dB to avoid distorting the average in very low noise environments

		dblAVGSNdB += min(50.0f, 10.0f * log10f((intDominateTones[i] - dblAvgNonDominateTone) / dblAvgNonDominateTone));  // average in the S:N;
	}
	intSNdB = (dblAVGSNdB / intNumSymbols) - 17.8f;  // 17.8 converts from nominal 50 Hz "bin" BW to standard 3 KHz BW (10* Log10(3000/50))
	return intSNdB;
}

// Function to Demodulate and Decode 1 carrier 4FSK 50 baud Ping frame

BOOL Decode4FSKPing(StationId* caller, StationId* target)
{
	stationid_init(caller);
	stationid_init(target);
	BOOL FrameOK;

	int NErrors = rs_correct(bytFrameData1, 16, 4, true, false);
	FrameOK = TRUE;

	if (NErrors > 0)
	{
		// Unfortunately, Reed Solomon correction will sometimes return a
		// non-negative result indicating that the data is OK when it is not.
		// This is most likely to occur if the number of errors exceeds
		// intRSLen.  However, espectially for small intRSLen, it may also
		// occur even if the number of errors is less than or equal to
		// intRSLen.  The test for invalid corrections in the padding bytes
		// performed by rs_correct() can reduce the likelyhood of this,
		// occuring, but it cannot be eliminiated.
		// Without a CRC, it is not possible to be completely confident
		// that the frame is correctly decoded.
		//
		// Check for valid callsigns?
		ZF_LOGD("PING Frame Corrected by RS");
	} else if (NErrors < 0) {
		ZF_LOGD("PING Still bad after RS");
		FrameOK = FALSE;
	}

	station_id_err ec = stationid_from_bytes(&bytFrameData1[0], caller);
	if (ec) {
		ZF_LOGD_MEM(&bytFrameData1[0], PACKED6_SIZE, "Failed to decode Ping frame calling station ID: %s, bytes: ", stationid_strerror(ec));
		FrameOK = FALSE;
	}

	station_id_err et = stationid_from_bytes(&bytFrameData1[PACKED6_SIZE], target);
	if (et) {
		ZF_LOGD_MEM(&bytFrameData1[PACKED6_SIZE], PACKED6_SIZE, "Failed to decode Ping frame target station ID: %s, bytes: ", stationid_strerror(et));
		FrameOK = FALSE;
	}


	if (FrameOK == FALSE)
	{
		bytData[0] = '\0';
		stationid_init(caller);
		stationid_init(target);
		SendCommandToHost("CANCELPENDING");
		return FALSE;
	}

	snprintf(bytData, sizeof(bytData), "%s %s", caller->str, target->str);
	DrawRXFrame(1, bytData);
	char fr_info[32] = "";
	snprintf(fr_info, sizeof(fr_info), "Ping %s>%s", caller->str, target->str);
	wg_send_rxframet(0, 1, fr_info);

	intSNdB = Compute4FSKSN();

	if (ProtocolState == DISC || ProtocolMode == RXO)
	{
		char Msg[80];

		snprintf(Msg, sizeof(Msg), "PING %s>%s %d %d", caller->str, target->str, intSNdB, intLastRcvdFrameQuality);
		SendCommandToHost(Msg);

		ZF_LOGD("[DemodDecode4FSKPing] PING %s>%s S:N=%d Q=%d", caller->str, target->str, intSNdB, intLastRcvdFrameQuality);

		stcLastPingdttTimeReceived = time(NULL);
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

// Dim bytCall(5) As Byte


	if (bytFrameData1[0] == bytFrameData1[1])
		Timing = 10 * bytFrameData1[0];
	else if (bytFrameData1[0] == bytFrameData1[2])
		Timing = 10 * bytFrameData1[0];
	else if (bytFrameData1[1] == bytFrameData1[2])
		Timing = 10 * bytFrameData1[1];

	if (Timing >= 0)
	{
		*intTiming = Timing;

		ZF_LOGD("[DemodDecode4FSKConACK]  Remote leader timing reported: %d ms", *intTiming);

		if (AccumulateStats)
			intGoodFSKFrameDataDecodes++;

		// intTestFrameCorrectCnt++;
		bytLastReceivedDataFrameType = 0;  // initialize the LastFrameType to an illegal Data frame
		return TRUE;
	}

	if (AccumulateStats)
		intFailedFSKFrameDataDecodes++;

	return FALSE;
}


// Function  Decode 1 carrier 4FSK 50 baud PingACK with S:N and Quality
BOOL Decode4FSKPingACK(UCHAR bytFrameType, int * intSNdB, int * intQuality)
{
	int Ack = -1;

	// PingAck contains three copies of a byte that carries SN and Quality data.
	// If any two of these match, assume that they are valid.  If all three are
	// different, then report that the PingAck could not be decoded.
	if ((bytFrameData1[0] == bytFrameData1[1]) || (bytFrameData1[0] == bytFrameData1[2]))
		Ack = bytFrameData1[0];
	else
		if (bytFrameData1[1] == bytFrameData1[2])
			Ack = bytFrameData1[1];

	if (Ack >= 0)
	{
		*intSNdB = ((Ack & 0xF8) >> 3) - 10;  // Range -10 to + 21 dB steps of 1 dB
		*intQuality = (Ack & 7) * 10 + 30;  // Range 30 to 100 steps of 10

		if (*intSNdB == 21)
			ZF_LOGD("[DemodDecode4FSKPingACK]  S:N> 20 dB Quality=%d" ,*intQuality);
		else
			ZF_LOGD("[DemodDecode4FSKPingACK]  S:N= %d dB Quality=%d",  *intSNdB, *intQuality);

		blnPINGrepeating = False;
		blnFramePending = False;	// Cancels last repeat
		return TRUE;
	}
	else {
		*intSNdB = -1;
		*intQuality = -1;
		ZF_LOGD("[DemodDecode4FSKPingACK]  Unable to decode S:N and Quality.");
	}
	return FALSE;
}


BOOL Decode4FSKID(UCHAR bytFrameType, StationId* sender, Locator* grid)
{
	UCHAR bytCall[COMP_SIZE];
	BOOL FrameOK;
	unsigned char * p = bytFrameData1;
	stationid_init(sender);
	locator_init(grid);


	ZF_LOGD("%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x ",
		p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);

	int NErrors = rs_correct(bytFrameData1, 16, 4, true, false);
	FrameOK = TRUE;

	if (NErrors > 0)
	{
		// Unfortunately, Reed Solomon correction will sometimes return a
		// non-negative result indicating that the data is OK when it is not.
		// This is most likely to occur if the number of errors exceeds
		// intRSLen.  However, espectially for small intRSLen, it may also
		// occur even if the number of errors is less than or equal to
		// intRSLen.  The test for invalid corrections in the padding bytes
		// performed by rs_correct() can reduce the likelyhood of this,
		// occuring, but it cannot be eliminiated.
		// Without a CRC, it is not possible to be completely confident
		// that the frame is correctly decoded.
		//
		// Check for valid callsigns?
		ZF_LOGD("ID Frame Corrected by RS");
	} else if (NErrors < 0) {
		ZF_LOGD("ID Still bad after RS");
		FrameOK = FALSE;
	}

	station_id_err es = stationid_from_bytes(&bytFrameData1[0], sender);
	if (es) {
		ZF_LOGD_MEM(&bytFrameData1[0], PACKED6_SIZE, "Failed to decode ID Frame station ID: %s, bytes: ", stationid_strerror(es));
		FrameOK = FALSE;
	}

	locator_err e = locator_from_bytes(&bytFrameData1[COMP_SIZE], grid);
	if (e) {
		ZF_LOGD("Failed to decode ID Frame (sender: %s) grid square: %s", sender->str, locator_strerror(e));
	}

	if (AccumulateStats) {
		if (FrameOK)
			intGoodFSKFrameDataDecodes++;
		else
			intFailedFSKFrameDataDecodes++;
	}

	return FrameOK;
}



// Function to Demodulate Frame based on frame type
// Will be called repeatedly as new samples arrive

void DemodulateFrame(int intFrameType)
{
// Dim stcStatus As Status = Nothing

	int intConstellationQuality = 0;

	// ReDim bytData(-1)

	// stcStatus.ControlName = "lblRcvFrame"

	// DataACK/NAK and short control frames

	if ((intFrameType >= DataNAKmin && intFrameType <= DataNAKmax) ||  intFrameType >= DataACKmin)  // DataACK/NAK
	{
		Demod1Car4FSK();
		return;
	}

	if (intFrameType == IDFRAME || (intFrameType >= ConReqmin && intFrameType <= ConReqmax) || intFrameType == PING)
	{
		// ID and CON Req

		Demod1Car4FSK();
		return;
	}

	switch (intFrameType)
	{
		case ConAck200:
		case ConAck500:
		case ConAck1000:
		case ConAck2000:  // Connect ACKs with Timing
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

		// 4FSK Data

		case 0x48:
		case 0x49:
		case 0x4A:
		case 0x4B:
		case 0x4C:
		case 0x4D:

			Demod1Car4FSK();
			break;

		// 2 Carrier PSK Data frames

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

		default:

			ZF_LOGD("Unsupported frame type %x", intFrameType);
			DiscardOldSamples();
			ClearAllMixedSamples();
			State = SearchingForLeader;


			intFilteredMixedSamplesLength = 0;  // Testing
	}
}


// Function to Strip quality from ACK/NAK frame types

BOOL DecodeACKNAK(int intFrameType, int *  intQuality)
{
	*intQuality = 38 + (2 * (intFrameType & 0x1F));  // mask off lower 5 bits  // Range of 38 to 100 in steps of 2
	return TRUE;
}




BOOL DecodeFrame(int xxx, uint8_t bytData[MAX_DATA_LENGTH])
{
	BOOL blnDecodeOK = FALSE;
	stationid_init(&LastDecodedStationCaller);
	stationid_init(&LastDecodedStationTarget);
	Locator gridSQ;
	locator_init(&gridSQ);
	int intTiming;
	int intRcvdQuality;
	char Reply[80];
	char Good[8] = {1, 1, 1, 1, 1, 1, 1, 1};

	// Some value for handling 4FSK.2000.600 frames.
	int intPartDataLen;
	int intPartRSLen;
	int intPartRawLen;
	int intPartTonesLen;
	int PriorFrameLen;

	// DataACK/NAK and short control frames

	// TODO: Confirm that this is not used because DecodeFrame() is not called
	// for short control frames including ACK and NAK.
	if ((intFrameType >= DataNAKmin && intFrameType <= DataNAKmax) || intFrameType >= DataACKmin)  // DataACK/NAK
	{
		blnDecodeOK = DecodeACKNAK(intFrameType, &intRcvdQuality);
		if (blnDecodeOK) {
			DrawRXFrame(1, Name(intFrameType));
			wg_send_rxframet(0, 1, Name(intFrameType));
		} else {
			DrawRXFrame(2, Name(intFrameType));
			wg_send_rxframet(0, 2, Name(intFrameType));
		}

		goto returnframe;
	}
	else if (IsShortControlFrame(intFrameType))  // Short Control Frames
	{
		blnDecodeOK = TRUE;
		DrawRXFrame(1, Name(intFrameType));
		wg_send_rxframet(0, 1, Name(intFrameType));

		goto returnframe;
	}

	// DON'T reset totalRSErrors here.  RS correction for PSK and QAM frames is already done.
	// totalRSErrors = 0;

	ZF_LOGD("DecodeFrame MEMARQ Flags %d %d %d %d %d %d %d %d",
		CarrierOk[0], CarrierOk[1], CarrierOk[2], CarrierOk[3],
		CarrierOk[4], CarrierOk[5], CarrierOk[6], CarrierOk[7]);

	switch (intFrameType)
	{
		case ConAck200:
		case ConAck500:
		case ConAck1000:
		case ConAck2000:  // Connect ACKs with Timing 0x39-0x3C

			blnDecodeOK = Decode4FSKConACK(intFrameType, &intTiming);

			if (blnDecodeOK)
			{
				bytData[0] = intTiming / 10;
				DrawRXFrame(1, Name(intFrameType));
				wg_send_rxframet(0, 1, Name(intFrameType));
			}

		break;

		case PINGACK:  // 0x3D

			blnDecodeOK = Decode4FSKPingACK(intFrameType, &intSNdB, &intQuality);

			// PingAck includes nothing to indicate who it is being sent to or
			// the identity of the Ping frame it is in response to.  So, the
			// value of dttLastPINGSent is used to determine whether a Ping frame
			// was recently sent that this might be in response to, and ignore it
			// if it no Ping was sent.
			if (blnDecodeOK && ProtocolState == DISC && Now  - dttLastPINGSent < 5000)
			{
				sprintf(Reply, "PINGACK %d %d", intSNdB, intQuality);
				SendCommandToHost(Reply);

				DrawRXFrame(1, Reply);
			}
			// Show in WebGui whether it was to me or not.
			// intSNdB is in the range -10 to 21
			// intQuality is in the range of 30-100
			if (blnDecodeOK) {
				sprintf(Reply, "PingAck (SN%s=%ddB Q%s=%d/100)",
					intSNdB > 20 ? ">" : "", intSNdB,
					intQuality == 30 ? "<" : "", intQuality);
				wg_send_rxframet(0, 1, Reply);
			}
			break;


		case IDFRAME:  // 0x30

			blnDecodeOK = Decode4FSKID(IDFRAME, &LastDecodedStationCaller, &gridSQ);

			frameLen = snprintf(bytData, MAX_DATA_LENGTH, "ID:%s [%s]:" , LastDecodedStationCaller.str, gridSQ.grid);

			if (blnDecodeOK) {
				ZF_LOGI("[DecodeFrame] IDFrame: %s [%s]", LastDecodedStationCaller.str, gridSQ.grid);
				DrawRXFrame(1, bytData);
				wg_send_rxframet(0, 1, (char *)bytData);
			}

			break;

		case ConReq200M:
		case ConReq500M:
		case ConReq1000M:
		case ConReq2000M:
		case ConReq200F:
		case ConReq500F:
		case ConReq1000F:
		case ConReq2000F:  // 0x31-0x38

			blnDecodeOK = Decode4FSKConReq(&LastDecodedStationCaller, &LastDecodedStationTarget);
			if (blnDecodeOK) {
				DrawRXFrame(1, Name(intFrameType));
				char fr_info[32] = "";
				snprintf(fr_info, sizeof(fr_info), "%s (%s)",
					Name(intFrameType), bytData);
				wg_send_rxframet(0, 1, fr_info);
			}

			break;

		case PING:  // 0x3E

			blnDecodeOK = Decode4FSKPing(&LastDecodedStationCaller, &LastDecodedStationTarget);
			break;


		// PSK 1 Carrier Data

		case 0x40:
		case 0x41:
		case 0x42:
		case 0x43:
		case 0x44:
		case 0x45:

			blnDecodeOK = CarrierOk[0];

			if (blnDecodeOK) {
				DrawRXFrame(1, Name(intFrameType));
				wg_send_rxframet(0, 1, Name(intFrameType));
			}

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

			if (blnDecodeOK) {
				DrawRXFrame(1, Name(intFrameType));
				wg_send_rxframet(0, 1, Name(intFrameType));
			}

			break;

		// FSK 1 Carrier Modes
		char HexData[1024];

		case 0x48:
		case 0x49:
		case 0x4a:
		case 0x4b:
		case 0x4c:
		case 0x4d:

			snprintf(HexData, sizeof(HexData), "bytFrameData1 as decoded:   ");
			for (int i = 0; i < intDataLen; ++i)
				snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %02X", bytFrameData1[i]);
			ZF_LOGI("%s", HexData);
			frameLen = CorrectRawDataWithRS(bytFrameData1, bytData, intDataLen, intRSLen, intFrameType, 0);
			snprintf(HexData, sizeof(HexData), "bytFrameData1 after RS:   ");
			for (int i = 0; i < intDataLen; ++i)
				snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %02X", bytFrameData1[i]);
			ZF_LOGI("%s", HexData);

			// Since these are single carrier/part, hard code carrier=part=0
			if (!CarrierOk[0]) {
				// SaveFSKSamples() stores a copy of intToneMags for the first
				// copy (intSumCounts[0] == 0) of a frame that can't be decoded,
				// and updates intToneMags by averaging the magnitudes with each
				// additional repeat of that frame.
				SaveFSKSamples(0, intToneMags, intToneMagsLength); // This increments intSumCounts
				if (intSumCounts[0] > 1) {
					ZF_LOGD("Decode FSK retry RS after MEM ARQ");
					// Re-attempt to decode using intToneMagsAvg[0] updated by
					// SaveFSKSamples()
					Decode1Car4FSK(bytFrameData1, intToneMagsAvg[0], intToneMagsLength);
					snprintf(HexData, sizeof(HexData), "bytFrameData1 after averaging:   ");
					for (int i = 0; i < intDataLen; ++i)
						snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %02X", bytFrameData1[i]);
					ZF_LOGI("%s", HexData);
					frameLen = CorrectRawDataWithRS(bytFrameData1, bytData, intDataLen, intRSLen, intFrameType, 0);
					snprintf(HexData, sizeof(HexData), "bytFrameData1 after RS:   ");
					for (int i = 0; i < intDataLen; ++i)
						snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %02X", bytFrameData1[i]);
					ZF_LOGI("%s", HexData);
					// TODO: It might be interesting to calculate Quality and
					// plot the constellation for this averaged data so as to
					// quantify how much improvement is achieved by using
					// averaged data.
				}
			}

			blnDecodeOK = CarrierOk[0];

			if (blnDecodeOK) {
				DrawRXFrame(1, Name(intFrameType));
				wg_send_rxframet(0, 1, Name(intFrameType));
			}
			break;


		// 2 Carrier PSK Data frames

		case 0x50:
		case 0x51:
		case 0x52:
		case 0x53:

			if (CarrierOk[0] && CarrierOk[1])
				blnDecodeOK = TRUE;

			if (blnDecodeOK) {
				DrawRXFrame(1, Name(intFrameType));
				wg_send_rxframet(0, 1, Name(intFrameType));
			}

			break;

		// 1000 Hz Data frames 4 Carrier

		case 0x60:
		case 0x61:
		case 0x62:
		case 0x63:

			if (memcmp(CarrierOk, Good, intNumCar) == 0)
				blnDecodeOK = TRUE;

			if (blnDecodeOK) {
				DrawRXFrame(1, Name(intFrameType));
				wg_send_rxframet(0, 1, Name(intFrameType));
			}

			break;

		// 2000 Hz Data frames 8 Carrier

		case 0x70:
		case 0x71:
		case 0x72:
		case 0x73:

			if (memcmp(CarrierOk, Good, intNumCar) == 0)
				blnDecodeOK = TRUE;

			if (blnDecodeOK) {
				DrawRXFrame(1, Name(intFrameType));
				wg_send_rxframet(0, 1, Name(intFrameType));
			}
			break;

		case 0x7A:
		case 0x7B:

			// 2000 Hz 600 baud 4FSK frames
			// These are demodulated as a single long block into bytFrameData1
			// (and intToneMags).  However, the contents of bytFrameData1
			// contain three "carriers", each containing their own length byte
			// and 2-byte CRC.  RS error correction and Memory ARQ operate on
			// each of these "carriers" independently.

			// intDataLen and intRSLen define the sum of the number of data and
			// RS bytes over all three "carriers". Define intPartDataLen and
			// intPartRSLen to represent the number of data and RS bytes in each
			// one of these "carriers".
			intPartDataLen = intDataLen / 3;
			intPartRSLen = intRSLen / 3;
			// intPartRawLen is the length data in bytFrameData1 corresponding
			// to each part.
			intPartRawLen = intPartDataLen + intPartRSLen + 3;
			intPartTonesLen = intPartRawLen * 16;  // 4 tones per symbol, 4 symbols per byte

			frameLen = 0;
			for (int part = 0; part < 3; part++) {
				PriorFrameLen = frameLen;
				UCHAR *bytRawPartData = &bytFrameData1[part * intPartRawLen];
				snprintf(HexData, sizeof(HexData), "bytRawPartData (part=%d) as decoded:   ", part);
				for (int i = 0; i < intPartDataLen + 1; ++i)
					snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %02X", bytRawPartData[i]);
				ZF_LOGI("%s", HexData);
				frameLen += CorrectRawDataWithRS(bytRawPartData, &bytData[frameLen], intPartDataLen, intPartRSLen, intFrameType, part);
				snprintf(HexData, sizeof(HexData), "bytRawPartData (part=%d) after RS:   ", part);
				for (int i = 0; i < intPartDataLen + 1; ++i)
					snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %02X", bytRawPartData[i]);
				ZF_LOGI("%s", HexData);

				if (!CarrierOk[part]) {
					// SaveFSKSamples() stores a copy of intToneMags for the first
					// copy (intSumCounts[part] == 0) of a part that can't be decoded,
					// and updates intToneMags by averaging the magnitudes with each
					// additional repeat of that part.
					SaveFSKSamples(part, &intToneMags[part * intPartTonesLen], intPartTonesLen); // This increments intSumCounts
					if (intSumCounts[part] > 1) {
						ZF_LOGD("Decode FSK 600 retry RS after MEM ARQ");
						// try to decode based on intToneMags revised by SaveFSKSamples()
						Decode1Car4FSK(bytRawPartData, intToneMagsAvg[part], intPartTonesLen);
						snprintf(HexData, sizeof(HexData), "bytRawPartData (part=%d) after averaging:   ", part);
						for (int i = 0; i < intPartDataLen + 1; ++i)
							snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %02X", bytRawPartData[i]);
						ZF_LOGI("%s", HexData);
						// reset frameLen so new corrections will overwrite prior (failed) corrections
						frameLen = PriorFrameLen;
						frameLen = CorrectRawDataWithRS(bytRawPartData, &bytData[frameLen], intPartDataLen, intPartRSLen, intFrameType, part);
						snprintf(HexData, sizeof(HexData), "bytRawPartData (part=%d)after RS:   ", part);
						for (int i = 0; i < intPartDataLen + 1; ++i)
							snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %02X", bytRawPartData[i]);
						ZF_LOGI("%s", HexData);
						// TODO: It might be interesting to calculate Quality and
						// plot the constellation for this averaged data so as to
						// quantify how much improvement is achieved by using
						// averaged data.
					}
				}
			}

			if (memcmp(CarrierOk, Good, 3) == 0)
				blnDecodeOK = TRUE;

			if (blnDecodeOK) {
				DrawRXFrame(1, Name(intFrameType));
				wg_send_rxframet(0, 1, Name(intFrameType));
			}

			break;

		case 0x7C:
		case 0x7D:

			// Short 2000 Hz 600 baud 4FSK frames

			snprintf(HexData, sizeof(HexData), "bytFrameData1 as decoded:   ");
			for (int i = 0; i < intDataLen; ++i)
				snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %02X", bytFrameData1[i]);
			ZF_LOGI("%s", HexData);
			frameLen = CorrectRawDataWithRS(bytFrameData1, bytData, intDataLen, intRSLen, intFrameType, 0);
			snprintf(HexData, sizeof(HexData), "bytFrameData1 after RS:   ");
			for (int i = 0; i < intDataLen; ++i)
				snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %02X", bytFrameData1[i]);
			ZF_LOGI("%s", HexData);

			// Since these are single carrier/part, hard code carrier=part=0
			if (!CarrierOk[0]) {
				// SaveFSKSamples() stores a copy of intToneMags for the first
				// copy (intSumCounts[0] == 0) of a frame that can't be decoded,
				// and updates intToneMags by averaging the magnitudes with each
				// additional repeat of that frame.
				SaveFSKSamples(0, intToneMags, intToneMagsLength); // This increments intSumCounts
				if (intSumCounts[0] > 1) {
					ZF_LOGD("Decode FSK 600 retry RS after MEM ARQ");
					// Re-attempt to decode using intToneMagsAvg[0] updated by
					// SaveFSKSamples()

					Decode1Car4FSK(bytFrameData1, intToneMagsAvg[0], intToneMagsLength);
					snprintf(HexData, sizeof(HexData), "bytFrameData1 after averaging:   ");
					for (int i = 0; i < intDataLen; ++i)
						snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %02X", bytFrameData1[i]);
					ZF_LOGI("%s", HexData);
					frameLen = CorrectRawDataWithRS(bytFrameData1, bytData, intDataLen, intRSLen, intFrameType, 0);
					snprintf(HexData, sizeof(HexData), "bytFrameData1 after RS:   ");
					for (int i = 0; i < intDataLen; ++i)
						snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %02X", bytFrameData1[i]);
					ZF_LOGI("%s", HexData);
					// TODO: It might be interesting to calculate Quality and
					// plot the constellation for this averaged data so as to
					// quantify how much improvement is achieved by using
					// averaged data.
				}
			}

			blnDecodeOK = CarrierOk[0];

			if (blnDecodeOK) {
				DrawRXFrame(1, Name(intFrameType));
				wg_send_rxframet(0, 1, Name(intFrameType));
			}

			break;

	}

	if (blnDecodeOK)
	{
		ZF_LOGI("[DecodeFrame] Frame: %s Decode PASS,  Quality= %d,  RS fixed %d (of %d max).", Name(intFrameType),  intLastRcvdFrameQuality, totalRSErrors, (intRSLen / 2) * intNumCar);
		wg_send_quality(0, intLastRcvdFrameQuality, totalRSErrors, (intRSLen / 2) * intNumCar);

		if (frameLen > 0) {
			char Msg[3000] = "";

			snprintf(Msg, sizeof(Msg), "[Decoded bytData] %d bytes as hex values: ", frameLen);
			for (int i = 0; i < frameLen; i++)
				snprintf(Msg + strlen(Msg), sizeof(Msg) - strlen(Msg) - 1, "%02X ", bytData[i]);
			ZF_LOGV("%s", Msg);

			if (utf8_check(bytData, frameLen) == NULL)
				ZF_LOGV("[Decoded bytData] %d bytes as utf8 text: '%.*s'", frameLen, frameLen, bytData);
		}

#ifdef PLOTCONSTELLATION
		if ((intFrameType == IDFRAME || (intFrameType >= ConReqmin && intFrameType <= ConReqmax)) && stationid_ok(&LastDecodedStationCaller))
			DrawDecode(LastDecodedStationCaller.str);  // ID or CONREQ
		else
			DrawDecode("PASS");
		updateDisplay();
#endif
	}

	else
	{
		ZF_LOGI("[DecodeFrame] Frame: %s Decode FAIL,  Quality= %d", Name(intFrameType),  intLastRcvdFrameQuality);
		// For a failure to decode, send max + 1 as the number of
		// RS errors.
		wg_send_quality(0, intLastRcvdFrameQuality, (intRSLen / 2) * intNumCar + 1, (intRSLen / 2) * intNumCar);
#ifdef PLOTCONSTELLATION
		DrawDecode("FAIL");
		updateDisplay();
#endif
	}
	// if a data frame with few error and quality very low, adjust

	if (blnDecodeOK && (totalRSErrors / intNumCar) < (intRSLen / 4) && intLastRcvdFrameQuality < 80)
	{
		ZF_LOGD("RS Errors %d Carriers %d RLen %d Qual %d - adjusting Qual",
			totalRSErrors, intNumCar, intRSLen, intLastRcvdFrameQuality);

		intLastRcvdFrameQuality = 80;
	}


returnframe:

	if (blnDecodeOK && intFrameType >= DataFRAMEmin && intFrameType <= DataFRAMEmax)
		bytLastReceivedDataFrameType = intFrameType;

	return blnDecodeOK;
}

// Function to update the 4FSK Constellation

void drawFastVLine(int x0, int y0, int length, int color);
void drawFastHLine(int x0, int y0, int length, int color);

void Update4FSKConstellation(int * intToneMags, int * intQuality)
{
	// Function to update bmpConstellation plot for 4FSK modes...
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
			intRad = (intRad * PLOTRADIUS) / 50;  // rescale for OLED (50 instead of 42 as we rotate constellation 35 degrees
			x = xCenter + intRad;
			y = yCenter + intRad;
		}
		else if (intToneMags[i + 1] > intToneMags[i] && intToneMags[i + 1] > intToneMags[i + 2] && intToneMags[i + 1] > intToneMags[i + 3])
		{
			if (intToneSum > 0)
				intRad = max(5, 42 - 80 * (intToneMags[i] + intToneMags[i + 2] + intToneMags[i + 3]) / intToneSum);

			dblDistanceSum += (42 - intRad);
			intRad = (intRad * PLOTRADIUS) / 50;  // rescale for OLED (50 instead of 42 as we rotate constellation 35 degrees
			x = xCenter + intRad;
			y = yCenter - intRad;
		}
		else if (intToneMags[i + 2] > intToneMags[i] && intToneMags[i + 2] > intToneMags[i + 1] && intToneMags[i + 2] > intToneMags[i + 3])
		{
			if (intToneSum > 0)
				intRad = max(5, 42 - 80 * (intToneMags[i + 1] + intToneMags[i] + intToneMags[i + 3]) / intToneSum);

			dblDistanceSum += (42 - intRad);
			intRad = (intRad * PLOTRADIUS) / 50;  // rescale for OLED (50 instead of 42 as we rotate constellation 35 degrees
			x = xCenter - intRad;
			y = yCenter - intRad;
		}
		else if (intToneSum > 0)
		{
			intRad = max(5, 42 - 80 * (intToneMags[i + 1] + intToneMags[i + 2] + intToneMags[i]) / intToneSum);

			dblDistanceSum += (42 - intRad);
			intRad = (intRad * PLOTRADIUS) / 50;  // rescale for OLED (50 instead of 42 as we rotate constellation 35 degrees
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

	*intQuality = 100 - (2.7f * (dblDistanceSum / (intToneMagsLength / 4)));  // factor 2.7 emperically chosen for calibration (Qual range 25 to 100)

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
	DrawAxes(*intQuality, strMod);
#endif

	return;
}


// Function to Update the PhaseConstellation

int UpdatePhaseConstellation(short * intPhases, short * intMag, char * strMod, BOOL blnQAM)
{
	// Function to update bmpConstellation plot for PSK modes...
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
	int yCenter = (ConstellationHeight - 2) / 2;
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
			intMagMax = max(intMagMax, intMag[j]);  // find the max magnitude to auto scale
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
		for (j = 1; j < intPhasesLen; j++)  // skip the magnitude of the reference in calculation
		{
			intMagMax = max(intMagMax, intMag[j]);  // find the max magnitude to auto scale
			dblAvgRad += intMag[j];
		}
	}

	dblAvgRad = dblAvgRad / (intPhasesLen - 1);  // the average radius

	for (i = 1; i <  intPhasesLen; i++)  // Don't plot the first phase (reference)
	{
		intP = round((0.001f * intPhases[i]) / dbPhaseStep);

		// compute the Phase and Radius errors

		if (intMag[i] > (dblAvgRadInner + dblAvgRadOuter) / 2)
			dblRadErrorOuter += fabsf(dblAvgRadOuter - intMag[i]);
		else
			dblRadErrorInner += fabsf(dblAvgRadInner - intMag[i]);

		dblPhaseError = fabsf(((0.001 * intPhases[i]) - intP * dbPhaseStep));  // always positive and < .5 *  dblPhaseStep
		dblPhaseErrorSum += dblPhaseError;

#ifdef PLOTCONSTELLATION
		dblRad = PLOTRADIUS * intMag[i] / intMagMax;  // scale the radius dblRad based on intMagMax
		intX = xCenter + dblRad * cosf(dblPlotRotation + intPhases[i] / 1000.0f);
		intY = yCenter + dblRad * sinf(dblPlotRotation + intPhases[i] / 1000.0f);


		// 20240718 LaRue, It may be less attractive on Wiseman's GUI, but it is
		// more useful for diagnostic purposes to include all points (and WebGUI
		// plots the axis lines after all points are plotted).
		mySetPixel(intX, intY, Yellow);
//		if (intX > 0 && intY > 0)
//			if (intX != xCenter && intY != yCenter)
//				mySetPixel(intX, intY, Yellow);  // don't plot on top of axis
#endif
	}

	if (blnQAM)
	{
//		intQuality = max(0, ((100 - 200 * (dblPhaseErrorSum / (intPhasesLen)) / dbPhaseStep)));  // ignore radius error for (PSK) but include for QAM
		intQuality = max(0, (1 - (dblRadErrorInner / (intRadInner * dblAvgRadInner) + dblRadErrorOuter / (intRadOuter * dblAvgRadOuter))) * (100 - 200 * (dblPhaseErrorSum / intPhasesLen) / dbPhaseStep));

//		intQuality = max(0, ((100 - 200 * (dblPhaseErrorSum / (intPhasesLen)) / dbPhaseStep)));  // ignore radius error for (PSK) but include for QAM

		if (AccumulateStats)
		{
			intQAMQualityCnts += 1;
			intQAMQuality += intQuality;
			intQAMSymbolsDecoded += intPhasesLen;
		}
	}
	else
	{
		intQuality =  max(0, ((100 - 200 * (dblPhaseErrorSum / (intPhasesLen)) / dbPhaseStep)));  // ignore radius error for (PSK) but include for QAM

		if (AccumulateStats)
		{
			intPSKQualityCnts[intPSKIndex]++;
			intPSKQuality[intPSKIndex] += intQuality;
			intPSKSymbolsDecoded += intPhasesLen;
		}
	}
#ifdef PLOTCONSTELLATION
	DrawAxes(intQuality, strMod);
#endif
	return intQuality;
}


// Function to track 1 carrier 4FSK. Used for both single and multiple simultaneous carrier 4FSK modes.


VOID Track1Car4FSK(short * intSamples, int * intPtr, int intSampPerSymbol, float dblSearchFreq, int intBaud, UCHAR * bytSymHistory)
{
	// look at magnitude of the tone for bytHistory(1)  2 sample2 earlier and 2 samples later.  and pick the maximum adjusting intPtr + or - 1
	// this seems to work fine on test Mar 16, 2015. This should handle sample rate offsets (sender to receiver) up to about 2000 ppm

	float dblReal, dblImag, dblMagEarly, dblMag, dblMagLate;
	float dblBinToSearch = (dblSearchFreq - (intBaud * bytSymHistory[1])) / intBaud;  // select the 2nd last symbol for magnitude comparison


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

// Demod1Car4FSKChar() or Demod1Car4FSK_SDFT() populated intToneMags[].
// This function uses intToneMags to populate Decoded with byte values.
VOID Decode1Car4FSK(UCHAR * Decoded, int *Magnitudes, int MagsLength) {
	int maxMag = 0;
	unsigned char symbol;
	int index = 0;
	memset(Decoded, 0x00, MagsLength / 16);

	// 4 sequential values in Magnitudes define a symbol.
	// 4 symbols (2 bits each) define a decoded data byte.
	for (int byteNum = 0; byteNum < MagsLength / 16; byteNum++) {
		for (int symNum = 0; symNum < 4; symNum++) {
			maxMag = 0;
			for (int toneNum = 0; toneNum < 4; toneNum++) {
				if (Magnitudes[index] > maxMag) {
					symbol = toneNum;
					maxMag = Magnitudes[index];
				}
				index++;
			}
			Decoded[byteNum] = (Decoded[byteNum] << 2) + symbol;
		}
	}
	return;
}


// Function to Decode one Carrier of PSK modulation

// Ideally want to be able to call on for each symbol, as I don't have the
// RAM to build whole frame

// Call for each set of 4 or 8 Phase Values

int pskStart = 0;


VOID Decode1CarPSK(UCHAR * Decoded, int Carrier)
{
	unsigned int int24Bits;
	UCHAR bytRawData;
	int k;
	int Len = intPhasesLen;

	if (CarrierOk[Carrier])
		return;  // don't do it again

	pskStart = 0;
	charIndex = 0;


	while (Len > 0)
	{

		// Phase Samples are in intPhases

		switch (intPSKMode)
		{
		case 4:  // process 4 sequential phases per byte (2 bits per phase)

			for (k = 0; k < 4; k++)
			{
				if (k == 0)
					bytRawData = 0;
				else
					bytRawData <<= 2;

				if (intPhases[Carrier][pskStart] < 786 && intPhases[Carrier][pskStart] > -786)
				{
				}  // Zero so no need to do anything
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

		case 8:  // Process 8 sequential phases (3 bits per phase)  for 24 bits or 3 bytes

			// Status verified on 1 Carrier 8PSK with no RS needed for High S/N

			// Assume we check for 8 available phase samples before being called

			int24Bits = 0;

			for (k = 0; k < 8; k++)
			{
				int24Bits <<= 3;

				if (intPhases[Carrier][pskStart] < 393 && intPhases[Carrier][pskStart] > -393)
				{
				}  // Zero so no need to do anything
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
			return;  // ????
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

	float dblAlpha = 0.3f;  // low pass filter constant may want to optimize value after testing with large sample rate error.
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
		// Debug.WriteLine("Filtered>LastTrack: Cnt=" & intCountAtLastTrack.ToString & "  Filtered = " & Format(dblFilteredPhaseOffset, "00.000") & "  Offset = " & Format(dblPhaseOffset, "00.000") & "  Unfiltered = " & Format(dblUnfilteredPhase, "00.000"))
		dblFilteredPhaseOffset = dblPhaseOffset - dblRadiansPerSample;
		dblPhaseAtLastTrack = dblFilteredPhaseOffset;

		if (AccumulateStats)
		{
			if (strPSKMod[0] == '1')  // 16QAM Then
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
		// Debug.WriteLine("Filtered<LastTrack: Cnt=" & intCountAtLastTrack.ToString & "  Filtered = " & Format(dblFilteredPhaseOffset, "00.000") & "  Offset = " & Format(dblPhaseOffset, "00.000") & "  Unfiltered = " & Format(dblUnfilteredPhase, "00.000"))
		dblFilteredPhaseOffset = dblPhaseOffset + dblRadiansPerSample;
		dblPhaseAtLastTrack = dblFilteredPhaseOffset;

		if (AccumulateStats)
		{
			if (strPSKMod[0] == '1')  // 16QAM Then
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
	// Debug.WriteLine("Filtered Phase = " & Format(dblFilteredPhaseOffset, "00.000") & "  Offset = " & Format(dblPhaseOffset, "00.000") & "  Unfiltered = " & Format(dblUnfilteredPhase, "00.000"))
	return 0;
}

// Function to compute the difference of two angles

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

// Function to "rotate" the phases to try and set the average offset to 0.

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

	short intPhaseMargin  = 2095 / intPSKMode;  // Compute the acceptable phase correction range (+/-30 degrees for 4 PSK)
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

	intPhaseMargin  = 2793 / intPSKMode;  // Compute the acceptable phase correction range (+/-30 degrees for 4 PSK)
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
		ZF_LOGD("[CorrectPhaseForTuningOffset] AvgOffsetBeginning=%d AvgOffsetEnd=%d AccOffsetCnt=%d/%d",
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
		ZF_LOGD("[CorrectPhaseForTuningOffset] AvgOffset=%d AccOffsetCnt=%d/%d",
				intAvgOffset, intAccOffsetCnt, intPhaseLength);

	}
}

// Function to Decode one Carrier of 16QAM modulation

// Call for each set of 4 or 8 Phase Values

short intCarMagThreshold[8] = {0};


VOID Decode1CarQAM(UCHAR * Decoded, int Carrier)
{
	unsigned int intData;
	int k;
	float dblAlpha = 0.1f;  // this determins how quickly the rolling average dblTrackingThreshold responds.

	// dblAlpha value of .1 seems to work well...needs to be tested on fading channel (e.g. Multipath)

	int Threshold = intCarMagThreshold[Carrier];
	int Len = intPhasesLen;

	if (CarrierOk[Carrier])
		return;  // don't do it again

	pskStart = 0;
	charIndex = 0;

	// We calculated initial mag from reference symbol

	// use filtered tracking of refernce phase amplitude
	// (should be full amplitude value)

	// On WGN this appears to improve decoding threshold about 1 dB 9/3/2016

	while (Len > 0)
	{
		// Phase Samples are in intPhases

		intData = 0;

		for (k = 0; k < 2; k++)
		{
			intData <<= 4;

			if (intPhases[Carrier][pskStart] < 393 && intPhases[Carrier][pskStart] > -393)
			{
			}  // Zero so no need to do anything
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
				intData += 8;  // add 8 to "inner circle" symbols.
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
// Functions to demod all PSKData frames single or multiple carriers


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

	// obsolete versions of this code accommodated intBaud = 167 as well as 100.
	intSampPerSym = 120;

	if (intNumCar == 1)
		intCarFreq = 1500;
	else
		intCarFreq = 1400 + (intNumCar / 2) * 200;  // start at the highest carrier freq which is actually the lowest transmitted carrier due to Reverse sideband mixing

	for (i= 0; i < intNumCar; i++)
	{
		// obsolete versions of this code accommodated intBaud = 167 as well as 100.
		// Experimental use of Hanning Windowing

		intNforGoertzel[i] = 120;
		dblFreqBin[i] = intCarFreq / 100;
		intCP[i] = 0;

/*		if (intBaud == 100 && intCarFreq == 1500)
		{
			intCP[i] = 20;  // These values selected for best decode percentage (92%) and best average 4PSK Quality (82) on MPP0dB channel
			dblFreqBin[i] = intCarFreq / 150;
			intNforGoertzel[i] = 80;
		}
		else if (intBaud == 100)
		{
			intCP[i] = 28;  // This value selected for best decoding percentage (56%) and best Averag 4PSK Quality (77) on mpg +5 dB
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

		intCarMagThreshold[i] = sqrtf(powf(dblReal, 2) + powf(dblImag, 2));
		intCarMagThreshold[i] *= 0.75;

		intCarFreq -= 200;  // Step through each carrier Highest to lowest which is equivalent to lowest to highest before RSB mixing.
	}
}

int Demod1CarPSKChar(int Start, int Carrier);
void SavePSKSamples(int i);

void DemodPSK()
{
	int Used[8] = {0};
	int Start = 0;
	int MemARQRetries = 0;

	// We can't wait for the full frame as we don't have enough RAM, so
	// we do one DMA Buffer at a time, until we run out or end of frame

	// Only continue if we have enough samples

	intPSKMode = strMod[0] - '0';

	while (State == AcquireFrame)
	{
		// Check for sufficient audio available for 1.5 Symbols.
		// Only 1 will nominally be used, but slightly more may be consumed.
		if (intFilteredMixedSamplesLength < 1.5 * intPSKMode * intSampPerSym)
		{
			// Move any unprocessessed data down buffer

			// (while checking process - will use cyclic buffer eventually

			if (intFilteredMixedSamplesLength > 0 && Start > 0)
				memmove(intFilteredMixedSamples,
					&intFilteredMixedSamples[Start], intFilteredMixedSamplesLength * 2);

			return;  // Wait for more samples
		}


		if (PSKInitDone == 0)  // First time through
		{
			// Check for sufficient audio available for 2.5 Symbols.
			// Only 2 will nominally be used, but slightly more may be consumed.
			if (intFilteredMixedSamplesLength < 2.5 * intPSKMode * intSampPerSym)
				return;  // Wait for more samples

			InitDemodPSK();
			intFilteredMixedSamplesLength -= intSampPerSym;
			Start += intSampPerSym;
		}

		// If this is a multicarrier mode, we must call the
		// decode char routing for each carrier

		if (intNumCar == 1)
			intCarFreq = 1500;
		else
			intCarFreq = 1400 + (intNumCar / 2) * 200;  // start at the highest carrier freq which is actually the lowest transmitted carrier due to Reverse sideband mixing

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
			SymbolsLeft--;  // number still to decode
		else
			SymbolsLeft -=3;

		// If/when we reenable phase correction we can take average of Used values.
		// ?? Should be also keep start value per carrier ??

		Start += Used[0];
		intFilteredMixedSamplesLength -= Used[0];

		if (intFilteredMixedSamplesLength < 0)
			ZF_LOGE("Corrupt intFilteredMixedSamplesLength");

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

		for (int Carrier = 0; Carrier < intNumCar; Carrier++)
		{
			if (!CarrierOk[Carrier])
			{
				// Decode error - save data for MEM ARQ

				SavePSKSamples(Carrier);

				if (intSumCounts[Carrier] > 1)
				{
					Decode1CarPSK(bytFrameData[Carrier], Carrier);  // try to decode based on the WeightedAveragePhases
					MemARQRetries++;
				}
			}
		}

		if (MemARQRetries)
		{
			// We've retryed to decode - see if ok now

			int OKNow = TRUE;

			ZF_LOGD("DemodPSK retry RS on MEM ARQ Corrected frames");
			frameLen = 0;

			for (int Carrier = 0; Carrier < intNumCar; Carrier++)
			{
				frameLen += CorrectRawDataWithRS(bytFrameData[Carrier], &bytData[frameLen], intDataLen, intRSLen, intFrameType, Carrier);
				if (CarrierOk[Carrier] == 0)
					OKNow = FALSE;
			}

			if (OKNow && AccumulateStats)
				intGoodPSKSummationDecodes++;
		}

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

	// This is called for one DMA buffer of samples (normally 1200)

	float dblReal, dblImag;
	int intMiliRadPerSample = intCarFreq * M_PI / 6;
	int i;
	int intNumOfSymbols = intPSKMode;
	int origStart = Start;

	if (CarrierOk[Carrier])  // Already decoded this carrier?
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

	return (Start - origStart);  // Symbols we've consumed
}

VOID InitDemodQAM()
{
	// Called at start of frame

	int i;
	float dblPhase, dblReal, dblImag;

	intPSKMode = 8;  // 16QAM uses 8 PSK
	dblPhaseInc = 2 * M_PI * 1000 / 8;
	intPhasesLen = 0;

	PSKInitDone = TRUE;

	intSampPerSym = 120;

	if (intNumCar == 1)
		intCarFreq = 1500;
	else
		intCarFreq = 1400 + (intNumCar / 2) * 200;  // start at the highest carrier freq which is actually the lowest transmitted carrier due to Reverse sideband mixing

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

// Function to average two angles using magnitude weighting

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
	MemarqUpdated();
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
	MemarqUpdated();
}


// Rather than save and average the raw tone magnitude values, convert each set
// of four tone magnitudes (1 symbol) into relative values.  This way,
// variations in the the average magnitude between repetitions will not
// influence the results.  While taking advantage of that average magnitude
// might be benficial when signal is suppressed by fading, it would be
// detrimental when a loud static crash or other interference is stronger than
// the signal that is being decoded.

// Magnitudes are averaged into intToneMagsAvg[Part].  These averaged values
// should then be used to re-attempt to decode the part.
void SaveFSKSamples(int Part, int *Magnitudes, int Length) {
	int m;
	const float SCALE = 1000.0;  // Sum of each set of 4 tones in intToneMagsAvg
	char HexData[2000];
	snprintf(HexData, sizeof(HexData), "Mags (part=%d) :  ", Part);
	for (int i = 0; i < 20; ++i)
		snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %d", Magnitudes[i]/1000);
	snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " ...");
	for (int i = Length - 20; i < Length; ++i)
		snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %d", Magnitudes[i]/1000);
	ZF_LOGI("%s", HexData);

	snprintf(HexData, sizeof(HexData), "intToneMagsAvg[%d] :  ", Part);
	for (int i = 0; i < 20; ++i)
		snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %d", intToneMagsAvg[Part][i]);
	snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " ...");
	for (int i = Length - 20; i < Length; ++i)
		snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %d", intToneMagsAvg[Part][i]);
	ZF_LOGI("%s", HexData);

	if (intSumCounts[Part] == 0)
	{
		// First try - copy normalized Magnitudes to intToneMagsAvg
		for (m = 0; m < Length; m += 4) {
			// Rescale Magnitudes
			float sum = 0.0;
			for(int i = 0; i < 4; i++)
				sum += Magnitudes[m + i];
			for(int i = 0; i < 4; i++)
				intToneMagsAvg[Part][m + i] = (int) round(Magnitudes[m + i] / sum * SCALE);
		}
	}
	else
	{
		for (m = 0; m < Length; m += 4)
		{
			// Use simple weighted average for Mags
			// Rescale Magnitudes before averaging in
			float sum = 0.0;
			for(int i = 0; i < 4; i++)
				sum += Magnitudes[m + i];
			for(int i = 0; i < 4; i++) {
				int scaledMag = (int) round(Magnitudes[m + i] / sum * SCALE);
				intToneMagsAvg[Part][m + i] = (intToneMagsAvg[Part][m + i] * intSumCounts[Part] + scaledMag) / (intSumCounts[Part] + 1);
			}
		}
	}
	snprintf(HexData, sizeof(HexData), "intToneMagsAvg[%d] :  ", Part);
	for (int i = 0; i < 20; ++i)
		snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %d", intToneMagsAvg[Part][i]);
	snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " ...");
	for (int i = Length - 20; i < Length; ++i)
		snprintf(HexData + strlen(HexData), sizeof(HexData) - strlen(HexData), " %d", intToneMagsAvg[Part][i]);
	ZF_LOGI("%s", HexData);

	intSumCounts[Part]++;
	MemarqUpdated();
}

BOOL DemodQAM()
{
	int Used = 0;
	int Start = 0;
	int MemARQRetries = 0;

	// We can't wait for the full frame as we don't have enough RAM, so
	// we do one DMA Buffer at a time, until we run out or end of frame

	// Only continue if we have enough samples

	while (State == AcquireFrame)
	{
		if (intFilteredMixedSamplesLength < 8 * intSampPerSym + 10)  // allow for a few phase corrections
		{
			// Move any unprocessessed data down buffer

			//	(while checking process - will use cyclic buffer eventually

			if (intFilteredMixedSamplesLength > 0)
				memmove(intFilteredMixedSamples,
					&intFilteredMixedSamples[Start], intFilteredMixedSamplesLength * 2);

			return FALSE;
		}

		if (PSKInitDone == 0)  // First time through
		{
			if (intFilteredMixedSamplesLength < 9 * intSampPerSym + 10)
				return FALSE;  // Wait for at least 2 chars worth

			InitDemodQAM();
			intFilteredMixedSamplesLength -= intSampPerSym;
			Start += intSampPerSym;
		}

		// If this is a multicarrier mode, we must call the
		// decode char routine for each carrier

		if (intNumCar == 1)
			intCarFreq = 1500;
		else
			intCarFreq = 1400 + (intNumCar / 2) * 200;  // start at the highest carrier freq which is actually the lowest transmitted carrier due to Reverse sideband mixing


		Used = Demod1CarQAMChar(Start, 0);  // demods two phase values - enough for one char

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

		SymbolsLeft--;  // number still to decode - we've done one

		Start += Used;
		intFilteredMixedSamplesLength -= Used;

		if (SymbolsLeft <= 0)
		{
			// Frame complete - decode it

			DecodeCompleteTime = Now;

			CorrectPhaseForTuningOffset(&intPhases[0][0], intPhasesLen, strMod);

//			if (intNumCar > 1)
//				CorrectPhaseForTuningOffset(&intPhases[1][0], intPhasesLen, strMod);

			if (intNumCar > 2)
			{
//				CorrectPhaseForTuningOffset(&intPhases[2][0], intPhasesLen, strMod);
//				CorrectPhaseForTuningOffset(&intPhases[3][0], intPhasesLen, strMod);
			}
			if (intNumCar > 4)
			{
//				CorrectPhaseForTuningOffset(&intPhases[4][0], intPhasesLen, strMod);
//				CorrectPhaseForTuningOffset(&intPhases[5][0], intPhasesLen, strMod);
//				CorrectPhaseForTuningOffset(&intPhases[6][0], intPhasesLen, strMod);
//				CorrectPhaseForTuningOffset(&intPhases[7][0], intPhasesLen, strMod);
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
			for (int Carrier = 0; Carrier < intNumCar; Carrier++)
			{
				if (!CarrierOk[Carrier])
				{
					// Decode error - save data for MEM ARQ

					SaveQAMSamples(Carrier);

					if (intSumCounts[Carrier] > 1)
					{
						Decode1CarQAM(bytFrameData[Carrier], Carrier);  // try to decode based on the WeightedAveragePhases
						MemARQRetries++;
					}
				}
			}

			if (MemARQRetries)
			{
				// We've retryed to decode - see if ok now

				int OKNow = TRUE;

				ZF_LOGD("DemodQAM retry RS on MEM ARQ Corrected frames");
				frameLen = 0;

				for (int Carrier = 0; Carrier < intNumCar; Carrier++)
				{
					frameLen += CorrectRawDataWithRS(bytFrameData[Carrier], &bytData[frameLen], intDataLen, intRSLen, intFrameType, Carrier);
					if (CarrierOk[Carrier] == 0)
						OKNow = FALSE;
				}

				if (OKNow && AccumulateStats)
					intGoodQAMSummationDecodes++;
			}

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
	int origStart = Start;

	if (CarrierOk[Carrier])  // Already decoded this carrier?
	{
		intPhasesLen += intNumOfSymbols;
		return intSampPerSym * intNumOfSymbols;
	}

	for (i = 0; i <  intNumOfSymbols; i++)
	{
		// GoertzelRealImag(intFilteredMixedSamples, Start + intCP[Carrier], intNforGoertzel[Carrier], dblFreqBin[Carrier], &dblReal, &dblImag);
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

				// GoertzelRealImag(intFilteredMixedSamples, Start + intCP[Carrier], intNforGoertzel[Carrier], dblFreqBin[Carrier], &dblReal, &dblImag);
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

	return (Start - origStart);  // Symbols we've consumed
}
