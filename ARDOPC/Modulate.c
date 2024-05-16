//	Sample Creation routines (encode and filter) for ARDOP Modem

#ifdef WIN32
#define _CRT_SECURE_NO_DEPRECATE
#include <windows.h>
#endif

#include "ARDOPC.h"
#include "../ARDOPCommonCode/wav.h"

unsigned int pttOnTime;

#pragma warning(disable : 4244)		// Code does lots of  float to int

FILE * fp1;

#define MAX(x, y) ((x) > (y) ? (x) : (y))

// Function to generate the Two-tone leader and Frame Sync (used in all frame types) 

extern short Dummy;
extern int DriveLevel;
extern BOOL WriteTxWav;

int wg_send_txframet(int cnum, const char *frame);

// writing unfiltered tx audio to WAV disabled
// extern struct WavFile *txwfu;

int intSoftClipCnt = 0;

void StartTxWav();
void Flush();

void GetTwoToneLeaderWithSync(int intSymLen)
{
	// Generate a 50 baud (20 ms symbol time) 2 tone leader 
    // leader tones used are 1475 and 1525 Hz.  
  
	int intSign = 1;
	int i, j;
	short intSample;

    if ((intSymLen & 1) == 1) 
		intSign = -1;

	for (i = 0; i < intSymLen; i++)   //for the number of symbols needed (two symbols less than total leader length) 
	{
		for (j = 0; j < 240; j++)	// for 240 samples per symbol (50 baud) 
		{
           if (i != (intSymLen - 1)) 
			   intSample = intSign * int50BaudTwoToneLeaderTemplate[j];
		   else
			   intSample = -intSign * int50BaudTwoToneLeaderTemplate[j];
   
		   SampleSink(intSample);
		}
		intSign = -intSign;
	}
}

void SendLeaderAndSYNC(UCHAR * bytEncodedBytes, int intLeaderLen)
{
	int intMask = 0;
	int intLeaderLenMS;
	int j, k, n;
	UCHAR bytMask;
	UCHAR bytSymToSend;
	short intSample;
	char DebugMess[256];
	if (intLeaderLen == 0)
		intLeaderLenMS = LeaderLength;
	else
		intLeaderLenMS = intLeaderLen;

 	// Create the leader

	GetTwoToneLeaderWithSync(intLeaderLenMS / 20);
		       
	//Create the 8 symbols (16 bit) 50 baud 4FSK frame type with Implied SessionID
	// No reference needed for 4FSK

	// note revised To accomodate 1 parity symbol per byte (10 symbols total)

	sprintf(DebugMess, "LeaderAndSYNC tones : ");
	for(j = 0; j < 2; j++)		 // for the 2 bytes of the frame type
	{              
		bytMask = 0xc0;
		
		for(k = 0; k < 5; k++)	 // for 5 symbols per byte (4 data + 1 parity)
		{
			if (k < 4)
				bytSymToSend = (bytMask & bytEncodedBytes[j]) >> (2 * (3 - k));
			else
				bytSymToSend = ComputeTypeParity(bytEncodedBytes[0]);
			sprintf(DebugMess + strlen(DebugMess), "%d", bytSymToSend);

			for(n = 0; n < 240; n++)
			{
				if (((5 * j + k) & 1 ) == 0)
					intSample = intFSK50bdCarTemplate[bytSymToSend][n];
				else
					intSample = -intFSK50bdCarTemplate[bytSymToSend][n]; // -sign insures no phase discontinuity at symbol boundaries

				SampleSink(intSample);	
			}
			bytMask = bytMask >> 2;
		}
	}
	// Include these tone values in debug log only if FileLogLevel is LOGDEBUGPLUS
	WriteDebugLog(LOGDEBUGPLUS, "%s", DebugMess);
}


