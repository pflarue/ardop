// definition of ProductVersion moved to version.h
// This simplifies test builds with using local version numbers independent
//   of version numbers pushed to git repository.
#include "os_util.h"
#include "common/version.h"
#include "common/ptt.h"

void PollReceivedSamples();

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
#define LOG_OUTPUT_SYSLOG
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

extern char HostPort[80];
extern int host_port;
char CaptureDevice[80] = "";  // If not specified, becomes default of "0"
char PlaybackDevice[80] = "";  // If not specified, becomes default of "0"

struct WavFile *rxwf = NULL;  // For recording of RX audio
struct WavFile *txwff = NULL;  // For recording of filtered TX audio
// writing unfiltered tx audio to WAV disabled
// struct WavFile *txwfu = NULL;  // For recording of unfiltered TX audio

#define RXWFTAILMS 10000  // 10 seconds
unsigned int rxwf_EndNow = 0;

short InputNoiseStdDev = 0;
int add_noise(short *samples, unsigned int nSamples, short stddev);
int wg_send_wavrx(int cnum, bool isRecording);
int wg_send_pixels(int cnum, unsigned char *data, size_t datalen);
VOID TCPHostPoll();
void WebguiPoll();


int	intARQDefaultDlyMs = 240;
int wg_port = 0;  // If not changed from 0, do not use WebGui
bool HWriteRxWav = false;  // Record RX controlled by host command RECRX
bool WriteRxWav = false;  // Record RX controlled by Command line/TX/Timer
bool WriteTxWav = false;  // Record TX
bool UseSDFT = false;
bool FixTiming = true;
bool WG_DevMode = false;
// the --decodewav option can be repeated to provide the pathnames of multiple
// WAV files to be decoded.  If more are given than DecodeWav can hold, then the
// remainder is ignored.
char DecodeWav[5][256] = {"", "", "", "", ""};
// HostCommands may contain one or more semicolon separated host commands
// provided as a command line parameter.  These are to be interpreted at
// startup of ardopcf as if they were issued by a connected host program
char HostCommands[3000] = "";

bool UseLeftRX = true;
bool UseRightRX = true;

bool UseLeftTX = true;
bool UseRightTX = true;


// Called while waiting during TX. Run background processes.
// If mS <= 0, return quickly
void txSleep(int mS) {
	if (strcmp(PlaybackDevice, "NOSOUND") == 0)
		return;

	unsigned int endNow = Now + mS;
	while (Now < endNow && !blnClosing) {
		if (PKTLEDTimer && Now > PKTLEDTimer) {
			PKTLEDTimer = 0;
			SetLED(PKTLED, 0);  // turn off packet rxed led
		}
		TCPHostPoll();
		WebguiPoll();
		// If !Capturing (as intended when called from here),
		// PollReceivedSamples() reads samples from soundcard, but discards them
		// rather than passing them to ProcessNewSamples().  This prevents the
		// RX audio buffer flow overflowing, as could occur if
		// PollReceivedSamples() was not called.
		PollReceivedSamples();
		Sleep(5);  // TODO: Explore whether this value should be adjusted.
	}
}

void extendRxwf() {
	rxwf_EndNow = Now + RXWFTAILMS;
}

