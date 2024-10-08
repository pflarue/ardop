//	ARDOP Modem Decode Sound Samples

#include "common/ARDOPC.h"

extern BOOL blnAbort;

int intFECFramesSent;
int FECRepeatsSent;

UCHAR bytFrameType;
BOOL blnSendIDFrame;
extern BOOL NeedID;  // SENDID Command Flag
extern int intRepeatCount;

extern int intLastFrameIDToHost;
int bytFailedDataLength;
extern int intLastFailedFrameID;
int crcLastFECDataPassedToHost;

UCHAR bytFailedData[1600];  // do we really need that much ????

extern int intNumCar;
extern int intBaud;
extern int intDataLen;
extern int intRSLen;
extern int intSampleLen;
extern int intDataPtr;
extern int intSampPerSym;
extern int intDataBytesPerCar;
extern BOOL blnOdd;
extern char strType[18];
extern char strMod[16];
extern UCHAR bytMinQualThresh;
extern unsigned int LastIDFrameTime;

UCHAR bytLastFECDataFrameSent;

char strCurrentFrameFilename[16];

extern int intCalcLeader;  // the computed leader to use based on the reported Leader Length

void ResetMemoryARQ();

// Function to start sending FEC data

BOOL StartFEC(UCHAR * bytData, int Len, char * strDataMode, int intRepeats, BOOL blnSendID)
{
	// Return True if OK false if problem

	BOOL blnModeOK = FALSE;
	int i;

	FECRepeats = intRepeats;

	if (ProtocolState == FECSend)  // If already sending FEC data simply add to the OB queue
	{
		AddDataToDataToSend(bytData, Len);  // add new data to queue

		ZF_LOGD("[ARDOPprotocol.StartFEC] %d bytes received while in FECSend state...append to data to send.", Len);
		return TRUE;
	}

	// Check to see that there is data in the buffer.

	if (Len == 0 && bytDataToSendLength == 0)
	{
		ZF_LOGD("[ARDOPprotocol.StartFEC] No data to send!");
		return FALSE;
	}

	// Check intRepeates

	if (intRepeats < 0 || intRepeats > 5)
	{
		// Logs.Exception("[ARDOPprotocol.StartFEC] Repeats out of range: " & intRepeats.ToString)
		return FALSE;
	}

	// check call sign

	if (!stationid_ok(&Callsign))
	{
		// Logs.Exception("[ARDOPprotocol.StartFEC] Invalid call sign: " & MCB.Callsign)
		return FALSE;
	}

	// Check to see that strDataMode is correct

	for (i = 0;  i < strAllDataModesLen; i++)
	{
		if (strcmp(strDataMode, strAllDataModes[i]) == 0)
		{
			if (strcmp(strFECMode, strDataMode) != 0)
				// This conditional avoids an undefined strcpy-param-overlap
				// error that would otherwise occur when StartFEC() is called
				// with strDataMode=strFECMode.
				strcpy(strFECMode, strDataMode);
			blnModeOK = TRUE;
			break;
		}
	}

	if (blnModeOK == FALSE)
	{
		// Logs.Exception("[ARDOPprotocol.StartFEC] Illegal FEC mode: " & strDataMode)
		return FALSE;
	}

	// While objMain.SoundIsPlaying
	//  Thread.Sleep(100)
	// End While

	blnAbort = FALSE;

	intFrameRepeatInterval = 400;  // should be a safe number for FEC...perhaps could be shortened down to 200 -300 ms

	AddDataToDataToSend(bytData, Len);  // add new data to queue

	SetARDOPProtocolState(FECSend);  // set to FECSend state

	blnSendIDFrame = blnSendID;

	if (blnSendID)
	{
		NeedID = TRUE;
	}
	else
	{
		// Cant we just call GetNextFECFrame??

//		GetNextFECFrame();  // Use timer to start so cmd response is immediate
/*
		Dim bytFrameData(-1) As Byte
		strFrameComponents = strFECMode.Split(".")
		bytFrameType = objFrameInfo.FrameCode(strFECMode & ".E")
		If bytFrameType = bytLastFECDataFrameSent Then  // Added 0.3.4.1
			bytFrameType = bytFrameType Xor &H1  // insures a new start is on a different frame type.
		End If
		objFrameInfo.FrameInfo(bytFrameType, blnOdd, intNumCar, strMod, intBaud, intDataLen, intRSLen, bytQualThres, strType)
		GetDataFromQueue(bytFrameData, intDataLen * intNumCar)
		// If bytFrameData.Length < (intDataLen * intNumCar) Then ReDim Preserve bytFrameData((intDataLen * intNumCar) - 1)
		// Logs.WriteDebug("[ARDOPprotocol.StartFEC]  Frame Data (string) = " & GetString(bytFrameData))
		If strMod = "4FSK" Then
			bytFrameData = objMain.objMod.EncodeFSKData(bytFrameType, bytFrameData, strCurrentFrameFilename)
			intCurrentFrameSamples = objMain.objMod.Mod4FSKData(bytFrameType, bytFrameData)
		Else
			bytFrameData = objMain.objMod.EncodePSK(bytFrameType, bytFrameData, strCurrentFrameFilename)
			intCurrentFrameSamples = objMain.objMod.ModPSK(bytFrameType, bytFrameData)
		End If
		bytLastFECDataFrameSent = bytFrameType
		objMain.SendFrame(intCurrentFrameSamples, strCurrentFrameFilename)
		intFECFramesSent = 1
*/
	}
	return TRUE;
}

