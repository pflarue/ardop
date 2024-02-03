// ARDOP TNC Host Interface
//

#include "ARDOPC.h"

BOOL blnHostRDY = FALSE;
extern int intFECFramesSent;

void SendData();
BOOL CheckForDisconnect();
int Encode4FSKControl(UCHAR bytFrameType, UCHAR bytSessionID, UCHAR * bytreturn);
int ComputeInterFrameInterval(int intRequestedIntervalMS);
HANDLE OpenCOMPort(VOID * pPort, int speed, BOOL SetDTR, BOOL SetRTS, BOOL Quiet, int Stopbits);
BOOL WriteCOMBlock(HANDLE fd, char * Block, int BytesToWrite);
void SetupGPIOPTT();
VOID ConvertCallstoAX25();
int GetEEPROM(int Reg);
void SaveEEPROM(int reg, int val);

void Break();

extern BOOL NeedID;			// SENDID Command Flag
extern BOOL NeedConReq;		// ARQCALL Command Flag
extern BOOL NeedPing;
extern BOOL PingCount;
extern char ConnectToCall[16];
extern enum _ARQBandwidth CallBandwidth;
extern int PORTT1;			// L2 TIMEOUT
extern int PORTN2;			// RETRIES
extern int extraDelay ;		// Used for long delay paths eg Satellite


#define L2TICK 10			// Timer called every 1/10 sec


int SerialMode = 0;

extern BOOL NeedTwoToneTest;

extern unsigned int lastRTCTick;// Make sure this is set to ticks mod 1000 so RTC incrememtns at mS 0
extern unsigned int RTC;		// set to seconds since 01.01.2000 by DATE and TIME Commands (Dragon log time epoch)


#ifndef WIN32

#define strtok_s strtok_r
#define _strupr strupr

char * strupr(char* s)
{
  char* p = s;

  if (s == 0)
	  return 0;

  while (*p = toupper( *p )) p++;
  return s;
}

int _memicmp(unsigned char *a, unsigned char *b, int n)
{
	if (n)
	{
		while (n && toupper(*a) == toupper(*b))
			n--, a++, b++;

		if (n)
			return toupper(*a) - toupper(*b);
   }
   return 0;
}

#endif

extern int dttTimeoutTrip;
#define BREAK 0x23
extern UCHAR bytSessionID;


//	Subroutine to add data to outbound queue (bytDataToSend)

void AddDataToDataToSend(UCHAR * bytNewData, int Len)
{
	char HostCmd[32];

	if (Len == 0)
		return;

	if ((bytDataToSendLength + Len) >= DATABUFFERSIZE)
		return;			// Flow control has failed

	GetSemaphore();

	memcpy(&bytDataToSend[bytDataToSendLength], bytNewData, Len);
	bytDataToSendLength += Len;

	if (bytDataToSendLength > DATABUFFERSIZE)
		return;

	FreeSemaphore();

	SetLED(TRAFFICLED, TRUE);

	sprintf(HostCmd, "BUFFER %d", bytDataToSendLength);
	QueueCommandToHost(HostCmd);
}

char strFault[100] = "";

VOID DoTrueFalseCmd(char * strCMD, char * ptrParams, BOOL * Value)
{
	char cmdReply[128];

	if (ptrParams == NULL)
	{
		sprintf(cmdReply, "%s %s", strCMD, (*Value) ? "TRUE": "FALSE");
		SendReplyToHost(cmdReply);
		return;
	}
		
	if (strcmp(ptrParams, "TRUE") == 0)
		*Value = TRUE;
	else if (strcmp(ptrParams, "FALSE") == 0)
		*Value = FALSE;
	else
	{
		sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);
		return;
	}
	sprintf(cmdReply, "%s now %s", strCMD, (*Value) ? "TRUE": "FALSE");
	SendReplyToHost(cmdReply);
	return;
}


// Subroutine for processing a command from Host

