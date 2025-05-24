// ARDOP TNC Host Interface
//

#include <stdbool.h>

#include "common/ARDOPC.h"
#include "common/ardopcommon.h"
#include "common/audio.h"
#include "common/wav.h"
#include "common/ptt.h"  // PTT and CAT
#include "common/Webgui.h"
#include "common/eutf8.h"

bool blnHostRDY = false;
extern int intFECFramesSent;

extern bool UseSDFT;  // Enable use of the alternative Sliding DFT demodulator
extern bool WriteTxWav;  // Record TX
extern bool WriteRxWav;  // Record RX controlled by Command line/TX/Timer
extern bool HWriteRxWav;  // Record RX controlled by host command RECRX
extern struct WavFile *rxwf;  // For recording of RX audio
void StartRxWav();

void SendData();
bool CheckForDisconnect();
int Encode4FSKControl(UCHAR bytFrameType, UCHAR bytSessionID, UCHAR * bytreturn);
int ComputeInterFrameInterval(int intRequestedIntervalMS);
void SetupGPIOPTT();
void setProtocolMode(char* strMode);

void Break();
int txframe(char * frameParams);
unsigned char parseCatStr(char *hexstr, unsigned char *cmd, char *descstr);
int bytes2hex(char *outputStr, size_t count, unsigned char *data, size_t datalen, bool spaces);

extern bool NeedID;  // SENDID Command Flag
extern bool NeedConReq;  // ARQCALL Command Flag
extern bool NeedPing;
extern int PingCount;
extern StationId ConnectToCall;
extern enum _ARQBandwidth CallBandwidth;
extern int extraDelay ;  // Used for long delay paths eg Satellite
extern bool WG_DevMode;
extern int intARQDefaultDlyMs;

unsigned char *utf8_check(unsigned char *s, size_t slen);

extern bool NeedTwoToneTest;
extern short InputNoiseStdDev;

#ifndef WIN32

#define _strupr strupr