// Function to get the next FEC data frame

BOOL GetNextFECFrame()
{
	int Len;
	int intNumCar, intBaud, intDataLen, intRSLen;
	BOOL blnOdd;
	char strType[18] = "";
	char strMod[16] = "";

	if (blnAbort)
	{
		ClearDataToSend();

		ZF_LOGD("[GetNextFECFrame] FECAbort. Going to DISC state");
		KeyPTT(FALSE);  // insurance for PTT off
		SetARDOPProtocolState(DISC);
		blnAbort = FALSE;
		return FALSE;
	}

	if (intFECFramesSent == -1)
	{
		ZF_LOGD("[GetNextFECFrame] intFECFramesSent = -1.  Going to DISC state");

		SetARDOPProtocolState(DISC);
		KeyPTT(FALSE);  // insurance for PTT off
		return FALSE;
	}

	if (bytDataToSendLength == 0 && FECRepeatsSent >= FECRepeats && ProtocolState == FECSend)
	{
		ZF_LOGD("[GetNextFECFrame] All data and repeats sent.  Going to DISC state");

		SetARDOPProtocolState(DISC);
		blnEnbARQRpt = FALSE;
		KeyPTT(FALSE);  // insurance for PTT off

		return FALSE;
	}

	if (ProtocolState == DISC && intPINGRepeats > 0)
	{
		intRepeatCount++;
		if (intRepeatCount <= intPINGRepeats && blnPINGrepeating)
		{
			dttLastPINGSent = Now;
			return TRUE;  // continue PING
		}

		intPINGRepeats = 0;
		blnPINGrepeating = False;
		return FALSE;
	}


	if (ProtocolState != FECSend)
		return FALSE;

	if (intFECFramesSent == 0)
	{
		// Initialize the first FEC Data frame (even) from the queue and compute the Filtered samples and filename

		char FullType[18];

		strcpy(FullType, strFECMode);
		strcat(FullType, ".E");

		bytFrameType = FrameCode(FullType);

//		If bytFrameType = bytLastFECDataFrameSent Then  // Added 0.3.4.1
//			bytFrameType = bytFrameType Xor 0x1  // insures a new start is on a different frame type.
//		End If


		FrameInfo(bytFrameType, &blnOdd, &intNumCar, strMod, &intBaud, &intDataLen, &intRSLen, &bytMinQualThresh, strType);

		Len = intDataLen * intNumCar;

		if (Len > bytDataToSendLength)
			Len = bytDataToSendLength;

		bytLastFECDataFrameSent = bytFrameType;

sendit:
		if (FECRepeats)
			blnEnbARQRpt = TRUE;
		else
			blnEnbARQRpt = FALSE;

		intFrameRepeatInterval = 400;  // should be a safe number for FEC...perhaps could be shortened down to 200 -300 ms

		FECRepeatsSent = 0;

		intFECFramesSent += 1;

		bytFrameType = bytLastFECDataFrameSent;

		if (strcmp(strMod, "4FSK") == 0)
		{
			if ((EncLen = EncodeFSKData(bytFrameType, bytDataToSend, Len, bytEncodedBytes)) <= 0) {
				ZF_LOGE("ERROR: In GetNextFECFrame() 4FSK Invalid EncLen (%d).", EncLen);
				return FALSE;
			}
			RemoveDataFromQueue(Len);  // No ACKS in FEC

			if (bytFrameType >= 0x7A && bytFrameType <= 0x7D)
				Mod4FSK600BdDataAndPlay(bytEncodedBytes[0], bytEncodedBytes, EncLen, intCalcLeader);  // Modulate Data frame
			else
				Mod4FSKDataAndPlay(bytEncodedBytes[0], bytEncodedBytes, EncLen, intCalcLeader);  // Modulate Data frame
		}
		else  // This handles PSK and QAM
		{
			if ((EncLen = EncodePSKData(bytFrameType, bytDataToSend, Len, bytEncodedBytes)) <= 0) {
				ZF_LOGE("ERROR: In GetNextFECFrame() PSK and QAM Invalid EncLen (%d).", EncLen);
				return FALSE;
			}
			RemoveDataFromQueue(Len);  // No ACKS in FEC
			ModPSKDataAndPlay(bytEncodedBytes[0], bytEncodedBytes, EncLen, intCalcLeader);  // Modulate Data frame
		}
		return TRUE;
	}

	// Not First

	if (FECRepeatsSent >= FECRepeats)
	{
		// Send New Data

		// Need to add pause

		txSleep(400);

		// While sending FEC data, only send an IDFrame before sending new data
		// so as not to disrupt repeated frames.  Because some delay is
		// possible, begin trying to send an IDFrame every 9 minutes rather than
		// waiting for a full 10 minutes.
		if(LastIDFrameTime != 0 && Now - LastIDFrameTime > 540000) {  // more than 9 minutes elapsed
			SendID(NULL, "FECSend 10 minute ID");
			return FALSE; // Don't repeat
		}

		FrameInfo(bytLastFECDataFrameSent, &blnOdd, &intNumCar, strMod, &intBaud, &intDataLen, &intRSLen, &bytMinQualThresh, strType);

		Len = intDataLen * intNumCar;

		if (Len > bytDataToSendLength)
			Len = bytDataToSendLength;

		bytLastFECDataFrameSent = bytLastFECDataFrameSent ^ 1;
		goto sendit;
	}

	// just a repeat of the last frame so no changes to samples...just inc frames Sent

	FECRepeatsSent++;
	intFECFramesSent++;

	return TRUE;
}