void StartRxWav() {
	// Open a new WAV file if not already recording.
	// If already recording, then just extend the time before
	// recording will end.  Do nothing if already recording due
	// to HWriteRxWav, which does not use a timer.
	//
	// Wav files will use a filename that includes host port, UTC date,
	// and UTC time, similar to log files but with added time to
	// the nearest second.  Like Log files, these Wav files will be
	// written to the Log directory if defined, else to the current
	// directory
	//
	// As currently implemented, the wav file written contains only
	// received audio.  Since nothing is written for the time while
	// transmitting, and thus not receiving, this recording is not
	// time continuous.  Thus, the filename indicates the time that
	// the recording was started, but the times of the received
	// transmissions, other than the first one, are not indicated.
	// The size should be 100 larger than the size of ArdopLogDir in log.c so
	// that a WAV pathname in the directory from ardop_log_get_directory()
	// will always fit.
	char rxwf_pathname[612];
	int pnlen;
	char timestr[16];  // 15 char time string plus terminating NULL
	get_utctimestr(timestr);

	if (rxwf != NULL) {
		if (!HWriteRxWav) {
			// Already recording, so just extend recording time.
			extendRxwf();
		}  // else do nothing for HWriteRxWav, which does not use a timer
		return;
	}
	if (ardop_log_get_directory()[0])
		pnlen = snprintf(rxwf_pathname, sizeof(rxwf_pathname),
			"%s/ARDOP_rxaudio_%d_%15s.wav",
			ardop_log_get_directory(), host_port, timestr);
	else
		pnlen = snprintf(rxwf_pathname, sizeof(rxwf_pathname),
			"ARDOP_rxaudio_%d_%15s.wav", host_port, timestr);
	if (pnlen == -1 || pnlen > (int) sizeof(rxwf_pathname)) {
		ZF_LOGE("Unable to write WAV file, invalid pathname. Logpath may be"
			" too long.");
		WriteRxWav = false;
		return;
	}
	rxwf = OpenWavW(rxwf_pathname);
	wg_send_wavrx(0, true);  // update "RECORDING RX" indicator on WebGui
	if (!HWriteRxWav) {
		// A timer is not used with HWriteRxWav, so no need to extend.
		extendRxwf();
	}
}

// writing unfiltered tx audio to WAV disabled.  Only filtered
// tx audio will be written.  However, the code for unfiltered
// audio is left in place but commented out so that it can eaily
// be restored if desired.
void StartTxWav() {
	// Open two new WAV files for filtered and unfiltered Tx audio.
	//
	// Wav files will use a filename that includes host port, UTC date,
	// and UTC time, similar to log files but with added time to
	// the nearest second.  Like Log files, these Wav files will be
	// written to the Log directory if defined, else to the current
	// directory
	// The size should be 100 larger than the size of ArdopLogDir in log.c so
	// that a WAV pathname in the directory from ardop_log_get_directory()
	// will always fit.
	char txwff_pathname[612];
	// char txwfu_pathname[612];
	int pnflen;
	// int pnulen;
	char timestr[16];  // 15 char time string plus terminating NULL
	get_utctimestr(timestr);

	if (ardop_log_get_directory()[0])
		pnflen = snprintf(txwff_pathname, sizeof(txwff_pathname),
			"%s/ARDOP_txfaudio_%d_%15s.wav",
			ardop_log_get_directory(), host_port, timestr);
	else
		pnflen = snprintf(txwff_pathname, sizeof(txwff_pathname),
			"ARDOP_txfaudio_%d_%15s.wav", host_port, timestr);
	if (pnflen == -1 || pnflen > (int) sizeof(txwff_pathname)) {
		ZF_LOGE("Unable to write WAV file, invalid pathname. Logpath may be"
			" too long.");
		WriteTxWav = false;
		return;
	}
	// if (pnulen == -1 || pnulen > (int) sizeof(txwfu_pathname)) {
		// ZF_LOGE("Unable to write WAV file, invalid pathname. Logpath may be"
		//	" too long.");
		// WriteTxWav = false;
		// return;
	// }
	txwff = OpenWavW(txwff_pathname);
	// txwfu = OpenWavW(txwfu_pathname);
}

char Leds[8]= {0};
unsigned int PKTLEDTimer = 0;

void SetLED(int LED, int State) {
	// If GUI active send state

	Leds[LED] = State;
	SendtoGUI('D', Leds, 8);
}

void DrawTXMode(const char * Mode) {
	unsigned char Msg[64];
	strcpy(Msg, Mode);
	SendtoGUI('T', Msg, strlen(Msg) + 1);  // TX Frame
}

