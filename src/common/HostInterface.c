// ARDOP TNC Host Interface
//

#include "common/ARDOPC.h"
#include "common/ardopcommon.h"

BOOL blnHostRDY = FALSE;
extern int intFECFramesSent;

void SendData();
BOOL CheckForDisconnect();
int Encode4FSKControl(UCHAR bytFrameType, UCHAR bytSessionID, UCHAR * bytreturn);
int ComputeInterFrameInterval(int intRequestedIntervalMS);
HANDLE OpenCOMPort(VOID * pPort, int speed, BOOL SetDTR, BOOL SetRTS, BOOL Quiet, int Stopbits);
BOOL WriteCOMBlock(HANDLE fd, char * Block, int BytesToWrite);
void SetupGPIOPTT();
void setProtocolMode(char* strMode);

void Break();
int txframe(char * frameParams);

extern BOOL NeedID;  // SENDID Command Flag
extern BOOL NeedConReq;  // ARQCALL Command Flag
extern BOOL NeedPing;
extern BOOL PingCount;
extern char ConnectToCall[CALL_BUF_SIZE];
extern enum _ARQBandwidth CallBandwidth;
extern int extraDelay ;  // Used for long delay paths eg Satellite
extern BOOL WG_DevMode;
extern int intARQDefaultDlyMs;

unsigned char *utf8_check(unsigned char *s, size_t slen);
int wg_send_mycall(int cnum, char *call);
int wg_send_bandwidth(int cnum);
int wg_send_hostmsg(int cnum, char msgtype, char *strText);
int wg_send_hostdatab(int cnum, char *prefix, unsigned char *data, int datalen);
int wg_send_hostdatat(int cnum, char *prefix, unsigned char *data, int datalen);int wg_send_drivelevel(int cnum);

extern BOOL NeedTwoToneTest;

#ifndef WIN32

#define strtok_s strtok_r
#define _strupr strupr