void ProcessCommandFromHost(char * strCMD)
{
	char * ptrParams;
	char cmdCopy[80] = "";
	char strFault[100] = "";
	char cmdReply[1024];

	strFault[0] = 0;

	strCMD[79] = 0;				// in case cmd handler gets garbage

	memcpy(cmdCopy, strCMD, 79);	// save before we split it up

	_strupr(strCMD);

	if (CommandTrace) WriteDebugLog(LOGDEBUG, "[Command Trace FROM host: %s", strCMD);

	ptrParams = strlop(strCMD, ' ');

	if (strcmp(strCMD, "ABORT") == 0 || strcmp(strCMD, "DD") == 0)
	{
		Abort();
		SendReplyToHost("ABORT");
		goto cmddone;
	}

	if (strcmp(strCMD, "ARQBW") == 0)
	{
		int i;

		if (ptrParams == 0)
		{
			sprintf(cmdReply, "ARQBW %s", ARQBandwidths[ARQBandwidth]);
			SendReplyToHost(cmdReply);
			goto cmddone;
		}
		else
		{
			for (i = 0; i < 8; i++)
			{
				if (strcmp(ptrParams, ARQBandwidths[i]) == 0)
					break;
			}

			if (i == 8)
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);
			else
			{
				ARQBandwidth = i;
				sprintf(cmdReply, "ARQBW now %s", ARQBandwidths[ARQBandwidth]);
				SendReplyToHost(cmdReply);
			}
		}
		goto cmddone;
	}

	if (strcmp(strCMD, "ARQCALL") == 0)
	{
		char * strCallParam = NULL;
		if (ptrParams)
			strCallParam = strlop(ptrParams, ' ');

		if (strCallParam)
		{
			if ((strcmp(ptrParams, "CQ") == 0) || CheckValidCallsignSyntax(ptrParams))
			{
				int param = atoi(strCallParam);

				if (param > 1 && param < 16)
				{
					if (Callsign[0] == 0)
					{
						sprintf(strFault, "MYCALL not Set");
						goto cmddone;
					}
						
					if (ProtocolMode == ARQ)
					{
						ARQConReqRepeats = param;

						NeedConReq =  TRUE;
						strcpy(ConnectToCall, ptrParams);
						SendReplyToHost(cmdCopy);
						goto cmddone;
					}
					if (ProtocolMode == RXO)
						sprintf(strFault, "Not from mode RXO");
					else
						sprintf(strFault, "Not from mode FEC");
					goto cmddone;
				}
			}
		}
		sprintf(strFault, "Syntax Err: %s", cmdCopy);
		goto cmddone;
	}

	if (strcmp(strCMD, "ARQTIMEOUT") == 0)
	{
		int i;

		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %d", strCMD, ARQTimeout);
			SendReplyToHost(cmdReply);
			goto cmddone;
		}
		else
		{
			i = atoi(ptrParams);

			if (i > 29 && i < 241)	
			{
				ARQTimeout = i;
				sprintf(cmdReply, "%s now %d", strCMD, ARQTimeout);
				SendReplyToHost(cmdReply);
			}
			else
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);	
		}
		goto cmddone;
	}
   
	if (strcmp(strCMD, "AUTOBREAK") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &AutoBreak);
		goto cmddone;
	}

	if (strcmp(strCMD, "BREAK") == 0)
	{
		Break();
		goto cmddone;  
	}

	if (strcmp(strCMD, "BUFFER") == 0)
	{
		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %d", strCMD, bytDataToSendLength);
			SendReplyToHost(cmdReply);
		}
		else
			sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);

		goto cmddone;
	}

	if (strcmp(strCMD, "BUSYBLOCK") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &BusyBlock);
		goto cmddone;
	}
 
	if (strcmp(strCMD, "BUSYDET") == 0)
	{
		int i;

		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %d", strCMD, BusyDet);
			SendReplyToHost(cmdReply);
		}
		else
		{
			i = atoi(ptrParams);

			if (i >= 0  && i <= 10)
			{
				BusyDet = i;
				sprintf(cmdReply, "%s now %d", strCMD, BusyDet);
				SendReplyToHost(cmdReply);

			}
			else
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);	
		}
		goto cmddone;
	}

	if (strcmp(strCMD, "CALLBW") == 0)
	{
		int i;

		if (ptrParams == 0)
		{
			sprintf(cmdReply, "CALLBW %s", ARQBandwidths[CallBandwidth]);
			SendReplyToHost(cmdReply);
			goto cmddone;
		}
		else
		{
			for (i = 0; i < 9; i++)
			{
				if (strcmp(ptrParams, ARQBandwidths[i]) == 0)
					break;
			}

			if (i == 9)
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);
			else
			{
				CallBandwidth = i;
				sprintf(cmdReply, "CALLBW now %s", ARQBandwidths[CallBandwidth]);
				SendReplyToHost(cmdReply);
			}
		}
		goto cmddone;
	}

	if (strcmp(strCMD, "CAPTURE") == 0)
	{
		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %s", strCMD, CaptureDevice);
			SendReplyToHost(cmdReply);
		}
		else
		{
			// Can't change on Teensy
#ifndef Teensy
			strcpy(CaptureDevice, ptrParams);
#endif
			sprintf(cmdReply, "%s now %s", strCMD, CaptureDevice);
			SendReplyToHost(cmdReply);
		}
		goto cmddone;
	}
     
	if (strcmp(strCMD, "CAPTUREDEVICES") == 0)
	{
		sprintf(cmdReply, "%s %s", strCMD, CaptureDevices);
		SendReplyToHost(cmdReply);
		goto cmddone;
	}

	if (strcmp(strCMD, "CL") == 0)		// For PTC Emulator
	{
		ClearDataToSend();
		goto cmddone;
	}


	if (strcmp(strCMD, "CLOSE") == 0)
	{
		blnClosing = TRUE;
		goto cmddone;
	}

	if (strcmp(strCMD, "CMDTRACE") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &CommandTrace);
		goto cmddone;
	}

	if (strcmp(strCMD, "CODEC") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &blnCodecStarted);
		
		if (strcmp(ptrParams, "TRUE") == 0)
			StartCodec(strFault);
		else if (strcmp(ptrParams, "FALSE") == 0)
			StopCodec(strFault);
	
		goto cmddone;
	}

	if (strcmp(strCMD, "CONSOLELOG") == 0)
	{
		int i;

		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %d", strCMD, ConsoleLogLevel);
			SendReplyToHost(cmdReply);
		}
		else
		{
			i = atoi(ptrParams);

			if (i >= LOGEMERGENCY  && i <= LOGDEBUG)
			{
				ConsoleLogLevel = i;
				sprintf(cmdReply, "%s now %d", strCMD, ConsoleLogLevel);
				SendReplyToHost(cmdReply);
			}
			else
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);	
		}
		goto cmddone;
	}



	if (strcmp(strCMD, "CWID") == 0)
	{
		if (ptrParams == NULL)
		{
			if (wantCWID)
				if	(CWOnOff)
					sprintf(cmdReply, "CWID ONOFF");
				else
					sprintf(cmdReply, "CWID TRUE");
			else
				sprintf(cmdReply, "CWID FALSE");

			SendReplyToHost(cmdReply);
			goto cmddone;
		}
		
		if (strcmp(ptrParams, "TRUE") == 0)
		{
			wantCWID = TRUE;
			CWOnOff = FALSE;
		}
		else 
		if (strcmp(ptrParams, "FALSE") == 0)
			wantCWID = FALSE;
		else 
		if (strcmp(ptrParams, "ONOFF") == 0)
		{
			wantCWID = TRUE;
			CWOnOff = TRUE;
		}
		else
		{
			sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);
			goto cmddone;
		}
			
		if (wantCWID)
			if	(CWOnOff)
				sprintf(cmdReply, "CWID now ONOFF");
			else
				sprintf(cmdReply, "CWID now TRUE");
		else
			sprintf(cmdReply, "CWID now FALSE");

		SendReplyToHost(cmdReply);
		goto cmddone;
	}

	if (strcmp(strCMD, "DATATOSEND") == 0)
	{
		int i;

		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %d", strCMD, bytDataToSendLength);
			SendReplyToHost(cmdReply);
			goto cmddone;
		}
		else
		{
			i = atoi(ptrParams);

			if (i == 0)	
			{
				bytDataToSendLength = 0;
				sprintf(cmdReply, "%s now %d", strCMD, bytDataToSendLength);
				SendReplyToHost(cmdReply);
			}
			else
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);	
		}
		goto cmddone;
	}

  
	if (strcmp(strCMD, "DATETIME") == 0)
	{
#ifndef TEENSY
 		sprintf(strFault, "DATETIME not supported on this platform");
#else
		// Set RTC to Date (DD MM YY HH MM SS format)

		struct tm tm;
		int n; 

		if (ptrParams == NULL || strlen(ptrParams) != 17)
		{
			sprintf(strFault, "Syntax Err: %s", cmdCopy);	
			goto cmddone;
		}

		n = sscanf(ptrParams, "%d %d %d %d %d %d",
			&tm.tm_mday, &tm.tm_mon, &tm.tm_year,
			&tm.tm_hour, &tm.tm_min, &tm.tm_sec);

		if (n != 6)
		{
			sprintf(strFault, "Syntax Err: %s", cmdCopy);	
			goto cmddone;
		}

		tm.tm_year += 100;
		tm.tm_mon--;

		RTC = mktime(&tm);
		sprintf(cmdReply, "RTC set");
		RTC -= 946684800;	// Adjust to start from 1/1/2000

		SendReplyToHost(cmdReply);
#endif
		goto cmddone;
	}
  
	if (strcmp(strCMD, "DEBUGLOG") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &DebugLog);
		goto cmddone;
	}

	if (strcmp(strCMD, "DISCONNECT") == 0)
	{
		if (ProtocolState == IDLE || ProtocolState == IRS || ProtocolState == ISS || ProtocolState == IRStoISS)
		{
			blnARQDisconnect = TRUE;
			SendReplyToHost("DISCONNECT NOW TRUE");
		}
		else
			SendReplyToHost("DISCONNECT IGNORED");

		goto cmddone;
	}

	if (strcmp(strCMD, "DRIVELEVEL") == 0)
	{
		int i;

		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %d", strCMD, DriveLevel);
			SendReplyToHost(cmdReply);
			goto cmddone;
		}
		else
		{
			i = atoi(ptrParams);

			if (i >= 0 && i <= 100)
			{
				DriveLevel = i;
				sprintf(cmdReply, "%s now %d", strCMD, DriveLevel);
				SendReplyToHost(cmdReply);
				goto cmddone;
			}
			else
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);	
		}
		goto cmddone;
	}