void DrawTXFrame(const char * Frame) {
	unsigned char Msg[64];
	strcpy(Msg, Frame);
	SendtoGUI('T', Msg, strlen(Msg) + 1);  // TX Frame
}

void DrawRXFrame(int State, const char * Frame) {
	unsigned char Msg[64];

	Msg[0] = State;  // Pending/Good/Bad
	strcpy(&Msg[1], Frame);
	SendtoGUI('R', Msg, strlen(Frame) + 1);  // RX Frame
}
// mySetPixel() uses 3 bytes from Pixels per call.  So it must be 3 times the
// size of the larger of inPhases[0] or intToneMags/4. (intToneMags/4 is larger)
UCHAR Pixels[9108];
UCHAR * pixelPointer = Pixels;


// This data may be copied and pasted from the debug log file into the
// "Host Command" input box in the WebGui in developer mode to reproduce
// the constellation plot.
void LogConstellation() {
	char Msg[10000] = "CPLOT ";
	for (int i = 0; i < pixelPointer - Pixels; i++)
		snprintf(Msg + strlen(Msg), sizeof(Msg) - strlen(Msg), "%02X", Pixels[i]);
	ZF_LOGV("%s", Msg);
}


void mySetPixel(unsigned char x, unsigned char y, unsigned int Colour) {
	// Used on Windows for constellation. Save points and send to GUI at end
	static bool overflowed = false;
	if ((pixelPointer + 2 - Pixels) > (long int) sizeof(Pixels)) {
		// Pixels should be large enough, but in case it isn't this avoids
		// writing beyond the end.
		if (!overflowed)
			ZF_LOGW("WARNING: Memory overflow averted in mySetPixel.  This"
				" warning is only logged once.");
		overflowed = true;  // Prevent repeated logging of this warning
		return;
	}

	*(pixelPointer++) = x;
	*(pixelPointer++) = y;
	*(pixelPointer++) = Colour;
}
void clearDisplay() {
	// Reset pixel pointer

	pixelPointer = Pixels;

}
void updateDisplay() {
//	 SendtoGUI('C', Pixels, pixelPointer - Pixels);
}
void DrawAxes(int Qual, char * Mode) {
	UCHAR Msg[80];
	SendtoGUI('C', Pixels, pixelPointer - Pixels);
	wg_send_pixels(0, Pixels, pixelPointer - Pixels);
	LogConstellation();
	pixelPointer = Pixels;

	sprintf(Msg, "%s Quality: %d", Mode, Qual);
	SendtoGUI('Q', Msg, strlen(Msg) + 1);
}
void DrawDecode(char * Decode) {
}