void Mod4FSKDataAndPlay(int Type, unsigned char * bytEncodedBytes, int Len, int intLeaderLen)
{
	// Function to Modulate data encoded for 4FSK, create
	// the 16 bit samples and send to sound interface

	// Function works for 1, 2 or 4 simultaneous carriers 

	int intNumCar, intBaud, intDataLen, intRSLen, intDataPtr, intSampPerSym, intDataBytesPerCar;
	BOOL blnOdd;

	int intSample;

    char strType[18] = "";
    char strMod[16] = "";
    char DebugMess[1024];

	UCHAR bytSymToSend, bytMask, bytMinQualThresh;

	float dblCarScalingFactor;
	int intMask = 0;
	int intLeaderLenMS;
	int k, m, n;

	if (Len < 0) {
		WriteDebugLog(LOGERROR, "ERROR: In Mod4FSKDataAndPlay() Invalid Len (%d).", Len);
		return;
	}

	if (!FrameInfo(Type, &blnOdd, &intNumCar, strMod, &intBaud, &intDataLen, &intRSLen, &bytMinQualThresh, strType))
		return;

	if (strcmp(strMod, "4FSK") != 0)
		return;

	if (intBaud == 600) {
		WriteDebugLog(LOGERROR,
			"ERROR: Mod4FSKDataAndPlay() cannot be used for 600 baud 4FSK Frames."
			"Use Mod4FSK600BdDataAndPlay() instead.");
		return;
	}

	WriteDebugLog(LOGINFO, "Sending Frame Type %s", strType);
	DrawTXFrame(strType);
	wg_send_txframet(0, strType);

	if (Type == PktFrameHeader)
	{
		// Meader is 4FSK which needs 500 filter

		if (pktBW[pktMode] < 1000)
			initFilter(500,1500);
		else if (pktBW[pktMode] < 2000)
			initFilter(1000,1500);
		else
			initFilter(2000,1500);
	}
	else
	{
		if (intBaud == 50)
			initFilter(200,1500);
		else if (intNumCar == 1)
			initFilter(500,1500);
		else if (intNumCar == 2)
			initFilter(1000,1500);
		else if (intNumCar == 4)
			initFilter(2000,1500);
	}

//	If Not (strType = "DataACK" Or strType = "DataNAK" Or strType = "IDFrame" Or strType.StartsWith("ConReq") Or strType.StartsWith("ConAck")) Then
 //               strLastWavStream = strType
  //          End If


	if (intLeaderLen == 0)
		intLeaderLenMS = LeaderLength;
	else
		intLeaderLenMS = intLeaderLen;

    switch(intBaud)
	{		
	case 50:
		intSampPerSym = 240;
		break;
	case 100:
		intSampPerSym = 120;
		break;
	default:
		WriteDebugLog(LOGERROR, "ERROR: Invalid baud rate (%d) in Mod4FSKDataAndPlay().", intBaud);
		return;
	}
		
	intDataBytesPerCar = (Len - 2) / intNumCar;		// We queue the samples here, so dont copy below
    
	SendLeaderAndSYNC(bytEncodedBytes, intLeaderLen);

	intDataPtr = 2;

Reenter:

	switch(intNumCar)
	{
	case 1:			 // use carriers 0-3
		
		dblCarScalingFactor = 1.0; //  (scaling factors determined emperically to minimize crest factor) 

		sprintf(DebugMess, "Mod4FSKDataAndPlay 1Car tones :");
		for (m = 0; m < intDataBytesPerCar; m++)  // For each byte of input data
		{
			bytMask = 0xC0;		 // Initialize mask each new data byte
			sprintf(DebugMess + strlen(DebugMess), " ");
			for (k = 0; k < 4; k++)		// for 4 symbol values per byte of data
			{
				bytSymToSend = (bytMask & bytEncodedBytes[intDataPtr]) >> (2 * (3 - k)); // Values 0-3
				sprintf(DebugMess + strlen(DebugMess), "%d", bytSymToSend);

				for (n = 0; n < intSampPerSym; n++)	 // Sum for all the samples of a symbols 
				{
					if((k & 1) == 0)
					{
						if(intBaud == 50)
							intSample = intFSK50bdCarTemplate[bytSymToSend][n];
						else
							intSample = intFSK100bdCarTemplate[bytSymToSend][n];
							
						SampleSink(intSample);
					}
					else
 					{
						if(intBaud == 50)
							intSample = -intFSK50bdCarTemplate[bytSymToSend][n];
						else
							intSample = -intFSK100bdCarTemplate[bytSymToSend][n];
							
						SampleSink(intSample);	
					}
				}

				bytMask = bytMask >> 2;
			}
			intDataPtr += 1;
		}
		// Include these tone values in debug log only if FileLogLevel is LOGDEBUGPLUS
		if (intDataBytesPerCar == 0)
			sprintf(DebugMess + strlen(DebugMess), "(None)");
		WriteDebugLog(LOGDEBUGPLUS, "%s", DebugMess);

		if (Type == PktFrameHeader)
		{
		
			// just sent packet header. Send rest in current mode
			// Assumes we are using 4FSK for Packet Header

			Type = 0;			// Prevent reentry
	
			strcpy(strMod, &pktMod[pktMode][0]);
			intDataBytesPerCar = pktDataLen + pktRSLen + 3;
			intDataPtr = 11;		// Over Header
			intNumCar = pktCarriers[pktMode];

			// This assumes Packet Data is sent as PSK/QAM

			switch(intNumCar)
			{		
			case 1:
		//		intCarStartIndex = 4;
				dblCarScalingFactor = 1.0f; // Starting at 1500 Hz  (scaling factors determined emperically to minimize crest factor)  TODO:  needs verification
				break;
			case 2:
		//		intCarStartIndex = 3;
				dblCarScalingFactor = 0.53f; // Starting at 1400 Hz
				break;
			case 4:
		//		intCarStartIndex = 2;
				dblCarScalingFactor = 0.29f; // Starting at 1200 Hz
				break;
			case 8:
		//		intCarStartIndex = 0;
				dblCarScalingFactor = 0.17f; // Starting at 800 Hz
			}
			
			// Reenter to send rest of variable length packet frame
		
			if (pktFSK[pktMode])
				goto Reenter;
			else
				ModPSKDataAndPlay(PktFrameData, bytEncodedBytes, 0, 0);
			return;
		}

		Flush();

		break;

	case 2:			// use carriers 8-15 (100 baud only)

		dblCarScalingFactor = 0.51f; //  (scaling factors determined emperically to minimize crest factor)

		for (m = 0; m < intDataBytesPerCar; m++)	  // For each byte of input data 
		{
			bytMask = 0xC0;	// Initialize mask each new data byte
                        			
			for (k = 0; k < 4; k++)		// for 4 symbol values per byte of data
			{
				for (n = 0; n < intSampPerSym; n++)	 // for all the samples of a symbol for 2 carriers
				{
					//' First carrier
                      
					bytSymToSend = (bytMask & bytEncodedBytes[intDataPtr]) >> (2 * (3 - k)); // Values 0-3
					intSample = intFSK100bdCarTemplate[8 + bytSymToSend][n];
					// Second carrier
                    
					bytSymToSend = (bytMask & bytEncodedBytes[intDataPtr + intDataBytesPerCar]) >> (2 * (3 - k));	// Values 0-3
					intSample = dblCarScalingFactor * (intSample + intFSK100bdCarTemplate[12 + bytSymToSend][n]);
			
					SampleSink(intSample);
				}
				bytMask = bytMask >> 2;
			}
			intDataPtr += 1;
		}
             
		Flush();

		break;

	case 4:		 // use carriers 4-19 (100 baud only)

 		dblCarScalingFactor = 0.27f; //  (scaling factors determined emperically to minimize crest factor)

		for (m = 0; m < intDataBytesPerCar; m++)	  // For each byte of input data 
		{
			bytMask = 0xC0;	// Initialize mask each new data byte
                        			
			for (k = 0; k < 4; k++)		// for 4 symbol values per byte of data
			{
				for (n = 0; n < intSampPerSym; n++)	 // for all the samples of a symbol for 2 carriers
				{
					//' First carrier
                      
					bytSymToSend = (bytMask & bytEncodedBytes[intDataPtr]) >> (2 * (3 - k)); // Values 0-3
					intSample = intFSK100bdCarTemplate[4 + bytSymToSend][n];
					// Second carrier
                    
					bytSymToSend = (bytMask & bytEncodedBytes[intDataPtr + intDataBytesPerCar]) >> (2 * (3 - k));	// Values 0-3
					intSample = intSample + intFSK100bdCarTemplate[8 + bytSymToSend][n];
			
					//' Third carrier
					
					bytSymToSend = (bytMask & bytEncodedBytes[intDataPtr + 2 * intDataBytesPerCar]) >> (2 * (3 - k));	// Values 0-3
					intSample = intSample + intFSK100bdCarTemplate[12 + bytSymToSend][n];

					// ' Fourth carrier
   
					bytSymToSend = (bytMask & bytEncodedBytes[intDataPtr + 3 * intDataBytesPerCar]) >> (2 * (3 - k));	// Values 0-3
					intSample = dblCarScalingFactor * (intSample + intFSK100bdCarTemplate[16 + bytSymToSend][n]);

					SampleSink(intSample);
				}
				bytMask = bytMask >> 2;
			}
			intDataPtr += 1;
		}       
		Flush();
		break;
	}
}

// Function to Modulate encoded data to 8FSK and send to sound interface

