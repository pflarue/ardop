///////////////////////////////////////////////////////////////
// txframe() and its use by the TXFRAME Host command is
// intended for development and debugging.
// It is NOT intended for normal use by Host applications.
// It may be removed or modfied without notice in future
// versions of ardopcf.
///////////////////////////////////////////////////////////////

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "common/ARDOPC.h"

extern int intLastRcvdFrameQuality;  // defined in ARDOPC.c

extern UCHAR bytSessionID;  // defined in ARQ.c
extern BOOL blnEnbARQRpt;  // defined in ARQ.c
extern int intLeaderRcvdMs;  // defined and updated in SoundInput.c. ref in ARQ.c

int parse_params(char *paramstr, char *parsed[10]) {
	int paramcount = 1;
	parsed[paramcount - 1] = paramstr;
	for (;;) {
		if (parsed[paramcount - 1][0] == '"') {
			// param wrapped in double quotes
			parsed[paramcount] = strchr(parsed[paramcount - 1] + 1, '"');
			if (parsed[paramcount] == NULL)
				break;
			parsed[paramcount]++;
		} else {
			parsed[paramcount] = strchr(parsed[paramcount - 1], ' ');
			if (parsed[paramcount] == NULL)
				break;
		}
		*(parsed[paramcount])++ = 0x00;
		paramcount++;
	}
	// Allow TXFRAME to interpret /NONE/ as an empty string.  This can be
	// useful for diagnostic purposes.
	for (int i = 0; i < paramcount; i++) {
		if (strcmp(parsed[i], "/NONE/") == 0)
			parsed[i][0] = 0x00;
	}
	return paramcount;
}

// return 0 on success, 1 on failure
// len is the number of bytes to return which required
// 2*len hex digits.
int hex2int(char *ptr, unsigned int len, unsigned char *output) {
	unsigned char half;
	for (unsigned int i = 0; i < len; i++) {
		output[i] = 0;
		for (unsigned int j = 0; j < 2; j++) {
			half = ptr[2 * i + j];
			if (half < '0')
				return (1);
			else if (half <= '9')
				half = half - '0';
			else if (half < 'A' || half > 'f' || (half > 'F' && half < 'a'))
				return (1);
			else
				half = 0x09 + (half & 0x07);
			output[i] = (output[i] << 4) + half;
		}
	}
	return (0);
}

