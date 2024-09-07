//
// Code Common to all versions of ARDOP.
//

// definition of ProductVersion moved to version.h
// This simplifies test builds with using local version numbers independent
//   of version numbers pushed to git repository.
#include "common/version.h"

#ifdef WIN32
#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#else
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#define SOCKET int
#define closesocket close
#define HANDLE int
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>

#include "common/ardopcommon.h"
#include "common/wav.h"

void ProcessCommandFromHost(char * strCMD);

const char strLogLevels[9][13] =
{
	"LOGEMERGENCY",
	"LOGALERT",
	"LOGCRIT",
	"LOGERROR",
	"LOGWARNING",
	"LOGNOTICE",
	"LOGINFO",
	"LOGDEBUG",
	"LOGDEBUGPLUS"
};

extern int gotGPIO;
extern int useGPIO;

extern int pttGPIOPin;
extern int pttGPIOInvert;

extern HANDLE hCATDevice;  // port for Rig Control
extern char CATPort[80];
extern int CATBAUD;
extern int EnableHostCATRX;

extern HANDLE hPTTDevice;  // port for PTT
extern char PTTPort[80];  // Port for Hardware PTT - may be same as control port.
extern int PTTBAUD;

extern unsigned char PTTOnCmd[];
extern unsigned char PTTOnCmdLen;

extern unsigned char PTTOffCmd[];
extern unsigned char PTTOffCmdLen;

extern int PTTMode;  // PTT Control Flags.
extern char HostPort[80];
extern char CaptureDevice[80];
extern char PlaybackDevice[80];

int extraDelay = 0;  // Used for long delay paths eg Satellite
int	intARQDefaultDlyMs = 240;
int TrailerLength = 20;
int wg_port = 0;  // If not changed from 0, do not use WebGui
BOOL InitRXO = FALSE;
BOOL WriteRxWav = FALSE;
BOOL WriteTxWav = FALSE;
BOOL TwoToneAndExit = FALSE;
BOOL UseSDFT = FALSE;
BOOL FixTiming = TRUE;
BOOL WG_DevMode = FALSE;
char DecodeWav[256] = "";  // Pathname of WAV file to decode.
// HostCommands may contain one or more semicolon separated host commands
// provided as a command line parameter.  These are to be interpreted at
// startup of ardopcf as if they were issued by a connected host program
char HostCommands[3000] = "";
bool DeprecationWarningsIssued = false;

int PTTMode = PTTRTS;  // PTT Control Flags.

struct sockaddr HamlibAddr;  // Dest for above
int useHamLib = 0;


extern int LeaderLength;

extern BOOL UseLeftRX;
extern BOOL UseRightRX;

extern BOOL UseLeftTX;
extern BOOL UseRightTX;

extern char LogDir[256];

static struct option long_options[] =
{
	{"logdir",  required_argument, 0 , 'l'},
	{"hostcommands",  required_argument, 0 , 'H'},
	{"verboselog",  required_argument, 0 , 'v'},
	{"verboseconsole",  required_argument, 0 , 'V'},
	{"ptt",  required_argument, 0 , 'p'},
	{"cat",  required_argument, 0 , 'c'},
	{"keystring",  required_argument, 0 , 'k'},
	{"unkeystring",  required_argument, 0 , 'u'},
	{"extradelay",  required_argument, 0 , 'e'},
	{"leaderlength",  required_argument, 0 , 'x'},
	{"trailerlength",  required_argument, 0 , 't'},
	{"webgui",  required_argument, 0 , 'G'},
	{"receiveonly", no_argument, 0, 'r'},
	{"writewav",  no_argument, 0, 'w'},
	{"writetxwav",  no_argument, 0, 'T'},
	{"decodewav",  required_argument, 0, 'd'},
	{"twotone", no_argument, 0, 'n'},
	{"sdft", no_argument, 0, 's'},
	{"ignorealsaerror", no_argument, 0, 'A'},
	{"help",  no_argument, 0 , 'h'},
	{ NULL , no_argument , NULL , no_argument }
};