void Mod8FSKDataAndPlay(int Type, unsigned char * bytEncodedBytes, int Len, int intLeaderLen)
{
	// Function to Modulate data encoded for 8FSK, create
	// the 16 bit samples and send to sound interface

	int intBaud, intDataLen, intRSLen, intDataPtr, intSampPerSym, intDataBytesPerCar;
	BOOL blnOdd;
	int intNumCar;

	short intSample;
	unsigned int intThreeBytes = 0;

    char strType[18] = "";
    char strMod[16] = "";

	UCHAR bytSymToSend, bytMinQualThresh;
	int intMask = 0;
	int k, m, n;

	if (Len < 0) {
		WriteDebugLog(LOGERROR, "ERROR: In Mod8FSKDataAndPlay() Invalid Len (%d).", Len);
		return;
	}

	if (!FrameInfo(Type, &blnOdd, &intNumCar, strMod, &intBaud, &intDataLen, &intRSLen, &bytMinQualThresh, strType))
		return;

	if (strcmp(strMod, "8FSK") != 0)
		return;

	WriteDebugLog(LOGINFO, "Sending Frame Type %s", strType);
	DrawTXFrame(strType);
	wg_send_txframet(0, strType);

	initFilter(200,1500);

//	If Not (strType = "DataACK" Or strType = "DataNAK" Or strType = "IDFrame" Or strType.StartsWith("ConReq") Or strType.StartsWith("ConAck")) Then
 //               strLastWavStream = strType
  //          End If


	intSampPerSym = 240;
	
	intDataBytesPerCar = (Len - 2) / intNumCar;		// We queue the samples here, so dont copy below
    
	SendLeaderAndSYNC(bytEncodedBytes, intLeaderLen);

	intSampPerSym = 480;			// 25 Baud

	intDataPtr = 2;

 	for (m = 0; m < intDataBytesPerCar; m += 3)  // For each byte of input data
	{
		intThreeBytes = bytEncodedBytes[intDataPtr++];
		intThreeBytes = (intThreeBytes << 8) + bytEncodedBytes[intDataPtr++];
		intThreeBytes = (intThreeBytes << 8) + bytEncodedBytes[intDataPtr++];
		intMask = 0xE00000;
                 
		for (k = 0; k < 8; k++)
		{
			bytSymToSend = (intMask & intThreeBytes) >> (3 * (7 - k));

			// note value of "+ 4" below allows using 16FSK template for 8FSK using only the "inner" 8 tones around 1500

			for (n = 0; n < intSampPerSym; n++)	 // Sum for all the samples of a symbols 
			{
				if((k & 1) == 0)
					intSample = intFSK25bdCarTemplate[bytSymToSend + 4][n]; // Symbol vlaues 4- 11 (surrounding 1500 Hz)  
				else
					intSample = -intFSK25bdCarTemplate[bytSymToSend + 4][n]; // Symbol vlaues 4- 11 (surrounding 1500 Hz)  

				SampleSink(intSample);
			}
			intMask = intMask >> 3;
		}
	}
	Flush();
}

// Function to Modulate encoded data to 16FSK and send to sound interface

void Mod16FSKDataAndPlay(int Type, unsigned char * bytEncodedBytes, int Len, int intLeaderLen)
{
	// Function to Modulate data encoded for 16FSK, create
	// the 16 bit samples and send to sound interface

	int intBaud, intDataLen, intRSLen, intDataPtr, intSampPerSym, intDataBytesPerCar;
	BOOL blnOdd;
	int intNumCar;

	short intSample;
	unsigned int intThreeBytes = 0;

    char strType[18] = "";
    char strMod[16] = "";

	UCHAR bytSymToSend, bytMask, bytMinQualThresh;

	int intMask = 0;
	int k, m, n;

	if (Len < 0) {
		WriteDebugLog(LOGERROR, "ERROR: In Mod16FSKDataAndPlay() Invalid Len (%d).", Len);
		return;
	}

	if (!FrameInfo(Type, &blnOdd, &intNumCar, strMod, &intBaud, &intDataLen, &intRSLen, &bytMinQualThresh, strType))
		return;

	if (strcmp(strMod, "16FSK") != 0)
		return;

	WriteDebugLog(LOGINFO, "Sending Frame Type %s", strType);
	DrawTXFrame(strType);
	wg_send_txframet(0, strType);

	initFilter(500,1500);

//	If Not (strType = "DataACK" Or strType = "DataNAK" Or strType = "IDFrame" Or strType.StartsWith("ConReq") Or strType.StartsWith("ConAck")) Then
 //               strLastWavStream = strType
  //          End If

	intDataBytesPerCar = (Len - 2) / intNumCar;		// We queue the samples here, so dont copy below    
	intSampPerSym = 480;			// 25 Baud

	SendLeaderAndSYNC(bytEncodedBytes, intLeaderLen);

	intDataPtr = 2;

	for (m = 0; m < intDataBytesPerCar; m++)  // For each byte of input data 
	{
		bytMask = 0xF0;	 // Initialize mask each new data byte
		for (k = 0; k < 2; k++)	// for 2 symbol values per byte of data
		{
			bytSymToSend = (bytMask & bytEncodedBytes[intDataPtr]) >> (4 * (1 - k)); // Values 0 - 15

			for (n = 0; n < intSampPerSym; n++)	 // Sum for all the samples of a symbols 
			{
				if((k & 1) == 0)
					intSample = intFSK25bdCarTemplate[bytSymToSend][n];
				else
					intSample = -intFSK25bdCarTemplate[bytSymToSend][n];

				SampleSink(intSample);
			}
			bytMask = bytMask >> 4;
		}
		intDataPtr++;
	}
	Flush();
}

//	Function to Modulate data encoded for 4FSK High baud rate and create the integer array of 32 bit samples suitable for playing 



void Mod4FSK600BdDataAndPlay(int Type, unsigned char * bytEncodedBytes, int Len, int intLeaderLen)
{
	// Function to Modulate data encoded for 4FSK, create
	// the 16 bit samples and send to sound interface

	// Function works for 1, 2 or 4 simultaneous carriers 

	int intNumCar, intBaud, intDataLen, intRSLen, intDataPtr, intSampPerSym, intDataBytesPerCar;
	BOOL blnOdd;

	short intSample;

    char strType[18] = "";
    char strMod[16] = "";

	UCHAR bytSymToSend, bytMask, bytMinQualThresh;

	int intMask = 0;
	int k, m, n;

	if (Len < 0) {
		WriteDebugLog(LOGERROR, "ERROR: In Mod4FSK600BdDataAndPlay() Invalid Len (%d).", Len);
		return;
	}

	if (!FrameInfo(Type, &blnOdd, &intNumCar, strMod, &intBaud, &intDataLen, &intRSLen, &bytMinQualThresh, strType))
		return;

	if (strcmp(strMod, "4FSK") != 0)
		return;

	if (intBaud != 600) {
		WriteDebugLog(LOGERROR,
			"ERROR: Mod4FSK600BdDataAndPlay() is only for 600 baud 4FSK Frames."
			"Use Mod4FSKDataAndPlay() instead.");
		return;
	}

	WriteDebugLog(LOGINFO, "Sending Frame Type %s", strType);
	DrawTXFrame(strType);
	wg_send_txframet(0, strType);

	initFilter(2000,1500);

//	If Not (strType = "DataACK" Or strType = "DataNAK" Or strType = "IDFrame" Or strType.StartsWith("ConReq") Or strType.StartsWith("ConAck")) Then
 //               strLastWavStream = strType
  //          End If

	intDataBytesPerCar = (Len - 2) / intNumCar;		// We queue the samples here, so dont copy below

	intSampPerSym = 12000 / intBaud;
    
	SendLeaderAndSYNC(bytEncodedBytes, intLeaderLen);

	intDataPtr = 2;

	for (m = 0; m < intDataBytesPerCar; m++)  // For each byte of input data
	{
		bytMask = 0xC0;		 // Initialize mask each new data byte			
		for (k = 0; k < 4; k++)		// for 4 symbol values per byte of data
		{
			bytSymToSend = (bytMask & bytEncodedBytes[intDataPtr]) >> (2 * (3 - k)); // Values 0-3
			for (n = 0; n < intSampPerSym; n++)	 // Sum for all the samples of a symbols 
			{
    			intSample = intFSK600bdCarTemplate[bytSymToSend][n];
				SampleSink(intSample);
			}
			bytMask = bytMask >> 2;
		}
		intDataPtr += 1;
	}
	Flush();
}