static struct option long_options[] =
{
	{"logdir",  required_argument, 0 , 'l'},
	{"hostcommands",  required_argument, 0 , 'H'},
	{"nologfile",  no_argument, 0 , 'm'},
#ifdef LOG_OUTPUT_SYSLOG
	{"syslog",  no_argument, 0 , 'S'},
#endif
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
	"%s [Options] host_port [CaptureDevice] [PlaybackDevice]\n"
	"defaults are host_port=8515, CaptureDevice=0, and PlaybackDevice=0\n"
	"Capture and Playback devices may be set either with positional parameters (which\n"
	"requires also setting host_port), or using the -i and -o options.\n"
	"\n"
	"host_port is host interface TCP Port Number.  data_port is automatically 1 higher.\n"
	"\n"
	"Optional Paramters\n"
	"-h or --help                         Display this help screen.\n"
	"-i CaptureDevice                     Set CaptureDevice (alternative to positional parameters)\n"
	"-o PlaybackDevice                    Set PlaybackDevice (alternative to positional parameters)\n"
	"-l path or --logdir path             Path for log files\n"
	"-H string or --hostcommands string   String of semicolon separated host commands to apply\n"
	"                                       in the order provided to ardopcf during startup, as\n"
	"                                       if they had come from a connected host.  This\n"
	"                                       provides some capabilities provided by obsolete\n"
	"                                       command line options used by earlier versions of\n"
	"                                       ardopcf and ardopc.\n"
	"-m or --nologfile                    Don't write log files. Use console output only.\n"
#ifdef LOG_OUTPUT_SYSLOG
	"-S or --syslog                       Send console log to syslog instead.\n"
#endif
	"-c device or --cat device            Device to use for CAT Control\n"
	"                                     or TCP:port to use a TCP CAT port on the local machine\n"
	"                                     or TCP:ddd.ddd.ddd.ddd:port to use a TCP CAT port on a\n"
	"                                     networked machine."
	"-p device or --ptt device            Device to use for PTT control using RTS\n"
	// RTS:device is also permitted, but is equivalent to just device
	"                                     or DTR:device to use DTR for PTT instead of RTS,\n"
#ifdef WIN32
	// For Windows, use VID:PID for CM108 devices, though use of device name is also accepted.
	"                                     or CM108:VID:PID of CM108-like Device to use for PTT.\n"
	"                                     Using CM108:? displays a list of VID:PID values for attached\n"
	"                                     devices known to be CM108 compatible for PTT control.\n"
#else
	// For Linux, CM108 devices like /dev/hidraw0 are used.
	"                                     or CM108:device of CM108-like device to use for PTT\n"
#endif
	"                                     Using RIGCTLD as the --ptt device is equivalent to\n"
	"                                     -c TCP:4532 -k 5420310A -u 5420300A to use hamlib/rigctld\n"
	"                                     running on its default TCP port 4532 for PTT.\n"
#ifdef __ARM_ARCH
	"-g Pin                               GPIO pin to use for PTT (ARM Only)\n"
	"                                     Use -Pin to invert PTT state\n"
#endif
	"-k string or --keystring string      String (In HEX) to send to the radio to key PTT\n"
	"-u string or --unkeystring string    String (In HEX) to send to the radio to unkey PTT\n"
	"-L use Left Channel of Soundcard for receive in stereo mode\n"
	"-R use Right Channel of Soundcard for receive in stereo mode\n"
	"-y use Left Channel of Soundcard for transmit in stereo mode\n"
	"-z use Right Channel of Soundcard for transmit in stereo mode\n"
	"-G wg_port or --webgui wg_port       Enable WebGui and specify wg_port number.\n"
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
	" See the ardopcf documentation for command line options at\n"
	" https://github.com/pflarue/ardop/blob/master/docs/Commandline_options.md\n"
	" for more information, especially for cat and ptt options.\n\n";

static const char *startstrings[] = {
	// [0] requires ProductName, ProductVersion.
	"%s Version %s (https://www.github.com/pflarue/ardop)",
	"Copyright (c) 2014-2025 Rick Muething, John Wiseman, Peter LaRue",
	"See https://github.com/pflarue/ardop/blob/master/LICENSE for licence"
	" details including\n information about authors of external libraries"
	" used and their licenses."};
static const int nstartstrings = 3;
static char cmdstr[3000] = "";

static void logstart(bool enable_log_files, bool enable_syslog, char *err) {
	ardop_log_start(enable_log_files, enable_syslog);
	// Always begin log with startstrings and cmdstr
	ZF_LOGI(startstrings[0], ProductName, ProductVersion);
	for (int j = 1; j < nstartstrings; ++j)
		ZF_LOGI("%s", startstrings[j]);
	ZF_LOGD("Command line: %s", cmdstr);
	if (err[0] != 0x00)
		ZF_LOGE("%s", err);  // Not an empty string
}

// Return 0 for success, < 0 for failure, and > 1 for success but program should
// exit anyways.
// With the exception of the -h (help) option, all generated warnings and error
// messages are routed through ZF_LOG so that they may go to console (or syslog)
// and/or a log file.  If LOGFILE and/or CONSOLELOG are the first elements of a
// --hostcommands or -H option, these are parsed before writing anything to the
// log.
int processargs(int argc, char * argv[]) {
	int c;
	bool enable_log_files = true;
	bool enable_syslog = false;
	char deferredErr[ZF_LOG_BUF_SZ - 80] = "";  // If set, log this error and exit.
	unsigned int WavFileCount = 0;

	cmdstr[0] = 0x00;  // reset to a zero length str
	for (int i = 0; i < argc; ++i) {
		if ((int)(sizeof(cmdstr) - strlen(cmdstr))
			<= snprintf(
				cmdstr + strlen(cmdstr),
				sizeof(cmdstr) - strlen(cmdstr),
				"%s ",
				argv[i])
		) {
			// cmdstr insufficient to hold full command string for logging.
			// So, put "..." at the end to indicate that it is incomplete.
			sprintf(cmdstr + sizeof(cmdstr) - 4, "...");
			break;
		}
	}

	// Starting optstring with : prevents getopt_long() from printing errors
	char optstring[64] = ":i:o:l:H:mc:p:k:u:G:hLRyzwTd:sA";
#ifdef LOG_OUTPUT_SYSLOG
	// -S is only a valid option on Linux systems
	snprintf(optstring + strlen(optstring), sizeof(optstring), "S");
#endif
#ifdef __ARM_ARCH
	// -g <pin> is only a valid option on ARM systems
	snprintf(optstring + strlen(optstring), sizeof(optstring), "g:");
#endif

	// To allow logging while evaluating the command line arguments in
	// accordance with those command line arguments which set logging
	// parameters, parse only logging related options first.  Then start and
	// configure the logging system before parsing the remaining options.
	for (int i = 1; i < argc; ++i) {
		// These options do not generate any errors.
		if (strcmp(argv[i], "--nologfile") == 0 || strcmp(argv[i], "-m") == 0)
			enable_log_files = false;
#ifdef LOG_OUTPUT_SYSLOG
		if (strcmp(argv[i], "--syslog") == 0 || strcmp(argv[i], "-S") == 0)
			enable_syslog = true;
#endif
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			// help screen and exit.
			// Do not start logging.  Instead, print the start strings and
			printf(startstrings[0], ProductName, ProductVersion);
			for (int i = 1; i < nstartstrings; ++i) {
				printf("%s%s\n", i == 1 ? "\n" : "", startstrings[i]);
			}
			printf("Command line: %s", cmdstr);
			printf(HelpScreen, ProductName);
			return 1;  // Exit immediately, but without indicating an error.
		}
	}