char * strupr(char* s)
{
	char* p = s;

	if (s == 0)
		return 0;

	while ((*p = toupper( *p )))
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
	// Up to 8192 bytes of data may be provided from ProcessReceivedData().
	// Worst case behavior of eutf8() produces output that is three times as
	// long as the input data plus one byte.  So, an available message length of
	// 25000 is adequate to log the eutf8 encoded data with a suitable preamble.
	char Msg[25000] = "";
	snprintf(Msg, sizeof(Msg), "[bytNewData: Add to TX queue] %d bytes (eutf8):\n", Len);
	eutf8(Msg + strlen(Msg), sizeof(Msg) - strlen(Msg), (char *) bytNewData, Len);
	ZF_LOGV("%s", Msg);

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

	SetLED(TRAFFICLED, true);

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
 * a new Value to set (even if it duplicates the existing value). If there is no
 * update to the Value, returns false.
 */
bool DoTrueFalseCmd(char * strCMD, char * ptrParams, bool * Value)
{
	char cmdReply[128];

	if (ptrParams == NULL)
	{
		snprintf(cmdReply, sizeof(cmdReply), "%s %s", strCMD, (*Value) ? "TRUE" : "FALSE");
		SendReplyToHost(cmdReply);
		return false;
	}

	if (strcmp(ptrParams, "TRUE") == 0)
		*Value = true;
	else if (strcmp(ptrParams, "FALSE") == 0)
		*Value = false;
	else
	{
		snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
		return false;
	}
	snprintf(cmdReply, sizeof(cmdReply), "%s now %s", strCMD, (*Value) ? "TRUE" : "FALSE");
	SendReplyToHost(cmdReply);
	ZF_LOGD("%s now %s", strCMD, (*Value) ? "TRUE" : "FALSE");
	return true;
}

/**
 * @brief Parse command `params` like "`N0CALL-A 5`"
 *
 * Reads command `params` that take a `target` ID and a `nattempts`
 * count.
 *
 * @param[in] cmd         Command name, like `ARQCALL`. Must be
 *                        non-NULL and NUL-terminated.
 * @param[in] params      Command parameters. May be NULL. If not,
 *                        must be NUL-terminated. Will be
 *                        destructively overwritten.
 * @param[out] fault      Destination buffer for failure message
 * @param[in] fault_size  `sizeof(fault)`
 * @param[out] target     Station ID parsed
 * @param[out] nattempts  Attempt count parsed. Will be positive.
 *
 * @return true if the params are valid and a `target` and
 * `nattempts` are populated. false if the params are not valid
 * and `fault` is populated instead. If this method returns false,
 * the value of `target` and `nattempts` are undefined.
 */
ARDOP_MUSTUSE
bool parse_station_and_nattempts(
	const char* cmd,
	char* params,
	char* fault,
	size_t fault_size,
	StationId* target,
	long* nattempts)
{
	stationid_init(target);
	*nattempts = 0;

	if (! params) {
		snprintf(fault, fault_size, "Syntax Err: %s: expected \"TARGET NATTEMPTS\"", cmd);
		return false;
	}

	const char* target_str = params;
	const char* nattempts_str = strlop(params, ' ');
	if (!nattempts_str) {
		snprintf(fault, fault_size, "Syntax Err: %s %s: expected \"TARGET NATTEMPTS\"", cmd, target_str);
		return false;
	}

	station_id_err e = stationid_from_str(target_str, target);
	if (e) {
		snprintf(
			fault,
			fault_size,
			"Syntax Err: %s %s %s: invalid TARGET: %s",
			cmd, target_str, nattempts_str, stationid_strerror(e));
		return false;
	}

	if (! try_parse_long(nattempts_str, nattempts)) {
		snprintf(
			fault,
			fault_size,
			"Syntax Err: %s %s %s: NATTEMPTS not valid as number",
			cmd, target_str, nattempts_str);
		return false;
	}

	if (! (*nattempts >= 1)) {
		snprintf(
			fault,
			fault_size,
			"Syntax Err: %s %s %s: NATTEMPTS must be positive",
			cmd, target_str, nattempts_str);
		return false;
	}

	return true;
}

bool process_capturechannel(char *param) {
	if (strcmp(param, "MONO") == 0) {
		UseLeftRX = true;
		UseRightRX = true;
	} else if (strcmp(param, "LEFT") == 0) {
		UseLeftRX = true;
		UseRightRX = false;
	} else if (strcmp(param, "RIGHT") == 0) {
		UseLeftRX = false;
		UseRightRX = true;
	} else {
		return false;
	}
	wg_send_capturechannel(0);
	ZF_LOGD("CAPTURECHANNEL now %s", param);
	if (RXEnabled && Cch != getCch(true)) {
		CloseSoundCapture(true);  // calls updateWebGuiAudioConfig(true);
		// It is possible that the following will fail due to this new
		// setting or because of a problem with the device.
		OpenSoundCapture("RESTORE", getCch(true));
	}
	return true;
}

bool process_playbackchannel(char *param) {
	if (strcmp(param, "MONO") == 0) {
		UseLeftTX = true;
		UseRightTX = true;
	} else if (strcmp(param, "LEFT") == 0) {
		UseLeftTX = true;
		UseRightTX = false;
	} else if (strcmp(param, "RIGHT") == 0) {
		UseLeftTX = false;
		UseRightTX = true;
	} else {
		return false;
	}
	wg_send_playbackchannel(0);
	ZF_LOGD("PLAYBACKCHANNEL now %s", param);
	if (TXEnabled && Pch != getPch(true)) {
		CloseSoundPlayback(true);  // calls updateWebGuiAudioConfig(true);
		// It is possible that the following will fail due to this new
		// setting or because of a problem with the device.
		OpenSoundPlayback("RESTORE", getPch(true));
	}
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
	// cmdReply expanded to accomodate long string response to CAPTUREDEVICES
	// AND PLAYBACKDEVICES commands.
	// TODO: Is cmdReply long enough now?  Should DevicesToCSV() be modified to
	// produce a truncated valid response if dstsize is too small?
	char cmdReply[4096];

	if (WG_DevMode)
		wg_send_hostmsg(0, 'F', strCMD);

	strFault[0] = 0;

	if (strlen(strCMD) >= sizeof(cmdCopy)) {
		ZF_LOGE(
			"Host command too long to process (%u).  Ignoring. '%.40s...'",
			(unsigned int) strlen(strCMD), strCMD);
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
		long nattempts = 0;
		if (! parse_station_and_nattempts(
				strCMD,
				ptrParams,
				strFault,
				sizeof(strFault),
				&ConnectToCall,
				&nattempts))
		{
			goto cmddone;
		}

		if (! stationid_ok(&Callsign)) {
			snprintf(strFault, sizeof(strFault), "MYCALL not set");
			goto cmddone;
		}

		switch (ProtocolMode) {
			case ARQ:
				ARQConReqRepeats = (int)nattempts;
				NeedConReq = true;
				SendReplyToHost(cmdCopy);
				break;
			case FEC:
				snprintf(strFault, sizeof(strFault), "Not from mode FEC");
				break;
			case RXO:
				snprintf(strFault, sizeof(strFault), "Not from mode RXO");
				break;
		}

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

	// Warning: If a CaptureDevice is currently open and the value provided
	// with this command is not identical to the current value of
	// CaptureDevice, then the device will be closed so that this new
	// CaptureDevice can be opened.  This is true even if using the same device
	// is specified by another name.  This may result in loss of received data.
	// If the special device name "NONE" is used, close any existing CAPTURE
	// device if one is open.  This is similar to RXENABLED FALSE.
	// If the special device name "RESTORE" is used, do nothing if RXEnabled is
	// true.  However, if RXEnabled is false, but a CAPTURE device has
	// previously been successfully opened, then try to reopen that device.
	// Unlike most commands, the arguments to CAPTURE and PLAYBACK are case
	// sensitive.
	if (strcmp(strCMD, "CAPTURE") == 0) {
		char *ptrCaseParams = strlop(cmdCopy, ' ');
		if (ptrParams == 0) {
			sprintf(cmdReply, "%s %s", strCMD,
				CaptureDevice[0] == 0x00 ? "NONE" : CaptureDevice);
			SendReplyToHost(cmdReply);
		} else {
			bool ret;
			if (strcmp(ptrCaseParams, "NONE") == 0)
				ret = OpenSoundCapture("", getCch(true));
			else
				ret = OpenSoundCapture(ptrCaseParams, getCch(true));
			if (ret) {
				sprintf(cmdReply, "%s now %s", strCMD, CaptureDevice);
				SendReplyToHost(cmdReply);
			} else {
				snprintf(strFault, sizeof(strFault),
					"Cannot open CaptureDevice as configured: %s %s",
					strCMD, ptrCaseParams);
			}
		}
		goto cmddone;
	}


	// Warning: If a CaptureDevice is currently open and the value provided
	// with this command is not identical to the current value, then the device
	// may be closed so that it can be re-opened with this new configuration.
	// This may result in loss of received data.
	// With no parameter, respond with  CAPTURECHANNEL LEFT, RIGHT, or MONO.
	// With a parameter of LEFT, RIGHT, or MONO, set the channel used by
	// CaptureDevice.
	if (strcmp(strCMD, "CAPTURECHANNEL") == 0) {
		if (ptrParams == 0) {
			if (UseLeftRX && UseRightRX)
				sprintf(cmdReply, "%s MONO", strCMD);
			else if (UseLeftRX)
				sprintf(cmdReply, "%s LEFT", strCMD);
			else // UseRightRX
				sprintf(cmdReply, "%s RIGHT", strCMD);
			SendReplyToHost(cmdReply);
			goto cmddone;
		}
		if (!process_capturechannel(ptrParams)) {
			snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
			goto cmddone;
		}
		sprintf(cmdReply, "%s now %s", strCMD, ptrParams);
		SendReplyToHost(cmdReply);
		goto cmddone;
	}

	// Per the Ardop specification (Protocol Native TNC Commands), this command
	// "Returns a comma delimited list of all currently installed capture
	// devices."  The specification does not indicate how device names that
	// contain commas shall be handled.  Nor does it provide any explicit
	// mechanism to include a device description in addition to a device name.
	//
	// So: The following is used:
	// Device names may be wrapped in double quotes, and double double quotes
	// within double quotes shall be interpreted as a literal double quotes
	// character.  If any device name includes a linefeed (\n), then the text
	// before the linefeed shall be interpreted as the name, while any text
	// after the first linefeed shall be interpreted as a description.  The name
	// portion of this value is suitable to pass to the CAPTURE command.
	//
	// Furthermore:
	// If the description begins with "[BUSY", then this is an indicaton that the
	// device is currently in use (by this or another program).  This may be
	// followed immediately by a closing bracket "]" or additional details may be
	// included before that closing bracket.
	//
	// The response CSV text is preceeded by the command string and a space.
	// The host should probably discard all whitespace after the command string.
	if (strcmp(strCMD, "CAPTUREDEVICES") == 0) {
		GetDevices();
		LogDevices(AudioDevices, "Capture (input) Devices for host command",
			true, false);

		// TODO: Should DevicesToCSV() be modified to produce a valid but
		// truncated response if dstsize is inadequate?
		snprintf(cmdReply, sizeof(cmdReply), "%s ", strCMD);
		if (!DevicesToCSV(AudioDevices, cmdReply + strlen(cmdReply),
			sizeof(cmdReply) - strlen(cmdReply), true)
		) {
			snprintf(strFault, sizeof(strFault),
				"%s failed because buffer is too small", strCMD);
			goto cmddone;
		}
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
		blnClosing = true;
		goto cmddone;
	}

	if (strcmp(strCMD, "CMDTRACE") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &CommandTrace);
		goto cmddone;
	}

	// Set/get RXState and TXState
	if (strcmp(strCMD, "CODEC") == 0) {
		// Typically RXEnabled == TXEnabled, in which case this works entirely
		// as expected.  However, if one or both are false, CODEC will return
		// FALSE
		bool codec = (RXEnabled && TXEnabled);
		// This is similar to DoTrueFalseCmd(strCMD, ptrParams, &codec), but
		// with a wider range of responses
		if (ptrParams == NULL) {
			snprintf(cmdReply, sizeof(cmdReply), "%s %s", strCMD,
				codec ? "TRUE" : "FALSE");
			SendReplyToHost(cmdReply);
			goto cmddone;
		}

		if (strcmp(ptrParams, "TRUE") == 0) {
			if (!RXEnabled)
				OpenSoundCapture("RESTORE", getCch(true));
			if (!TXEnabled)
				OpenSoundPlayback("RESTORE", getPch(true));
			if (RXEnabled && TXEnabled) {
				snprintf(cmdReply, sizeof(cmdReply), "%s now TRUE", strCMD);
				SendReplyToHost(cmdReply);
				ZF_LOGD("%s now TRUE", strCMD);
				goto cmddone;
			}
			if (!RXEnabled && !TXEnabled) {
				snprintf(strFault, sizeof(strFault),
					"%s cannot be set to TRUE.  CAPTURE and PLAYBACK required"
					, strCMD);
			} else if (RXEnabled) {
				snprintf(strFault, sizeof(strFault),
					"%s cannot be set to TRUE.  CAPTURE required", strCMD);
			} else if (TXEnabled) {
				snprintf(strFault, sizeof(strFault),
					"%s cannot be set to TRUE.  PLAYBACK required", strCMD);
			} else {
				snprintf(cmdReply, sizeof(cmdReply), "%s now TRUE", strCMD);
				SendReplyToHost(cmdReply);
				ZF_LOGD("%s now TRUE", strCMD);
			}
			goto cmddone;
		} else if (strcmp(ptrParams, "FALSE") == 0) {
			// Set RXEnabled and TXEnabled to false
			CloseSoundCapture(true);  // calls updateWebGuiAudioConfig(true);
			CloseSoundPlayback(false);  // calls updateWebGuiAudioConfig(false);
			snprintf(cmdReply, sizeof(cmdReply), "%s now FALSE", strCMD);
			SendReplyToHost(cmdReply);
			ZF_LOGD("%s now FALSE", strCMD);
			goto cmddone;
		} else {
			snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
			goto cmddone;
		}
		goto cmddone;
	}

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
			wantCWID = true;
			CWOnOff = false;
		}
		else if (strcmp(ptrParams, "FALSE") == 0)
			wantCWID = false;
		else if (strcmp(ptrParams, "ONOFF") == 0)
		{
			wantCWID = true;
			CWOnOff = true;
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
		bool enable_files = ardop_log_is_enabled_files();
		if (DoTrueFalseCmd(strCMD, ptrParams, &enable_files)) {
			ardop_log_enable_files((bool)enable_files);
		}
		goto cmddone;
	}

	if (strcmp(strCMD, "DISCONNECT") == 0)
	{
		if (ProtocolState == IDLE || ProtocolState == IRS || ProtocolState == ISS || ProtocolState == IRStoISS)
		{
			blnARQDisconnect = true;
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

	if (strcmp(strCMD, "FASTSTART") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &fastStart);
		goto cmddone;
	}

	if (strcmp(strCMD, "FECID") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &FECId);
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
			// Previously this check to ensure that MYCALL is set was included
			// in StartFEC().  Moving this test here helps provide a consistent
			// fault message for any attempt to initiate transmitting without
			// first setting MYCALL so that an IDFrame can be sent when needed.
			if (!stationid_ok(&Callsign)) {
				snprintf(strFault, sizeof(strFault), "MYCALL not set");
				goto cmddone;
			}

			ZF_LOGD("FECRepeats %d", FECRepeats);

			if (!StartFEC(NULL, 0, strFECMode, FECRepeats, FECId)) {
				// This can occur for several reasons including no data queued
				// send, an invalid setting for FECREPEATS or FECMODE, MYCALL
				// not set or invalid.  Previously, no indication was passed
				// to the host if StartFEC() failed.
				snprintf(strFault, sizeof(strFault), "StartFEC failed for FECSEND TRUE.");
				goto cmddone;
			}
			SendReplyToHost("FECSEND now TRUE");
		}
		else if (strcmp(ptrParams, "FALSE") == 0)
		{
			blnAbort = true;
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
			snprintf(cmdReply, sizeof(cmdReply), "%s %s", strCMD, GridSquare.grid);
			SendReplyToHost(cmdReply);
		}
		else
		{
			Locator inp;
			locator_err e = locator_from_str(ptrParams, &inp);
			if (e) {
				snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s: %s", strCMD, ptrParams, locator_strerror(e));
			} else {
				memcpy(&GridSquare, &inp, sizeof(GridSquare));
				snprintf(cmdReply, sizeof(cmdReply), "%s now %s", strCMD, GridSquare.grid);
				SendReplyToHost(cmdReply);
			}
		}

		goto cmddone;
	}

	if (strcmp(strCMD, "INITIALIZE") == 0)
	{
		blnInitializing = true;
		ClearDataToSend();
		blnHostRDY = true;
		blnInitializing = false;

		SendReplyToHost("INITIALIZE");
		goto cmddone;
	}

	// Set the standard deviation of AWGN to be added to 16-bit input audio.
	// Set it to 0 for no noise.
	if (strcmp(strCMD, "INPUTNOISE") == 0)
	{
		if (ptrParams == 0) {
			sprintf(cmdReply, "%s %d", strCMD, InputNoiseStdDev);
			SendReplyToHost(cmdReply);
		} else {
			// TODO: error checking of value
			InputNoiseStdDev = atoi(ptrParams);
			sprintf(cmdReply, "%s now %hd", strCMD, InputNoiseStdDev);
			SendReplyToHost(cmdReply);
		}
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
				// to the obsolete --leaderlength command line option.
				// TODO:  Need to review use of LeaderLength, intARQDefaultDlyMS,
				// and intCalcLeader.  It appears that calculation of optimum
				// leader length (see CalculateOptimumLeader()) is not being done.
				// It this OK or might implementing (or re-implementing) this
				// improve reliability under some conditions?
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
		if (ptrParams == 0)
		{
			stationid_array_to_str(
				AuxCalls,
				AuxCallsLength,
				cmdReply,
				sizeof(cmdReply),
				",",
				"MYAUX",
				" "
			);
			SendReplyToHost(cmdReply);
			goto cmddone;
		}

		memset(AuxCalls, 0, sizeof(AuxCalls));
		AuxCallsLength = 0;
		station_id_err e = stationid_from_str_to_array(
			ptrParams,
			&AuxCalls[0],
			sizeof(AuxCalls) / sizeof(AuxCalls[0]),
			&AuxCallsLength
		);

		if (e) {
			snprintf(strFault, sizeof(strFault), "Syntax Err: at callsign %u: %s",
				(unsigned int) AuxCallsLength, stationid_strerror(e));
			/* The TNC protocol requires that invalid input
			 * completely clears the MYAUX array */
			AuxCallsLength = 0;
			goto cmddone;
		}

		stationid_array_to_str(
			AuxCalls,
			AuxCallsLength,
			cmdReply,
			sizeof(cmdReply),
			",",
			"MYAUX now",
			" "
		);
		ZF_LOGD_STR(cmdReply);
		SendReplyToHost(cmdReply);
		goto cmddone;
	}

	if (strcmp(strCMD, "MYCALL") == 0)
	{
		if (ptrParams == 0)
		{
			snprintf(cmdReply, sizeof(cmdReply), "%s %s", strCMD, Callsign.str);
			SendReplyToHost(cmdReply);
			goto cmddone;
		}

		StationId new_mycall;
		station_id_err e = stationid_from_str(ptrParams, &new_mycall);
		if (e) {
			snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s: %s", strCMD, ptrParams, stationid_strerror(e));
		} else {
			memcpy(&Callsign, &new_mycall, sizeof(Callsign));
			wg_send_mycall(0, Callsign.str);
			snprintf(cmdReply, sizeof(cmdReply), "%s now %s", strCMD, Callsign.str);
			ZF_LOGD_STR(cmdReply);
			SendReplyToHost(cmdReply);
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
		long nattempts = 0;
		if (!parse_station_and_nattempts(
				strCMD,
				ptrParams,
				strFault,
				sizeof(strFault),
				&ConnectToCall,
				&nattempts))
		{
			goto cmddone;
		}

		if (!stationid_ok(&Callsign)) {
			snprintf(strFault, sizeof(strFault), "MYCALL not set");
			goto cmddone;
		}

		if (ProtocolMode == RXO) {
			snprintf(strFault, sizeof(strFault), "Not from mode RXO");
		} else if (ProtocolState != DISC) {
			snprintf(strFault, sizeof(strFault), "No PING from state %s", ARDOPStates[ProtocolState]);
			goto cmddone;
		}

		PingCount = (int)nattempts;
		NeedPing = true;  // request ping from background
		SendReplyToHost(cmdCopy);
		goto cmddone;
	}

	// Warning: If a PlaybackDevice is currently open and the value provided
	// with this command is not identical to the current value of
	// PlaybackDevice, then the device will be closed so that this new
	// PlaybackDevice can be opened.  This is true even if using the same device
	// is specified by another name.  If this occurs while transmitting, it will
	// result in a failed transmission.
	// If the special device name "NONE" is used, close any existing PLAYBACK
	// device if one is open.  This is similar to TXENABLED FALSE.
	// If the special device name "RESTORE" is used, do nothing if TXEnabled is
	// true.  However, if TXEnabled is false, but a PLAYBACK device has
	// previously been successfully opened, then try to reopen that device.
	// Unlike most commands, the arguments to CAPTURE and PLAYBACK are case
	// sensitive.
	if (strcmp(strCMD, "PLAYBACK") == 0) {
		char *ptrCaseParams = strlop(cmdCopy, ' ');
		if (ptrParams == 0) {
			ZF_LOGV("%s %s", strCMD,
				PlaybackDevice[0] == 0x00 ? "NONE" : PlaybackDevice);
			sprintf(cmdReply, "%s %s", strCMD,
				PlaybackDevice[0] == 0x00 ? "NONE" : PlaybackDevice);
			SendReplyToHost(cmdReply);
		} else {
			bool ret;
			if (strcmp(ptrCaseParams, "NONE") == 0)
				ret = OpenSoundPlayback("", getPch(true));
			else
				ret = OpenSoundPlayback(ptrCaseParams, getPch(true));
			if (ret) {
				sprintf(cmdReply, "%s now %s", strCMD, PlaybackDevice);
				SendReplyToHost(cmdReply);
			} else {
				snprintf(strFault, sizeof(strFault),
					"Cannot open PlaybackDevice as configured: %s %s",
					strCMD, ptrCaseParams);
			}
		}
		goto cmddone;
	}

	// Warning: If a PlaybackDevice is currently open and the value provided
	// with this command is not identical to the current value, then the device
	// may be closed so that it can be re-opened with this new configuration.
	// If this occurs while transmitting, it will
	// result in a failed transmission.
	// With no parameter, respond with  PLAYBACKCHANNEL LEFT, RIGHT, or MONO.
	// With a parameter of LEFT, RIGHT, or MONO, set the channel used by
	// PlaybackDevice.
	if (strcmp(strCMD, "PLAYBACKCHANNEL") == 0) {
		if (ptrParams == 0) {
			if (UseLeftTX && UseRightTX)
				sprintf(cmdReply, "%s MONO", strCMD);
			else if (UseLeftTX)
				sprintf(cmdReply, "%s LEFT", strCMD);
			else // UseRightTX
				sprintf(cmdReply, "%s RIGHT", strCMD);
			SendReplyToHost(cmdReply);
			goto cmddone;
		}
		if (!process_playbackchannel(ptrParams)) {
			snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
			goto cmddone;
		}
		sprintf(cmdReply, "%s now %s", strCMD, ptrParams);
		SendReplyToHost(cmdReply);
		goto cmddone;
	}

	// Per the Ardop specification (Protocol Native TNC Commands), this command
	// "Returns a comma delimited list of all currently installed playback
	// devices."  The specification does not indicate how device names that
	// contain commas shall be handled.  Nor does it provide any explicit
	// mechanism to include a device description in addition to a device name.
	//
	// So: The following is used:
	// Device names may be wrapped in double quotes, and double double quotes
	// within double quotes shall be interpreted as a literal double quotes
	// character.  If any device name includes a linefeed (\n), then the text
	// before the linefeed shall be interpreted as the name, while any text
	// after the first linefeed shall be interpreted as a description.  The name
	// portion of this value is suitable to pass to the PLAYBACK command.
	//
	// Futhermore:
	// If the description begins with "[BUSY", then this is an indicaton that the
	// device is currently in use (by this or another program).  This may be
	// followed immediately by a closing bracket "]" or additional details may be
	// included before that closing bracket.
	//
	// The response CSV text is preceeded by the command string and a space.
	// The host should probably discard all whitespace after the command string.
	if (strcmp(strCMD, "PLAYBACKDEVICES") == 0) {
		GetDevices();
		LogDevices(AudioDevices, "Playback (output) Devices for host command",
			false, true);

		// TODO: Should DevicesToCSV() be modified to produce a valid but
		// truncated response if dstsize is inadequate?
		snprintf(cmdReply, sizeof(cmdReply), "%s ", strCMD);
		if (!DevicesToCSV(AudioDevices, cmdReply + strlen(cmdReply),
			sizeof(cmdReply) - strlen(cmdReply), false)
		) {
			snprintf(strFault, sizeof(strFault),
				"%s failed because buffer is too small", strCMD);
			goto cmddone;
		}
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

	// Set/get PTTEnabled
	// A PTTENABLED TRUE command responds PTTENABLED now TRUE but does nothing
	// else if already true.  If currently false, parse_pttstr("RESTORE")
	// is tried.  On success, respond with PTTENABLED now TRUE.  If that doesn't
	// work, parse_catstr("RESTORE") is tried. On success, respond with
	// PTTENABLED now TRUE.  If both of these fail, or if parse_catstr()
	// succeeds but isPTTmodeEnabled() still returns false (probably because
	// the PTTON and PTTOFF strings are not also set) respond with PTTENABLED
	// cannot be set to TRUE.  RADIOPTT or RADIOCTRLPORT, RADIOPTTON,
	// RADIOPTTOFF required.  The assumption is that if CAT PTT was used, but is
	// now not usable, that the PTTON and PTTOFF strings are probably still
	// valid so that restoring RADIOCTRLPORT is suffient to restore usability of
	// CAT PTT.  TX may still be possible when PTTEnabled is false if a host
	// program is configured to to PTT control or if the radio is configured to
	// use VOX.  Ardopcf has no way of detecting whether either of these is
	// true, so it cannot detect whether or not TX has actually occured.
	if (strcmp(strCMD, "PTTENABLED") == 0) {
		bool PTTEnabled = isPTTmodeEnabled();
		// This is similar to DoTrueFalseCmd(strCMD, ptrParams, &PTTEnabled),
		// but with a wider range of responses
		if (ptrParams == NULL) {
			snprintf(cmdReply, sizeof(cmdReply), "%s %s", strCMD,
				PTTEnabled ? "TRUE" : "FALSE");
			SendReplyToHost(cmdReply);
			goto cmddone;
		}

		if (strcmp(ptrParams, "TRUE") == 0) {
			if (PTTEnabled) {
				snprintf(cmdReply, sizeof(cmdReply), "%s now TRUE", strCMD);
				SendReplyToHost(cmdReply);
				ZF_LOGD("%s now TRUE", strCMD);
				goto cmddone;
			}
			// !PTTEnabled
			// PTT can be controlled via CAT or from a PTT device.  It is
			// possible that since ardopcf was started, both CAT and PTT devices
			// have been successfully used, such that both
			// parse_pttstr("RESTORE") and parse_catstr("RESTORE") might both
			// succeed.  Try parse_catstr("RESTORE") first if
			// wasLastGoodControlCAT() returns true, else try
			// parse_pttstr("RESTORE") first.  In either case, if the first
			// fails, try the other.
			if (wasLastGoodControlCAT()) {
				// parse_catstr() can succeed while PTTENABLED remains FALSE
				// if RADIOPTTON and RADIOPTTOFF are not also both set.  So,
				// check isPTTmodeEnabled() to be sure
				if (parse_catstr("RESTORE") == 0 && isPTTmodeEnabled()) {
					snprintf(cmdReply, sizeof(cmdReply), "%s now TRUE", strCMD);
					SendReplyToHost(cmdReply);
					ZF_LOGD("%s now TRUE after parse_catstr(\"RESTORE\")", strCMD);
					goto cmddone;
				}
				if (parse_pttstr("RESTORE") == 0) {
					snprintf(cmdReply, sizeof(cmdReply), "%s now TRUE", strCMD);
					SendReplyToHost(cmdReply);
					ZF_LOGD("%s now TRUE after parse_pttstr(\"RESTORE\")", strCMD);
					goto cmddone;
				}
			} else {
				if (parse_pttstr("RESTORE") == 0) {
					snprintf(cmdReply, sizeof(cmdReply), "%s now TRUE", strCMD);
					SendReplyToHost(cmdReply);
					ZF_LOGD("%s now TRUE after parse_pttstr(\"RESTORE\")", strCMD);
					goto cmddone;
				}
				if (parse_catstr("RESTORE") == 0 && isPTTmodeEnabled()) {
					snprintf(cmdReply, sizeof(cmdReply), "%s now TRUE", strCMD);
					SendReplyToHost(cmdReply);
					ZF_LOGD("%s now TRUE after parse_catstr(\"RESTORE\")", strCMD);
					goto cmddone;
				}
			}
			if (!isPTTmodeEnabled()) {
				snprintf(strFault, sizeof(strFault),
					"%s cannot be set to TRUE.  RADIOPTT or RADIOCTRLPORT,"
					" RADIOPTTON, RADIOPTTOFF required.",
					strCMD);
			}
			goto cmddone;
		} else if (strcmp(ptrParams, "FALSE") == 0) {
			// If both are open, close both PTT and CAT.
			// The order in whcih they are closed can change
			// wasLastGoodControlCAT().  So, choose the order of closing them
			// so as not to change this result.
			if (wasLastGoodControlCAT()) {
				close_PTT(false);  // Closes PTT port if one was open.
				close_CAT(false);  // Closes CAT port if one was open.
			} else {
				close_CAT(false);  // Closes CAT port if one was open.
				close_PTT(false);  // Closes PTT port if one was open.
			}
			snprintf(cmdReply, sizeof(cmdReply), "%s now FALSE", strCMD);
			SendReplyToHost(cmdReply);
			ZF_LOGD("%s now FALSE", strCMD);
			goto cmddone;
		} else {
			snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
			goto cmddone;
		}
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
			RCB.RadioControl = Cbool(strParameters)
		ElseIf strParameters = "FALSE" Then
			If Not IsNothing(objMain.objRadio) Then
				objMain.objRadio = Nothing
			End If
			RCB.RadioControl = Cbool(strParameters)
		Else
			strFault = "Syntax Err:" & strCMD
		End If
*/

	// WARNING: If this command fails (especiaily if a remote TCP port is
	// selected and the address is unreachable), it may introduce an extended
	// delay that will cause an overrun in the RX audio system.  So, DO NOT
	// DO THIS while there is an active ARQ session.  If done when there is no
	// active ARQ session, expect that incoming frames such a ConReq or FEC data
	// may be missed.
	if (strcmp(strCMD, "RADIOCTRLPORT") == 0) {
		// Set the port to use for CAT commands.  The argument takes the same
		// form as the --cat/-c command line option, including the use of the
		// TCP: prefix to select a TCP port rather than a harware device/port.
		// Like the command line option, this command also accepts the RIGCTLD
		// special shortcut which is equivalent to the host commands:
		// RADIOCTRLPORT TCP:4532, RADIOPTTON 5420310A, and
		// RADIOPTTOFF 5420300A.
		// Two special arguments may be used that are not valid to use from the
		// command line:
		// A value of NONE closes any existing CAT connection.
		// A value of RESTORE has no effect if there is currently an open CAT
		// connection or if a valid CAT connection has not previously been set.
		// However, if a CAT connection was previously set but is now closed
		// (due to a failure or use of the NONE option to close it), then this
		// attempts to restore that connection.  The use case for
		// RADIOCTRLPORT RESTORE for a CAT connection is similar to CODEC TRUE
		// for the audio devices.
		// A hardware device/port used for CAT control may be the same
		// device/port used non-cat PTT control with the RADIOPTT command.
		// If no argument is provided, it returns the string used to open the
		// current CAT connection if one exists, or NONE if no CAT connection
		// is open.
		// The port opened with this command is used for any subsequent RADIOHEX
		// host command.  If a CAT port is open and hex strings have been set
		// with both the RADIOPTTON and RADIOPTTOFF host commands, then those
		// commands are sent to this port for PTT control.
		// Unlike most Host commands, the arguments to this command are case
		// sensitive.
		if (ptrParams == NULL) {
			sprintf(cmdReply, "%s ", strCMD);
			int catlen = get_catstr(cmdReply + strlen(cmdReply),
				sizeof(cmdReply) - strlen(cmdReply));
			if (catlen == 0) {
				snprintf(cmdReply + strlen(cmdReply),
					sizeof(cmdReply) - strlen(cmdReply), "NONE");
			}
			SendReplyToHost(cmdReply);
		} else {
			char *ptrCaseParams = strlop(cmdCopy, ' ');
			int ret;
			if (strcmp(ptrCaseParams, "NONE") == 0)
				ret = parse_catstr("");
			else
				ret = parse_catstr(ptrCaseParams);
			if (ret == 0) {
				sprintf(cmdReply, "%s now ", strCMD);
				int catlen = get_catstr(cmdReply + strlen(cmdReply),
					sizeof(cmdReply) - strlen(cmdReply));
				if (catlen == 0) {
					snprintf(cmdReply + strlen(cmdReply),
						sizeof(cmdReply) - strlen(cmdReply), "NONE");
				}
				SendReplyToHost(cmdReply);
			} else {
				snprintf(strFault, sizeof(strFault), "%s cannot be set to %s",
					strCMD, ptrCaseParams);
			}
		}
		goto cmddone;
	}

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

	if (strcmp(strCMD, "RADIOHEX") == 0) {
		// Parameter is a hex or ASCII string representing a radio specific
		// command to be immediately sent to the radio.
		// If the string contains only an even number of valid hex characters
		// (upper or lower case with no whitespace), then it is intrepreted as
		// hex.  Otherwise, if it contains only printable ASCII characters
		// 0x20-0x7E, it is interpreted as ASCII text with substition for "\\n"
		// and "\\r".  A prefix of "ASCII:" may be used to force the string to
		// be interpreted as ASCII text, and this is required for ASCII text
		// that contains only an even number of valid hex characters.
		// Unlike most Host commands, the arguments to this command are case
		// sensitive (because required ascii values may be case sensitive).
		if (ptrParams == NULL) {
			snprintf(strFault, sizeof(strFault),
				"RADIOHEX command string missing");
			goto cmddone;
		}
		char *ptrCaseParams = strlop(cmdCopy, ' ');
		if (sendCAT(ptrParams) == -1) {
			snprintf(strFault, sizeof(strFault), "RADIOHEX %s failed.",
				ptrCaseParams);
			goto cmddone;
		}
		sprintf(cmdReply, "%s %s", strCMD, ptrCaseParams);
		SendReplyToHost(cmdReply);
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
			RCB.InternalSoundCard = Cbool(strParameters)
		Else
			strFault = "Syntax Err:" & strCMD
		End If
	Case "RADIOMENU"
		If ptrSpace = -1 Then
			SendReplyToHost(strCommand & " " & objMain.RadioMenu.Enabled.ToString)
		ElseIf strParameters = "TRUE" Or strParameters = "FALSE" Then
			objMain.RadioMenu.Enabled = Cbool(strParameters)
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
			RCB.PTTDTR = Cbool(strParameters)
			objMain.objRadio.InitRadioPorts()
		Else
			strFault = "Syntax Err:" & strCMD
		End If
	Case "RADIOPTTRTS"
		If ptrSpace = -1 Then
			SendReplyToHost(strCommand & " " & RCB.PTTRTS.ToString)
		ElseIf strParameters = "TRUE" Or strParameters = "FALSE" Then
			RCB.PTTRTS = Cbool(strParameters)
			objMain.objRadio.InitRadioPorts()
		Else
			strFault = "Syntax Err:" & strCMD
		End If
		// End of optional Radio Commands
*/

	if (strcmp(strCMD, "RADIOPTT") == 0) {
		// Set the device to use for non-cat PTT control.  The argument takes
		// the same form as the --ptt/-p command line option, including the use
		// of RTS: DTR: and CM108: prefixes.  An argument of CM108:? will fail,
		// but like the command line option, on Windows, this will write a list
		// of available CM108 devices to the log.
		// Two special arguments may be used that are not valid to use from the
		// command line:
		// A value of NONE closes any existing non-cat PTT control connection.
		// A value of RESTORE has no effect if there is currently an open
		// non-cat PTT control connection or if a valid non-cat PTT control
		// connection has not previously been set.  However, if a non-cat PTT
		// control connection was previously set but is now inactive (due to a
		// failure or use of the NONE option to close it), then this attempts to
		// restore that connection.  The use case for RADIOPTT RESTORE for
		// non-cat PTT control is similar to CODEC TRUE for the audio devices.
		// A hardware device/port used for RTS or DTR PTT conrol may be the same
		// device/port used CAT control with the RADIOCTRLPORT command.
		// If no argument is provided, return the string used to set the current
		// non-cat PTT control connection if one exists, or NONE if no non-cat
		// PTT control connection is open.
		// Unlike most Host commands, the arguments to this command are case
		// sensitive.
		if (ptrParams == NULL) {
			sprintf(cmdReply, "%s ", strCMD);
			int pttlen = get_pttstr(cmdReply + strlen(cmdReply),
				sizeof(cmdReply) - strlen(cmdReply));
			if (pttlen == 0) {
				snprintf(cmdReply + strlen(cmdReply),
					sizeof(cmdReply) - strlen(cmdReply), "NONE");
			}
			SendReplyToHost(cmdReply);
		} else {
			char *ptrCaseParams = strlop(cmdCopy, ' ');
			int ret;
			if (strcmp(ptrCaseParams, "NONE") == 0)
				ret = parse_pttstr("");
			else
				ret = parse_pttstr(ptrCaseParams);
			if (ret == 0) {
				sprintf(cmdReply, "%s now ", strCMD);
				int pttlen = get_pttstr(cmdReply + strlen(cmdReply),
					sizeof(cmdReply) - strlen(cmdReply));
				if (pttlen == 0) {
					snprintf(cmdReply + strlen(cmdReply),
						sizeof(cmdReply) - strlen(cmdReply), "NONE");
				}
				SendReplyToHost(cmdReply);
			} else {
				snprintf(strFault, sizeof(strFault), "%s cannot be set to %s",
					strCMD, ptrCaseParams);
			}
		}
		goto cmddone;
	}

	if (strcmp(strCMD, "RADIOPTTOFF") == 0) {
		// Parameter is a hex or ASCII string representing a radio specific
		// command to be send to the radio to transition from TX to RX.
		// If the string contains only an even number of valid hex characters
		// (upper or lower case with no whitespace), then it is intrepreted as
		// hex.  Otherwise, if it contains only printable ASCII characters
		// 0x20-0x7E, it is interpreted as ASCII text with substitution for
		// "\\n" and "\\r".  A prefix of "ASCII:" may be used to force the
		// string to be interpreted as ASCII text, and this is required for
		// ASCII text that contains only an even number of valid hex characters.
		// The special value of NONE may be used discard an existing value.
		// Unlike most Host commands, the arguments to this command are case
		// sensitive (because required ascii values may be case sensitive).
		if (ptrParams == NULL) {
			char tmpstr[MAXCATLEN * 2 + 1];
			if (get_ptt_off_cmd(tmpstr, sizeof(tmpstr)) == -1
				|| strlen(tmpstr) == 0
			) {
				sprintf(cmdReply, "%s VALUE NOT SET", strCMD);
				SendReplyToHost(cmdReply);
				goto cmddone;
			}
			sprintf(cmdReply, "%s %s", strCMD, tmpstr);
			SendReplyToHost(cmdReply);
			goto cmddone;
		}
		char *ptrCaseParams = strlop(cmdCopy, ' ');
		if (strcmp(ptrCaseParams, "NONE") == 0) {
			set_ptt_off_cmd("", "RADIOPTTOFF host command");
			sprintf(cmdReply, "%s now %s", strCMD, ptrCaseParams);
			SendReplyToHost(cmdReply);
			goto cmddone;
		}
		if (set_ptt_off_cmd(ptrCaseParams, "RADIOPTTOFF host command") == -1) {
			snprintf(strFault, sizeof(strFault),
				"RADIOPTTOFF command string is invalid.");
			goto cmddone;
		}
		sprintf(cmdReply, "%s now %s", strCMD, ptrCaseParams);
		SendReplyToHost(cmdReply);
		goto cmddone;
	}

	if (strcmp(strCMD, "RADIOPTTON") == 0) {
		// Parameter is a hex or ASCII string representing a radio specific
		// command to be send to the radio to transition from RX to TX.
		// If the string contains only an even number of valid hex characters
		// (upper or lower case with no whitespace), then it is intrepreted as
		// hex.  Otherwise, if it contains only printable ASCII characters
		// 0x20-0x7E, it is interpreted as ASCII text with substitution for
		// "\\n" and "\\r".  A prefix of "ASCII:" may be used to force the
		// string to be interpreted as ASCII text, and this is required for
		// ASCII text that contains only an even number of valid hex characters.
		// The special value of NONE may be used discard an existing value.
		// Unlike most Host commands, the arguments to this command are case
		// sensitive (because required ascii values may be case sensitive).
		if (ptrParams == NULL) {
			char tmpstr[MAXCATLEN * 2 + 1];
			if (get_ptt_on_cmd(tmpstr, sizeof(tmpstr)) == -1
				|| strlen(tmpstr) == 0
			) {
				sprintf(cmdReply, "%s VALUE NOT SET", strCMD);
				SendReplyToHost(cmdReply);
				goto cmddone;
			}
			sprintf(cmdReply, "%s %s", strCMD, tmpstr);
			SendReplyToHost(cmdReply);
			goto cmddone;
		}
		char *ptrCaseParams = strlop(cmdCopy, ' ');
		if (strcmp(ptrCaseParams, "NONE") == 0) {
			set_ptt_on_cmd("", "RADIOPTTON host command");
			sprintf(cmdReply, "%s now %s", strCMD, ptrCaseParams);
			SendReplyToHost(cmdReply);
			goto cmddone;
		}
		if (set_ptt_on_cmd(ptrCaseParams, "RADIOPTTON host command") == -1) {
			snprintf(strFault, sizeof(strFault),
				"RADIOPTTON command string is invalid.");
			goto cmddone;
		}
		sprintf(cmdReply, "%s now %s", strCMD, ptrCaseParams);
		SendReplyToHost(cmdReply);
		goto cmddone;
	}

	// Start/Stop recording of RX audio to a WAV file.
	// Use TRUE to start, use FALSE to stop, or provide no argument to query.
	//
	// Writing of audio data to the WAV file is paused while transmitting, but
	// it resumes automatically upon switching from transmit to receive,
	// continuing to write to the same WAV file.
	//
	// This command will fail if Ardop is currently recording receive audio due
	// to use of the -w or --writewav command line option.  Similarly, while
	// RECRX is TRUE, the -w or --writewav command line option is temporarily
	// disabled (but such recording may begin at the end of the next TX after
	// RECRX is set to FALSE).
	//
	// Do nothing if argument is TRUE when RECRX is already TRUE, or argument is
	// FALSE when RECRX is already FALSE.
	if (strcmp(strCMD, "RECRX") == 0)
	{
		if (rxwf != NULL && !HWriteRxWav) {
			// Currently recording due to -w or --writewav
			snprintf(strFault, sizeof(strFault),
				"RECRX IGNORED while recording due to -w or --writewav.");
			goto cmddone;
		}
		DoTrueFalseCmd(strCMD, ptrParams, &HWriteRxWav);  // Also sends reply
		if (HWriteRxWav && rxwf == NULL) {
			StartRxWav();  // This also updates WebGui
		} else if (rxwf != NULL && !HWriteRxWav) {
			// This is same condition that was checked before updating
			// HWriteRxWav.  If true then, it indicated that recording due
			// to -w or --writewav was active, so nothing was done.  If true
			// here (and not there), it indicates that recording due to
			// RECRX is active, but that RECRX FALSE has just been received.
			// So, stop recording.
			CloseWav(rxwf);
			rxwf = NULL;
			wg_send_wavrx(0, false);  // update "RECORDING RX" indicator on WebGui
		}
		goto cmddone;
	}

	// Set/get RXEnabled
	// An RXENABLED TRUE command responds RXENABLED now TRUE but does nothing
	// else if already true.  If currently false, OpenSoundCapture("RESTORE")
	// is tried.  On success, respond with RXENABLED now TRUE.  On failure
	// respond with RXENABLED cannot be set to TRUE.  CAPTURE required.  So
	// RXENABLED TRUE is similar in effect to CAPURE RESTORE, but the command
	// responses are different.
	if (strcmp(strCMD, "RXENABLED") == 0) {
		// This is similar to DoTrueFalseCmd(strCMD, ptrParams, &RXEnabled), but
		// with a wider range of responses
		if (ptrParams == NULL) {
			snprintf(cmdReply, sizeof(cmdReply), "%s %s", strCMD,
				RXEnabled ? "TRUE" : "FALSE");
			SendReplyToHost(cmdReply);
			goto cmddone;
		}

		if (strcmp(ptrParams, "TRUE") == 0) {
			if (RXEnabled) {
				snprintf(cmdReply, sizeof(cmdReply), "%s now TRUE", strCMD);
				SendReplyToHost(cmdReply);
				ZF_LOGD("%s now TRUE", strCMD);
				goto cmddone;
			}
			// !RXEnabled
			if (OpenSoundCapture("RESTORE", getCch(true))) {
				snprintf(cmdReply, sizeof(cmdReply), "%s now TRUE", strCMD);
				SendReplyToHost(cmdReply);
				ZF_LOGD("%s now TRUE", strCMD);
				goto cmddone;
			}
			// !RXEnabled, and OpenSoundCapture("RESTORE") failed
			snprintf(strFault, sizeof(strFault),
				"%s cannot be set to TRUE.  CAPTURE required", strCMD);
			goto cmddone;
		} else if (strcmp(ptrParams, "FALSE") == 0) {
			// sets RXEnabled = false
			CloseSoundCapture(true);  // calls updateWebGuiAudioConfig(true);
			snprintf(cmdReply, sizeof(cmdReply), "%s now FALSE", strCMD);
			SendReplyToHost(cmdReply);
			ZF_LOGD("%s now FALSE", strCMD);
			goto cmddone;
		} else {
			snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
			goto cmddone;
		}
		goto cmddone;
	}

	// RXLEVEL command previously provided for TEENSY support removed

	if (strcmp(strCMD, "SDFTENABLED") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &UseSDFT);
		goto cmddone;
	}

	if (strcmp(strCMD, "SENDID") == 0)
	{
		// Previously this check to ensure that MYCALL is set was handled in
		// the response to seting NeedID=true.  Adding this test here helps
		// provide a consistent fault message for any attempt to initiate
		// transmitting without first setting MYCALL.
		if (!stationid_ok(&Callsign)) {
			snprintf(strFault, sizeof(strFault), "MYCALL not set");
			goto cmddone;
		}

		if (ProtocolState == DISC)
		{
			NeedID = true;  // Send from background
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
			objMain.SetupMenu.Enabled = Cbool(strParameters)
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

	if (strcmp(strCMD, "TWOTONETEST") == 0)
	{
		// Previously this was permitted without MYCALL being set.  However,
		// this new restriction helps ensure that an IDFrame can be sent to
		// identify all transmissions including this two tone test signal.
		if (!stationid_ok(&Callsign)) {
			snprintf(strFault, sizeof(strFault), "MYCALL not set");
			goto cmddone;
		}

		if (ProtocolState == DISC)
		{
			NeedTwoToneTest = true;  // Send from background
			SendReplyToHost(strCMD);
		}
		else
			snprintf(strFault, sizeof(strFault), "Not from state %s", ARDOPStates[ProtocolState]);

		goto cmddone;

	}

	// Set/get TXEnabled
	// A TXENABLED TRUE command responds TXENABLED now TRUE but does nothing
	// else if already true.  If currently false, OpenSoundPlayback("RESTORE")
	// is tried.  On success, respond with TXENABLED now TRUE.  On failure
	// respond with TXENABLED cannot be set to TRUE.  PLAYBACK required.  So
	// TXENABLED TRUE is similar in effect to PLAYBACK RESTORE, but the command
	// responses are different.
	if (strcmp(strCMD, "TXENABLED") == 0) {
		// This is similar to DoTrueFalseCmd(strCMD, ptrParams, &TXEnabled), but
		// with a wider range of responses
		if (ptrParams == NULL) {
			snprintf(cmdReply, sizeof(cmdReply), "%s %s", strCMD,
				TXEnabled ? "TRUE" : "FALSE");
			SendReplyToHost(cmdReply);
			goto cmddone;
		}

		if (strcmp(ptrParams, "TRUE") == 0) {
			if (TXEnabled) {
				snprintf(cmdReply, sizeof(cmdReply), "%s now TRUE", strCMD);
				SendReplyToHost(cmdReply);
				ZF_LOGD("%s now TRUE", strCMD);
				goto cmddone;
			}
			// !TXEnabled
			if (OpenSoundPlayback("RESTORE", getCch(true))) {
				snprintf(cmdReply, sizeof(cmdReply), "%s now TRUE", strCMD);
				SendReplyToHost(cmdReply);
				ZF_LOGD("%s now TRUE", strCMD);
				goto cmddone;
			}
			// !TXEnabled, and OpenSoundPlayback("RESTORE") failed
			snprintf(strFault, sizeof(strFault),
				"%s cannot be set to TRUE.  PLAYBACK required", strCMD);
			goto cmddone;
		} else if (strcmp(ptrParams, "FALSE") == 0) {
			// sets TXEnabled = false
			CloseSoundPlayback(true);  // calls updateWebGuiAudioConfig(true);
			snprintf(cmdReply, sizeof(cmdReply), "%s now FALSE", strCMD);
			SendReplyToHost(cmdReply);
			ZF_LOGD("%s now FALSE", strCMD);
			goto cmddone;
		} else {
			snprintf(strFault, sizeof(strFault), "Syntax Err: %s %s", strCMD, ptrParams);
			goto cmddone;
		}
		goto cmddone;
	}

	///////////////////////////////////////////////////////////////
	// The TXFRAME command is intended for development and debugging.
	// It is NOT intended for normal use by Host applications.
	// It may be removed or modfied without notice in future
	// versions of ardopcf.
	///////////////////////////////////////////////////////////////
	if (strcmp(strCMD, "TXFRAME") == 0)
	{
		// Like all other host commands that initiate transmitting, MYCALL must
		// be set first.
		if (!stationid_ok(&Callsign)) {
			snprintf(strFault, sizeof(strFault), "MYCALL not set");
			goto cmddone;
		}

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

	// TXLEVEL command previously provided for TEENSY support removed

	if (strcmp(strCMD, "USE600MODES") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &Use600Modes);
		goto cmddone;
	}

	if (strcmp(strCMD, "VERSION") == 0)
	{
		sprintf(cmdReply, "VERSION %s_%s_%s",
			ProductName, ProductVersion, OSName);
		SendReplyToHost(cmdReply);
		goto cmddone;
	}
	// RDY processed earlier Case "RDY".  no response required for RDY

	if (strcmp(strCMD, "WRITERXWAV") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &WriteRxWav);
		goto cmddone;
	}

	if (strcmp(strCMD, "WRITETXWAV") == 0)
	{
		DoTrueFalseCmd(strCMD, ptrParams, &WriteTxWav);
		goto cmddone;
	}

	snprintf(strFault, sizeof(strFault), "CMD %s not recognized", strCMD);

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

void AddTagToDataAndSendToHost(UCHAR * bytData, char * strTag, int Len) {
	// The largest data capacity of any Ardop data frame is 1024 bytes for a
	// 16QAM.200.100 frame type.  Worst case behavior of eutf8() produces output
	// that is three times as long as the input data plus one byte.  So, an
	// available message length of 3200 should be more than adequate to log the
	// eutf8 encoded data from any data frame along with a suitable preamble.
	char Msg[3200] = "";
	snprintf(Msg, sizeof(Msg), "[RX Data: Send To Host with TAG=%s] %d bytes (eutf8):\n", strTag, Len);
	eutf8(Msg + strlen(Msg), sizeof(Msg) - strlen(Msg), (char*) bytData, Len);
	ZF_LOGV("%s", Msg);

	TCPAddTagToDataAndSendToHost(bytData, strTag, Len);
	if (WG_DevMode) {
		if (utf8_check(bytData, Len) == NULL)
			wg_send_hostdatat(0, strTag, bytData, Len);
		else
			wg_send_hostdatab(0, strTag, bytData, Len);
	}
}