// Function to extract an 8PSK symbol from an encoded data array


UCHAR GetSym8PSK(int intDataPtr, int k, int intCar, UCHAR * bytEncodedBytes, int intDataBytesPerCar)
{
	int int3Bytes = bytEncodedBytes[intDataPtr + intCar * intDataBytesPerCar];
//	int intMask  = 7;
	int intSym;
	UCHAR bytSym;

	int3Bytes = int3Bytes << 8;
	int3Bytes += bytEncodedBytes[intDataPtr + intCar * intDataBytesPerCar + 1];
	int3Bytes = int3Bytes << 8;
	int3Bytes += bytEncodedBytes[intDataPtr + intCar * intDataBytesPerCar + 2];  // now have 3 bytes, 24 bits or 8 8PSK symbols 
//	intMask = intMask << (3 * (7 - k));
	intSym = int3Bytes >> (3 * (7 - k));
	bytSym = intSym & 7;	//(intMask && int3Bytes) >> (3 * (7 - k));

	return bytSym;
}


// Function to soft clip combined waveforms. 
int SoftClip(int intInput)
{
	if (intInput > 30000) // soft clip above/below 30000
	{
		intInput = min(32700, 30000 + 20 * sqrt(intInput - 30000));
		intSoftClipCnt += 1;
	}
	else if(intInput < -30000)
	{
		intInput = max(-32700, -30000 - 20 * sqrt(-(intInput + 30000)));
		intSoftClipCnt += 1;
	}

	return intInput;
}
// Function to Modulate data encoded for PSK and 16QAM, create
// the 16 bit samples and send to sound interface
   