char HelpScreen[] =
	"Usage:\n"
	"%s port [capturedevice playbackdevice] [Options]\n"
	"defaults are port = 8515, capture device ARDOP playback device ARDOP\n"
	"If you need to specify capture and playback devices you must specify port\n"
	"\n"
	"port is TCP Port Number\n"
	"\n"
	"Optional Paramters\n"
	"-l path or --logdir path             Path for log files\n"
	"-H string or --hostcommands string   String of semicolon separated host commands to apply\n"
	"                                       to ardopcf during startup, as if they had come from\n"
	"                                       a connected host.  Since this duplicates the\n"
	"                                       functionality of several existing optional parameters,\n"
	"                                       they are marked below as (D) for deprecated.\n"
	"                                       This indicates that they will be removed in a future\n"
	"                                       release of ardopcf, and that their use should immediately\n"
	"                                       be discontinued.  Information about replacement host\n"
	"                                       commands is given for all deprecated parameters.\n"
	"(D) -v path or --verboselog val      Increase (decr for val<0) file log level from default.\n"
	"-----> Use -H \"LOGLEVEL #\" instead, where # is an integer from 0 to 8.\n"
	"(D) -V path or --verboseconsole val  Increase (decr for val<0) console log level from default.\n"
	"-----> Use -H \"CONSOLELOG #\" instead, where # is an integer from 0 to 8.\n"
	"-c device or --cat device            Device to use for CAT Control\n"
	"-p device or --ptt device            Device to use for PTT control using RTS\n"
#ifdef LINBPQ
	"                                     or CM108-like Device to use for PTT\n"
#else
	"                                     or VID:PID of CM108-like Device to use for PTT\n"
#endif
	"-g [Pin]                             GPIO pin to use for PTT (ARM Only)\n"
	"                                     Default 17. use -Pin to invert PTT state\n"
	"-k string or --keystring string      String (In HEX) to send to the radio to key PTT\n"
	"-u string or --unkeystring string    String (In HEX) to send to the radio to unkey PTT\n"
	"-L use Left Channel of Soundcard for receive in stereo mode\n"
	"-R use Right Channel of Soundcard for receive in stereo mode\n"
	"-e val or --extradelay val           Extend no response timeout for use on paths with long delay\n"
	"(D) -x val or --leaderlength val     Sets Leader Length (mS)\n"
	"-----> Use -H \"LEADER #\" instead, where # is an integer in the range or 120 to 2500 (mS)\n"
	"(D) -t val or --trailerlength val    Sets Trailer Length (mS)\n"
	"-----> Use -H \"TRAILER #\" instead, where # is an integer in the range or 0 to 200 (mS)\n"
	"-G port or --webgui port             Enable WebGui and specify port number.\n"
	"(D) -r or --receiveonly              Start in RXO (receive only) mode.\n"
	"-----> Use -H \"PROTOCOLMODE RXO\" instead\n"
	"-w or --writewav                     Write WAV files of received audio for debugging.\n"
	"-T or --writetxwav                   Write WAV files of sent audio for debugging.\n"
	"-d pathname or --decodewav pathname  Pathname of WAV file to decode instead of listening.\n"
	"(D) -n or --twotone                  Send a 5 second two tone signal and exit.\n"
	"-----> Use -H \"TWOTONETEST;CLOSE\" instead\n"
	"-s or --sdft                         Use the alternative Sliding DFT based 4FSK decoder.\n"
	"-A or --ignorealsaerror              Ignore ALSA config error that causes timing error.\n"
	"                                       DO NOT use -A option except for testing/debugging,\n"
	"                                       or if ardopcf fails to run and suggests trying this."
	"\n"
	" CAT and RTS PTT can share the same port.\n"
	" See the ardop documentation for more information on cat and ptt options\n"
	"  including when you need to use -k and -u\n\n";

