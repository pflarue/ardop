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
#include <sys/time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>

#include "common/ardopcommon.h"
#include "common/wav.h"

void ProcessCommandFromHost(char * strCMD);

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

extern short InputNoiseStdDev;
int add_noise(short *samples, unsigned int nSamples, short stddev);

int	intARQDefaultDlyMs = 240;
int wg_port = 0;  // If not changed from 0, do not use WebGui
BOOL WriteRxWav = FALSE;
BOOL WriteTxWav = FALSE;
BOOL UseSDFT = FALSE;
BOOL FixTiming = TRUE;
BOOL WG_DevMode = FALSE;
// the --decodewav option can be repeated to provide the pathnames of multiple
// WAV files to be decoded.  If more are given than DecodeWav can hold, then the
// remainder is ignored.
char DecodeWav[5][256] = {"", "", "", "", ""};
// HostCommands may contain one or more semicolon separated host commands
// provided as a command line parameter.  These are to be interpreted at
// startup of ardopcf as if they were issued by a connected host program
char HostCommands[3000] = "";

int PTTMode = PTTRTS;  // PTT Control Flags.

struct sockaddr HamlibAddr;  // Dest for above
int useHamLib = 0;

extern BOOL UseLeftRX;
extern BOOL UseRightRX;

extern BOOL UseLeftTX;
extern BOOL UseRightTX;

static struct option long_options[] =
{
	{"logdir",  required_argument, 0 , 'l'},
	{"hostcommands",  required_argument, 0 , 'H'},
	{"nologfile",  no_argument, 0 , 'm'},
	{"ptt",  required_argument, 0 , 'p'},
	{"cat",  required_argument, 0 , 'c'},
	{"keystring",  required_argument, 0 , 'k'},
	{"unkeystring",  required_argument, 0 , 'u'},
	{"webgui",  required_argument, 0 , 'G'},
	{"writewav",  no_argument, 0, 'w'},
	{"writetxwav",  no_argument, 0, 'T'},
	{"decodewav",  required_argument, 0, 'd'},
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
	"port is TCP Command Port Number.  Data port number is automatically 1 higher.\n"
	"\n"
	"Optional Paramters\n"
	"-h or --help                         Display this help screen.\n"
	"-l path or --logdir path             Path for log files\n"
	"-H string or --hostcommands string   String of semicolon separated host commands to apply\n"
	"                                       to ardopcf during startup, as if they had come from\n"
	"                                       a connected host.  This provides some capabilities\n"
	"                                       provided by obsolete command line options available\n"
	"                                       from earlier versions of ardopcf and ardopc.\n"
	"-m or --nologfile                    Don't write log files. Use console output only.\n"
	"-c device or --cat device            Device to use for CAT Control\n"
	"-p device or --ptt device            Device to use for PTT control using RTS\n"
	// RTS:device is also permitted, but is equivalent to just device
	"                                     or DTR:device to use DTR for PTT instead of RTS\n"
	// TODO: Verify that this actually works with a CM108-like device for PTT
	"                                     or VID:PID of CM108-like Device to use for PTT\n"
	"-g [Pin]                             GPIO pin to use for PTT (ARM Only)\n"
	"                                     Default 17. use -Pin to invert PTT state\n"
	"-k string or --keystring string      String (In HEX) to send to the radio to key PTT\n"
	"-u string or --unkeystring string    String (In HEX) to send to the radio to unkey PTT\n"
	"-L use Left Channel of Soundcard for receive in stereo mode\n"
	"-R use Right Channel of Soundcard for receive in stereo mode\n"
	"-y use Left Channel of Soundcard for transmit in stereo mode\n"
	"-z use Right Channel of Soundcard for transmit in stereo mode\n"
	"-G port or --webgui port             Enable WebGui and specify port number.\n"
	"-w or --writewav                     Write WAV files of received audio for debugging.\n"
	"-T or --writetxwav                   Write WAV files of sent audio for debugging.\n"
	"-d pathname or --decodewav pathname  Pathname of WAV file to decode instead of listening.\n"
	"                                       Repeat up to 5 times for multiple WAV files.\n"
	"-s or --sdft                         Use the alternative Sliding DFT based 4FSK decoder.\n"
	"-A or --ignorealsaerror              Ignore ALSA config error that causes timing error.\n"
	"                                       DO NOT use -A option except for testing/debugging,\n"
	"                                       or if ardopcf fails to run and suggests trying this.\n"
	"\n"
	" CAT and RTS/DTR PTT can share the same port.\n"
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
	bool enable_log_files = true;
	unsigned int WavFileCount = 0;