void ModPSKDataAndPlay(int Type, unsigned char * bytEncodedBytes, int Len, int intLeaderLen)
{
	int intNumCar, intBaud, intDataLen, intRSLen, intDataPtr, intSampPerSym, intDataBytesPerCar;
	BOOL blnOdd;

	int intSample;
    char strType[18] = "";
    char strMod[16] = "";
	UCHAR bytSym, bytSymToSend, bytMask, bytMinQualThresh;
	float dblCarScalingFactor;
	int intMask = 0;
	int intLeaderLenMS;
	int i, k, m, n;
	int intCarStartIndex;
	int intPeakAmp;
	int intCarIndex;

	UCHAR bytLastSym[9]; // = {0}; // Holds the last symbol sent (per carrier). bytLastSym(4) is 1500 Hz carrier (only used on 1 carrier modes) 

	if (Len < 0) {
		WriteDebugLog(LOGERROR, "ERROR: In ModPSKDataAndPlay() Invalid Len (%d).", Len);
		return;
	}

	if (!FrameInfo(Type, &blnOdd, &intNumCar, strMod, &intBaud, &intDataLen, &intRSLen, &bytMinQualThresh, strType))
		return;

	intDataBytesPerCar = (Len - 2) / intNumCar;		// We queue the samples here, so dont copy below

	switch(intNumCar)
	{
		// These new scaling factor combined with soft clipping to provide near optimum scaling Jan 6, 2018 
        // The Test form was changed to calculate the Peak power to RMS power (PAPR) of the test waveform and count the number of "soft clips" out of ~ 50,000 samples. 
        // These values arrived at emperically using the Test form (Quick brown fox message) to minimize PAPR at a minor decrease in maximum constellation quality
   

		// Rick uses these for QAM
		
		//dblCarScalingFactor = 1.2 ' Starting at 1500 Hz  Selected to give < 9% clipped values yielding a PAPR = 1.77 Constellation Quality >98
        //dblCarScalingFactor = 0.67 ' Carriers at 1400 and 1600 Selected to give < 2.5% clipped values yielding a PAPR = 2.17, Constellation Quality >92
       // dblCarScalingFactor = 0.4 ' Starting at 1200 Hz  Selected to give < 1.5% clipped values yielding a PAPR = 2.48, Constellation Quality >92
       // dblCarScalingFactor = 0.27 ' Starting at 800 Hz  Selected to give < 1% clipped values yielding a PAPR = 2.64, Constellation Quality >94


	case 1:
		intCarStartIndex = 4;
//		dblCarScalingFactor = 1.0f; // Starting at 1500 Hz  (scaling factors determined emperically to minimize crest factor)  TODO:  needs verification
		dblCarScalingFactor = 1.2f; // Starting at 1500 Hz  Selected to give < 13% clipped values yielding a PAPR = 1.6 Constellation Quality >98
		break;
	case 2:
		intCarStartIndex = 3;
//		dblCarScalingFactor = 0.53f;
		if (strcmp(strMod, "16QAM") == 0)
			dblCarScalingFactor = 0.67f; // Carriers at 1400 and 1600 Selected to give < 2.5% clipped values yielding a PAPR = 2.17, Constellation Quality >92
		else
			dblCarScalingFactor = 0.65f; // Carriers at 1400 and 1600 Selected to give < 4% clipped values yielding a PAPR = 2.0, Constellation Quality >95
		break;
	case 4:
		intCarStartIndex = 2;
//		dblCarScalingFactor = 0.29f; // Starting at 1200 Hz
		dblCarScalingFactor = 0.4f;  // Starting at 1200 Hz  Selected to give < 3% clipped values yielding a PAPR = 2.26, Constellation Quality >95
		break;
	case 8:
		intCarStartIndex = 0;
//		dblCarScalingFactor = 0.17f; // Starting at 800 Hz
		if (strcmp(strMod, "16QAM") == 0)
			dblCarScalingFactor = 0.27f; // Starting at 800 Hz  Selected to give < 1% clipped values yielding a PAPR = 2.64, Constellation Quality >94
		else
			dblCarScalingFactor = 0.25f; // Starting at 800 Hz  Selected to give < 2% clipped values yielding a PAPR = 2.5, Constellation Quality >95
	} 

	intSampPerSym = 120;

	if (Type == PktFrameData)
	{
		intDataBytesPerCar = pktDataLen + pktRSLen + 3;
		intDataPtr = 11;		// Over Header
		goto PktLoopBack;
	}
	
	WriteDebugLog(LOGINFO, "Sending Frame Type %s", strType);
	DrawTXFrame(strType);
	wg_send_txframet(0, strType);

/*	// DOnt use PSK Header at the moment
	if (Type == PktFrameHeader)
	{
		// Header is always 200 but Packet Data may vary

		if (pktNumCar == 1)
			initFilter(200,1500);
		else if (pktNumCar == 2)
			initFilter(500,1500);
		else if (pktNumCar == 4)
			initFilter(1000,1500);
		else if (pktNumCar == 8)
			initFilter(2000,1500);
	}
	else
	{
*/
	if (intNumCar == 1)
		initFilter(200,1500);
	else if (intNumCar == 2)
		initFilter(500,1500);
	else if (intNumCar == 4)
		initFilter(1000,1500);
	else if (intNumCar == 8)
		initFilter(2000,1500);
//	}
//	If Not (strType = "DataACK" Or strType = "DataNAK" Or strType = "IDFrame" Or strType.StartsWith("ConReq") Or strType.StartsWith("ConAck")) Then
//               strLastWavStream = strType
//          End If

	if (intLeaderLen == 0)
		intLeaderLenMS = LeaderLength;
	else
		intLeaderLenMS = intLeaderLen;

	intSoftClipCnt = 0;
	
	// Create the leader

	SendLeaderAndSYNC(bytEncodedBytes, intLeaderLen);

	intPeakAmp = 0;

	intDataPtr = 2;  // initialize pointer to start of data.

PktLoopBack:		// Reenter here to send rest of variable length packet frame

           
	// Now create a reference symbol for each carrier
      
	//	We have to do each carrier for each sample, as we write
	//	the sample immediately 

	for (n = 0; n < intSampPerSym; n++)  // Sum for all the samples of a symbols 
	{
		intSample = 0;
		intCarIndex = intCarStartIndex;  // initialize to correct starting carrier

		for (i = 0; i < intNumCar; i++)	// across all carriers
		{
			bytSymToSend = 0;  //  using non 0 causes error on first data byte 12/8/2014   ...Values 0-3  not important (carries no data).   (Possible chance for Crest Factor reduction?)
                
			bytLastSym[intCarIndex] = bytSymToSend;

			if (intBaud == 100)
				intSample += intPSK100bdCarTemplate[intCarIndex][0][n];  // double the symbol value during template lookup for 4PSK. (skips over odd PSK 8 symbols)
			else
				intSample += intPSK200bdCarTemplate[intCarIndex][0][n]; // subtract 2 from the symbol value before doubling and subtract value of table 

			intCarIndex += 1;
			if (intCarIndex == 4)
				intCarIndex += 1;	// skip over 1500 Hz for multi carrier modes (multi carrier modes all use even hundred Hz tones)
		}
		intSample = intSample * dblCarScalingFactor; // on the last carrier rescale value based on # of carriers to bound output
		SampleSink(intSample);
	}
      
	// End of reference phase generation 

	if (strcmp(strMod, "4PSK") == 0)
	{
		for (m = 0; m < intDataBytesPerCar; m++)  // For each byte of input data (all carriers) 
		{
			bytMask = 0xC0; // Initialize mask each new data byte
                        
			for (k = 0; k < 4; k++)  // for 4 symbol values per byte of data
			{                 
				for (n = 0; n < intSampPerSym; n++)  // Sum for all the samples of a symbols 
				{
					intSample = 0;
					intCarIndex = intCarStartIndex; // initialize the carrrier index
	
					for (i = 0; i < intNumCar ; i++) // across all carriers
					{
						bytSym = (bytMask & bytEncodedBytes[intDataPtr + i * intDataBytesPerCar]) >> (2 * (3 - k));
						bytSymToSend = ((bytLastSym[intCarIndex] + bytSym) & 3);  // Values 0-3
			
						if (intBaud == 100)
						{
							if (bytSymToSend < 2)
								intSample += intPSK100bdCarTemplate[intCarIndex][bytSymToSend * 2][n];  // double the symbol value during template lookup for 4PSK. (skips over odd PSK 8 symbols)
							else
								intSample -= intPSK100bdCarTemplate[intCarIndex][2 * (bytSymToSend - 2)][n]; // subtract 2 from the symbol value before doubling and subtract value of table 
						}
						else
						{
							if (bytSymToSend < 2)
								intSample += intPSK200bdCarTemplate[intCarIndex][bytSymToSend * 2][n];  // double the symbol value during template lookup for 4PSK. (skips over odd PSK 8 symbols)
							else
								intSample -= intPSK200bdCarTemplate[intCarIndex][2 * (bytSymToSend - 2)][n]; // subtract 2 from the symbol value before doubling and subtract value of table 
						}
						if (n == intSampPerSym - 1)		// Last sample?
							bytLastSym[intCarIndex] = bytSymToSend;

						intCarIndex += 1;
						if (intCarIndex == 4)
							intCarIndex += 1;	// skip over 1500 Hz for multi carrier modes (multi carrier modes all use even hundred Hz tones)
					}
					intSample = intSample * dblCarScalingFactor; // on the last carrier rescale value based on # of carriers to bound output

	//				if (intSample > 32700)
	//					intSample = 32700;
				
	//				if (intSample < -32700)
	//				  	intSample = -32700;

                    intSample = SoftClip(intSample);
	
					SampleSink(intSample);		
				}       
				bytMask = bytMask >> 2;
			}
			intDataPtr += 1;
		}
	}
	else if (strcmp(strMod, "8PSK") == 0)
	{
		// More complex ...must go through data in 3 byte chunks creating 8 Three bit symbols for each 3 bytes of data. 
     
		for (m = 0; m < intDataBytesPerCar / 3; m++)
		{
			for (k = 0; k < 8; k++) // for 8 symbols in 24 bits of int3Bytes
			{
				for (n = 0; n < intSampPerSym; n++)	//  Sum for all the samples of a symbols 
				{
					intSample = 0;
		
					// We have to sum all samples for all carriers

					intCarIndex = intCarStartIndex;
				
					for (i = 0; i < intNumCar; i++)
					{
						bytSym = GetSym8PSK(intDataPtr, k, i, bytEncodedBytes, intDataBytesPerCar);
						bytSymToSend = ((bytLastSym[intCarIndex] + bytSym) & 7);	// mod 8
				
						if (intBaud == 100)
						{
							if (bytSymToSend < 4) // This uses the symmetry of the symbols to reduce the table size by a factor of 2
								intSample += intPSK100bdCarTemplate[intCarIndex][bytSymToSend][n]; // positive phase values template lookup for 8PSK.
							else
								intSample -= intPSK100bdCarTemplate[intCarIndex][bytSymToSend - 4][n]; // negative phase values,  subtract value of table 
						}
						else
						{
							if (bytSymToSend < 4) // This uses the symmetry of the symbols to reduce the table size by a factor of 2
								intSample += intPSK200bdCarTemplate[intCarIndex][bytSymToSend][n]; // positive phase values template lookup for 8PSK.
							else
								intSample -= intPSK200bdCarTemplate[intCarIndex][bytSymToSend - 4][n]; // negative phase values,  subtract value of table 
						}

						if (n == intSampPerSym - 1)		// Last sample?
							bytLastSym[intCarIndex] = bytSymToSend;

						intCarIndex += 1;
						if (intCarIndex == 4)
							intCarIndex += 1;  // skip over 1500 Hz for multi carrier modes (multi carrier modes all use even hundred Hz tones)
					}
					intSample = intSample * dblCarScalingFactor; // on the last carrier rescale value based on # of carriers to bound output

	//				if (intSample > 32700)
	//					intSample = 32700;
				
	//				if (intSample < -32700)
	//				  	intSample = -32700;

                    intSample = SoftClip(intSample);
					
					SampleSink(intSample);		
				}
			}
			intDataPtr += 3;
		}
	}
	else if (strcmp(strMod, "16QAM") == 0)
	{
		for (m = 0; m < intDataBytesPerCar; m++)  // For each byte of input data (all carriers) 
		{
			bytMask = 0xF0; // Initialize mask each new data byte
                        
			for (k = 0; k < 2; k++)  // for 2 symbol values per byte of data
			{                 
				for (n = 0; n < intSampPerSym; n++)  // Sum for all the samples of a symbols 
				{
					intSample = 0;
					intCarIndex = intCarStartIndex; // initialize the carrrier index
		
					for (i = 0; i < intNumCar ; i++) // across all carriers
					{
						bytSym = (bytMask & bytEncodedBytes[intDataPtr + i * intDataBytesPerCar]) >> (4 * (1 - k));
						bytSymToSend = (bytLastSym[intCarIndex] + (bytSym & 7)) & 7;  // Values 0-7
								
					//if (intBaud == 100) only use 100
					//{
						if (bytSym < 8)
						{
							if (bytSymToSend < 4) // This uses the symmetry of the symbols to reduce the table size by a factor of 2
								intSample += intPSK100bdCarTemplate[intCarIndex][bytSymToSend][n]; // positive phase values template lookup for 8PSK.
							else
								intSample -= intPSK100bdCarTemplate[intCarIndex][bytSymToSend - 4][n]; // negative phase values,  subtract value of table 
						}
						else
						{
							if (bytSymToSend < 4) // This uses the symmetry of the symbols to reduce the table size by a factor of 2
								intSample += 0.5f * intPSK100bdCarTemplate[intCarIndex][bytSymToSend][n]; // positive phase values template lookup for 8PSK.
							else
								intSample -= 0.5f * intPSK100bdCarTemplate[intCarIndex][bytSymToSend - 4][n]; // negative phase values,  subtract value of table 
						}
					//}	
						if (n == intSampPerSym - 1)		// Last sample?
							bytLastSym[intCarIndex] = bytSymToSend;
					
						intCarIndex += 1;
						if (intCarIndex == 4)
							intCarIndex += 1;  // skip over 1500 Hz for multi carrier modes (multi carrier modes all use even hundred Hz tones)
					}

					intSample = intSample * dblCarScalingFactor; // on the last carrier rescale value based on # of carriers to bound output

	//				if (intSample > 32700)
	//					intSample = 32700;
				
	//				if (intSample < -32700)
	//				  	intSample = -32700;

                    intSample = SoftClip(intSample);
					
					SampleSink(intSample);		
				}      
				bytMask = bytMask >> 4;
			}
			intDataPtr += 1;
		}
	}
	if (Type == PktFrameHeader)
	{
		// just sent packet header. Send rest in current mode

		Type = 0;			// Prevent reentry

		strcpy(strMod, &pktMod[pktMode][0]);
		intDataBytesPerCar = pktDataLen + pktRSLen + 3;
		intDataPtr = 11;		// Over Header
		intNumCar = pktCarriers[pktMode];

		switch(intNumCar)
		{		
		case 1:
			intCarStartIndex = 4;
//			dblCarScalingFactor = 1.0f; // Starting at 1500 Hz  (scaling factors determined emperically to minimize crest factor)  TODO:  needs verification
			dblCarScalingFactor = 1.2f; // Starting at 1500 Hz  Selected to give < 13% clipped values yielding a PAPR = 1.6 Constellation Quality >98
			break;
		case 2:
			intCarStartIndex = 3;
//			dblCarScalingFactor = 0.53f;
			if (strcmp(strMod, "16QAM") == 0)
				dblCarScalingFactor = 0.67f; // Carriers at 1400 and 1600 Selected to give < 2.5% clipped values yielding a PAPR = 2.17, Constellation Quality >92
			else
				dblCarScalingFactor = 0.65f; // Carriers at 1400 and 1600 Selected to give < 4% clipped values yielding a PAPR = 2.0, Constellation Quality >95
			break;
		case 4:
			intCarStartIndex = 2;
//			dblCarScalingFactor = 0.29f; // Starting at 1200 Hz
			dblCarScalingFactor = 0.4f;  // Starting at 1200 Hz  Selected to give < 3% clipped values yielding a PAPR = 2.26, Constellation Quality >95
			break;
		case 8:
			intCarStartIndex = 0;
//			dblCarScalingFactor = 0.17f; // Starting at 800 Hz
			if (strcmp(strMod, "16QAM") == 0)
				dblCarScalingFactor = 0.27f; // Starting at 800 Hz  Selected to give < 1% clipped values yielding a PAPR = 2.64, Constellation Quality >94
			else
				dblCarScalingFactor = 0.25f; // Starting at 800 Hz  Selected to give < 2% clipped values yielding a PAPR = 2.5, Constellation Quality >95
		} 
		goto PktLoopBack;		// Reenter to send rest of variable length packet frame
	}
	Flush();
	if (intSoftClipCnt > 0)
		WriteDebugLog(LOGDEBUG, "Soft Clips %d ", intSoftClipCnt);

}