	// The first non-positional parameter is the host port, which is used as part
	// of the filename for the log file
	while (true) {
		c = getopt_long(argc, argv, optstring, long_options, NULL);
		if (c == -1)
			break; // end of non-positional parameters, or an error occured
	}
	if (argc > optind) {
		if ((host_port = atoi(argv[optind])) <= 0) {
			// An invalid host_port was specified.  So, for error logging
			// purposes, use the default of 8515.  If no other errors are
			// enountered first, this will produce an error after all
			// non-positional parameters are parsed.
			host_port = 8515;
		}
	}
	ardop_log_set_port(host_port);

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--logdir") != 0 && strcmp(argv[i], "-l") != 0)
			continue;
		// If an error occurs here, store it as deferrredErr and then
		// proceed to parse --hostcommands for logging related commands.
		// Once logging is started (using the default log directory), this
		// error will be logged, and then the program will exit.
		if (i == argc - 1) {
			snprintf(deferredErr, sizeof(deferredErr),
				"ERROR: --logdir (or -l) requires an argument, but none was"
				"provided");
			break;
		}
		if (!ardop_log_set_directory(argv[i + 1])) {
			snprintf(deferredErr, sizeof(deferredErr),
				"ERROR: --logdir (or -l) argument too long: \"%s\"",
				argv[i + 1]);
			break;
		}
	}

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--hostcommands") != 0 && strcmp(argv[i], "-H") != 0)
			continue;
		if (i == argc - 1) {
			logstart(enable_log_files, enable_syslog, deferredErr);
			ZF_LOGE("ERROR: --hostcommands (or -H) requires an argument, but"
				" none was provided");
			return (-1);
		}
		if (strlen(argv[i + 1]) >= sizeof(HostCommands)) {
			logstart(enable_log_files, enable_syslog, deferredErr);
			ZF_LOGE("ERROR: --hostcommands (or -H) argument too long: \"%s\"",
				argv[i + 1]);
			return (-1);
		}
		// The commands in the argument to -H or --hostcommands are mostly
		// handled after the the startup process is finished.  However, if
		// this argument starts with LOGLEVEL and/or CONSOLELOG, handle them
		// here before logging is started
		strcpy(HostCommands, argv[i + 1]);
		char *nextHC = HostCommands;
		char *logcmds[] = {"LOGLEVEL", "CONSOLELOG"};
		while (nextHC[0] != 0x00) {
			for (int i = 0; i < 2; ++i) {
				if (strncasecmp(nextHC, logcmds[i], strlen(logcmds[i])) != 0)
					continue;
				nextHC += strlen(logcmds[i]);
				if (nextHC[0] != ' ') {
					// Missing space between command and argument
					logstart(enable_log_files, enable_syslog, deferredErr);
					ZF_LOGE("ERROR: Missing space after %s in HostCommands: \"%s\"",
						logcmds[i], HostCommands);
					return (-1);
				}
				nextHC++;
				long lv = strtol(nextHC, &nextHC, 10);
				if (nextHC[0] != 0x00 && nextHC[0] != ';') {
					// Argument contains something extra after integer.
					logstart(enable_log_files, enable_syslog, deferredErr);
					ZF_LOGE("ERROR: Invalid argument to HostCommands: \"%s\"",
						HostCommands);
					return (-1);
				} else if (nextHC[0] == ';')
					nextHC++;
				if (lv < ZF_LOG_VERBOSE || lv > ZF_LOG_FATAL) {
					logstart(enable_log_files, enable_syslog, deferredErr);
					ZF_LOGE("ERROR: Invalid argument to %s in HostCommands: %li",
						logcmds[i], lv);
					return (-1);
				}
				if (i == 0)
					ardop_log_set_level_file((int) lv);
				else
					ardop_log_set_level_console((int) lv);
				break;
			}
			if (nextHC == HostCommands) {
				// No log command found.  Break to defer processing of any
				// remaining host commands, including log commands later in the
				// string.
				break;
			} else {
				// Discard processed log command, and continue to next command
				memmove(HostCommands, nextHC, strlen(nextHC) + 1);
				nextHC = HostCommands;  // reseet nextHC
			}
		}
	}
	// begin logging.
	// If -m or --nologfile was specified, this will only log to the console.
	// If -S or --syslog was specified (Linux only), the console log will be
	// written to syslog instead.
	// if -l or --logdir was specified, the log file will be written in the
	// specified directory, else it will be written in the current directory.
	// Log filename includes the host_port number to allow multiple instances
	// running on different host ports to each create their own log file.
	logstart(enable_log_files, enable_syslog, deferredErr);
	if (deferredErr[0] != 0x00)
		return (-1);

	// Now consider all other command line options.  Logging related options
	// already handled will be ignored.
	optind = 1;  // This resets getopt_long() to start at the beginning
	while (true) {
		c = getopt_long(argc, argv, optstring, long_options, NULL);

		// Check for end of operation or error
		if (c == -1)
			break;

		// Handle options
		switch (c) {
			case 'm':
#ifdef LOG_OUTPUT_SYSLOG
			case 'S':
#endif
			case 'h':
			case 'l':
			case 'H':
				break;  // These options were already handled.

			case 'i':
				strcpy(CaptureDevice, optarg);
				break;

			case 'o':
				strcpy(PlaybackDevice, optarg);
				break;

#ifdef __ARM_ARCH
			// Previously, this took an optional argument, and defaulted to a
			// value of 18 without an argument.  However, due to limitations of
			// getopt_long(), this required a different syntax than the other
			// options to speccify a value.  So, now a value must be specified.
			case 'g':
				if (set_GPIOpin(atoi(optarg)) == -1) // -ve is acceptable
					return (-1);
				break;
#endif

			case 'k':
				if (set_ptt_on_cmd(optarg,
					"command line option --keystring or -k") == -1
				)
					return (-1);
				break;

			case 'u':
				if (set_ptt_off_cmd(optarg,
					"command line option --unkeystring or -u") == -1
				)
					return (-1);
				break;

			case 'p':
				if (parse_pttstr(optarg) == -1)
					return (-1);
				break;

			case 'c':
				if (parse_catstr(optarg) == -1)
					return (-1);
				break;

			case 'L':
				if (UseRightRX && !UseLeftRX) {
					ZF_LOGE("ERROR: Invalid use of both -R and -L");
					return (-1);
				}
				UseLeftRX = true;
				UseRightRX = false;
				break;

			case 'R':
				if (UseLeftRX && !UseRightRX) {
					ZF_LOGE("ERROR: Invalid use of both -L and -R");
					return (-1);
				}
				UseLeftRX = false;
				UseRightRX = true;
				break;

			case 'y':
				if (UseRightTX && !UseLeftTX) {
					ZF_LOGE("ERROR: Invalid use of both -z and -y");
					return (-1);
				}
				UseLeftTX = true;
				UseRightTX = false;
				break;

			case 'z':
				if (UseLeftTX && !UseRightTX) {
					ZF_LOGE("ERROR: Invalid use of both -y and -z");
					return (-1);
				}
				UseLeftTX = false;
				UseRightTX = true;
				break;

			case 'G':
				wg_port = atoi(optarg);
				if (wg_port == 0) {
					ZF_LOGE("ERROR: Invalid argument to --webgui (or -G)"
						" (expecting non-zero integer): \"%s\"",
						optarg);
					return (-1);
				}
				break;

			case 'w':
				WriteRxWav = true;
				break;

			case 'T':
				WriteTxWav = true;
				break;

			case 'd':
				if (WavFileCount < sizeof(DecodeWav) / sizeof(DecodeWav[0]))
					strcpy(DecodeWav[WavFileCount++], optarg);
				else {
					ZF_LOGE("ERROR: Too many WAV files specified with"
						" --decodewav (or -d).  A maximum of %u may be"
						" provided",
						(int) (sizeof(DecodeWav) / sizeof(DecodeWav[0])));
					return (-1);
				}
				break;

			case 's':
				UseSDFT = true;
				break;

			case 'A':
				FixTiming = false;
				break;

			case ':':
				ZF_LOGE("ERROR: Missing argument for -%c.", optopt);
				return (-1);

			case '?':
				ZF_LOGE("ERROR: Unknown option -%c.", optopt);
				if (optopt == '1') {
					ZF_LOGE("If you intended to use \"-1\" as an alias for"
						" \"NOSOUND\" as a positional parameter to specify an"
						" audio device, this is not supported.  You may use"
						" \"NOSOUND\" as a positional parameter, but \"-1\" is"
						" only valid as an argument to the -i or -o options.");
				}

				return (-1);
		}
	}

	// parse positional parameters
	if (argc > optind + 3) {
		ZF_LOGE("ERROR: More than three positional parameters (those that do"
			" not begin with - or --) were provided.  Review your command line"
			" for typos or use -h for help.");
		return (-1);
	}

	if (argc > optind) {
		strcpy(HostPort, argv[optind]);
		if ((host_port = atoi(HostPort)) <= 0) {
			ZF_LOGE("ERROR: Invalid Host Port (expecting positive integer):"
				" \"%s\"", HostPort);
			return (-1);
		}
	}

	if (argc > optind + 1) {
		if (CaptureDevice[0] != 0x00)
			ZF_LOGW("WARNING: CaptureDevice is set to %s with positional"
			" parameter.  So, '-i %s' is ignored.",
			argv[optind + 1], CaptureDevice);
		strcpy(CaptureDevice, argv[optind + 1]);
	}
	if (argc > optind + 2) {
		if (PlaybackDevice[0] != 0x00)
			ZF_LOGW("WARNING: PlaybackDevice is set to %s with positional"
			" parameter.  So, '-o %s' is ignored.",
			argv[optind + 2], PlaybackDevice);
		strcpy(PlaybackDevice, argv[optind + 2]);
	}

	if (wg_port < 0) {
		// This enables the WebGui DevMode.  In DevMode, it is possible to send
		// raw host commands to ardopcf from the WebGui, and additional details
		// are written to the WebGui Log display.  DevMode also includes a
		// button to start/stop recording RX audio to a WAV file.  This is
		// also possible by using the RECRX host command, but the button allows
		// this to be done more quickly.  The WebGui DevMode is not intended for
		// normal use, but is a useful tool for debugging purposes.
		wg_port = -wg_port;
		WG_DevMode = true;
	}
	if (wg_port == host_port) {
		ZF_LOGE("WebGui wg_port (%d) may not be the same as host_port (%d)",
			wg_port, host_port);
		return (-1);
	}
	if (wg_port == host_port + 1) {
		ZF_LOGE("WebGui wg_port (%d) may not be one greater than host_port (%d)"
			" since that is used as the host data_port.",
			wg_port, host_port);
		return (-1);
	}

	if (CaptureDevice[0] == 0x00) {
		ZF_LOGI("No audio input device was specified, so using default of 0.");
		strcpy(CaptureDevice, "0");
	} else if (strcmp(CaptureDevice, "-1") == 0)
		snprintf(CaptureDevice, sizeof(CaptureDevice), "NOSOUND");
	if (strcmp(CaptureDevice, "NOSOUND") == 0)
		ZF_LOGI("Using NOSOUND for audio input.  This is only"
		" useful for testing/diagnostic purposes.");

	if (PlaybackDevice[0] == 0x00) {
		ZF_LOGI("No audio output device was specified, so using default of 0.");
		strcpy(PlaybackDevice, "0");
	} else if (strcmp(PlaybackDevice, "-1") == 0)
		snprintf(PlaybackDevice, sizeof(PlaybackDevice), "NOSOUND");
	if (strcmp(PlaybackDevice, "NOSOUND") == 0)
		ZF_LOGI("Using NOSOUND for audio output.  This is only"
		" useful for testing/diagnostic purposes.");

	return 0;
}


extern enum _ARDOPState ProtocolState;

extern bool blnARQDisconnect;

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
// ProcessNewSamples() in ms.  Thus, it serves as a proxy for Now
// which is otherwise based on clock time.  This substitution occurs
// in getNow() in os_util.c.  It is also used to indicate time offsets
// in the log file.
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
				ZF_LOGI("WARNING: In decode_wav(), samples are clipped after adding"
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
			ZF_LOGI("WARNING: In decode_wav(), samples are clipped after adding"
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
		for (int i=0; i<20; ++i) {
			WavNow += blocksize * 1000 / 12000;
			if ((Now - NowOffset) % 100 == 0)  // time stamp at 100 ms intervals
				ZF_LOGV("Added silence/noise: %.3f sec (%.3f)", (Now - NowOffset)/1000.0, Now/1000.0);
			add_noise(samples, blocksize, InputNoiseStdDev);
			ProcessNewSamples(samples, blocksize);
		}
		ZF_LOGD("Done decoding %s.", DecodeWav[WavFileCount]);
		WavFileCount++;
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