#ifdef TEENSY

	if (strcmp(strCMD, "EEPROM") == 0)
	{
		if (ptrParams)
		{
			char * ptr, * context;
			int Reg = 0, Val = 0;

			ptr = strtok_s(ptrParams, ", ", &context);

			if (ptr)
			{
				Reg = atoi(ptr);
				ptr = strtok_s(NULL, ", ", &context);
				if (ptr)
				{
					Val = atoi(ptr);
				}
			}

			if (Reg == 0 || Reg == 12 || Reg > 14 || ptr == 0)
			{
				// Bad command
		
				sprintf(strFault, "Syntax Err: %s", cmdCopy);
				goto cmddone;
			}

			SaveEEPROM(Reg, Val);
		}

		// Display EEPROM Settings

		sprintf(cmdReply, "01 TXDelay - Zero means use ADC %3d\n", GetEEPROM(1));
		SendReplyToHost(cmdReply);
		sprintf(cmdReply, "02 Persistance                  %3d\n", GetEEPROM(2));
		SendReplyToHost(cmdReply);
		sprintf(cmdReply, "03 Slottime (in 10 mS)          %3d\n", GetEEPROM(3));
		SendReplyToHost(cmdReply);
		sprintf(cmdReply, "04 TXTail                       %3d\n", GetEEPROM(4));
		SendReplyToHost(cmdReply);
		sprintf(cmdReply, "05 Full Duplex - Not used       %3d\n", GetEEPROM(5));
		SendReplyToHost(cmdReply);
		sprintf(cmdReply, "06 Our Channel (Hex)             %02x\n", GetEEPROM(6));
		SendReplyToHost(cmdReply);
		sprintf(cmdReply, "07 I2C Address (0 = async) Hex   %02x\n", GetEEPROM(7));
		SendReplyToHost(cmdReply);
		sprintf(cmdReply, "08 Mode Speed                  %4d\n", GetEEPROM(8) * 100);
		SendReplyToHost(cmdReply);
		sprintf(cmdReply, "09 RX Level (Config)            %3d\n", GetEEPROM(9));
		SendReplyToHost(cmdReply);
		sprintf(cmdReply, "10 TX Level                     %3d\n", GetEEPROM(10));
		SendReplyToHost(cmdReply);
		sprintf(cmdReply, "11 RX Level (Actual)            %3d\n", GetEEPROM(11));
		SendReplyToHost(cmdReply);
		sprintf(cmdReply, "13 Centre Freq                 %4d\n", GetEEPROM(13) * 10);
		SendReplyToHost(cmdReply);
		sprintf(cmdReply, "14 TNC Mode                       %c\n", GetEEPROM(14));
		SendReplyToHost(cmdReply);
		goto cmddone;			
	}