// Subroutine to add trailer before filtering

void AddTrailer()
{
	int intAddedSymbols = 1 + (TrailerLength / 10); // add 1 symbol + 1 per each 10 ms of MCB.Trailer
	int i, k;

	for (i = 1; i <= intAddedSymbols; i++)
	{
		for (k = 0; k < 120; k++)
		{
			SampleSink(intPSK100bdCarTemplate[4][0][k]);
		}
	}
}

//	Resends the last frame

void RemodulateLastFrame()
{	
	int intNumCar, intBaud, intDataLen, intRSLen;
	UCHAR bytMinQualThresh;
	BOOL blnOdd;

	char strType[18] = "";
    char strMod[16] = "";

	if (!FrameInfo(bytEncodedBytes[0], &blnOdd, &intNumCar, strMod, &intBaud, &intDataLen, &intRSLen, &bytMinQualThresh, strType))
		return;

	if (strcmp(strMod, "4FSK") == 0)
	{
		if (bytEncodedBytes[0] >= 0x7A && bytEncodedBytes[0] <= 0x7D)
			Mod4FSK600BdDataAndPlay(bytEncodedBytes[0], bytEncodedBytes, EncLen, intCalcLeader);  // Modulate Data frame 
		else
			Mod4FSKDataAndPlay(bytEncodedBytes[0], bytEncodedBytes, EncLen, intCalcLeader);  // Modulate Data frame 

		return;
	}
	if (strcmp(strMod, "16FSK") == 0)
	{
		Mod16FSKDataAndPlay(bytEncodedBytes[0], bytEncodedBytes, EncLen, intCalcLeader);  // Modulate Data frame 
		return;
	}
	if (strcmp(strMod, "8FSK") == 0)
	{
		Mod8FSKDataAndPlay(bytEncodedBytes[0], bytEncodedBytes, EncLen, intCalcLeader);  // Modulate Data frame 
		return;
	}
	ModPSKDataAndPlay(bytEncodedBytes[0], bytEncodedBytes, EncLen, intCalcLeader);  // Modulate Data frame 
}