extern int frameLen;

// If failed FEC data exists, pass it to the host tagged as ERR
// In addition to its use in ProcessRcvdFECDataFrame(), this function is also
// called by CheckMemarqTimeout() when bytFailedData has become stale.
void PassFECErrDataToHost() {
	if (bytFailedDataLength > 0) {
		AddTagToDataAndSendToHost(bytFailedData, "ERR", bytFailedDataLength);
		if (CommandTrace)
			ZF_LOGI("[ARDOPprotocol.ProcessRcvdFECDataFrame] Pass failed frame ID %s to Host (%d bytes)", Name(intLastFailedFrameID), bytFailedDataLength);
		bytFailedDataLength = 0;
		intLastFailedFrameID = -1;
	}
}

// Function to process Received FEC data

void ProcessRcvdFECDataFrame(int intFrameType, UCHAR * bytData, BOOL blnFrameDecodedOK)
{
	// Unlike ARQ protocolmode, this facilitates decoding of successive unique
	// data frames of the same type.  Memory ARQ (using CarrierOk[] and Averaged
	// values) allows results from repeated copies of the same data frame to
	// also be combined to decode that frame even when no single copy of it
	// could be completely decoded by itself.  Unfortunately, this approach may
	// be impaired when a new data frame is received, which has the same type as
	// the prior frame (with different data), and that prior frame was not
	// successfully decoded.  For multi-carrier frame types, this can also
	// result in a data frame which claims to have been successfully decoded,
	// but which actually contains a mixture of data from multiple different
	// data frames.  While this possibility is not completely eliminated, it is
	// made less likely by resetting Memory ARQ values when they become stale.
	// See comments where MemarqTime is defined.

	// Determine if this frame should be passed to Host.

	if (blnFrameDecodedOK)
	{
		// Resetting Memory ARQ values whenever an FEC frame is correctly decoded
		// ensures that the next data frame received will always be decoded and
		// passed to this function rather than discarded as an assumed repeated
		// frame.  The logic which follows this will evaluate whether a frame is
		// actaully a repeat of the last frame received, and handle it
		// appropriately.
		ZF_LOGD("Memory ARQ data reset after FEC data frame decoded OK.");
		ResetMemoryARQ();

		int CRC = GenCRC16(bytData, frameLen);

		if (intFrameType == intLastFrameIDToHost && CRC == crcLastFECDataPassedToHost)
		{
			if (CommandTrace)
				ZF_LOGI("[ARDOPprotocol.ProcessRcvdFECDataFrame] Same Frame ID: %s and matching data, not passed to Host", Name(intFrameType));
			return;
		}

		if (intLastFailedFrameID != intFrameType)
			PassFECErrDataToHost();

		AddTagToDataAndSendToHost(bytData, "FEC", frameLen);
		if (CommandTrace)
			ZF_LOGI("[ARDOPprotocol.ProcessRcvdFECDataFrame] Pass good data frame  ID %s to Host (%d bytes)", Name(intFrameType), frameLen);

		crcLastFECDataPassedToHost = CRC;
		intLastFrameIDToHost = intFrameType;
		bytFailedDataLength = 0;
		intLastFailedFrameID = -1;
	}
	else
	{
		// Bad Decode

		if (intLastFailedFrameID != intFrameType)
			PassFECErrDataToHost();

		memcpy(bytFailedData, bytData, frameLen);  // capture the current data and frame type
		bytFailedDataLength = frameLen;
		intLastFailedFrameID = intFrameType;
	}
}