char * strupr(char* s)
{
	char* p = s;

	if (s == 0)
		return 0;

	while (*p = toupper( *p ))
		p++;
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


// Function to add data to outbound queue (bytDataToSend)

void AddDataToDataToSend(UCHAR * bytNewData, int Len)
{
	char Msg[3000] = "";

	snprintf(Msg, sizeof(Msg), "[bytNewData: Add to TX queue] %d bytes as hex values: ", Len);
	for (int i = 0; i < Len; i++)
		snprintf(Msg + strlen(Msg), sizeof(Msg) - strlen(Msg) - 1, "%02X ", bytNewData[i]);
	ZF_LOGV("%s", Msg);

	if (utf8_check(bytNewData, Len) == NULL)
		ZF_LOGV("[bytNewData: Add to TX queue] %d bytes as utf8 text: '%.*s'", Len, Len, bytNewData);

	char HostCmd[32];

	if (Len == 0)
		return;

	if ((bytDataToSendLength + Len) >= DATABUFFERSIZE)
		return;  // Flow control has failed

	GetSemaphore();

	memcpy(&bytDataToSend[bytDataToSendLength], bytNewData, Len);
	bytDataToSendLength += Len;

	if (bytDataToSendLength > DATABUFFERSIZE)
		return;

	FreeSemaphore();

	SetLED(TRAFFICLED, TRUE);

	snprintf(HostCmd, sizeof(HostCmd), "BUFFER %d", bytDataToSendLength);
	QueueCommandToHost(HostCmd);
}

char strFault[100] = "";

/*
 * Evaluates command with TRUE/FALSE argument
 *
 * At the input, Value must be set to the current setting for
 * the option named in strCMD.
 *
 * - If ptrParams is null, reports the current option value
 *   to the host. Returns false.
 *
 * - If the input ptrParams is a valid TRUE or FALSE value, sets
 *   Value to the input option, reports success to the client,
 *   and returns true.
 *
 * - If the input ptrParams is not valid as a boolean, reports
 *   failure and returns false.
 *
 * This method returns true if the client has successfully provided
 * a new Value to set. If there is no update to the Value, returns
 * false.
 */
bool DoTrueFalseCmd(char * strCMD, char * ptrParams, BOOL * Value)
{
	char cmdReply[128];

	if (ptrParams == NULL)
	{
		snprintf(cmdReply, sizeof(cmdReply), "%s %s", strCMD, (*Value) ? "TRUE" : "FALSE");
		SendReplyToHost(cmdReply);
		return false;
	}

	if (strcmp(ptrParams, "TRUE") == 0)
		*Value = TRUE;
	else if (strcmp(ptrParams, "FALSE") == 0)
		*Value = FALSE;
	else
	{
		snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
		return false;
	}
	snprintf(cmdReply, sizeof(cmdReply), "%s now %s", strCMD, (*Value) ? "TRUE" : "FALSE");
	SendReplyToHost(cmdReply);
	return true;
}


// Function for processing a command from Host

void ProcessCommandFromHost(char * strCMD)
{
	char * ptrParams;
	// cmdCopy expanded from 80 to 3000 to accomodate
	// TXFRAME with data up to 1024 bytes written as hex
	// requiring 2 string chars per data byte
	char cmdCopy[3000] = "";
	char cmdReply[1024];
	if (WG_DevMode)
		wg_send_hostmsg(0, 'F', strCMD);

	strFault[0] = 0;

	if (strlen(strCMD) >= sizeof(cmdCopy)) {
		ZF_LOGE(
			"Host command too long to process (%lu).  Ignoring. '%.40s...'",
			strlen(strCMD), strCMD);
		return;
	}

	memcpy(cmdCopy, strCMD, strlen(strCMD) + 1);  // save before we uppercase or split it up

	_strupr(strCMD);

	if (CommandTrace)
		ZF_LOGD("[Command Trace FROM host: %s]", strCMD);

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
				snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
			else
			{
				ARQBandwidth = i;
				wg_send_bandwidth(0);
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
						snprintf(strFault, sizeof(strFault), "MYCALL not Set");
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
						snprintf(strFault, sizeof(strFault), "Not from mode RXO");
					else
						snprintf(strFault, sizeof(strFault), "Not from mode FEC");
					goto cmddone;
				}
			}
		}
		snprintf(strFault, sizeof(strFault), "Syntax Err: %.80s", cmdCopy);
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
				snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
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
			snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);

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
				snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
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
				snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
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
			strcpy(CaptureDevice, ptrParams);
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

	if (strcmp(strCMD, "CL") == 0)  // For PTC Emulator
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

	// I'm not sure what CODEC is intended to do, but using it causes a
	// segfault.  It doesn't seem to be required for normal use, so disable
	// this command until it is better understood.  -LaRue May 2024
	/*
	if (strcmp(strCMD, "CODEC") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &blnCodecStarted);

		if (strcmp(ptrParams, "TRUE") == 0)
			StartCodec(strFault);
		else if (strcmp(ptrParams, "FALSE") == 0)
			StopCodec(strFault);

		goto cmddone;
	}
	*/

	if (strcmp(strCMD, "CONSOLELOG") == 0)
	{
		long i = 0;

		if (ptrParams == 0)
		{
			snprintf(cmdReply, sizeof(cmdReply), "%s %d", strCMD, ardop_log_get_level_console());
			SendReplyToHost(cmdReply);
		}
		else if (try_parse_long(ptrParams, &i))
		{
			ardop_log_set_level_console((int)i);
			ZF_LOGI("ConsoleLogLevel = %d", ardop_log_get_level_console());
			snprintf(cmdReply, sizeof(cmdReply), "%s now %d", strCMD, ardop_log_get_level_console());
			SendReplyToHost(cmdReply);
		}
		else
		{
			snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
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
		else if (strcmp(ptrParams, "FALSE") == 0)
			wantCWID = FALSE;
		else if (strcmp(ptrParams, "ONOFF") == 0)
		{
			wantCWID = TRUE;
			CWOnOff = TRUE;
		}
		else
		{
			snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
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
				snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
		}
		goto cmddone;
	}

	// DATETIME command previously provided for TEENSY support removed

	if (strcmp(strCMD, "DEBUGLOG") == 0)
	{
		BOOL enable_files = ardop_log_is_enabled_files();
		if (DoTrueFalseCmd(strCMD, ptrParams, &enable_files)) {
			ardop_log_enable_files((bool)enable_files);
		}
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
				wg_send_drivelevel(0);
				goto cmddone;
			}
			else
				snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
		}
		goto cmddone;
	}

	// EEPROM command previously provided for TEENSY support removed

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
				snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
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
					intFECFramesSent = 0;  // Force mode to be reevaluated
					sprintf(cmdReply, "%s now %s", strCMD, strFECMode);
					SendReplyToHost(cmdReply);
					goto cmddone;
				}
			}
			snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
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
				snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
		}
		goto cmddone;
	}

	if (strcmp(strCMD, "FECSEND") == 0)
	{
		if (ptrParams == 0)
		{
			snprintf(strFault, sizeof(strFault), "Syntax Err: %s", strCMD);
			goto cmddone;
		}
		if (strcmp(ptrParams, "TRUE") == 0)
		{
			ZF_LOGD("FECRepeats %d", FECRepeats);

			StartFEC(NULL, 0, strFECMode, FECRepeats, FECId);
			SendReplyToHost("FECSEND now TRUE");
		}
		else if (strcmp(ptrParams, "FALSE") == 0)
		{
			blnAbort = TRUE;
			SendReplyToHost("FECSEND now FALSE");
		}
		else
			snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);

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
				snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);

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
				LeaderLength *= 10;  // round to 10 mS
				// Also set this to intARQDefaultDlyMs to make this equivalent
				// to the DEPRECATED --leaderlength command line option.
				intARQDefaultDlyMs = LeaderLength;
				sprintf(cmdReply, "%s now %d", strCMD, LeaderLength);
				SendReplyToHost(cmdReply);
			}
			else
				snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
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
		long i = 0;

		if (ptrParams == 0)
		{
			snprintf(cmdReply, sizeof(cmdReply), "%s %d", strCMD, ardop_log_get_level_file());
			SendReplyToHost(cmdReply);
		}
		else if (try_parse_long(ptrParams, &i))
		{
			ardop_log_set_level_file((int)i);
			ZF_LOGI("FileLogLevel = %d", ardop_log_get_level_file());
			snprintf(cmdReply, sizeof(cmdReply), "%s now %d", strCMD, ardop_log_get_level_file());
			SendReplyToHost(cmdReply);
		}
		else
		{
			snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
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
			cmdReply[len - 1] = 0;  // remove trailing space or ,
			SendReplyToHost(cmdReply);
			goto cmddone;
		}

		ptr = strtok_s(ptrParams, ", ", &context);

		AuxCallsLength = 0;

		while (ptr && AuxCallsLength < AUXCALLS_ALEN)
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
		cmdReply[len - 1] = 0;  // remove trailing space or ,
		SendReplyToHost(cmdReply);

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
				wg_send_mycall(0, Callsign);
				sprintf(cmdReply, "%s now %s", strCMD, Callsign);
				SendReplyToHost(cmdReply);
			}
			else
				snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
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
					snprintf(strFault, sizeof(strFault), "No PING from state %s",  ARDOPStates[ProtocolState]);
					goto cmddone;
				}
				else
				{
					SendReplyToHost(cmdCopy);  // echo command back to host.
					strcpy(ConnectToCall, _strupr(ptrParams));
					PingCount = count;
					NeedPing = TRUE;  // request ping from background
					goto cmddone;
				}
			}
		}

		snprintf(strFault, sizeof(strFault), "Syntax Err: %.80s", cmdCopy);

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
			strcpy(PlaybackDevice, ptrParams);
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

		if (strcmp(ptrParams, "ARQ") != 0 && strcmp(ptrParams, "RXO") == 0
			&& strcmp(ptrParams, "FEC") == 0
		) {
			snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
			goto cmddone;
		}

		setProtocolMode(ptrParams);
		sprintf(cmdReply, "PROTOCOLMODE now %s", ptrParams);
		SendReplyToHost(cmdReply);

		SetARDOPProtocolState(DISC);  // set state to DISC on any Protocol mode change.
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
		// Currently only used for setting GUI Freq field

		if (ptrParams == NULL)
		{
			snprintf(strFault, sizeof(strFault), "RADIOFREQ command string missing");
			goto cmddone;
		}

		SendtoGUI('F', ptrParams, strlen(ptrParams));
		goto cmddone;
	}

	if (strcmp(strCMD, "RADIOHEX") == 0)
	{
		// Parameter is block to send to radio, in hex

		char c;
		int val;
		char * ptr1 = ptrParams;
		char * ptr2 = ptrParams;

		if (ptrParams == NULL)
		{
			snprintf(strFault, sizeof(strFault), "RADIOHEX command string missing");
			goto cmddone;
		}
		if (hCATDevice)
		{
			while (c = *(ptr1++))
			{
				val = c - 0x30;
				if (val > 15)
					val -= 7;
				val <<= 4;
				c = *(ptr1++) - 0x30;
				if (c > 15)
					c -= 7;
				val |= c;
				*(ptr2++) = val;
			}
			WriteCOMBlock(hCATDevice, ptrParams, ptr2 - ptrParams);
			EnableHostCATRX = TRUE;
		}
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
			// Send a comma delimited list of models?
			SendReplyToHost(strCommand & " " & objMain.objRadio.strSupportedRadios)  // Need to insure this isn't too long for Interfaces:
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
		// End of optional Radio Commands
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
			snprintf(strFault, sizeof(strFault), "RADIOPTTOFF command string missing");
			goto cmddone;
		}

		if (hCATDevice == 0)
		{
			snprintf(strFault, sizeof(strFault), "RADIOPTTOFF command CAT Port not defined");
			goto cmddone;
		}

		while (c = *(ptr1++))
		{
			val = c - 0x30;
			if (val > 15)
				val -= 7;
			val <<= 4;
			c = *(ptr1++) - 0x30;
			if (c > 15)
				c -= 7;
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
			snprintf(strFault, sizeof(strFault), "RADIOPTTON command string missing");
			goto cmddone;
		}

		if (hCATDevice == 0)
		{
			snprintf(strFault, sizeof(strFault), "RADIOPTTON command CAT Port not defined");
			goto cmddone;
		}

		while (c = *(ptr1++))
		{
			val = c - 0x30;
			if (val > 15)
				val -= 7;
			val <<= 4;
			c = *(ptr1++) - 0x30;
			if (c > 15)
				c -= 7;
			val |= c;
			*(ptr2++) = val;
		}

		PTTOnCmdLen = ptr2 - PTTOnCmd;

		sprintf(cmdReply, "CAT PTT On Command Defined");
		SendReplyToHost(cmdReply);
		goto cmddone;
	}

	// RXLEVEL command previously provided for TEENSY support removed

	if (strcmp(strCMD, "SENDID") == 0)
	{
		if (ProtocolState == DISC)
		{
			NeedID = TRUE;  // Send from background
			SendReplyToHost(strCMD);
		}
		else
			snprintf(strFault, sizeof(strFault), "Not from State %s", ARDOPStates[ProtocolState]);

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
				snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
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
			snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);

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
				TrailerLength *= 10;  // round to 10 mS

				sprintf(cmdReply, "%s now %d", strCMD, TrailerLength);
				SendReplyToHost(cmdReply);
			}
			else
				snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
		}
		goto cmddone;
	}

	if (strcmp(strCMD, "TWOTONETEST") == 0)
	{
		if (ProtocolState == DISC)
		{
			NeedTwoToneTest = TRUE;  // Send from background
			SendReplyToHost(strCMD);
		}
		else
			snprintf(strFault, sizeof(strFault), "Not from state %s", ARDOPStates[ProtocolState]);

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
				snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
		}
		goto cmddone;
	}

	// TXLEVEL command previously provided for TEENSY support removed

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
	// RDY processed earlier Case "RDY".  no response required for RDY

	///////////////////////////////////////////////////////////////
	// The TXFRAME command is intended for development and debugging.
	// It is NOT intended for normal use by Host applications.
	// It may be removed or modfied without notice in future
	// versions of ardopcf.
	///////////////////////////////////////////////////////////////
	if (strcmp(strCMD, "TXFRAME") == 0)
	{
		if (ptrParams == 0)
		{
			snprintf(strFault, sizeof(strFault), "Syntax Err: TXFRAME sendParams");
			goto cmddone;
		} else {
			// cmdCopy starts with arbitrary cased "txframe "
			// and has a max length of 2100.
			if(txframe(cmdCopy) != 0)
				snprintf(strFault, sizeof(strFault), "FAILED TXFRAME");
		}
		goto cmddone;
	}


	snprintf(strFault, sizeof(strFault), "CMD %s not recoginized", strCMD);