#endif
	
	if (strcmp(strCMD, "ENABLEPINGACK") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &EnablePingAck);
		goto cmddone;
	}

	if (strcmp(strCMD, "EXTRADELAY") == 0)
	{
		int i;

		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %d", strCMD, extraDelay);
			SendReplyToHost(cmdReply);
			goto cmddone;
		}
		else
		{
			i = atoi(ptrParams);

			if (i >= 0)	
			{
				extraDelay = i;
				sprintf(cmdReply, "%s now %d", strCMD, extraDelay);
				SendReplyToHost(cmdReply);
			}
			else
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);	
		}
		goto cmddone;
	}
 
	if (strcmp(strCMD, "FECID") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &FECId);
		goto cmddone;
	}

	if (strcmp(strCMD, "FASTSTART") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &fastStart);
		goto cmddone;
	}

	if (strcmp(strCMD, "FECMODE") == 0)
	{
		int i;

		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %s", strCMD, strFECMode);
			SendReplyToHost(cmdReply);
		}
		else
		{
			for (i = 0;  i < strAllDataModesLen; i++)
			{
				if (strcmp(ptrParams, strAllDataModes[i]) == 0)
				{
					strcpy(strFECMode, ptrParams);
					intFECFramesSent = 0;		// Force mode to be reevaluated
					sprintf(cmdReply, "%s now %s", strCMD, strFECMode);
					SendReplyToHost(cmdReply);
					goto cmddone;
				}
			}
			sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);
		}	
		goto cmddone;
	}
		
	if (strcmp(strCMD, "FECREPEATS") == 0)
	{
		int i;

		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %d", strCMD, FECRepeats);
			SendReplyToHost(cmdReply);
		}
		else
		{
			i = atoi(ptrParams);

			if (i >= 0 && i <= 5)	
			{
				FECRepeats = i;
				sprintf(cmdReply, "%s now %d", strCMD, FECRepeats);
				SendReplyToHost(cmdReply);
			}
			else
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);	
		}
		goto cmddone;
	}

	if (strcmp(strCMD, "FECSEND") == 0)
	{
		if (ptrParams == 0)
		{
			sprintf(strFault, "Syntax Err: %s", strCMD);	
			goto cmddone;
		}
		if (strcmp(ptrParams, "TRUE") == 0)
		{
			WriteDebugLog(LOGDEBUG, "FECRepeats %d", FECRepeats);

			StartFEC(NULL, 0, strFECMode, FECRepeats, FECId);
			SendReplyToHost("FECSEND now TRUE");
		}
		else if (strcmp(ptrParams, "FALSE") == 0)
		{
			blnAbort = TRUE;
			SendReplyToHost("FECSEND now FALSE");
		}
		else
			sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);

		goto cmddone;
	}

	if (strcmp(strCMD, "FSKONLY") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &FSKOnly);
		goto cmddone;
	}

	if (strcmp(strCMD, "GRIDSQUARE") == 0)
	{
		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %s", strCMD, GridSquare);
			SendReplyToHost(cmdReply);
		}
		else
			if (CheckGSSyntax(ptrParams))
			{
				strcpy(GridSquare, ptrParams);
				sprintf(cmdReply, "%s now %s", strCMD, GridSquare);
				SendReplyToHost(cmdReply);
			}
			else
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);

		goto cmddone;
	}

	if (strcmp(strCMD, "INITIALIZE") == 0)
	{
		blnInitializing = TRUE;
		ClearDataToSend();
		blnHostRDY = TRUE;
		blnInitializing = FALSE;

		SendReplyToHost("INITIALIZE");
		goto cmddone;
	}

	if (strcmp(strCMD, "LEADER") == 0)
	{
		int i;

		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %d", strCMD, LeaderLength);
			SendReplyToHost(cmdReply);
		}
		else
		{
			i = atoi(ptrParams);

			if (i >= 120  && i <= 2500)
			{
				LeaderLength = (i + 9) /10;
				LeaderLength *= 10;				// round to 10 mS
				sprintf(cmdReply, "%s now %d", strCMD, LeaderLength);
				SendReplyToHost(cmdReply);
			}
			else
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);	
		}
		goto cmddone;
	}

	if (strcmp(strCMD, "LISTEN") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &blnListen);

		if (blnListen)
			ClearBusy();

		goto cmddone;
	}

	if (strcmp(strCMD, "LOGLEVEL") == 0)
	{
		int i;

		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %d", strCMD, FileLogLevel);
			SendReplyToHost(cmdReply);
		}
		else
		{
			i = atoi(ptrParams);

			if (i >= LOGEMERGENCY  && i <= LOGDEBUG)
			{
				FileLogLevel = i;
				sprintf(cmdReply, "%s now %d", strCMD, FileLogLevel);
				SendReplyToHost(cmdReply);
			}
			else
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);	
		}
		goto cmddone;
	}

	if (strcmp(strCMD, "MONITOR") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &Monitor);
		goto cmddone;
	}

	if (strcmp(strCMD, "MYAUX") == 0)
	{
		int i, len;
		char * ptr, * context;

		if (ptrParams == 0)
		{
			len = sprintf(cmdReply, "%s", "MYAUX ");

			for (i = 0; i < AuxCallsLength; i++)
			{
				len += sprintf(&cmdReply[len], "%s,", AuxCalls[i]);
			}
			cmdReply[len - 1] = 0;	// remove trailing space or ,
			SendReplyToHost(cmdReply);	
			goto cmddone;
		}

		ptr = strtok_s(ptrParams, ", ", &context);

		AuxCallsLength = 0;

		while (ptr && AuxCallsLength < 10)
		{
			if (CheckValidCallsignSyntax(ptr))
				strcpy(AuxCalls[AuxCallsLength++], ptr);

			ptr = strtok_s(NULL, ", ", &context);
		}

		len = sprintf(cmdReply, "%s", "MYAUX now ");
		for (i = 0; i < AuxCallsLength; i++)
		{
			len += sprintf(&cmdReply[len], "%s,", AuxCalls[i]);
		}
		cmdReply[len - 1] = 0;	// remove trailing space or ,
		SendReplyToHost(cmdReply);	
		ConvertCallstoAX25();

		goto cmddone;
	}

	if (strcmp(strCMD, "MYCALL") == 0)
	{
		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %s", strCMD, Callsign);
			SendReplyToHost(cmdReply);
		}
		else
		{
			if (CheckValidCallsignSyntax(ptrParams))
			{
				strcpy(Callsign, ptrParams);
				sprintf(cmdReply, "%s now %s", strCMD, Callsign);
				SendReplyToHost(cmdReply);
				ConvertCallstoAX25();
			}
			else
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);
		}
		goto cmddone;
	}