// Filter State Variables

static float dblR = (float)0.9995f;	// insures stability (must be < 1.0) (Value .9995 7/8/2013 gives good results)
static int intN = 120;				//Length of filter 12000/100
static float dblRn;

static float dblR2;
static float dblCoef[32] = {0.0f};			// the coefficients
float dblZin = 0, dblZin_1 = 0, dblZin_2 = 0, dblZComb= 0;  // Used in the comb generator

// The resonators 
      
float dblZout_0[32] = {0.0f};	// resonator outputs
float dblZout_1[32] = {0.0f};	// resonator outputs delayed one sample
float dblZout_2[32] = {0.0f};	// resonator outputs delayed two samples

int fWidth;				// Filter BandWidth
int SampleNo;
int outCount = 0;
int first, last;		// Filter slots
int centreSlot;

float largest = 0;
float smallest = 0;

short Last120[128];

int Last120Get = 0;
int Last120Put = 120;

int Number = 0;				// Number waiting to be sent

extern unsigned short buffer[2][1200];

unsigned short * DMABuffer;

unsigned short * SendtoCard(unsigned short * buf, int n);
unsigned short * SoundInit();

// initFilter is called to set up each packet. It selects filter width

void initFilter(int Width, int Centre)
{
	int i, j;

	if (WriteTxWav)
		StartTxWav();
	
	fWidth = Width;
	centreSlot = Centre / 100;
	largest = smallest = 0;
	SampleNo = 0;
	Number = 0;
	outCount = 0;
	memset(Last120, 0, 256);

	DMABuffer = SoundInit();

	KeyPTT(TRUE);
	pttOnTime = Now;

	SoundIsPlaying = TRUE;
	StopCapture();

	Last120Get = 0;
	Last120Put = 120;

	dblRn = powf(dblR, intN);
	dblR2 = powf(dblR, 2);

	dblZin_2 = dblZin_1 = 0;

	switch (fWidth)
	{
	case 200:

		// implements 3 100 Hz wide sections centered on 1500 Hz  (~200 Hz wide @ - 30dB centered on 1500 Hz)

		first = centreSlot - 1;
		last = centreSlot + 1;		// 3 filter sections
		break;

	case 500:

		// implements 7 100 Hz wide sections centered on 1500 Hz  (~500 Hz wide @ - 30dB centered on 1500 Hz)

		first = centreSlot - 3;
		last = centreSlot + 3;		// 7 filter sections
//		first = 12;
//		last = 18;		// 7 filter sections
		break;

	case 1000:
		
		// implements 11 100 Hz wide sections centered on 1500 Hz  (~1000 Hz wide @ - 30dB centered on 1500 Hz)

		first = centreSlot - 5;
		last = centreSlot + 5;		// 11 filter sections
//		first = 10;
//		last = 20;		// 7 filter sections
		break;

	case 2000:
		
		// implements 21 100 Hz wide sections centered on 1500 Hz  (~2000 Hz wide @ - 30dB centered on 1500 Hz)

		first = centreSlot - 10;
		last = centreSlot + 10;		// 21 filter sections
//		first = 5;
//		last = 25;		// 7 filter sections
	}


	for (j = first; j <= last; j++)
	{
		dblZout_0[j] = 0;
		dblZout_1[j] = 0;
		dblZout_2[j] = 0;
	}

	// Initialise the coefficients

	if (dblCoef[last] == 0.0)
	{
		for (i = first; i <= last; i++)
		{
			dblCoef[i] = 2 * dblR * cosf(2 * M_PI * i / intN); // For Frequency = bin i
		}
	}
 }