void processargs(int argc, char * argv[])
{
	// Since the log directory and log levels have not yet been set, don't
	// write to the log in this function.  Instead, print warnings and errors
	// to the console.

	int val;
	UCHAR * ptr1;
	UCHAR * ptr2;
	int c;

	while (1)
	{
		int option_index = 0;

		c = getopt_long(argc, argv, "l:H:v:V:c:p:g::k:u:e:G:x:hLRyt:rzwTd:nsA", long_options, &option_index);


		// Check for end of operation or error
		if (c == -1)
			break;

		// Handle options
		switch (c)
		{
		case 'h':

			printf("%s Version %s (https://www.github.com/pflarue/ardop)\n", ProductName, ProductVersion);
			printf("Copyright (c) 2014-2024 Rick Muething, John Wiseman, Peter LaRue\n");
			printf(
				"See https://github.com/pflarue/ardop/blob/master/LICENSE for licence details including\n"
				"  information about authors of external libraries used and their licenses.\n"
			);
			printf(HelpScreen, ProductName);
			exit (0);

		case 'H':
			if (strlen(optarg) >= sizeof(HostCommands)) {
				printf("ERROR: --hostcommands (or -H) argument too long.  Ignoring this parameter.\n");
				break;
			}
			strcpy(HostCommands, optarg);
			break;

		case 'l':
			strcpy(LogDir, optarg);
			break;

		case 'v':
			printf(
				"*********************************************************************\n"
				"* WARNING: The -v and --verboselog parameters are DEPRECATED.  They *\n"
				"* will be eliminated in a future release of ardopcf.  So, their use *\n"
				"* should be immediately discontinued.  Use -H \"LOGLEVEL #\" where    *\n"
				"* # must be an integer between 0 (to write only the most severe     *\n"
				"* messages to the log file) and %d (to write all possible messages   *\n"
				"* to the log file).  The default value is %d when this command is    *\n"
				"* not used.                                                         *\n"
				"*********************************************************************\n",
				LOGDEBUGPLUS, FileLogLevel);
			DeprecationWarningsIssued = true;
			FileLogLevel += atoi(optarg);
			if (FileLogLevel > LOGDEBUGPLUS)
				FileLogLevel = LOGDEBUGPLUS;
			else if (FileLogLevel < LOGEMERGENCY)
				FileLogLevel = LOGEMERGENCY;
			break;

		case 'V':
			printf(
				"*********************************************************************\n"
				"* WARNING: The -V and --verboseconsole parameters are DEPRECATED.   *\n"
				"* They will be eliminated in a future release of ardopcf.  So,      *\n"
				"* their use should be immediately discontinued.  Use                *\n"
				"* -H \"CONSOLELOG #\" where # must be an integer between 0 (to        *\n"
				"* print only the most severe messages to the console) and %d (to     *\n"
				"* print all possible messages to the console).  The default value   *\n"
				"* is %d when this command is not used.                               *\n"
				"*********************************************************************\n",
				LOGDEBUGPLUS, ConsoleLogLevel);
			DeprecationWarningsIssued = true;
			ConsoleLogLevel += atoi(optarg);
			if (ConsoleLogLevel > LOGDEBUGPLUS)
				ConsoleLogLevel = LOGDEBUGPLUS;
			else if (ConsoleLogLevel < LOGEMERGENCY)
				ConsoleLogLevel = LOGEMERGENCY;
			break;

		case 'g':
			if (optarg)
				pttGPIOPin = atoi(optarg);
			else
				pttGPIOPin = 17;
			break;

		case 'k':

			ptr1 = optarg;
			ptr2 = PTTOnCmd;
			if (ptr1 == NULL)

			{
				printf("RADIOPTTON command string missing\n");
				break;
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
			PTTMode = PTTCI_V;

			printf ("PTTOnString %s len %d\n", optarg, PTTOnCmdLen);
			break;

		case 'u':

			ptr1 = optarg;
			ptr2 = PTTOffCmd;

			if (ptr1 == NULL)
			{
				printf("RADIOPTTOFF command string missing\n");
				break;
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

			printf ("PTTOffString %s len %d\n", optarg, PTTOffCmdLen);
			break;

		case 'p':
			strcpy(PTTPort, optarg);
			break;

		case 'c':
			strcpy(CATPort, optarg);
			break;

		case 'L':
//			UseLeftTX = UseLeftRX = 1;
//			UseRightTX = UseRightRX = 0;
			UseLeftRX = 1;
			UseRightRX = 0;
			break;

		case 'R':
//			UseLeftTX = UseLeftRX = 0;
//			UseRightTX = UseRightRX = 1;
			UseLeftRX = 0;
			UseRightRX = 1;
			break;

		case 'y':
			UseLeftTX = 1;
			UseRightTX = 0;
			break;

		case 'z':
			UseLeftTX = 0;
			UseRightTX = 1;
			break;

		case 'e':
			extraDelay = atoi(optarg);
			break;

		case 'G':
			wg_port = atoi(optarg);
			break;

		case 'x':
			printf(
				"*********************************************************************\n"
				"* WARNING: The -x and --leaderlength parameters are DEPRECATED.     *\n"
				"* They will be eliminated in a future release of ardopcf.  So,      *\n"
				"* their use should be immediately discontinued.  Use                *\n"
				"* -H \"LEADER #\" instead, where # is an integer in the range of      *\n"
				"* 120 to 2500 (mS).                                                 *\n"
				"*********************************************************************\n");
			intARQDefaultDlyMs = LeaderLength = atoi(optarg);
			DeprecationWarningsIssued = true;
			break;

		case 't':
			printf(
				"*********************************************************************\n"
				"* WARNING: The -t and --trailerlength parameters are DEPRECATED.    *\n"
				"* They will be eliminated in a future release of ardopcf.  So,      *\n"
				"* their use should be immediately discontinued.  Use                *\n"
				"* -H \"TRAILER #\" instead, where # is an integer in the range of     *\n"
				"* 0 to 200 (mS).                                                    *\n"
				"*********************************************************************\n");
			DeprecationWarningsIssued = true;
			TrailerLength = atoi(optarg);
			break;

		case 'r':
			printf(
				"*********************************************************************\n"
				"* WARNING: The -r and --receiveonly parameters are DEPRECATED.      *\n"
				"* They will be eliminated in a future release of ardopcf.  So,      *\n"
				"* their use should be immediately discontinued.  Use                *\n"
				"* -H \"PROTOCOLMODE RXO\" instead.                                    *\n"
				"*********************************************************************\n");
			DeprecationWarningsIssued = true;
			InitRXO = TRUE;
			break;

		case 'w':
			WriteRxWav = TRUE;
			break;

		case 'T':
			WriteTxWav = TRUE;
			break;

		case 'd':
			strcpy(DecodeWav, optarg);
			break;

		case 'n':
			printf(
				"*********************************************************************\n"
				"* WARNING: The -n and --twotonetest parameters are DEPRECATED.      *\n"
				"* They will be eliminated in a future release of ardopcf.  So,      *\n"
				"* their use should be immediately discontinued.  Use                *\n"
				"* -H \"TWOTONETEST;CLOSE\" instead.                                   *\n"
				"*********************************************************************\n");
			DeprecationWarningsIssued = true;
			TwoToneAndExit = TRUE;
			break;

		case 's':
			UseSDFT = TRUE;
			break;

		case 'A':
			FixTiming = FALSE;
			break;

		case '?':
			// getopt_long already printed an error message.
			break;

		default:
			abort();
		}
	}


	if (argc > optind)
	{
		strcpy(HostPort, argv[optind]);
	}

	if (argc > optind + 2)
	{
		strcpy(CaptureDevice, argv[optind + 1]);
		strcpy(PlaybackDevice, argv[optind + 2]);
	}

	if (argc > optind + 3)
	{
		printf("%s Version %s\n", ProductName, ProductVersion);
		printf("Only three positional parameters allowed\n");
		printf ("%s", HelpScreen);
		exit(0);
	}

	if (wg_port < 0) {
		// This is an "undocumented" feature that may be discontinued in future releases
		wg_port = -wg_port;
		WG_DevMode = TRUE;
	}
	if (HostPort[0] != 0x00 && wg_port == atoi(HostPort)) {
		printf(
			"WebGui port (%d) may not be the same as host port (%s)",
			wg_port, HostPort);
		exit(0);
	}
	else if (HostPort[0] != 0x00 && wg_port == atoi(HostPort) + 1) {
		printf(
			"WebGui port (%d) may not be one greater than host port (%s)"
			" since that is used as the host data port.",
			wg_port, HostPort);
		exit(0);
	}
	else if (wg_port == 8515) {
		printf(
			"WebGui port (%d) may not be equal to the default host port (8515)"
			" when an alternative host port is not specified.",
			wg_port);
		exit(0);
	}
	else if (wg_port == 8516) {
		printf(
			"WebGui port (%d) may not be equal to one greater than the default"
			" host port (8515 + 1 = 8516) when an alternative host port is not"
			" specified since that is used as the host data port.",
			wg_port);
		exit(0);
	}
}

extern enum _ARDOPState ProtocolState;

extern int blnARQDisconnect;

void ClosePacketSessions();

void LostHost()
{
	// Called if Link to host is lost

	// Close any sessions

	if (ProtocolState == IDLE || ProtocolState == IRS || ProtocolState == ISS || ProtocolState == IRStoISS)
		blnARQDisconnect = 1;
}

void displayState(const char * State)
{
	char Msg[80];
	strcpy(Msg, State);
	SendtoGUI('S', Msg, strlen(Msg) + 1);  // Protocol State
}


void displayCall(int dirn, char * Call)
{
	char Msg[32];
	sprintf(Msg, "%c%s", dirn, Call);
	SendtoGUI('I', Msg, strlen(Msg));
}

// When decoding a WAV file, WavNow will be set to the offset from the
// start of that file to the end of the data about to be passed to
// ProcessNewSamples() in ms.  Thus, it serves as a proxy for Now()
// which is otherwise based on clock time.  This substitution occurs
// in getTicks() in ALSASound.c or Waveout.c.
int WavNow;

int decode_wav()
{
	FILE *wavf;
	unsigned char wavHead[44];
	size_t readCount;
	const char headStr[5] = "RIFF";
	unsigned int nSamples;
	int sampleRate;
	short samples[1024];
	const unsigned int blocksize = 240;  // Number of 16-bit samples to read at a time
	char *nextHostCommand = HostCommands;
	WavNow = 0;

	if (DeprecationWarningsIssued) {
		WriteDebugLog(LOGERROR,
			"*********************************************************************\n"
			"* WARNING: DEPRECATED command line parameters used.  Details shown  *\n"
			"* above.  You may need to scroll up or review the Debug Log file to *\n"
			"* see those details                                                 *\n"
			"*********************************************************************\n");
	}
	while (nextHostCommand != NULL) {
		// Process the next host command from the --hostcommands
		// command line argument.
		char *thisHostCommand = nextHostCommand;
		nextHostCommand = strlop(nextHostCommand, ';');
		if (thisHostCommand[0] != 0x00)
			// not an empty string
			ProcessCommandFromHost(thisHostCommand);
	}

	// Regardless of whether this was set with a command line argument, proceed in
	// RXO (receive only) protocol mode.  During normal operation, this is set
	// in ardopmain(), which is not used when decoding a WAV file.
	setProtocolMode("RXO");

	WriteDebugLog(LOGINFO, "Decoding WAV file %s.", DecodeWav);
	wavf = fopen(DecodeWav, "rb");
	if (wavf == NULL)
	{
		WriteDebugLog(LOGERROR, "Unable to open WAV file %s.", DecodeWav);
		return 1;
	}
	readCount = fread(wavHead, 1, 44, wavf);
	if (readCount != 44)
	{
		WriteDebugLog(LOGERROR, "Error reading WAV file header.");
		return 2;
	}
	if (memcmp(wavHead, headStr, 4) != 0)
	{
		WriteDebugLog(LOGERROR, "%s is not a valid WAV file. 0x%x %x %x %x != 0x%x %x %x %x",
					DecodeWav, wavHead[0], wavHead[1], wavHead[2], wavHead[3],
					headStr[0], headStr[1], headStr[2], headStr[3]);
		return 3;
	}
	if (wavHead[20] != 0x01)
	{
		WriteDebugLog(LOGERROR, "Unexpected WAVE type.");
		return 4;
	}
	if (wavHead[22] != 0x01)
	{
		WriteDebugLog(LOGERROR, "Expected single channel WAV.  Consider converting it with SoX.");
		return 7;
	}
	sampleRate = wavHead[24] + (wavHead[25] << 8) + (wavHead[26] << 16) + (wavHead[27] << 24);
	if (sampleRate != 12000)
	{
		WriteDebugLog(LOGERROR, "Expected 12kHz sample rate but found %d Hz.  Consider converting it with SoX.", sampleRate);
		return 8;
	}

	nSamples = (wavHead[40] + (wavHead[41] << 8) + (wavHead[42] << 16) + (wavHead[43] << 24)) / 2;
	WriteDebugLog(LOGDEBUG, "Reading %d 16-bit samples.", nSamples);
	// Send blocksize silent samples to ProcessNewSamples() before start of WAV file data.
	memset(samples, 0, sizeof(samples));
	ProcessNewSamples(samples, blocksize);
	while (nSamples >= blocksize)
	{
		readCount = fread(samples, 2, blocksize, wavf);
		if (readCount != blocksize)
		{
			WriteDebugLog(LOGERROR, "Premature end of data while reading WAV file.");
			return 5;
		}
		WavNow += blocksize * 1000 / 12000;
		ProcessNewSamples(samples, blocksize);
		nSamples -= blocksize;
	}
	readCount = fread(samples, 2, nSamples, wavf);
	if (readCount != nSamples)
	{
		WriteDebugLog(LOGERROR, "Premature end of data while reading WAV file.");
		return 6;
	}
	WavNow += nSamples * 1000 / 12000;
	ProcessNewSamples(samples, nSamples);
	nSamples = 0;
	// Send additional silent samples to ProcessNewSamples() after end of WAV file data.
	// Without this, a frame that too close to the end of the WAV file might not be decoded.
	memset(samples, 0, sizeof(samples));
	for (int i=0; i<20; i++) {
		WavNow += blocksize * 1000 / 12000;
		ProcessNewSamples(samples, blocksize);
	}

	fclose(wavf);
	WriteDebugLog(LOGDEBUG, "Done decoding %s.", DecodeWav);
	return 0;
}

/*
 * Attempt to parse `str` as a base ten number. Returns
 * true and sets `num` if the entire string is valid as
 * a number.
 */
bool try_parse_long(const char* str, long* num) {
	const char* s = str ? str : "";
	char* end = 0;
	*num = strtol(s, &end, 10);
	return (end > s && *end == '\0');
}