//	if (strcmp(strCMD, "NEGOTIATEBW") == 0)
//	{
//		DoTrueFalseCmd(strCMD, ptrParams, &NegotiateBW);
//		goto cmddone;
//	}

	if (strcmp(strCMD, "PING") == 0)
	{
		if (ptrParams)
		{
			char * countp = strlop(ptrParams, ' ');
			int count = 0;

			if (countp)
				count = atoi(countp);

			if (CheckValidCallsignSyntax(ptrParams) && count > 0 && count < 16)
			{
				if (ProtocolState != DISC)
				{
					sprintf(strFault, "No PING from state %s",  ARDOPStates[ProtocolState]);
					goto cmddone;
				}
				else
				{
					SendReplyToHost(cmdCopy); // echo command back to host.
					strcpy(ConnectToCall, _strupr(ptrParams));
					PingCount = count;
					NeedPing = TRUE;			// request ping grom background
					goto cmddone;
				}
			}
		}

		sprintf(strFault, "Syntax Err: %s", cmdCopy);

		goto cmddone;
	}

	if (strcmp(strCMD, "PAC") == 0)
	{
		// Packet Mode Subcommands

//extern int pktNumCar;
//extern int pktDataLen;
//extern int pktRSLen;
//extern char pktMod[4][8];
//extern int pktMode;


		if (ptrParams)
		{
			char * PacVal = strlop(ptrParams, ' ');

			if (strcmp(ptrParams, "MODE") == 0)
			{
				int i;

				if (PacVal == NULL)
				{
					sprintf(cmdReply, "PAC MODE %s", &pktMod[initMode][0]);
					SendReplyToHost(cmdReply);
					goto cmddone;
				}

				for (i = 0; i < pktModeLen; i++)
				{
					if (strcmp(PacVal, &pktMod[i][0]) == 0)
					{
						initMode = i;
						sprintf(cmdReply, "PAC MODE now %s", PacVal);
						SendReplyToHost(cmdReply);
						goto cmddone;
					}
				}
	
				sprintf(strFault, "Syntax Err: PAC MODE %s", PacVal);
				goto cmddone;
			}

			if (strcmp(ptrParams, "RETRIES") == 0)
			{
				int i;

				if (PacVal == NULL)
				{
					sprintf(cmdReply, "PAC RETRIES %d", PORTN2);
					SendReplyToHost(cmdReply);
					goto cmddone;
				}

				i = atoi(PacVal);
				
				if (i >= 3  && i <= 30)
					PORTN2 = i;

				sprintf(cmdReply, "PAC RETRIES now %d", PORTN2);
				SendReplyToHost(cmdReply);
				goto cmddone;
			}

			//PORTT1 = 4 * L2TICK

			if (strcmp(ptrParams, "FRACK") == 0)
			{
				int i;

				if (PacVal == NULL)
				{
					sprintf(cmdReply, "PAC FRACK %d", PORTT1 / L2TICK);
					SendReplyToHost(cmdReply);
					goto cmddone;
				}

				i = atoi(PacVal);
				
				if (i >= 2  && i <= 15)
					PORTT1 = i * L2TICK;

				sprintf(cmdReply, "PAC FRACK now %d",  PORTT1 / L2TICK);
				SendReplyToHost(cmdReply);
				goto cmddone;
			}

			SendReplyToHost(_strupr(cmdCopy)); // echo command back to host.
			goto cmddone;
		}

		sprintf(strFault, "Syntax Err: %s", cmdCopy);

		goto cmddone;
	}


	if (strcmp(strCMD, "PLAYBACK") == 0)
	{
		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %s", strCMD, PlaybackDevice);
			SendReplyToHost(cmdReply);
		}
		else
		{
			// Can't change sound devices on Teensy
#ifndef Teensy
			strcpy(PlaybackDevice, ptrParams);
#endif
			sprintf(cmdReply, "%s now %s", strCMD, PlaybackDevice);
			SendReplyToHost(cmdReply);
		}
		goto cmddone;
	}
     
	if (strcmp(strCMD, "PLAYBACKDEVICES") == 0)
	{
		sprintf(cmdReply, "%s %s", strCMD, PlaybackDevices);
		SendReplyToHost(cmdReply);
		goto cmddone;
	}

	if (strcmp(strCMD, "PROTOCOLMODE") == 0)
	{
		if (ptrParams == NULL)
		{
			if (ProtocolMode == ARQ)
				sprintf(cmdReply, "PROTOCOLMODE ARQ");
			else
			if (ProtocolMode == RXO)
				sprintf(cmdReply, "PROTOCOLMODE RXO");
			else
				sprintf(cmdReply, "PROTOCOLMODE FEC");

			SendReplyToHost(cmdReply);
			goto cmddone;
		}
		
		if (strcmp(ptrParams, "ARQ") == 0)
			ProtocolMode = ARQ;
		else 
		if (strcmp(ptrParams, "RXO") == 0)
			ProtocolMode = RXO;
		else
		if (strcmp(ptrParams, "FEC") == 0)
			ProtocolMode = FEC;
		else
		{
			sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);
			goto cmddone;
		}
		if (ProtocolMode == ARQ)
			sprintf(cmdReply, "PROTOCOLMODE now ARQ");
		else
		if (ProtocolMode == RXO)
			sprintf(cmdReply, "PROTOCOLMODE now RXO");
		else
			sprintf(cmdReply, "PROTOCOLMODE now FEC");

		SendReplyToHost(cmdReply);

		SetARDOPProtocolState(DISC);	// set state to DISC on any Protocol mode change. 
		goto cmddone;
	}

	if (strcmp(strCMD, "PURGEBUFFER") == 0)
	{
		ClearDataToSend();  // Should precipitate an asynchonous BUFFER 0 reponse. 

		SendReplyToHost(strCMD);
		goto cmddone;  
	}

