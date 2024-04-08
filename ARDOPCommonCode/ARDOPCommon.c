
//
//	Code Common to all versions of ARDOP. 
//

// definition of ProductVersion moved to version.h
// This simplifies test builds with using local version numbers independent 
//   of version numbers pushed to git repository.
#include "version.h"

#ifdef WIN32
#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#else
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#ifndef TEENSY
#include <sys/socket.h>
#endif
#define SOCKET int
#define closesocket close
#define HANDLE int
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "ardopcommon.h"
#include "wav.h"
#include "getopt.h"

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

extern HANDLE hCATDevice;		// port for Rig Control
extern char CATPort[80];
extern int CATBAUD;
extern int EnableHostCATRX;

extern HANDLE hPTTDevice;			// port for PTT
extern char PTTPort[80];			// Port for Hardware PTT - may be same as control port.
extern int PTTBAUD;

extern unsigned char PTTOnCmd[];
extern unsigned char PTTOnCmdLen;

extern unsigned char PTTOffCmd[];
extern unsigned char PTTOffCmdLen;

extern int PTTMode;				// PTT Control Flags.
extern char HostPort[80];
extern char CaptureDevice[80];
extern char PlaybackDevice[80];

int extraDelay = 0;				// Used for long delay paths eg Satellite
int	intARQDefaultDlyMs = 240;
int TrailerLength = 20;
BOOL InitRXO = FALSE;
BOOL WriteRxWav = FALSE;
BOOL WriteTxWav = FALSE;
BOOL TwoToneAndExit = FALSE;
BOOL UseSDFT = FALSE;
BOOL FixTiming = TRUE;
char DecodeWav[256] = "";			// Pathname of WAV file to decode.

int PTTMode = PTTRTS;				// PTT Control Flags.

#ifndef TEENSY
struct sockaddr HamlibAddr;		// Dest for above
#endif
int useHamLib = 0;


extern int LeaderLength;

unsigned const short CRCTAB[256] = {
0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf, 
0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7, 
0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e, 
0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876, 
0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd, 
0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5, 
0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c, 
0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974, 
0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb, 
0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3, 
0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a, 
0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72, 
0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9, 
0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1, 
0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738, 
0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70, 
0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7, 
0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff, 
0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036, 
0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e, 
0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5, 
0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd, 
0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134, 
0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c, 
0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3, 
0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb, 
0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232, 
0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a, 
0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1, 
0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9, 
0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330, 
0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78 
}; 


unsigned short int compute_crc(unsigned char *buf,int len)
{
	unsigned short fcs = 0xffff; 
	int i;

	for(i = 0; i < len; i++) 
		fcs = (fcs >>8 ) ^ CRCTAB[(fcs ^ buf[i]) & 0xff]; 

	return fcs;
}

#ifndef TEENSY

extern BOOL UseLeftRX;
extern BOOL UseRightRX;

extern BOOL UseLeftTX;
extern BOOL UseRightTX;

extern char LogDir[256];

static struct option long_options[] =
{
	{"logdir",  required_argument, 0 , 'l'},
	{"verboselog",  required_argument, 0 , 'v'},
	{"verboseconsole",  required_argument, 0 , 'V'},
	{"ptt",  required_argument, 0 , 'p'},
	{"cat",  required_argument, 0 , 'c'},
	{"keystring",  required_argument, 0 , 'k'},
	{"unkeystring",  required_argument, 0 , 'u'},
	{"extradelay",  required_argument, 0 , 'e'},
	{"leaderlength",  required_argument, 0 , 'x'},
	{"trailerlength",  required_argument, 0 , 't'},
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
	"For TCP Host connection, port is TCP Port Number\n"
	"For Serial Host Connection port must start with \"COM\" or \"com\"\n"
	"  On Windows use the name of the BPQ Virtual COM Port, eg COM4\n"
	"  On Linux the program will create a pty and symlink it to the specified name.\n"
	"\n"
	"Optional Paramters\n"
	"-l path or --logdir path             Path for log files\n"
	"-v path or --verboselog val          Increase (decr for val<0) file log level from default.\n"
	"-V path or --verboseconsole val      Increase (decr for val<0) console log level from default.\n"
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
	"--leaderlength val                   Sets Leader Length (mS)\n"
	"--trailerlength val                  Sets Trailer Length (mS)\n"
	"-r or --receiveonly                  Start in RXO (receive only) mode.\n"
	"-w or --writewav                     Write WAV files of received audio for debugging.\n"
	"-T or --writetxwav                   Write WAV files of sent audio for debugging.\n"
	"-d pathname or --decodewav pathname  Pathname of WAV file to decode instead of listening.\n"
	"-n or --twotone                      Send a 5 second two tone signal and exit.\n"
	"-s or --sdft                         Use the alternative Sliding DFT based 4FSK decoder.\n"
	"-A or --ignorealsaerror              Ignore ALSA config error that causes timing error.\n"
	"                                       DO NOT use -A option except for testing/debugging.\n"
	"\n"
	" CAT and RTS PTT can share the same port.\n"
	" See the ardop documentation for more information on cat and ptt options\n"
	"  including when you need to use -k and -u\n\n";

void processargs(int argc, char * argv[])
{
	int val;
	UCHAR * ptr1;
	UCHAR * ptr2;
	int c;

	while (1)
	{		
		int option_index = 0;

		c = getopt_long(argc, argv, "l:v:V:c:p:g::k:u:e:hLRytrzwTd:nsA", long_options, &option_index);

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

		case 'l':
			strcpy(LogDir, optarg);
			break;

		case 'v':
			FileLogLevel += atoi(optarg);
			if (FileLogLevel > LOGDEBUGPLUS)
				FileLogLevel = LOGDEBUGPLUS;
			else if (FileLogLevel < LOGEMERGENCY)
				FileLogLevel = LOGEMERGENCY;
			break;

		case 'V':
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
				printf("RADIOPTTON command string missing\r");
				break;
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
			PTTMode = PTTCI_V;

			printf ("PTTOnString %s len %d\n", optarg, PTTOnCmdLen);
			break;

		case 'u':

			ptr1 = optarg;
			ptr2 = PTTOffCmd;

			if (ptr1 == NULL)
			{
				printf("RADIOPTTOFF command string missing\r");
				break;
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

		case 'x':
			intARQDefaultDlyMs = LeaderLength = atoi(optarg);
			break;

		case 't':
			TrailerLength = atoi(optarg);
			break;

		case 'r':
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
			TwoToneAndExit = TRUE;
			break;

		case 's':
			UseSDFT = TRUE;
			break;

		case 'A':
			FixTiming = FALSE;
			break;

		case '?':
			/* getopt_long already printed an error message. */
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

	ClosePacketSessions();
}

#endif

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
	WavNow = 0;

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
	for (int i=0; i<10; i++) {
		WavNow += blocksize * 1000 / 12000;
		ProcessNewSamples(samples, blocksize);
	}

	fclose(wavf);
	WriteDebugLog(LOGDEBUG, "Done decoding %s.", DecodeWav);
	return 0;
}
