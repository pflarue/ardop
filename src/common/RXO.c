#include <stdbool.h>

#include "common/ARDOPC.h"
#include "common/ardopcommon.h"
#include "common/RXO.h"
#include "common/eutf8.h"

extern UCHAR bytSessionID;
extern UCHAR bytFrameData1[760];
extern int stcLastPingintRcvdSN;  // defined in ARDOPC.c. updated in SoundInput.c
extern int stcLastPingintQuality;  // defined in ARDOPC.c. updated in SoundInput.c
extern int intSNdB;  // defined in SoundInput.c
extern int intQuality;  // defined in SoundInput.c
extern int intLastRcvdFrameQuality;  // defined in SoundInput.c
extern bool WG_DevMode;
int wg_send_hostdatat(int cnum, char *prefix, unsigned char *data, int datalen);
int wg_send_hostdatab(int cnum, char *prefix, unsigned char *data, int datalen);
void ResetMemoryARQ();

// Use of RXO is assumed to indicate that the user is interested in observing all received traffic.
// So LOGI (level=3), rather than a lower log level such as LOGD (level=2), is used for most positive results.

// Function to compute the "distance" from a specific bytFrame Xored by bytID using 1 symbol parity
// The tones representing the Frame Type include two parity symbols which should be identical, and
// should match ComputeTypeParity(bytFrameType).  For RXO mode, the SessionID is unknown/unreliable,
// So Tones 5-8, which represent FrameType ^ SessionID are not used to compute FrameType.  However,
// the second copy of the parity symbol (Tone 9) should be used.  RxoComputeDecodeDistance() differs
// from ComputeDecodeDistance() in its use of the second copy of the parity symbol.
// (It also differs from ComputeDecodeDistance() by not requiring intTonePtr or bytId arguments.)
float RxoComputeDecodeDistance(int * intToneMags, UCHAR bytFrameType)
{
	float dblDistance = 0;
	int int4ToneSum;
	int intToneIndex;
	UCHAR bytMask = 0xC0;
	int j, k;

	for (j = 0; j < 10; j++)
	{
		if (j > 4 && j != 9)  // ignore symbols 5-8, which are useless if SessionID is unknown/unreliable
			continue;

		int4ToneSum = 0;
		for (k = 0; k < 4; k++)
		{
			int4ToneSum += intToneMags[(4 * j) + k];
		}
		if (int4ToneSum == 0)
			int4ToneSum = 1;  // protects against possible overflow
		if (j < 4)
			intToneIndex = (bytFrameType & bytMask) >> (6 - 2 * j);
		else
			intToneIndex = ComputeTypeParity(bytFrameType);

		dblDistance += 1.0f - ((1.0f * intToneMags[(4 * j) + intToneIndex]) / (1.0f * int4ToneSum));
		bytMask = bytMask >> 2;
	}

	dblDistance = dblDistance / 6;  // normalize back to 0 to 1 range
	return dblDistance;
}

// Decode the likely SessionID. If the decode distance is less than dblMaxDistance, then
// set bytSessionID and return true, else leave bytSessionID unchanged and return false.
// SessionID is useful in RXO mode to indicate whether decoded frames are part of the
// same session (or at least another session between the same two stations).
bool RxoDecodeSessionID(UCHAR bytFrameType, int * intToneMags, float dblMaxDistance)
{
	UCHAR bytID = 0;
	int int4ToneSum;
	int intMaxToneMag;
	UCHAR bytTone;
	int j, k;
	float dblDistance = 1.0;

	// Direct decoding of the tones 5-8
	for (j = 20; j < 36; j += 4)
	{
		int4ToneSum = 0;
		for (k = 0; k < 4; k++)
			int4ToneSum += intToneMags[j + k];

		intMaxToneMag = 0;
		for (k = 0; k < 4; k++)
		{
			if (intToneMags[j + k] > intMaxToneMag)
			{
				bytTone = k;
				intMaxToneMag = intToneMags[j + k];
			}
		}
		bytID = (bytID << 2) + bytTone;
		dblDistance -= 0.25 * intMaxToneMag / int4ToneSum;
	}
	bytID ^= bytFrameType;

	if (dblDistance > dblMaxDistance)
	{
		if (bytID == bytSessionID)
			ZF_LOGD("[RXO DecodeSessionID FAIL] Decoded ID=H%02hhX Dist=%.2f (%.2f Max). (Matches Prior ID)",
					bytID, dblDistance, dblMaxDistance);
		else
			ZF_LOGD("[RXO DecodeSessionID FAIL] Decoded ID=H%02hhX Dist=%.2f (%.2f Max). (Retain prior ID=H%02X)",
					bytID, dblDistance, dblMaxDistance, bytSessionID);
		return false;
	}
	if (bytID == bytSessionID)
	{
		ZF_LOGD("[RXO DecodeSessionID OK  ] Decoded ID=H%02hhX Dist=%.2f (%.2f Max). (No change)",
				bytID, dblDistance, dblMaxDistance);
	return true;
	}
	ZF_LOGI("[RXO DecodeSessionID OK  ] Decoded ID=H%02hhX Dist=%.2f (%.2f Max). (Prior ID=H%02hhX)",
			bytID, dblDistance, dblMaxDistance, bytSessionID);
	bytSessionID = bytID;
	return true;
}