/*
            Case "RADIOANT"
                If ptrSpace = -1 Then
                    SendReplyToHost(strCommand & " " & RCB.Ant.ToString)
                ElseIf strParameters = "0" Or strParameters = "1" Or strParameters = "2" Then
                    RCB.Ant = CInt(strParameters)
                Else
                    strFault = "Syntax Err:" & strCMD
                End If
            Case "RADIOCTRL"
                If ptrSpace = -1 Then
                    SendReplyToHost(strCommand & " " & RCB.RadioControl.ToString)
                ElseIf strParameters = "TRUE" Then
                    If IsNothing(objMain.objRadio) Then
                        objMain.SetNewRadio()
                        objMain.objRadio.InitRadioPorts()
                    End If
                    RCB.RadioControl = CBool(strParameters)
                ElseIf strParameters = "FALSE" Then
                    If Not IsNothing(objMain.objRadio) Then
                        objMain.objRadio = Nothing
                    End If
                    RCB.RadioControl = CBool(strParameters)
                Else
                    strFault = "Syntax Err:" & strCMD
                End If
*/
	if (strcmp(strCMD, "RADIOFREQ") == 0)
	{
		// Currently only used for setting GUI Freq	 field

		if (ptrParams == NULL)
		{
			sprintf(strFault, "RADIOFREQ command string missing");
			goto cmddone;
		}
		
		SendtoGUI('F', ptrParams, strlen(ptrParams));
		goto cmddone;
	}

	if (strcmp(strCMD, "RADIOHEX") == 0)
	{
#ifdef TEENSY
 		sprintf(strFault, "RADIOHEX not supported on this platform");
#else
		// Parameter is block to send to radio, in hex
		
		char c;
		int val;
		char * ptr1 = ptrParams;
		char * ptr2 = ptrParams;

		if (ptrParams == NULL)
		{
			sprintf(strFault, "RADIOHEX command string missing");
			goto cmddone;
		}
		if (hCATDevice)
		{
			while (c = *(ptr1++))
			{
				val = c - 0x30;
				if (val > 15) val -= 7;
				val <<= 4;
				c = *(ptr1++) - 0x30;
				if (c > 15) c -= 7;
				val |= c;
				*(ptr2++) = val;
			}
			WriteCOMBlock(hCATDevice, ptrParams, ptr2 - ptrParams);
			EnableHostCATRX = TRUE;
		}
#endif	
		goto cmddone;
	}

/*
            Case "RADIOICOMADD"
                If ptrSpace = -1 Then
                    SendReplyToHost(strCommand & " " & RCB.IcomAdd)
                ElseIf strParameters.Length = 2 AndAlso ("0123456789ABCDEF".IndexOf(strParameters(0)) <> -1) AndAlso _
                        ("0123456789ABCDEF".IndexOf(strParameters(1)) <> -1) Then
                    RCB.IcomAdd = strParameters
                Else
                    strFault = "Syntax Err:" & strCMD
                End If
            Case "RADIOISC"
                If ptrSpace = -1 Then
                    SendReplyToHost(strCommand & " " & RCB.InternalSoundCard)
                ElseIf strParameters = "TRUE" Or strParameters = "FALSE" Then
                    RCB.InternalSoundCard = CBool(strParameters)
                Else
                    strFault = "Syntax Err:" & strCMD
                End If
            Case "RADIOMENU"
                If ptrSpace = -1 Then
                    SendReplyToHost(strCommand & " " & objMain.RadioMenu.Enabled.ToString)
                ElseIf strParameters = "TRUE" Or strParameters = "FALSE" Then
                    objMain.RadioMenu.Enabled = CBool(strParameters)
                Else
                    strFault = "Syntax Err:" & strCMD
                End If
            Case "RADIOMODE"
                If ptrSpace = -1 Then
                    SendReplyToHost(strCommand & " " & RCB.Mode)
                ElseIf strParameters = "USB" Or strParameters = "USBD" Or strParameters = "FM" Then
                    RCB.Mode = strParameters
                Else
                    strFault = "Syntax Err:" & strCMD
                End If
            Case "RADIOMODEL"
                If ptrSpace = -1 Then
                    SendReplyToHost(strCommand & " " & RCB.Model)
                Else
                    Dim strRadios() As String = objMain.objRadio.strSupportedRadios.Split(",")
                    Dim strRadioModel As String = ""
                    For Each strModel As String In strRadios
                        If strModel.ToUpper = strParameters.ToUpper Then
                            strRadioModel = strParameters
                            Exit For
                        End If
                    Next
                    If strRadioModel.Length > 0 Then
                        RCB.Model = strParameters
                    Else
                        strFault = "Model not supported :" & strCMD
                    End If
                End If

            Case "RADIOMODELS"
                If ptrSpace = -1 And Not IsNothing(objMain.objRadio) Then
                    ' Send a comma delimited list of models?
                    SendReplyToHost(strCommand & " " & objMain.objRadio.strSupportedRadios) ' Need to insure this isn't too long for Interfaces:
                Else
                    strFault = "Syntax Err:" & strCMD
                End If
				*/