	while (1)
	{
		int option_index = 0;

		c = getopt_long(argc, argv, "l:H:mc:p:g::k:u:G:hLRyzwTd:sA", long_options, &option_index);


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
			if (!ardop_log_set_directory(optarg)) {
				printf("ERROR: Unable to set log directory. Log files will not be written. The --logdir may be too long.");
				exit(1);
			}
			break;

		case 'm':
			enable_log_files = false;
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
			if (strstr(PTTPort, "RTS:") == PTTPort && strlen(PTTPort) > 4) {
				PTTMode = PTTRTS;  // This is also the default w/o a prefix
				strcpy(PTTPort, optarg + 4);
			} else if (strstr(PTTPort, "DTR:") == PTTPort && strlen(PTTPort) > 4) {
				PTTMode = PTTDTR;
				strcpy(PTTPort, optarg + 4);
			}
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

		case 'G':
			wg_port = atoi(optarg);
			break;

		case 'w':
			WriteRxWav = TRUE;
			break;

		case 'T':
			WriteTxWav = TRUE;
			break;

		case 'd':
			if (WavFileCount < sizeof(DecodeWav) / sizeof(DecodeWav[0]))
				strcpy(DecodeWav[WavFileCount++], optarg);
			else
				printf(
					"Too many WAV files specified with -d/--decodewav.  %s will"
					" not be used.\n", optarg);
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

	// log files use the host port number to permit multiple
	// concurrent instances
	uint16_t host_port = atoi(HostPort);
	if (! host_port) {
		host_port = 8515;
	}
	ardop_log_set_port(host_port);

	// begin logging
	ardop_log_start(enable_log_files);
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


void displayCall(int dirn, const char * Call)
{
	char Msg[32];
	sprintf(Msg, "%c%s", dirn, Call);
	SendtoGUI('I', Msg, strlen(Msg));
}

// When decoding WAV files, WavNow will be set to the offset from the
// start of that file to the end of the data about to be passed to
// ProcessNewSamples() in ms.  Thus, it serves as a proxy for Now()
// which is otherwise based on clock time.  This substitution occurs
// in getTicks() in ALSASound.c or Waveout.c.  It is also used to indicate
// time offsets in the log file.
int WavNow = 0;

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
	int WavFileCount = 0;
	char *nextHostCommand = HostCommands;
	bool warnedClipping = false;  // Use this to only log warning about clipping once.

	// Seed the random number generator.  This is useful if INPUTNOISE is being
	// used.  Seeding is normally done in ardopmain(), which is bypassed when
	// using decode_wav().
	struct timeval t1;
	gettimeofday(&t1, NULL);
	srand(t1.tv_usec + t1.tv_sec);

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

	// Send blocksize silent/noise samples to ProcessNewSamples() before start of WAV file data.
	memset(samples, 0, sizeof(samples));
	add_noise(samples, blocksize, InputNoiseStdDev);
	ProcessNewSamples(samples, blocksize);

	WavNow = 0;
	unsigned int NowOffset = 0;

	while (true) {
		NowOffset = Now;
		ZF_LOGI("Decoding WAV file %s.", DecodeWav[WavFileCount]);
		wavf = fopen(DecodeWav[WavFileCount], "rb");
		if (wavf == NULL)
		{
			ZF_LOGE("Unable to open WAV file %s.", DecodeWav[WavFileCount]);
			return 1;
		}
		if ((Now - NowOffset) % 100 == 0)  // time stamp at 100 ms intervals
			ZF_LOGV("%s: %.3f sec (%.3f)", DecodeWav[WavFileCount], (Now - NowOffset)/1000.0, Now/1000.0);
		readCount = fread(wavHead, 1, 44, wavf);
		if (readCount != 44)
		{
			ZF_LOGE("Error reading WAV file header.");
			return 2;
		}
		if (memcmp(wavHead, headStr, 4) != 0)
		{
			ZF_LOGE("%s is not a valid WAV file. 0x%x %x %x %x != 0x%x %x %x %x",
						DecodeWav[WavFileCount], wavHead[0], wavHead[1], wavHead[2],
						wavHead[3], headStr[0], headStr[1], headStr[2], headStr[3]);
			return 3;
		}
		if (wavHead[20] != 0x01)
		{
			ZF_LOGE("Unexpected WAVE type.");
			return 4;
		}
		if (wavHead[22] != 0x01)
		{
			ZF_LOGE("Expected single channel WAV.  Consider converting it with SoX.");
			return 7;
		}
		sampleRate = wavHead[24] + (wavHead[25] << 8) + (wavHead[26] << 16) + (wavHead[27] << 24);
		if (sampleRate != 12000)
		{
			ZF_LOGE("Expected 12kHz sample rate but found %d Hz.  Consider converting it with SoX.", sampleRate);
			return 8;
		}

		nSamples = (wavHead[40] + (wavHead[41] << 8) + (wavHead[42] << 16) + (wavHead[43] << 24)) / 2;
		ZF_LOGD("Reading %d 16-bit samples.", nSamples);
		while (nSamples >= blocksize)
		{
			readCount = fread(samples, 2, blocksize, wavf);
			if (readCount != blocksize)
			{
				ZF_LOGE("Premature end of data while reading WAV file.");
				return 5;
			}
			WavNow += blocksize * 1000 / 12000;
			if ((Now - NowOffset) % 100 == 0)  // time stamp at 100 ms intervals
				ZF_LOGV("%s: %.3f sec (%.3f)", DecodeWav[WavFileCount], (Now - NowOffset)/1000.0, Now/1000.0);
			if (add_noise(samples, blocksize, InputNoiseStdDev) > 0 && !warnedClipping) {
				// The warning normally logged when audio is too loud does not
				// appear when using decode_wav().  So, add it here.
				ZF_LOGI(
					"WARNING: In decode_wav(), samples are clipped after adding"
					" noise because they exceeded the range of a 16-bit integer.");
				warnedClipping = true;
			}
			ProcessNewSamples(samples, blocksize);
			nSamples -= blocksize;
		}
		// nSamples is less than or equal to blocksize.
		// Fill samples with 0, read nSamples, but pass blocksize samples to
		// ProcessNewSamples().  This adds a small amount of silence/noise to the end
		// of the audio, and keeps WavNow incrementing by blocksize (which is
		// convenient for logging).
		memset(samples, 0, sizeof(samples));
		readCount = fread(samples, 2, nSamples, wavf);
		if (readCount != nSamples)
		{
			ZF_LOGE("Premature end of data while reading WAV file.");
			return 6;
		}
		WavNow += blocksize * 1000 / 12000;
		if ((Now - NowOffset) % 100 == 0)  // time stamp at 100 ms intervals
			ZF_LOGV("%s: %.3f sec (%.3f)", DecodeWav[WavFileCount], (Now - NowOffset)/1000.0, Now/1000.0);
		if (add_noise(samples, blocksize, InputNoiseStdDev) > 0 && !warnedClipping) {
			// The warning normally logged when audio is too loud does not
			// appear when using decode_wav().  So, add it here.
			ZF_LOGI(
				"WARNING: In decode_wav(), samples are clipped after adding"
				" noise because they exceeded the range of a 16-bit integer.");
			warnedClipping = true;
		}
		ProcessNewSamples(samples, blocksize);
		nSamples = 0;
		fclose(wavf);
		// Send additional silent/noise samples to ProcessNewSamples() after end of WAV file data.
		// Without this, a frame that ends too close to the end of the WAV file might not be decoded.
		// When decoding multiple WAV files (which may also be repeats of the same WAV file to
		// test Memory ARQ), the extra audio samples between the WAV files avoids problems with
		// failure to detect the start of the frame in later WAV files.  This same problem would
		// occur if an adequate delay were not inserted between frames sent over the air.
		//
		// Attempting to decode multiple copies of the same noisy WAV file will
		// not improve results compared to a single copy because the noise is
		// identical in all copies.  However, if the noise is different between
		// the copies (as they will be if a frame is repeated over the air, and
		// as can be simulated with the INPUTNOISE host command), then the noise
		// can be averaged out.  For multi-carrier frame types successfully
		// decoded portions from different copies can also be combined.  Thus,
		// decoding results may improve with repetition.
		memset(samples, 0, sizeof(samples));
		for (int i=0; i<20; i++) {
			WavNow += blocksize * 1000 / 12000;
			if ((Now - NowOffset) % 100 == 0)  // time stamp at 100 ms intervals
				ZF_LOGV("Added silence/noise: %.3f sec (%.3f)", (Now - NowOffset)/1000.0, Now/1000.0);
			add_noise(samples, blocksize, InputNoiseStdDev);
			ProcessNewSamples(samples, blocksize);
		}
		ZF_LOGD("Done decoding %s.", DecodeWav[WavFileCount]);
		WavFileCount ++;
		if (WavFileCount == sizeof(DecodeWav) / sizeof(DecodeWav[0])
			|| DecodeWav[WavFileCount][0] == 0x00
		)
			break;
	}
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