cmddone:

	if (strFault[0])
	{
		// Logs.Exception("[ProcessCommandFromHost] Cmd Rcvd=" & strCommand & "   Fault=" & strFault)
		sprintf(cmdReply, "FAULT %s", strFault);
		SendReplyToHost(cmdReply);
		ZF_LOGW("Host Command Fault: %s", strFault);
	}
//	SendCommandToHost("RDY");  // signals host a new command may be sent
}

// Function to send a text command to the Host

void SendCommandToHost(char * strText)
{
	TCPSendCommandToHost(strText);
	if (WG_DevMode)
		wg_send_hostmsg(0, 'C', strText);
}


void SendCommandToHostQuiet(char * strText)  // Higher Debug Level for PTT
{
	TCPSendCommandToHostQuiet(strText);
	if (WG_DevMode)
		wg_send_hostmsg(0, 'T', strText);
}

void QueueCommandToHost(char * strText)
{
	TCPQueueCommandToHost(strText);
	if (WG_DevMode)
		wg_send_hostmsg(0, 'Q', strText);
}

void SendReplyToHost(char * strText)
{
	// This redirects to SendCommandToHost(), so don't do duplicate wg_send_hostmsg()
	TCPSendReplyToHost(strText);
}
// Function to add a short 3 byte tag (ARQ, FEC, ERR, or IDF) to data and send to the host

void AddTagToDataAndSendToHost(UCHAR * bytData, char * strTag, int Len)
{
	char Msg[3000] = "";

	snprintf(Msg, sizeof(Msg), "[RX Data: Send To Host with TAG=%s] %d bytes as hex values: ", strTag, Len);
	for (int i = 0; i < Len; i++)
		snprintf(Msg + strlen(Msg), sizeof(Msg) - strlen(Msg) - 1, "%02X ", bytData[i]);
	ZF_LOGV("%s", Msg);

	if (utf8_check(bytData, Len) == NULL)
		ZF_LOGV("[RX Data: Send To Host with TAG=%s] %d bytes as utf8 text: '%.*s'", strTag, Len, Len, bytData);

	TCPAddTagToDataAndSendToHost(bytData, strTag, Len);
	if (WG_DevMode) {
		if (utf8_check(bytData, Len) == NULL)
			wg_send_hostdatat(0, strTag, bytData, Len);
		else
			wg_send_hostdatab(0, strTag, bytData, Len);
	}
}