int RxoMinimalDistanceFrameType(int * intToneMags)
{
	float dblMinDistance = 5;  // minimal distance. initialize to large value
	UCHAR bytIatMinDistance;
	float dblDistance;
	int i;

	// Search through all the valid frame types finding the minimal distance
	for (i = 0; i < bytValidFrameTypesLengthALL; i++)
	{
		dblDistance = RxoComputeDecodeDistance(intToneMags, bytValidFrameTypesALL[i]);
		if (dblDistance < dblMinDistance)
		{
			dblMinDistance = dblDistance;
			bytIatMinDistance = bytValidFrameTypesALL[i];
		}
	}

	ZF_LOGD("RXO MD Decode Type=H%02X:%s, Dist = %.2f", bytIatMinDistance, Name(bytIatMinDistance), dblMinDistance);
	if (dblMinDistance < 0.3)
	{
		// Decode of Frame Type is Good independent of bytSessionID
		ZF_LOGI("[Frame Type Decode OK  ] H%02X:%s", bytIatMinDistance, Name(bytIatMinDistance));

		// Only update bytSessionID if the decode distance is nearly as good as the
		// decode distance for the Frame Type.  Recall that the two parity tones and
		// the sparseness of ValidFrameTypesALL increase the computed dblMinDistance
		// for an invalid or noisy FrameType, while the decode distance for the
		// SessionID is based only on the noise in the four tones since the parity
		// tones are not useful for this, and RxoDecodeSessionID() considers all
		// SessionID values to be equally likely (no sparseness).  Also, failure to
		// accept a decoded SessionID does not impact the decoding of the remainder
		// of the frame.  In RXO mode, SessionID is only used as an indicator that
		// decoded frames are part of the same session, or at lease a session between
		// the same stations.
		RxoDecodeSessionID(bytIatMinDistance, intToneMags, dblMinDistance + 0.02);

		return bytIatMinDistance;
	}
	// Failure (independent of SessionID)
	ZF_LOGD("[Frame Type Decode Fail]");
	return -1;  // indicates poor quality decode so don't use
}

void ProcessRXOFrame(UCHAR bytFrameType, int frameLen, UCHAR * bytData, bool blnFrameDecodedOK)
{
	// The largest data capacity of any Ardop data frame is 1024 bytes for a
	// 16QAM.200.100 frame type.  Worst case behavior of eutf8() produces output
	// that is three times as long as the input data plus one byte.  So, an
	// available message length of 3200 should be more than adequate to log the
	// eutf8 encoded data from any data frame along with a suitable preamble.
	char Msg[3200];

	if (bytFrameType >= ConReqmin && bytFrameType <= ConReqmax)
	{
		// Is there a reason why frameLen is not defined for ConReq?
		ZF_LOGI("    [RXO %02hhX] ConReq data is callerID targetID", bytSessionID);
		frameLen = strlen((char*) bytData);
	}
	else if (bytFrameType >= ConAckmin && bytFrameType <= ConAckmax)
	{
		ZF_LOGI("    [RXO %02hhX] ConAck data is the length (in tens of ms) of the received leader repeated 3 times: %d %d %d",
			bytSessionID, bytFrameData1[0], bytFrameData1[1], bytFrameData1[2]);
	}
	else if (bytFrameType == PINGACK)
	{
		ZF_LOGI("    [RXO %02hhX] PingAck data is S:N=%d and Quality=%d of the Ping. (Any S:N > 20 is reorted as 21.)",
			bytSessionID, intSNdB, intQuality);
	}
	else if (bytFrameType == PING)
	{
		ZF_LOGI("    [RXO %02hhX] Ping data is caller and target callsigns: '%s'.  While this frame does uses FEC to improve likelihood of correct transmission, it does not include a CRC check with which to confirm correctness.  This Ping was received with S:N=%d, Q=%d.",
			bytSessionID, bytData, stcLastPingintRcvdSN, stcLastPingintQuality);
	}
	else if (bytFrameType >= DataACKmin)
	{
		ZF_LOGI("    [RXO %02hhX] DataAck FrameType (0x%02X) indicates decode quality (%d/100). 60+ typically required for decoding.",
			bytSessionID, bytFrameType, 38 + (2 * (bytFrameType & 0x1F)));
	}
	else if (bytFrameType <= DataNAKmax)
	{
		ZF_LOGI("    [RXO %02hhX] DataNak FrameType (0x%02X) indicates decode quality (%d/100). 60+ typically required for decoding.",
			bytSessionID, bytFrameType, 38 + (2 * (bytFrameType & 0x1F)));
	}

	if (blnFrameDecodedOK) {
		ZF_LOGI("    [RXO %02hhX] %s frame received OK.  frameLen = %d",
				bytSessionID, Name(bytFrameType), frameLen);
	} else {
		ZF_LOGD("    [RXO %02hhX] %s frame decode FAIL.  frameLen = %d",
				bytSessionID, Name(bytFrameType), frameLen);
	}
	if (frameLen > 0)
	{
		snprintf(Msg, sizeof(Msg),
			"    [RXO %02hhX] %d bytes of data (eutf8):\n",
			bytSessionID, frameLen);
		eutf8(Msg + strlen(Msg), sizeof(Msg) - strlen(Msg), (char*) bytData, frameLen);
		ZF_LOGI("%s", Msg);
	}
	if (blnFrameDecodedOK) {
		snprintf(Msg, sizeof(Msg), "STATUS [RXO %02hhX] %s frame received OK.", bytSessionID, Name(bytFrameType));
	} else {
		snprintf(Msg, sizeof(Msg), "STATUS [RXO %02hhX] %s frame decode FAIL.", bytSessionID, Name(bytFrameType));
	}
	SendCommandToHost(Msg);

	if (blnFrameDecodedOK && IsDataFrame(bytFrameType)) {
		// Like FEC protocolmode, this facilitates decoding of successive unique
		// data frames of the same type.  Memory ARQ (using CarrierOk[] and
		// Averaged values) allows results from repeated copies of the same data
		// frame to also be combined to decode that frame even when no single
		// copy of it could be completely decoded by itself.  Unfortunately,
		// this approach may be impaired when a new data frame is received,
		// which has the same type as the prior frame (with different data), and
		// that prior frame was not successfully decoded.  For multi-carrier
		// frame types, this can also result in a data frame which claims to
		// have been successfully decoded, but which actually contains a mixture
		// of data from multiple different data frames.  While this possibility
		// is not completely eliminated, it is made less likely by resetting
		// Memory ARQ values when they become stale.  See comments where
		// MemarqTime is defined.  Those comments suggest that when using
		// RXO ProtocolMode, it may be advantageous to adjust the value of
		// ARQTimeout.

		// Like FEC protocolmode, the MemoryARQ values are reset whenever a data
		// frame is correctly decoded to facilitate decoding a sequence of data
		// frames of the same type but containing different data.
		if (blnFrameDecodedOK) {
			ZF_LOGD("Memory ARQ data reset after RXO data frame decoded OK.");
			ResetMemoryARQ();
		}
	}
}