void SampleSink(short Sample)
{
	//	Filter and send to sound interface

	// This version is passed samples one at a time, as we don't have
	//	enough RAM in embedded systems to hold a full audio frame

	int intFilLen = intN / 2;
	int j;
	float intFilteredSample = 0;			//  Filtered sample

	Sample = Sample * DriveLevel / 100;
	// writing unfiltered tx audio to WAV disabled
	// if (txwfu != NULL)
	//	WriteWav(&Sample, 1, txwfu);
	
	//	We save the previous intN samples
	//	The samples are held in a cyclic buffer

	if (SampleNo < intN)
		dblZin = Sample;
	else 
		dblZin = Sample - dblRn * Last120[Last120Get];

	if (++Last120Get == 121)
		Last120Get = 0;

	//Compute the Comb

	dblZComb = dblZin - dblZin_2 * dblR2;
	dblZin_2 = dblZin_1;
	dblZin_1 = dblZin;

	// Now the resonators
		
	for (j = first; j <= last; j++)	   // calculate output for 3 or 7 resonators 
	{
		dblZout_0[j] = dblZComb + dblCoef[j] * dblZout_1[j] - dblR2 * dblZout_2[j];
		dblZout_2[j] = dblZout_1[j];
		dblZout_1[j] = dblZout_0[j];

		switch (fWidth)
		{
		case 200:

			// scale each by transition coeff and + (Even) or - (Odd) 

			if (SampleNo >= intFilLen)
			{
				if (j == first || j == last)
					intFilteredSample += (float)0.7389f * dblZout_0[j];
				else
					intFilteredSample -= (float)dblZout_0[j];
			}
			break;

		case 500:

			// scale each by transition coeff and + (Even) or - (Odd) 
			// Resonators 6 and 9 scaled by .15 to get best shape and side lobe supression to - 45 dB while keeping BW at 500 Hz @ -26 dB
			// practical range of scaling .05 to .25
			// Scaling also accomodates for the filter "gain" of approx 60. 

			if (SampleNo >= intFilLen)
			{
				if (j == first || j == last)
					intFilteredSample += 0.10601f * dblZout_0[j];
				else if (j == (first + 1) || j == (last - 1))
					intFilteredSample -= 0.59383f * dblZout_0[j];
				else if ((j & 1) == 0)	// 14 15 16
					intFilteredSample += (int)dblZout_0[j];
				else
					intFilteredSample -= (int)dblZout_0[j];
			}
        
			break;
		
		case 1000:

			// scale each by transition coeff and + (Even) or - (Odd) 
			// Resonators 6 and 9 scaled by .15 to get best shape and side lobe supression to - 45 dB while keeping BW at 500 Hz @ -26 dB
			// practical range of scaling .05 to .25
			// Scaling also accomodates for the filter "gain" of approx 60. 
         

			if (SampleNo >= intFilLen)
			{
				if (j == first || j == last)
					intFilteredSample +=  0.377f * dblZout_0[j];
				else if ((j & 1) == 0)	// Even
					intFilteredSample += (int)dblZout_0[j];
				else
					intFilteredSample -= (int)dblZout_0[j];
			}
        
			break;

		case 2000:

			// scale each by transition coeff and + (Even) or - (Odd) 
			// Resonators 6 and 9 scaled by .15 to get best shape and side lobe supression to - 45 dB while keeping BW at 500 Hz @ -26 dB
			// practical range of scaling .05 to .25
			// Scaling also accomodates for the filter "gain" of approx 60. 
          
			if (SampleNo >= intFilLen)
			{
				if (j == first || j == last)
					intFilteredSample +=  0.371f * dblZout_0[j];
				else if ((j & 1) == 0)	// Even
					intFilteredSample += (int)dblZout_0[j];
				else
					intFilteredSample -= (int)dblZout_0[j];
			}
		}
	}

	if (SampleNo >= intFilLen)
	{
		intFilteredSample = intFilteredSample * 0.00833333333f; //  rescales for gain of filter
		largest = max(largest, intFilteredSample);	
		smallest = min(smallest, intFilteredSample);
		
		if (intFilteredSample > 32700)  // Hard clip above 32700
			intFilteredSample = 32700;
		else if (intFilteredSample < -32700)
			intFilteredSample = -32700;

#ifdef TEENSY	
		int work = (short)(intFilteredSample);
		DMABuffer[Number++] = (work + 32768) >> 4; // 12 bit left justify
#else
		DMABuffer[Number++] = (short)intFilteredSample;
#endif
		if (Number == SendSize)
		{
			// send this buffer to sound interface

			DMABuffer = SendtoCard(DMABuffer, SendSize);
			Number = 0;
		}
	}
		
	Last120[Last120Put++] = Sample;

	if (Last120Put == 121)
		Last120Put = 0;

	SampleNo++;
}

extern int dttTimeoutTrip;
#define BREAK 0x23
extern UCHAR bytSessionID;


void Flush()
{
	SoundFlush();
}



// Subroutine to make a CW ID Wave File

void sendCWID(char * strID, BOOL blnPlay)
{
	// This generates a phase synchronous FSK MORSE keying of strID
	// FSK used to maintain VOX on some sound cards
	// Sent at 90% of  max ampllitude

	char strAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/"; 

	//Look up table for strAlphabet...each bit represents one dot time, 3 adjacent dots = 1 dash
	// one dot spacing between dots or dashes

	int intCW[] = {0x17, 0x1D5, 0x75D, 0x75, 0x1, 0x15D, 
           0x1DD, 0x55, 0x5, 0x1777, 0x1D7, 0x175,
           0x77, 0x1D, 0x777, 0x5DD, 0x1DD7, 0x5D, 
           0x15, 0x7, 0x57, 0x157, 0x177, 0x757, 
           0x1D77, 0x775, 0x77777, 0x17777, 0x5777, 0x1577,
           0x557, 0x155, 0x755, 0x1DD5, 0x7775, 0x1DDDD, 0x1D57, 0x1D57};


	float dblHiPhaseInc = 2 * M_PI * 1609.375f / 12000; // 1609.375 Hz High tone
	float dblLoPhaseInc = 2 * M_PI * 1390.625f / 12000; // 1390.625  low tone
	float dblHiPhase = 0;
 	float dblLoPhase = 0;
 	int  intDotSampCnt = 768;  // about 12 WPM or so (should be a multiple of 256
	short intDot[768];
	short intSpace[768];
	int i, k;
	unsigned int j;
	int intAmp = 26000;	   // Selected to have some margin in calculations with 16 bit values (< 32767) this must apply to all filters as well. 
	char * index;
	int intMask;
	int idoffset;
	char gui_frametype[15];

    strlop(strID, '-');		// Remove any SSID    
	snprintf(gui_frametype, sizeof(gui_frametype), "CW.ID.%s", strID);
	wg_send_txframet(0, gui_frametype);

	// Generate the dot samples (high tone) and space samples (low tone) 

	for (i = 0; i < intDotSampCnt; i++)
	{
		if (CWOnOff)
			intSpace[i] = 0;
		else

			intSpace[i] = sin(dblLoPhase) * 0.9 * intAmp;

		intDot[i] = sin(dblHiPhase) * 0.9 * intAmp;
		dblHiPhase += dblHiPhaseInc;
		if (dblHiPhase > 2 * M_PI)
			dblHiPhase -= 2 * M_PI;
		dblLoPhase += dblLoPhaseInc;
		if (dblLoPhase > 2 * M_PI)
			dblLoPhase -= 2 * M_PI;
	}
	
	initFilter(500,1500);  // This keys PTT
   
	//Generate leader for VOX 6 dots long

	for (k = 6; k >0; k--)
		for (i = 0; i < intDotSampCnt; i++)
			SampleSink(intSpace[i]);

	for (j = 0; j < strlen(strID); j++)
	{
		index = strchr(strAlphabet, strID[j]);
		if (index)
			idoffset = index - &strAlphabet[0];
		else
			idoffset = 0;

		intMask = 0x40000000;

		if (index == NULL)
		{
			// process this as a space adding 6 dots worth of space to the wave file

			for (k = 6; k >0; k--)
				for (i = 0; i < intDotSampCnt; i++)
					SampleSink(intSpace[i]);
		}
		else
		{
		while (intMask > 0) //  search for the first non 0 bit
			if (intMask & intCW[idoffset])
				break;	// intMask is pointing to the first non 0 entry
			else
				intMask >>= 1;	//  Right shift mask
				
		while (intMask > 0) //  search for the first non 0 bit
		{
			if (intMask & intCW[idoffset])
				for (i = 0; i < intDotSampCnt; i++)
					SampleSink(intDot[i]);
			else
				for (i = 0; i < intDotSampCnt; i++)
					SampleSink(intSpace[i]);
	
			intMask >>= 1;	//  Right shift mask
		}
		}
			// add 3 dot spaces for inter letter spacing
			for (k = 6; k >0; k--)
				for (i = 0; i < intDotSampCnt; i++)
					SampleSink(intSpace[i]);
	}
	
	//add 3 spaces for the end tail
	
	for (k = 6; k >0; k--)
		for (i = 0; i < intDotSampCnt; i++)
			SampleSink(intSpace[i]);

	SoundFlush();
}