/*

             Case "RADIOPTTDTR"
                If ptrSpace = -1 Then
                    SendReplyToHost(strCommand & " " & RCB.PTTDTR.ToString)
                ElseIf strParameters = "TRUE" Or strParameters = "FALSE" Then
                    RCB.PTTDTR = CBool(strParameters)
                    objMain.objRadio.InitRadioPorts()
                Else
                    strFault = "Syntax Err:" & strCMD
                End If
            Case "RADIOPTTRTS"
                If ptrSpace = -1 Then
                    SendReplyToHost(strCommand & " " & RCB.PTTRTS.ToString)
                ElseIf strParameters = "TRUE" Or strParameters = "FALSE" Then
                    RCB.PTTRTS = CBool(strParameters)
                    objMain.objRadio.InitRadioPorts()
                Else
                    strFault = "Syntax Err:" & strCMD
                End If
                ' End of optional Radio Commands
*/


	if (strcmp(strCMD, "RADIOPTTOFF") == 0)
	{
		// Parameter is block to send to radio to disable PTT, in hex
		
		char c;
		int val;
		UCHAR * ptr1 = ptrParams;
		UCHAR * ptr2 = PTTOffCmd;

		if (ptrParams == NULL)
		{
			sprintf(strFault, "RADIOPTTOFF command string missing");
			goto cmddone;
		}

		if (hCATDevice == 0)
		{
			sprintf(strFault, "RADIOPTTOFF command CAT Port not defined");
			goto cmddone;
		}

		while (c = *(ptr1++))
		{
			val = c - 0x30;
			if (val > 15) val -= 7;
			val <<= 4;
			c = *(ptr1++) - 0x30;
			if (c > 15) c -= 7;
			val |= c;
			*(ptr2++) = val;
		}	
		PTTOffCmdLen = ptr2 - PTTOffCmd;
		PTTMode = PTTCI_V;
		RadioControl = TRUE;

		sprintf(cmdReply, "CAT PTT Off Command Defined");
		SendReplyToHost(cmdReply);
		goto cmddone;
	}

	if (strcmp(strCMD, "RADIOPTTON") == 0)
	{
		// Parameter is block to send to radio to enable PTT, in hex
		
		char c;
		int val;
		char * ptr1 = ptrParams;
		UCHAR * ptr2 = PTTOnCmd;

		if (ptrParams == NULL)
		{
			sprintf(strFault, "RADIOPTTON command string missing");
			goto cmddone;
		}

		if (hCATDevice == 0)
		{
			sprintf(strFault, "RADIOPTTON command CAT Port not defined");
			goto cmddone;
		}

		while (c = *(ptr1++))
		{
			val = c - 0x30;
			if (val > 15) val -= 7;
			val <<= 4;
			c = *(ptr1++) - 0x30;
			if (c > 15) c -= 7;
			val |= c;
			*(ptr2++) = val;
		}

		PTTOnCmdLen = ptr2 - PTTOnCmd;

		sprintf(cmdReply, "CAT PTT On Command Defined");
		SendReplyToHost(cmdReply);
		goto cmddone;
	}


	if (strcmp(strCMD, "RXLEVEL") == 0)
	{
#ifndef HASPOTS
		sprintf(cmdReply, "RXLEVEL command not available on this platform");
		SendReplyToHost(cmdReply);
		goto cmddone;
#else
		int i;

		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %d", strCMD, RXLevel);
			SendReplyToHost(cmdReply);
			goto cmddone;
		}
		else
		{
			i = atoi(ptrParams);

			if (i >= 0 && i <= 3000)
			{
				int Pot;
				RXLevel = i;
				AdjustRXLevel(RXLevel);
				sprintf(cmdReply, "%s now %d", strCMD, RXLevel);
				SendReplyToHost(cmdReply);
			}
			else
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);	
		}
		goto cmddone;
#endif
	}

	if (strcmp(strCMD, "SENDID") == 0)
	{
		if (ProtocolState == DISC)
		{
			NeedID = TRUE;			// Send from background
			SendReplyToHost(strCMD);
		}
		else
			sprintf(strFault, "Not from State %s", ARDOPStates[ProtocolState]);

		goto cmddone;
	}