/*
	The utf8_check() function scans the data of length slen starting
	at s. It returns a pointer to the first byte of the first malformed
	or overlong UTF-8 sequence found, or NULL if the string contains
	only correct UTF-8. It also spots UTF-8 sequences that could cause
	trouble if converted to UTF-16, namely surrogate characters
	(U+D800..U+DFFF) and non-Unicode positions (U+FFFE..U+FFFF). This
	routine is very likely to find a malformed sequence if the input
	uses any other encoding than UTF-8. It therefore can be used as a
	very effective heuristic for distinguishing between UTF-8 and other
	encodings.

	I wrote this code mainly as a specification of functionality; there
	are no doubt performance optimizations possible for certain CPUs.

	Markus Kuhn <http://www.cl.cam.ac.uk/~mgk25/> -- 2005-03-30
	License: http://www.cl.cam.ac.uk/~mgk25/short-license.html

	The above license URL indicates that the following code is licensed
	by Markus Kuhn under the users choice of multiple licenses including
	Apache, BSD, GPL, LGPL, MIT, and CC0.
*/

unsigned char *utf8_check(unsigned char *s, size_t slen)
{
	for (size_t i = 0; i < slen; i++) {
		if (*s < 0x80)
			// 0xxxxxxx
			s++;
		else if ((s[0] & 0xe0) == 0xc0) {
			// 110XXXXx 10xxxxxx
			if ((s[1] & 0xc0) != 0x80 || (s[0] & 0xfe) == 0xc0)  // overlong?
				return s;
			else
				s += 2;
		} else if ((s[0] & 0xf0) == 0xe0) {
			// 1110XXXX 10Xxxxxx 10xxxxxx
			if ((s[1] & 0xc0) != 0x80 ||
				(s[2] & 0xc0) != 0x80 ||
				(s[0] == 0xe0 && (s[1] & 0xe0) == 0x80) ||  // overlong?
				(s[0] == 0xed && (s[1] & 0xe0) == 0xa0) ||  // surrogate?
				(s[0] == 0xef && s[1] == 0xbf &&
				(s[2] & 0xfe) == 0xbe)  // U+FFFE or U+FFFF?
			)
				return s;
			else
				s += 3;
		} else if ((s[0] & 0xf8) == 0xf0) {
			// 11110XXX 10XXxxxx 10xxxxxx 10xxxxxx
			if ((s[1] & 0xc0) != 0x80 ||
				(s[2] & 0xc0) != 0x80 ||
				(s[3] & 0xc0) != 0x80 ||
				(s[0] == 0xf0 && (s[1] & 0xf0) == 0x80) ||  // overlong?
				(s[0] == 0xf4 && s[1] > 0x8f) || s[0] > 0xf4  // > U+10FFFF?
			)
				return s;
			else
				s += 4;
		} else
			return s;
	}
	return NULL;
}