// return 0 on success, 1 on failure
int txframe(char * frameParams) {
	unsigned char sessionid;
	char * params[10];
	int paramcount = parse_params(frameParams, params);
	if (paramcount < 2)
		// no frame type
		return (1);

	blnEnbARQRpt = FALSE;
	// Any param equal to "_" means use the value of the corrsponding global
	// Any missing param are equivalent to "_"

	// For data frames, if the data parameter starts with "_", then random data
	// will be used.  "_" followed by a number allows the number of random bytes
	// to be specified, else random data sufficient to fill the frame will be used.

	// global LeaderLength will always be used.  However, this can be queried and
	// changed with the Host command "LEADER";

	// TODO: range check inputs
	if (strcmp(params[1], "DataNAK") == 0) {
		// TXFRAME DataNAK [quality] [sessionid]
		// 0x00 - 0x1F DataNAK
		int quality;
		// Uses globals: intLastRcvdFrameQuality, bytSessionID
		// quality from 0 to 100.  Use of strtol() allows hex if prefixed with 0x of 0X
		if (paramcount > 2 && strcmp(params[2], "_") != 0)
			quality = strtol(params[2], NULL, 0);
		else
			quality = intLastRcvdFrameQuality;
		// sessionid from 0 to 255.  Use of strtol() allows hex if prefixed with 0x of 0X
		if (paramcount > 3 && strcmp(params[3], "_") != 0)
			sessionid = strtol(params[3], NULL, 0);
		else
			sessionid = bytSessionID;
		ZF_LOGD("TXFRAME DataNAK %d 0x%02X", quality, sessionid);
		// from ARQ.c/ProcessRcvdARQFrame()
		if ((EncLen = EncodeDATANAK(quality, sessionid, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In txframe() DataNAK Invalid EncLen (%d).", EncLen);
			return 1;
		}
		Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
	} else if (strcmp(params[1], "BREAK") == 0) {
		// TXFRAME BREAK [sessionid]
		// 0x20 - 0x22 unused
		// 0x23 DataNAK
		// sessionid from 0 to 255.  Use of strtol() allows hex if prefixed with 0x of 0X
		if (paramcount > 2 && strcmp(params[2], "_") != 0)
			sessionid = strtol(params[2], NULL, 0);
		else
			sessionid = bytSessionID;
		ZF_LOGD("TXFRAME BREAK 0x%02X", sessionid);
		if ((EncLen = Encode4FSKControl(BREAK, sessionid, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In txframe() BREAK Invalid EncLen (%d).", EncLen);
			return 1;
		}
		Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
	} else if(strcmp(params[1], "IDLE") == 0) {
		// TXFRAME IDLE [sessionid]
		// 0x24 IDLE
		// Uses globals: bytSessionID
		// sessionid from 0 to 255.  Use of strtol() allows hex if prefixed with 0x of 0X
		if (paramcount > 2 && strcmp(params[2], "_") != 0)
			sessionid = strtol(params[2], NULL, 0);
		else
			sessionid = bytSessionID;
		ZF_LOGD("TXFRAME IDLE 0x%02X", sessionid);
		if ((EncLen = Encode4FSKControl(IDLEFRAME, sessionid, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In txframe() IDLE Invalid EncLen (%d).", EncLen);
			return 1;
		}
		Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
	} else if(strcmp(params[1], "DISC") == 0) {
		// TXFRAME DISC [sessionid]
		// 0x25 - 0x28 unused
		// 0x29 DISC
		// Uses globals: bytSessionID
		// sessionid from 0 to 255.  Use of strtol() allows hex if prefixed with 0x of 0X
		if (paramcount > 2 && strcmp(params[2], "_") != 0)
			sessionid = strtol(params[2], NULL, 0);
		else
			sessionid = bytSessionID;
		ZF_LOGD("TXFRAME DISC 0x%02X", sessionid);
		if ((EncLen = Encode4FSKControl(DISCFRAME, sessionid, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In txframe() DISC Invalid EncLen (%d).", EncLen);
			return 1;
		}
		Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
	} else if(strcmp(params[1], "END") == 0) {
		// TXFRAME END [sessionid]
		// 0x2A - 0x2B unused
		// 0x2C END
		// Uses globals: bytSessionID
		// sessionid from 0 to 255.  Use of strtol() allows hex if prefixed with 0x of 0X
		if (paramcount > 2 && strcmp(params[2], "_") != 0)
			sessionid = strtol(params[2], NULL, 0);
		else
			sessionid = bytSessionID;
		ZF_LOGD("TXFRAME END 0x%02X", sessionid);
		if ((EncLen = Encode4FSKControl(END, sessionid, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In txframe() END Invalid EncLen (%d).", EncLen);
			return 1;
		}
		Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
	} else if(strcmp(params[1], "ConRejBusy") == 0) {
		// TXFRAME ConRejBusy [sessioid]
		// 0x2D ConRejBusy
		// Uses globals: bytSessionID.  (Normally uses bytPendingSessionID)
		// sessionid from 0 to 255.  Use of strtol() allows hex if prefixed with 0x of 0X
		if (paramcount > 2 && strcmp(params[2], "_") != 0)
			sessionid = strtol(params[2], NULL, 0);
		else
			sessionid = bytSessionID;
		ZF_LOGD("TXFRAME ConRejBusy 0x%02X", sessionid);
		if ((EncLen = Encode4FSKControl(ConRejBusy, sessionid, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In txframe() ConRejBusy Invalid EncLen (%d).", EncLen);
			return 1;
		}
		Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
	} else if(strcmp(params[1], "ConRejBW") == 0) {
		// TXFRAME ConRejBW [sessionid]
		// 0x2E ConRejBW
		// Uses globals: bytSessionID.  (Normally uses bytPendingSessionID)
		// sessionid from 0 to 255.  Use of strtol() allows hex if prefixed with 0x of 0X
		if (paramcount > 2 && strcmp(params[2], "_") != 0)
			sessionid = strtol(params[2], NULL, 0);
		else
			sessionid = bytSessionID;
		ZF_LOGD("TXFRAME ConRejBW 0x%02X", sessionid);
		if ((EncLen = Encode4FSKControl(ConRejBW, sessionid, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In txframe() ConRejBW Invalid EncLen (%d).", EncLen);
			return 1;
		}
		Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
	} else if(strcmp(params[1], "IDFrame") == 0) {
		// TXFRAME IDFrame [callsign] [gridsquare]
		// 0x2F unused
		// 0x30 IDFrame
		// Uses globals: Callsign, GridSquare
		char callsign[CALL_BUF_SIZE];
		char gridsquare[9];
		if (paramcount > 2 && strcmp(params[2], "_") != 0) {
			if (!CheckValidCallsignSyntax(params[2])) {
				ZF_LOGW(
					"Invalid callsign, '%s', for TXFRAME IDFrame.",
					params[2]);
				return(1);
			}
			strncpy(callsign, params[2], CALL_BUF_SIZE);
		} else if (Callsign[0] != 0x00)
			strncpy(callsign, Callsign, CALL_BUF_SIZE);
		else {
			ZF_LOGW(
				"TXFRAME IDFrame requires an implicit or explicit callsign.");
			return (1);
		}
		if (paramcount > 3 && strcmp(params[3], "_") != 0)
			// TODO: check whether gridsquare is valid?
			strncpy(gridsquare, params[3], 8);
		else if (GridSquare[0] != 0x00)
			strncpy(gridsquare, GridSquare, 8);
		else {
			ZF_LOGW(
				"TXFRAME IDFrame requires an implicit or explicit gridsquare.");
			return (1);
		}
		gridsquare[8] = 0x00;  // ensure NULL terminated
		ZF_LOGD("TXFRAME IDFrame %s %s", callsign, gridsquare);
		if ((EncLen = Encode4FSKIDFrame(callsign, gridsquare, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In txframe() IDFrame Invalid EncLen (%d).", EncLen);
			return 1;
		}
		Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
	} else if(strncmp(params[1], "ConReq", 6) == 0) {
		// TXFRAME ConReq targetcallsign [mycallsign] [bandwidth]
		// TXFRAME ConReqXXXX targetcallsign [mycallsign]
		// 0x31 to 0x38
		// All have unique names indicating bandwidth.  If only 'ConReq' is given,
		// accept a parameter in the form used for ARQBW, else default to value set
		// with ARQBW Host Command
		// Uses globals: Callsign. CallBandwidth, ARQBandwidth
		char targetcallsign[CALL_BUF_SIZE];
		char callsign[CALL_BUF_SIZE];
		int bandwidth_num = -1;  // This is an enum value, not an actual bandwith value
		if (paramcount > 2 && strcmp(params[2], "_") != 0) {
			if (!CheckValidCallsignSyntax(params[2])) {
				ZF_LOGW(
					"Invalid targetcallsign, '%s', for TXFRAME ConReq.",
					params[2]);
				return(1);
			}
			strncpy(targetcallsign, params[2], CALL_BUF_SIZE);
		} else {
			ZF_LOGW(
				"TXFRAME ConReq requires an explicit targetcallsign.");
			return (1);
		}
		if (paramcount > 3 && strcmp(params[3], "_") != 0) {
			if (!CheckValidCallsignSyntax(params[3])) {
				ZF_LOGW(
					"Invalid callsign, '%s', for TXFRAME ConReq.",
					params[3]);
				return(1);
			}
			strncpy(callsign, params[3], CALL_BUF_SIZE);
		} else if (Callsign[0] != 0x00)
			strncpy(callsign, Callsign, CALL_BUF_SIZE);
		else {
			ZF_LOGW(
				"TXFRAME ConReq requires an implicit or explicit callsign.");
			return (1);
		}
		if (strlen(params[1]) > 6) {
			// Notice that the order here corresponds to ARQBandwidths defined in
			// ARQ.c, rather than the order of the ConReq frames by frame type.
			char bandwidths[8][6] = {"200F", "500F", "1000F", "2000F", "200M", "500M", "1000M", "2000M"};
			for (int i = 0; i < 8; i++) {
				if (strcmp(params[1] + 6, bandwidths[i]) == 0) {
					bandwidth_num = i;
					break;
				}
			}
			if (bandwidth_num == -1) {
				ZF_LOGW(
					"TXFRAME ConReq: invalid bandwidth indicator. '%s'.", params[1]);
				return (1);
			}
		} else if (paramcount > 4 && strcmp(params[4], "_") != 0) {
			// processing of bandwidth is based on processing of Host Command ARQBW
			int i;
			for (i = 0; i < UNDEFINED; i++) {
				if (strcmp(params[4], ARQBandwidths[i]) == 0)
					break;
			}
			if (i == 8) {
				ZF_LOGW(
					"TXFRAME ConReq: invalid bandwidth='%s'.", params[4]);
				return (1);
			} else
				bandwidth_num = i;
		} else if (CallBandwidth != UNDEFINED)
			bandwidth_num = CallBandwidth;
		else if (ARQBandwidth != UNDEFINED)
			// Fallback if CallBandwidth is not set
			bandwidth_num = ARQBandwidth;
		else {
			// ARQBandwidth shouldn't ever be UNDEFINED, but handle this just in case
			ZF_LOGW(
				"TXFRAME ConReq requires an implicit or explicit bandwidth.");
			return (1);
		}
		ZF_LOGD("TXFRAME ConReq%s %s %s", ARQBandwidths[bandwidth_num], targetcallsign, callsign);
		if ((EncLen = EncodeARQConRequest(callsign, targetcallsign, bandwidth_num, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In txframe() ConReq Invalid EncLen (%d).", EncLen);
			return 1;
		}
		Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
	} else if(strncmp(params[1], "ConAck", 6) == 0) {
		// TXFRAME ConAck [leaderlen] [sessionid] [bandwidth]
		// TXFRAME ConAckXXX [leaderlen] [sessionid]
		// 0x39 to 0x3C
		// All have unique names indicating bandwidth.  If only 'ConAck' is given, accept a
		// number as a parameter since, unlike ConReq, ConAck does not distinguish between
		// MAX and FORCED.  However, the number given must be one of 200, 500, 1000, or 2000.
		// If none of these are provided, use the numerical portion of ARQBandwidth.
		// Uses globals: intLeaderRcvdMs, bytSessionID, ARQBandwidth
		// bandwidth from the set [200, 500, 1000, 2000].  Use of strtol() allows hex if
		// prefixed with 0x of 0X (though this is unlikely to be useful here).
		int frametype;
		int rcvdleaderlen;
		int bandwidth;  // Bandwidth in Hz [200, 500, 1000, 2000]
		// Received leader length in MS.  Default to intLeaderRcvdMs if not provided.
		// Use of strtol() allows hex if prefixed with 0x of 0X.
		if (paramcount > 2 && strcmp(params[2], "_") != 0)
			rcvdleaderlen = strtol(params[2], NULL, 0);
		else
			rcvdleaderlen = intLeaderRcvdMs;
		// sessionid from 0 to 255.  Use of strtol() allows hex if prefixed with 0x of 0X
		if (paramcount > 3 && strcmp(params[3], "_") != 0)
			sessionid = strtol(params[3], NULL, 0);
		else
			sessionid = bytSessionID;
		if (strlen(params[1]) > 6)
			// Notice that the order here corresponds to ARQBandwidths defined in
			// ARQ.c, rather than the order of the ConReq frames by frame type.
			bandwidth = atoi(params[1] + 6);
		else if (paramcount > 4 && strcmp(params[4], "_") != 0)
			bandwidth = strtol(params[4], NULL, 0);
		else if (ARQBandwidth != UNDEFINED)
			bandwidth = strtol(ARQBandwidths[ARQBandwidth], NULL, 0);
		if (bandwidth == 200)
			frametype = 0x39;
		else if (bandwidth == 500)
			frametype = 0x3A;
		else if (bandwidth == 1000)
			frametype = 0x3B;
		else if (bandwidth == 2000)
			frametype = 0x3C;
		else {
			ZF_LOGW(
				"TXFRAME ConAck requires an implicit or explicit bandwidth of 200, 500, 1000, or 2000");
			return (1);
		}
		ZF_LOGD("TXFRAME ConAck %d %d %02X (frame type = %02X)", bandwidth, rcvdleaderlen, sessionid, frametype);
		if ((EncLen = EncodeConACKwTiming(frametype, rcvdleaderlen, sessionid, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In txframe() ConAck Invalid EncLen (%d).", EncLen);
			return 1;
		}
		Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
	} else if(strcmp(params[1], "PingAck") == 0) {
		// TXFRAME PingAck [snr] [quality]
		// 0x3D
		// Uses globals: stcLastPingintRcvdSN, stcLastPingintQuality
		// SN ratio of received Ping in range of 0-21. If >= 21, result truncated.
		// Default to stcLastPingintRcvdSN if not provided.
		// Use of strtol() allows hex if prefixed with 0x of 0X.
		int snr;
		int quality;
		if (paramcount > 2 && strcmp(params[2], "_") != 0)
			// snr will be truncated to 21 for any value greater than 20
			snr = strtol(params[2], NULL, 0);
		else
			snr = stcLastPingintRcvdSN;
		// Quality in range of 30-100.  Default to stcLastPingintQuality if not provided.
		// Use of strtol() allows hex if prefixed with 0x of 0X.
		if (paramcount > 3 && strcmp(params[3], "_") != 0) {
			quality = strtol(params[3], NULL, 0);
			if (quality < 30 || quality > 1000) {
				ZF_LOGW("TXFRAME PingAck requires 30 <= quality <= 100, but %d was provided.", quality);
				return (1);
			}
		} else
			quality = stcLastPingintQuality;
		ZF_LOGD("TXFRAME PingAck %d %d.", snr, quality);
		if ((EncLen = EncodePingAck(PINGACK, snr, quality, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In txframe() PingAck Invalid EncLen (%d).", EncLen);
			return 1;
		}
		Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
	} else if(strcmp(params[1], "Ping") == 0) {
		// TXFRAME Ping targetcallsign [mycallsign]
		// 0x3$
		// Uses globals: Callsign
		char targetcallsign[CALL_BUF_SIZE];
		char callsign[CALL_BUF_SIZE];
		if (paramcount > 2 && strcmp(params[2], "_") != 0) {
			if (!CheckValidCallsignSyntax(params[2])) {
				ZF_LOGW(
					"Invalid targetcallsign, '%s', for TXFRAME Ping.",
					params[2]);
				return(1);
			}
			strncpy(targetcallsign, params[2], CALL_BUF_SIZE);
		} else {
			ZF_LOGW(
				"TXFRAME Ping requires an explicit targetcallsign.");
			return (1);
		}
		if (paramcount > 3 && strcmp(params[3], "_") != 0) {
			if (!CheckValidCallsignSyntax(params[3])) {
				ZF_LOGW(
					"Invalid callsign, '%s', for TXFRAME IDFrame.",
					params[3]);
				return(1);
			}
			strncpy(callsign, params[3], CALL_BUF_SIZE);
		} else if (Callsign[0] != 0x00)
			strncpy(callsign, Callsign, CALL_BUF_SIZE);
		else {
			ZF_LOGW(
				"TXFRAME Ping requires an implicit or explicit callsign.");
			return (1);
		}
		ZF_LOGD("TXFRAME Ping %s %s", targetcallsign, callsign);
		if ((EncLen = EncodePing(callsign, targetcallsign, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In txframe() Ping Invalid EncLen (%d).", EncLen);
			return 1;
		}
		Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
	} else if (strcmp(params[1], "DataACK") == 0) {
		// TXFRAME DataACK [quality] [sessionid]
		// 0xE0 - 0xFF DataACK
		int quality;
		// Uses globals: intLastRcvdFrameQuality, bytSessionID
		// quality from 0 to 100.  Use of strtol() allows hex if prefixed with 0x of 0X
		if (paramcount > 2 && strcmp(params[2], "_") != 0)
			quality = strtol(params[2], NULL, 0);
		else
			quality = intLastRcvdFrameQuality;
		// sessionid from 0 to 255.  Use of strtol() allows hex if prefixed with 0x of 0X
		if (paramcount > 3 && strcmp(params[3], "_") != 0)
			sessionid = strtol(params[3], NULL, 0);
		else
			sessionid = bytSessionID;
		ZF_LOGD("TXFRAME DataACK %d 0x%02X", quality, sessionid);
		if ((EncLen = EncodeDATAACK(quality, sessionid, bytEncodedBytes)) <= 0) {
			ZF_LOGE("ERROR: In txframe() DataACK Invalid EncLen (%d).", EncLen);
			return 1;
		}
		Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
	} else {
		// TXFRAME MOD.BW.BAUD.E/O "text data" [sessionid]
		// TXFRAME MOD.BW.BAUD.E/O hexdigits [sessionid]
		// TXFRAME MOD.BW.BAUD.E/O [_] [sessionid]
		// TXFRAME MOD.BW.BAUD.E/O _bytecount [sessionid]
		// Anything else should be a data frame.
		int frametype;
		unsigned char sessionid_bak;
		for (frametype = 0x40; frametype < 0x7E; frametype ++ ) {
			if (strFrameType[frametype][0] == 0x00
				|| strcmp(params[1], strFrameType[frametype]) != 0
			)
				continue;
			unsigned char dummyuchar;
			int dummyint;
			int numcar;
			char modulation[6];
			int datalen;
			char frname[64];
			unsigned int maxlen;
			unsigned char data[1024];
			char debugmsg[2100];
			if (!FrameInfo(frametype, &dummyint, &numcar, modulation, &dummyint, &datalen, &dummyint, &dummyuchar, frname)) {
				ZF_LOGW("TXFRAME %s (FrameInfo) Unknown frame type.", params[1]);
				return (1);
			}
			maxlen = datalen * numcar;
			if (maxlen > 1024) {
				ZF_LOGW("TXFRAME Error.  Unexpectedly high maxlen = %d.", maxlen);
				return (1);
			}
			if (paramcount > 2 && params[2][0] != '_') {
				if (params[2][0] == '"' && params[2][strlen(params[2]) - 1] == '"') {
					// If params[2] starts and ends with double quotes, interpret everying
					// between those quotes as text to send.
					if (strlen(params[2]) - 2 <= maxlen)
						datalen = strlen(params[2]) - 2;
					else {
						ZF_LOGW(
							"TXFRAME %s is discarding %lu bytes since only %u can be sent.",
							params[1], strlen(params[2]) - 2 - maxlen, maxlen);
						datalen = maxlen;
					}
					memcpy(data, params[2] + 1, datalen);
				} else {
					// params[2] should be an even number of hexidecimal digits with no spaces
					if (strlen(params[2]) / 2 <= maxlen)
						datalen = strlen(params[2]) / 2;
					else {
						ZF_LOGW(
							"TXFRAME %s is discarding %lu bytes since only %u can be sent.",
							params[1], strlen(params[2]) / 2 - maxlen, maxlen);
						datalen = maxlen;
					}
					if (hex2int(params[2], datalen, data) == 1) {
						ZF_LOGW("TXFRAME %s error parsing hex data.", params[1]);
						return (1);
					}
				}
			} else if (paramcount > 2 && params[2][0] == '_' && strlen(params[2]) > 1) {
				// "_" followed by a number allows the number of random bytes to be specified.
				// Use of strtol() allows hex if prefixed with 0x of 0X
				datalen = strtol(params[2] + 1, NULL, 0);
				if (datalen > (int)maxlen) {
					ZF_LOGD("TXFRAME requested %d random bytes, but only %d are allowed for %s.", datalen, maxlen, params[1]);
					datalen = maxlen;
				}
				for (int i = 0; i < datalen; i++)
					data[i] = rand() & 0xFF;
			} else {
				// use maxlen random 8-bit bytes
				datalen = maxlen;
				for (unsigned int i = 0; i < maxlen; i++)
					data[i] = rand() & 0xFF;
			}
			if (paramcount > 3 && strcmp(params[3], "_") != 0)
				sessionid = strtol(params[3], NULL, 0);
			else
				sessionid = bytSessionID;
			snprintf(debugmsg, sizeof(debugmsg), "TXFRAME %s with %d bytes of data using sessionid=%02X: ", params[1], datalen, sessionid);
			for (int i = 0; i < datalen; i++)
				snprintf(debugmsg + strlen(debugmsg), sizeof(debugmsg) - strlen(debugmsg), " %02X", data[i]);
			ZF_LOGD("%s", debugmsg);
			// EncodeFSKData() and EncodePSKData() use the global variable bytSessionID.
			// So, to use a different value, the bytSessionID will be changed, and then
			// after it is used by one of these functions, it will be restored.
			sessionid_bak = bytSessionID;
			if (sessionid != bytSessionID)
				bytSessionID = sessionid;
			if (strcmp(modulation, "4FSK") == 0) {
				if ((EncLen = EncodeFSKData(frametype, data, datalen, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In txframe() 4FSK Invalid EncLen (%d).", EncLen);
					return 1;
				}
				if (frametype >= 0x7A && frametype <= 0x7D)
					Mod4FSK600BdDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
				else
					Mod4FSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
			} else if (strcmp(modulation, "4PSK") == 0 || strcmp(modulation, "8PSK") == 0) {
				if ((EncLen = EncodePSKData(frametype, data, datalen, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In txframe() 4PSK Invalid EncLen (%d).", EncLen);
					return 1;
				}
				ModPSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
			} else if (strcmp(modulation, "16QAM") == 0) {
				if ((EncLen = EncodePSKData(frametype, data, datalen, bytEncodedBytes)) <= 0) {
					ZF_LOGE("ERROR: In txframe() 16QAM Invalid EncLen (%d).", EncLen);
					return 1;
				}
				ModPSKDataAndPlay(bytEncodedBytes[0], &bytEncodedBytes[0], EncLen, LeaderLength);
			} else {
				bytSessionID = sessionid_bak;
				ZF_LOGW("TXFRAME: Unexpected modulation='%s' for frame type=%s", modulation, params[1]);
				return (1);
			}
			bytSessionID = sessionid_bak;
			return (0);
		}
		if (frametype == 0x7E) {
			ZF_LOGW("TXFRAME: Unknown frame type=%s", params[1]);
			return (1);
		}
	}
	return (0);
}