/*
            Case "SETUPMENU"
                If ptrSpace = -1 Then
                    SendReplyToHost(strCommand & " " & objMain.SetupMenu.Enabled.ToString)
                ElseIf strParameters = "TRUE" Or strParameters = "FALSE" Then
                    objMain.SetupMenu.Enabled = CBool(strParameters)
                Else
                    strFault = "Syntax Err:" & strCMD
                End If

*/

	if (strcmp(strCMD, "SQUELCH") == 0)
	{
		int i;

		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %d", strCMD, Squelch);
			SendReplyToHost(cmdReply);
		}
		else
		{
			i = atoi(ptrParams);

			if (i >= 1  && i <= 10)
			{
				Squelch = i;
				sprintf(cmdReply, "%s now %d", strCMD, Squelch);
				SendReplyToHost(cmdReply);
			}
			else
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);	
		}
		goto cmddone;
	}


	if (strcmp(strCMD, "STATE") == 0)
	{
		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %s", strCMD, ARDOPStates[ProtocolState]);
			SendReplyToHost(cmdReply);
		}
		else
			sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);

		goto cmddone;
	}

	if (strcmp(strCMD, "TRAILER") == 0)
	{
		int i;

		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %d", strCMD, TrailerLength);
			SendReplyToHost(cmdReply);
		}
		else
		{
			i = atoi(ptrParams);

			if (i >= 0  && i <= 200)
			{
				TrailerLength = (i + 9) /10;
				TrailerLength *= 10;				// round to 10 mS
				
				sprintf(cmdReply, "%s now %d", strCMD, TrailerLength);
				SendReplyToHost(cmdReply);
			}
			else
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);	
		}
		goto cmddone;
	}

	if (strcmp(strCMD, "TWOTONETEST") == 0)
	{
       if (ProtocolState == DISC)
	   {
		   NeedTwoToneTest = TRUE;			// Send from background
		   SendReplyToHost(strCMD);
	   }
	   else
		   sprintf(strFault, "Not from state %s", ARDOPStates[ProtocolState]);
       
		   goto cmddone;

	}



	if (strcmp(strCMD, "TUNINGRANGE") == 0)
	{
		int i;

		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %d", strCMD, TuningRange);
			SendReplyToHost(cmdReply);
			goto cmddone;
		}
		else
		{
			i = atoi(ptrParams);

			if (i >= 0 && i <= 200)	
			{
				TuningRange = i;
				sprintf(cmdReply, "%s now %d", strCMD, TuningRange);
				SendReplyToHost(cmdReply);
			}
			else
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);	
		}
		goto cmddone;
	}

	if (strcmp(strCMD, "TXLEVEL") == 0)
	{
#ifndef HASPOTS
		sprintf(cmdReply, "TXLEVEL command not available on this platform");
		SendReplyToHost(cmdReply);
		goto cmddone;
#else
		int i;

		if (ptrParams == 0)
		{
			sprintf(cmdReply, "%s %d", strCMD, TXLevel);
			SendReplyToHost(cmdReply);
			goto cmddone;
		}
		else
		{
			i = atoi(ptrParams);

			if (i >= 0 && i <= 3000)	
			{
				int Pot;
				TXLevel = i;
				sprintf(cmdReply, "%s now %d", strCMD, i);
				AdjustTXLevel(TXLevel);
				SendReplyToHost(cmdReply);			}
			else
				sprintf(strFault, "Syntax Err: %s %s", strCMD, ptrParams);	
		}
		goto cmddone;
#endif
	}

	if (strcmp(strCMD, "USE600MODES") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &Use600Modes);
		goto cmddone;
	}

	if (strcmp(strCMD, "VERSION") == 0)
	{
		sprintf(cmdReply, "VERSION %s_%s", ProductName, ProductVersion);
		SendReplyToHost(cmdReply);
		goto cmddone;
	} 
	// RDY processed earlier Case "RDY" ' no response required for RDY

	sprintf(strFault, "CMD %s not recoginized", strCMD);

cmddone:

	if (strFault[0])
	{
		//Logs.Exception("[ProcessCommandFromHost] Cmd Rcvd=" & strCommand & "   Fault=" & strFault)
		sprintf(cmdReply, "FAULT %s", strFault);
		SendReplyToHost(cmdReply);
	}
//	SendCommandToHost("RDY");		// signals host a new command may be sent
}

//	Function to send a text command to the Host

void SendCommandToHost(char * strText)
{
	if (SerialMode)
		SCSSendCommandToHost(strText);
	else
		TCPSendCommandToHost(strText);
}


void SendCommandToHostQuiet(char * strText)		// Higher Debug Level for PTT
{
	if (SerialMode)
		SCSSendCommandToHostQuiet(strText);
	else
		TCPSendCommandToHostQuiet(strText);
}

void QueueCommandToHost(char * strText)
{
	if (SerialMode)
		SCSQueueCommandToHost(strText);
	else
		TCPQueueCommandToHost(strText);
}

void SendReplyToHost(char * strText)
{
	if (SerialMode)
		SCSSendReplyToHost(strText);
	else
		TCPSendReplyToHost(strText);
}
//  Subroutine to add a short 3 byte tag (ARQ, FEC, ERR, or IDF) to data and send to the host 

void AddTagToDataAndSendToHost(UCHAR * bytData, char * strTag, int Len)
{
	if (SerialMode)
		SCSAddTagToDataAndSendToHost(bytData, strTag, Len);
	else
		TCPAddTagToDataAndSendToHost(bytData, strTag, Len);

}

#ifdef TEENSY

// Dummies for Linker

void TCPSendCommandToHost(char * strText)
{}
void TCPQueueCommandToHost(char * strText)
{}
void TCPSendReplyToHost(char * strText)
{}
void TCPAddTagToDataAndSendToHost(UCHAR * bytData, char * strTag, int Len)
{}

#endif


 


