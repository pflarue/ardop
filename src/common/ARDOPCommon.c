// definition of ProductVersion moved to version.h
// This simplifies test builds with using local version numbers independent
//   of version numbers pushed to git repository.
#include "os_util.h"
#include "common/version.h"
#include "common/audio.h"
#include "common/Webgui.h"
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
char CaptureDevice[DEVSTRSZ] = "";
int Cch = -1;  // Number of channels used to open CaptureDevice
char PlaybackDevice[DEVSTRSZ] = "";
int Pch = -1;  // Number of channels used to open PlaybackDevice

DeviceInfo **AudioDevices;  // A list of all audio devices (Capture and Playback)

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
int wg_port = 8514;  // Port for WebGui.  Set to 0 to not start WebGui.
bool HWriteRxWav = false;  // Record RX controlled by host command RECRX
bool WriteRxWav = false;  // Record RX controlled by Command line/TX/Timer
bool WriteTxWav = false;  // Record TX
bool UseSDFT = false;
bool WG_DevMode = false;
// the --decodewav option can be repeated to provide the pathnames of multiple
// WAV files to be decoded.  If more are given than DecodeWav can hold, then the
// remainder is ignored.
char DecodeWav[5][256] = {"", "", "", "", ""};
// HostCommands may contain one or more semicolon separated host commands
// provided as a command line parameter.  These are to be interpreted at
// startup of ardopcf as if they were issued by a connected host program.
// HostCommands is populated with strdup().  When the commands have been
// processed, free() this memory and set the pointer to NULL.
char *HostCommands = NULL;

bool UseLeftRX = true;
bool UseRightRX = true;

bool UseLeftTX = true;
bool UseRightTX = true;


// Called while waiting during TX. Run background processes.
void txSleep(unsigned int mS) {
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
		if (RXEnabled)
			PollReceivedSamples();
		// TODO: Explore whether sleep duration should be adjusted.
		Sleep(RXEnabled ? 5 : 100);  // (ms)
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
	// The wav file written contains only received audio, but continues to
	// record while transmitting.  While transmitting, the recorded audio is
	// usually near silence, but not always.  Depending on the hardware in use,
	// artifacts of PTT on/off may be discernable, as well as the changes in
	// noise level following PTT action.  Because recording is not interrupted
	// while transmitting, the timing between various received audio features
	// should be accurate, and the absolute time at which any audio was received
	// can be approximately calculated based on the offset within the recording
	// added to the time indicated by the filename.
	//
	// The size of rxwf_pathname should be 100 larger than the size of
	// ArdopLogDir in log.c so that a WAV pathname in the directory from
	// ardop_log_get_directory() will always fit.
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

// Create a WAV from num 16-bit samples in *ptr.
// tag is a string (max string length 40) to include in the filename, which will
// also include a typical seconds-resolution date/time.
// If multiple writes might occur within any clock second, then tag
// should be unique (perhaps using a counter) to avoid overwriting prior files.
// return 0 on success.  If an error occurs return -1.
//
// This function is not used in normal operation of ardopcf, but it is useful
// for diagnostic and debugging purposes.  It can be used to create WAV files
// from short segments of audio data for later inspection and analysis.  Unlike
// longer recordings of received audio, this can be used to capture partially
// processed (mixed, filtered) audio.  It can also be used to see exactly which
// portion of a received signal is being processed in some function.  Since
// OpenWavW() writes a message to the debug log that includes the filename, the
// contents of this WAV file can later be related to other data written to the
// debug log before or after its creation.  That level of time/sample
// specificity is not available in a longer recording of received audio.
int CreateWav(const char *tag, short *ptr, int num) {
	// The size of wf_pathname should be 100 larger than the size of
	// ArdopLogDir in log.c.  The length of the the additions to ArdopLogDir
	// excluding tag are 21 fixed bytes + 5 for host_port + 40 for tag
	// + 15 for timestr.  21+5+40+15=81<100.  So, the WAV pathname in the
	// directory from ardop_log_get_directory() will always fit.
	char wf_pathname[612];
	int pnlen;
	char timestr[16];  // 15 char time string plus terminating NULL
	get_utctimestr(timestr);
	if (strlen(tag) > 40) {
		ZF_LOGE("ERROR: tag=%s in CreateWav is too long. It must be 40"
			" characters or less.  Wav file not written.",
			tag);
		return -1;
	}

	if (ardop_log_get_directory()[0])
		pnlen = snprintf(wf_pathname, sizeof(wf_pathname),
			"%s/ARDOP_audio_%d_%s_%15s.wav",
			ardop_log_get_directory(), host_port, tag, timestr);
	else
		pnlen = snprintf(wf_pathname, sizeof(wf_pathname),
			"ARDOP_audio_%d_%s_%15s.wav", host_port, tag, timestr);
	if (pnlen == -1 || pnlen > (int) sizeof(wf_pathname)) {
		ZF_LOGE("Unable to write WAV file, invalid pathname. Logpath may be"
			" too long.");
		return -1;
	}

	struct WavFile* wf = OpenWavW(wf_pathname);
	if (wf == NULL)
		return -1;
	WriteWav(ptr, num, wf);
	if (CloseWav(wf) != 0)
		return -1;
	return 0;
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
	{"help",  no_argument, 0 , 'h'},
	{ NULL , no_argument , NULL , no_argument }
};

char HelpScreen[] =
	"Usage:\n"
	"%s [Options] host_port [CaptureDevice] [PlaybackDevice]\n"
	"defaults are host_port=8515, data_port=8516, and wg_port=8514\n"
	"If Capture and Playback devices are not set either with positional parameters (which\n"
	"requires also setting host_port), or using the -i and -o options, then ardopcf will\n"
	"be started with CODEC FALSE.  In that case, these devices must be set using the WebGui\n"
	"or via commands from a Host program "
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
	"                                     networked machine.\n"
	"                                     Using RIGCTLD as the --cat device is equivalent to\n"
	"                                     -c TCP:4532 -k 5420310A -u 5420300A to use hamlib/rigctld\n"
	"                                     for CAT on its default TCP port 4532, and use it for PTT.\n"
	"-p device or --ptt device            Device to use for PTT control using RTS\n"
	// RTS:device is also permitted, but is equivalent to just device
	"                                     or DTR:device to use DTR for PTT instead of RTS,\n"
#ifdef __ARM_ARCH
	"                                     or GPIO:pin to use a hardware GPIO pin for PTT\n"
	"                                     (Raspberry Pi only, use GPIO:-pin to invert PTT state)\n"
#endif
#ifdef WIN32
	// For Windows, use VID:PID for CM108 devices, though use of device name is also accepted.
	"                                     or CM108:VID:PID of CM108-like Device to use for PTT.\n"
	"                                     Using CM108:? displays a list of VID:PID values for attached\n"
	"                                     devices known to be CM108 compatible for PTT control.\n"
#else
	// For Linux, CM108 devices like /dev/hidraw0 are used.
	"                                     or CM108:device of CM108-like device to use for PTT.\n"
#endif
	"-k string or --keystring string      String (In HEX) to send to the radio to key PTT\n"
	"-u string or --unkeystring string    String (In HEX) to send to the radio to unkey PTT\n"
	"-L use Left Channel of Soundcard for receive in stereo mode\n"
	"-R use Right Channel of Soundcard for receive in stereo mode\n"
	"-y use Left Channel of Soundcard for transmit in stereo mode\n"
	"-z use Right Channel of Soundcard for transmit in stereo mode\n"
	"-G wg_port or --webgui wg_port       Specify wg_port for WebGui.  If wg_port is 0, then\n"
	"                                     disable WebGui.  Without this option, the WebGui\n"
	"                                     uses the default port of 8514.\n"
	"-w or --writewav                     Write WAV files of received audio for debugging.\n"
	"-T or --writetxwav                   Write WAV files of sent audio for debugging.\n"
	"-d pathname or --decodewav pathname  Pathname of WAV file to decode instead of listening.\n"
	"                                       Repeat up to 5 times for multiple WAV files.\n"
	"-s or --sdft                         Use the alternative Sliding DFT based 4FSK decoder.\n"
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

// Normally return 0 for success.  However, return 1 if the --help or -h option
// is used so that ardopcf exits without indicating an error immediately after
// printing the help info.  Unlike earlier versions of ardopcf, configuration
// errors should not cause ardopcf to exit.  Instead, suitable Warnings or
// Errors are written to the log and ardopcf remains running, usually by
// reverting to some default setting.  The Webgui or commands from a host
// program can now be used to fix/modify most configuration options.
//
// With the exception of the -h (help) option, all generated warnings and error
// messages are routed through ZF_LOG so that they may go to console (or syslog)
// and/or a log file.  If LOGFILE and/or CONSOLELOG are the first elements of a
// --hostcommands or -H option, these are parsed before writing anything to the
// log.
int processargs(int argc, char * argv[]) {
	int c;
	bool enable_log_files = true;
	bool enable_syslog = false;
	// If set, log this error once all logging parameters have been parsed.
	// It is possible that up to two deferred errors can occur:  due to
	// a problem with --logdir and due to an error in strdup() for HostCommands.
	char deferredErr[2][ZF_LOG_BUF_SZ - 80] = {"", ""};
	int deferredErrCount = 0;
	unsigned int WavFileCount = 0;
	// Use tmpCaptureDevice and tmpPlaybackDevice to store parsed device names.
	// Don't try to open them until after all arguments have been parsed
	// including those that set the use of left/right channels.
	char tmpCaptureDevice[DEVSTRSZ] = "";
	char tmpPlaybackDevice[DEVSTRSZ] = "";

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
	char optstring[64] = ":i:o:l:H:mc:p:k:u:G:hLRyzwTd:s";
#ifdef LOG_OUTPUT_SYSLOG
	// -S is only a valid option on Linux systems
	snprintf(optstring + strlen(optstring), sizeof(optstring), "S");
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
			return 1;  // Exit ardopcf immediately, but not indicating an error.
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
			snprintf(deferredErr[deferredErrCount],
				sizeof(deferredErr[deferredErrCount]),
				"ERROR: --logdir (or -l) requires an argument, but none was"
				"provided.  So, default log directory will be used.");
			++deferredErrCount;
			break;
		}
		if (!ardop_log_set_directory(argv[i + 1])) {
			snprintf(deferredErr[deferredErrCount],
				sizeof(deferredErr[deferredErrCount]),
				"ERROR: --logdir (or -l) argument too long (\"%s\").  So,"
				" default log directory will be used.",
				argv[i + 1]);
			++deferredErrCount;
			break;
		}
	}

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--hostcommands") != 0 && strcmp(argv[i], "-H") != 0)
			continue;
		if (i == argc - 1) {
			// Missing argument string for --hostcommands or -H.  Do nothing
			// here.  An error message will be generated during normal
			// processing of options below.
			break;
		}
		if ((HostCommands = strdup(argv[i + 1])) == NULL) {
			snprintf(deferredErr[deferredErrCount],
				sizeof(deferredErr[deferredErrCount]),
				"strdup() failed to duplicate the argument to the"
				" --hostcommands or -H command line option.  Ignoring that"
				" option.");
			++deferredErrCount;
			break;
		}
		// The commands in the argument to -H or --hostcommands are mostly
		// handled after the the startup process is finished.  However, if
		// this argument starts with one or more valid "LOGLEVEL N" or
		// "CONSOLELOG N" host commands, then handle them here before logging is
		// started.  If any other host command is encountered, including an
		// invalid variation on these commands, then wait to process them after
		// loogging has started.
		char *nextHC = HostCommands;
		// Notice that logcmds[] include trailing spaces.  While these host
		// commands are valid without a trailing space and a value, those
		// versions do not change the logging configuration, and thus are not
		// appropriate for early processing.
		char *logcmds[] = {"LOGLEVEL ", "CONSOLELOG "};
		// Set cerr true if a command is not a valid "LOGLEVEL N" or
		// "CONSOLELOG N" command.  In this case, do not remove it from
		// HostCommands and stop further early processing of host commands.
		bool cerr = false;
		while (nextHC[0] != 0x00) {
			for (int i = 0; i < 2; ++i) {
				if (strncasecmp(nextHC, logcmds[i], strlen(logcmds[i])) != 0)
					continue;
				nextHC += strlen(logcmds[i]);
				long lv = strtol(nextHC, &nextHC, 10);
				if ((nextHC[0] != 0x00 && nextHC[0] != ';')
					|| lv < ZF_LOG_VERBOSE || lv > ZF_LOG_FATAL
				) {
					// Argument contains something extra after value, or value
					// is out of range.
					cerr = true;
					break;
				} else if (nextHC[0] == ';')
					nextHC++;
				if (i == 0)
					ardop_log_set_level_file((int) lv);
				else
					ardop_log_set_level_console((int) lv);
				break;
			}
			if (cerr || nextHC == NULL || nextHC == HostCommands)
				break;
			// Discard processed log command, and continue to next command
			memmove(HostCommands, nextHC, strlen(nextHC) + 1);
			nextHC = HostCommands;  // reseet nextHC
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
	ardop_log_start(enable_log_files, enable_syslog);
	// Always begin log with startstrings and cmdstr
	ZF_LOGI(startstrings[0], ProductName, ProductVersion);
	for (int j = 1; j < nstartstrings; ++j)
		ZF_LOGI("%s", startstrings[j]);
	ZF_LOGD("Command line: %s", cmdstr);
	for (int i = 0; i < deferredErrCount; ++i)
		ZF_LOGE("%s", deferredErr[i]);

	// Build and log the list of available audio devices.  Notice that this is
	// done after logging has been started, but before audio device related
	// options are parsed.
	InitAudio(false);

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
				snprintf(tmpCaptureDevice, DEVSTRSZ, "%s", optarg);
				break;

			case 'o':
				snprintf(tmpPlaybackDevice, DEVSTRSZ, "%s", optarg);
				break;

			case 'k':
				set_ptt_on_cmd(optarg, "command line option --keystring or -k");
				break;

			case 'u':
				set_ptt_off_cmd(optarg, "command line option --unkeystring or -u");
				break;

			case 'p':
				parse_pttstr(optarg);
				break;

			case 'c':
				parse_catstr(optarg);
				break;

			case 'L':
				if (UseRightRX && !UseLeftRX) {
					ZF_LOGE("ERROR: Invalid use of both -R and -L.  Ignoring both");
					UseLeftRX = true;
					UseRightRX = true;
				} else {
					UseLeftRX = true;
					UseRightRX = false;
				}
				break;

			case 'R':
				if (UseLeftRX && !UseRightRX) {
					ZF_LOGE("ERROR: Invalid use of both -R and -L.  Ignoring both");
					UseLeftRX = true;
					UseRightRX = true;
				} else {
					UseLeftRX = false;
					UseRightRX = true;
				}
				break;

			case 'y':
				if (UseRightTX && !UseLeftTX) {
					ZF_LOGE("ERROR: Invalid use of both -y and -z.  Ignoring both");
					UseLeftTX = true;
					UseRightTX = true;
				} else {
					UseLeftTX = true;
					UseRightTX = false;
				}
				break;

			case 'z':
				if (UseLeftTX && !UseRightTX) {
					ZF_LOGE("ERROR: Invalid use of both -y and -z.  Ignoring both");
					UseLeftTX = true;
					UseRightTX = true;
				} else {
					UseLeftTX = false;
					UseRightTX = true;
				}
				break;

			case 'G':
				wg_port = atoi(optarg);
				if (wg_port == 0)
					ZF_LOGI("Disabling WebGui for --webgui 0 or -G 0 option.");
				break;

			case 'w':
				WriteRxWav = true;
				break;

			case 'T':
				WriteTxWav = true;
				break;

			case 'd':
				if (strlen(optarg) >= sizeof(DecodeWav[0])) {
					ZF_LOGE("ERROR: length of WAV filename too long. "
						" Skipping.");
					break;
				}
				if (WavFileCount < sizeof(DecodeWav) / sizeof(DecodeWav[0]))
					strcpy(DecodeWav[WavFileCount++], optarg);  // size checked
				else {
					ZF_LOGE("ERROR: Too many WAV files specified with"
						" --decodewav (or -d).  A maximum of %u may be"
						" provided.  The remaining files will be ignored.",
						(int) (sizeof(DecodeWav) / sizeof(DecodeWav[0])));
				}
				break;

			case 's':
				UseSDFT = true;
				break;

			case ':':
				ZF_LOGE("ERROR: Missing argument for -%c.  Ignoring this option"
					, optopt);
				break;

			case '?':
				ZF_LOGE("ERROR: Unknown option -%c.  Ignoring this option",
					optopt);
				if (optopt == '1') {
					ZF_LOGE("If you intended to use \"-1\" as an alias for"
						" \"NOSOUND\" as a positional parameter to specify an"
						" audio device, this is not supported.  You may use"
						" \"NOSOUND\" as a positional parameter, but \"-1\" is"
						" only valid as an argument to the -i or -o options.");
				}
				break;
		}
	}

	// parse positional parameters
	if (argc > optind + 3) {
		ZF_LOGE("ERROR: More than three positional parameters (those that do"
			" not begin with - or --) were provided.  Exccess positional"
			" parameters will be ignored.  Review your command line for typos"
			" or use -h for help.");
	}

	if (argc > optind) {
		snprintf(HostPort, sizeof(HostPort), "%s", argv[optind]);
		if ((host_port = atoi(HostPort)) <= 0) {
			ZF_LOGE("ERROR: Invalid Host Port so using default of 8515. "
				" Expecting a positive integer but found \"%s\"",
				HostPort);
			host_port = 8515;
		}
	}

	if (argc > optind + 1) {
		if (tmpCaptureDevice[0] != 0x00)
			ZF_LOGW("WARNING: CaptureDevice is set to %s with positional"
			" parameter.  So, '-i %s' is ignored.",
			argv[optind + 1], tmpCaptureDevice);
		snprintf(tmpCaptureDevice, DEVSTRSZ, "%s", argv[optind + 1]);
	}
	if (argc > optind + 2) {
		if (tmpPlaybackDevice[0] != 0x00)
			ZF_LOGW("WARNING: PlaybackDevice is set to %s with positional"
			" parameter.  So, '-o %s' is ignored.",
			argv[optind + 2], tmpPlaybackDevice);
		snprintf(tmpPlaybackDevice, DEVSTRSZ, "%s", argv[optind + 2]);
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
		ZF_LOGE("WebGui wg_port (%d) may not be the same as host_port (%d). "
			" Setting WebGui wg_port to %d which is one less than host_port.",
			wg_port, host_port - 1, host_port);
		wg_port = host_port - 1;
	}
	if (wg_port == host_port + 1) {
		ZF_LOGE("WebGui wg_port (%d) may not be one greater than host_port (%d)"
			" since that is used as the host data_port.  Setting WebGui wg_port"
			" to %d which is one less than host_port.",
			wg_port, host_port - 1, host_port);
		wg_port = host_port - 1;
	}

	if (strcmp(tmpCaptureDevice, "-1") == 0)
		snprintf(tmpCaptureDevice, sizeof(tmpCaptureDevice), "NOSOUND");
	if (strcmp(tmpCaptureDevice, "NOSOUND") == 0)
		ZF_LOGI("Using NOSOUND for audio input.  This is only"
		" useful for testing/diagnostic purposes.");

	if (strcmp(tmpPlaybackDevice, "-1") == 0)
		snprintf(tmpPlaybackDevice, sizeof(tmpPlaybackDevice), "NOSOUND");
	if (strcmp(tmpPlaybackDevice, "NOSOUND") == 0)
		ZF_LOGI("Using NOSOUND for audio output.  This is only"
		" useful for testing/diagnostic purposes.");

	if (tmpCaptureDevice[0] != 0x00) {
		if (!OpenSoundCapture(tmpCaptureDevice, getCch(false))) {
			ZF_LOGE("ERROR: CaptureDevice=%s could not be opened as configured.",
				tmpCaptureDevice);
		}
	}

	if (tmpPlaybackDevice[0] != 0x00) {
		if (!OpenSoundPlayback(tmpPlaybackDevice, getPch(false))) {
			ZF_LOGE("ERROR: PlaybackDevice=%s could not be opened as configured.",
				tmpPlaybackDevice);
		}
	}

	if (DecodeWav[0][0] != 0x00)
		return 0;  // --decodewav, so no need to log about audio/ptt devices

	// The following are very important if the user does not realize that
	// one some devices still need to be configured.  So, while this isn't
	// necessarily an ERROR, use ZF_LOGE() rather than a lower priority log
	// function, and format these messages to make them stand out.
	if (!TXEnabled && !RXEnabled)
		ZF_LOGE("\n"
			"  No audio devices were successfully opened based on the options\n"
			"  specified on the command line.  Suitable devices must be\n"
			"  configured by a host program or using the WebGUI before Ardop\n"
			"  will be usable.\n");
	else if (!TXEnabled)
		ZF_LOGE("\n"
			"  While an input/Capture/RX audio device has been opened, no\n"
			"  output/Playback/TX was successfully opened based on the\n"
			"  options specified on the command line.  This configuration is\n"
			"  currently only usable receiving FEC traffic or monitoring in\n"
			"  RXO mode.  To use ARQ mode or transmit anything, a suitable\n"
			"  device must be configured by a host program or using the\n"
			"  WebGUI.\n");
	else if (!RXEnabled) {
		ZF_LOGE("\n"
			"  While an output/Playback/TX audio device has been opened, no\n"
			"  input/Capture/RX was successfully opened based on the options\n"
			"  specified on the command line.  This configuration is not\n"
			"  suitable for typical usage.  A suitable device must be\n"
			"  configured by a host program or using the WebGUI before Ardop\n"
			"  will be usable.\n");
	}

	if (!isPTTmodeEnabled()) {
		ZF_LOGE("\n"
			"  No PTT control method has been successfully enabled based on\n"
			"  the options specified on the command line.  This configuration\n"
			"  is suitable for TX ONLY IF the host program is configured to\n"
			"  handle PTT or if the radio is configured to use VOX (which is\n"
			"  often not reliable).  Otherwise, a suitable method of PTT\n"
			"  control must be configured by a host program or using the\n"
			"  WebGUI before Ardop will be able to transmit.\n");
	}

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
// ProcessNewSamples() in ms.  This is returned by Now rather than the
// value typically provided based on clock time.  This substitution occurs
// in getNow() in os_util.c.  While the timestamps in the log file are always
// based on clock time, values of WavNow are also logged (with level VERBOSE)
// at 100 ms intervals.
int WavNow = 0;

int decode_wav() {
	FILE *wavf;
	unsigned char wavHead[44];
	size_t readCount;
	const char headStr[5] = "RIFF";
	unsigned int nSamples;
	int sampleRate;
	short samples[1024];
	// For logging of WavNow, 100 ms must be a multiple of the duration
	// corresponding to blocksize.
	const unsigned int blocksize = 240;  // Number of 16-bit samples to read at a time
	int WavFileCount = 0;
	char *nextHostCommand = HostCommands;

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
	if (HostCommands != NULL) {
		free(HostCommands);
		HostCommands = NULL;
	}

	// Regardless of whether this was set with a command line argument, proceed in
	// RXO (receive only) protocol mode.  During normal operation, this is set
	// in ardopmain(), which is not used when decoding a WAV file.
	setProtocolMode("RXO");

	// Send blocksize silent/noise samples to ProcessNewSamples() before start of WAV file data.
	memset(samples, 0, sizeof(samples));
	ProcessNewSamples(samples, blocksize);

	WavNow = 0;
	unsigned int NowOffset = 0;

	while (true) {
		NowOffset = Now;
		ZF_LOGI("Decoding WAV file %s.", DecodeWav[WavFileCount]);
		wavf = fopen(DecodeWav[WavFileCount], "rb");
		if (wavf == NULL) {
			ZF_LOGE("Unable to open WAV file %s.", DecodeWav[WavFileCount]);
			return 1;
		}
		if ((Now - NowOffset) % 100 == 0)  // time stamp at 100 ms intervals
			ZF_LOGV("%s: %.3f sec (%.3f)", DecodeWav[WavFileCount], (Now - NowOffset)/1000.0, Now/1000.0);
		readCount = fread(wavHead, 1, 44, wavf);
		if (readCount != 44) {
			ZF_LOGE("Error reading WAV file header.");
			return 2;
		}
		if (memcmp(wavHead, headStr, 4) != 0) {
			ZF_LOGE("%s is not a valid WAV file. 0x%x %x %x %x != 0x%x %x %x %x",
						DecodeWav[WavFileCount], wavHead[0], wavHead[1], wavHead[2],
						wavHead[3], headStr[0], headStr[1], headStr[2], headStr[3]);
			return 3;
		}
		if (wavHead[20] != 0x01) {
			ZF_LOGE("Unexpected WAVE type.");
			return 4;
		}
		if (wavHead[22] != 0x01) {
			ZF_LOGE("Expected single channel WAV.  Consider converting it with SoX.");
			return 7;
		}
		sampleRate = wavHead[24] + (wavHead[25] << 8) + (wavHead[26] << 16) + (wavHead[27] << 24);
		if (sampleRate != 12000) {
			ZF_LOGE("Expected 12kHz sample rate but found %d Hz.  Consider converting it with SoX.", sampleRate);
			return 8;
		}

		nSamples = (wavHead[40] + (wavHead[41] << 8) + (wavHead[42] << 16) + (wavHead[43] << 24)) / 2;
		ZF_LOGD("Reading %d 16-bit samples.", nSamples);
		while (nSamples >= blocksize) {
			readCount = fread(samples, 2, blocksize, wavf);
			if (readCount != blocksize) {
				ZF_LOGE("Premature end of data while reading WAV file.");
				return 5;
			}
			WavNow += blocksize * 1000 / 12000;
			if ((Now - NowOffset) % 100 == 0)  // time stamp at 100 ms intervals
				ZF_LOGV("%s: %.3f sec (%.3f)", DecodeWav[WavFileCount], (Now - NowOffset)/1000.0, Now/1000.0);
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
		if (readCount != nSamples) {
			ZF_LOGE("Premature end of data while reading WAV file.");
			return 6;
		}
		WavNow += blocksize * 1000 / 12000;
		if ((Now - NowOffset) % 100 == 0)  // time stamp at 100 ms intervals
			ZF_LOGV("%s: %.3f sec (%.3f)", DecodeWav[WavFileCount], (Now - NowOffset)/1000.0, Now/1000.0);
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

void InitDevices(DeviceInfo ***devicesptr) {
	if (*devicesptr != NULL) {
		ZF_LOGD("InitDevices() *devicesptr is not NULL, so do nothing.");
		return;  // nothing to free
	}
	*devicesptr = realloc(*devicesptr, sizeof(DeviceInfo *));
	(*devicesptr)[0] = NULL;
}

// Add an unused DeviceInfo item to the end of devicesprt and return the index
// of this new DeviceInfo item.
// Initialize all of the booleans in the new item to false
int ExtendDevices(DeviceInfo ***devicesptr) {
	int index = -1;
	// Find size of array of devices
	while((*devicesptr)[++index] != NULL);

	// Replace NULL in the last element by the new DeviceInfo item
	(*devicesptr)[index] = malloc(sizeof(DeviceInfo));
	// Initialize the string pointers to NULL
	(*devicesptr)[index]->name = NULL;
	(*devicesptr)[index]->alias = NULL;
	(*devicesptr)[index]->desc = NULL;
	// Initialize all of the booleans to false
	(*devicesptr)[index]->capture = false;
	(*devicesptr)[index]->playback = false;
	(*devicesptr)[index]->capturebusy = false;
	(*devicesptr)[index]->playbackbusy = false;
	// Expand the array
	*devicesptr = realloc(*devicesptr, (index + 2) * sizeof(DeviceInfo *));
	// Set last value to NULL
	(*devicesptr)[index + 1] = NULL;
	return index;
}

void FreeDevices(DeviceInfo ***devicesptr) {
	if (*devicesptr == NULL)  {
		ZF_LOGD("FreeDevices() *devicesptr is NULL, so do nothing");
		return;  // nothing to free
	}
	int index = 0;
	while((*devicesptr)[index] != NULL) {
		if ((*devicesptr)[index]->name != NULL)
			free((*devicesptr)[index]->name);
		if ((*devicesptr)[index]->alias != NULL)
			free((*devicesptr)[index]->alias);
		if ((*devicesptr)[index]->desc != NULL)
			free((*devicesptr)[index]->desc);
		free((*devicesptr)[index]);
		++index;
	}
	free(*devicesptr);  // Free the array of pointers
	*devicesptr = NULL;
}

// Write a CSV string suitable for the CAPTUREDEVICES or PLAYBACKDEVICES host
// commands.
// Per the Ardop specification (Protocol Native TNC Commands), these commands
// "Returns a comma delimited list of all currently installed capture/playback
// devices."  The specification does not indicate how device names that
// contain commas shall be handled.  Nor does it provide any explicit
// mechanism to include a device description in addition to a device name.
//
// So: The following is used:
// Device names may be wrapped in double quotes, and double double quotes
// within double quotes shall be interpreted as a literal double quotes
// character.  If any device name includes a linefeed (\n), then the text
// before the linefeed shall be interpreted as the name suitable to pass to the
// PLAYBACK or CAPTURE host command, while any text after the first linefeed
// shall be interpreted as a description.  If the description begins with
// "[BUSY", then this device is currently in use (by this or another program).
//
// Use forcapture to select whether output should be suitable for CAPTUREDEVICES
// or PLAYBACKDEVICES.
// The return value shall be true on success.
// If an error occurs, such as because dstsize is too small, then return false,
// in which case the contents of dst are undefined and should not be used.
bool DevicesToCSV(DeviceInfo **devices, char *dst, int dstsize, bool forcapture) {
	// dst and dstsize are updated to always point to the end of the string
	// written so far, and the amount of remaining free space respectively
	int index = -1;
	while(devices[++index] != NULL) {
		if ((forcapture && !devices[index]->capture)
			|| (!forcapture && !devices[index]->playback)
		)
			continue;

		char *name = devices[index]->name;
		char *alias = devices[index]->alias;
		char *desc = devices[index]->desc;
		char *qptr;
		int count;
		bool quotewrap = ((strchr(name, ',') != NULL)
			|| (strchr(name, '"') != NULL)
			|| (alias != NULL && strchr(alias, ',') != NULL)
			|| (alias != NULL && strchr(alias, '"') != NULL)
			|| (strchr(desc, ',') != NULL)
			|| (strchr(desc, '"') != NULL));
		if (quotewrap) {
			if (dstsize < 2)
				return false;
			*(dst++) = '"';  // Write the leading double quote to dst
			--dstsize;
		}
		while ((qptr = strchr(name, '"')) != NULL) {
			// Copy up to and including a double quote in src, and follow it
			// with a second double quote.
			count = snprintf(dst, dstsize, "%.*s\"",
				(int) ((qptr + 1) - name), name);
			if (count < 0 || count >= dstsize)
				return false;
			name += count - 1;  // count includes the extra double quote
			dst += count;
			dstsize -= count;
		}
		// last part of name and a linefeed to separate name from alias and desc
		count = snprintf(dst, dstsize, "%.*s\n", (int) (strlen(name)), name);
		if (count < 0 || count >= dstsize)
			return false;
		name += count - 1;  // count includes linefeed not from src
		dst += count;
		dstsize -= count;

		if ((forcapture && devices[index]->capturebusy)
			|| (!forcapture && devices[index]->playbackbusy)
		) {
			count = snprintf(dst, dstsize, "[BUSY] ");
			if (count < 0 || count >= dstsize)
				return false;
			dst += count;
			dstsize -= count;
		}

		if (alias != NULL) {
			while ((qptr = strchr(alias, '"')) != NULL) {
				// Copy up to and including a double quote in src, and follow it
				// with a second double quote.
				count = snprintf(dst, dstsize, "%.*s\"\"", (int) ((qptr + 1) - alias), alias);
				if (count < 0 || count >= dstsize)
					return false;
				alias += count - 1;  // count includes the extra double quote
				dst += count;
				dstsize -= count;
			}
			// last part of alias
			count = snprintf(dst, dstsize, "%.*s. ", (int) strlen(desc), alias);
			if (count < 0 || count >= dstsize)
				return false;
			alias += count;
			dst += count;
			dstsize -= count;
		}

		while ((qptr = strchr(desc, '"')) != NULL) {
			// Copy up to and including a double quote in src, and follow it
			// with a second double quote.
			count = snprintf(dst, dstsize, "%.*s\"\"", (int) ((qptr + 1) - desc), desc);
			if (count < 0 || count >= dstsize)
				return false;
			desc += count - 1;  // count includes the extra double quote
			dst += count;
			dstsize -= count;
		}
		// last part of desc
		count = snprintf(dst, dstsize, "%.*s", (int) strlen(desc), desc);
		if (count < 0 || count >= dstsize)
			return false;
		desc += count;
		dst += count;
		dstsize -= count;
		if (quotewrap) {
			if (dstsize < 2)
				return false;
			*(dst++) = '"';  // Write the closing double quote to dst
			--dstsize;
		}
		if (dstsize < 2)
			return false;
		*(dst++) = ',';  // comma separator between device strings
		--dstsize;
	}
	*(--dst) = 0x00;  // replace the last comma with a terminating null.
	return true;
}

// While AudioDevices is a single list that includes output/playback and
// input/capture devices, the output of this function can be filtered with
// inputonly or output only.  If both are true, then log an error and exit.
// If neither are true, then also log devices that could be opened for neither
// input nor output.  Regardless of inputonly and outputonly, append (capture)
// and/or (playback) to all devices to indicate their range of use.
void LogDevices(DeviceInfo **devices, char *headstr, bool inputonly, bool outputonly) {
	char modestr[24];  // long enough for maximum " (*capture) (*playback)"
	if (headstr != NULL)
		ZF_LOGI("%s", headstr);
	if (inputonly && outputonly) {
		ZF_LOGE("LogDevices() cannot be used with both inputonly and outputonly"
			" as true");
		return;
	}

	int index = -1;
	while(devices[++index] != NULL) {
		if (!(inputonly || outputonly)
			|| (inputonly && devices[index]->capture)
			|| (outputonly && devices[index]->playback)
		) {
			modestr[0] = 0x00;  // empty string
			if (devices[index]->capturebusy) {
				strcat(modestr, " (*capture)");
			} else if (devices[index]->capture) {
				strcat(modestr, " (capture)");
			}
			if (devices[index]->playbackbusy) {
				strcat(modestr, " (*playback)");
			} else if (devices[index]->playback) {
				strcat(modestr, " (playback)");
			}
			ZF_LOGI("   %s [%s%s%s]%s",
				devices[index]->name,
				devices[index]->alias != NULL ? devices[index]->alias : "",
				devices[index]->alias != NULL ? ". " : "",
				devices[index]->desc,
				modestr);
		}
	}
	ZF_LOGI("[* before capture or playback indicates that the device is"
		" currently in use for that mode by this or another program]");
}

int getPch(bool quiet) {
	if ((UseLeftTX && UseRightTX) || !(UseLeftTX || UseRightTX)) {
		if (!quiet)
			ZF_LOGI("Using single channel (mono) audio for playback");
		return 1;
	}
	if (!quiet) {
		if (UseLeftTX)
			ZF_LOGI("Using Left channel of stereo audio for playback.");
		else
			ZF_LOGI("Using Right channel of stereo audio for playback.");
	}
	return 2;
}

int getCch(bool quiet) {
	if ((UseLeftRX && UseRightRX) || !(UseLeftRX || UseRightRX)) {
		if (!quiet)
			ZF_LOGI("Using single channel (mono) audio for capture");
		return 1;
	}
	if (!quiet) {
		if (UseLeftRX)
			ZF_LOGI("Using Left channel of stereo audio for capture.");
		else
			ZF_LOGI("Using Right channel of stereo audio for capture.");
	}
	return 2;
}


// Case insensitive substring search.
// Return pointer to start of match, or NULL if no match found
// Like the (GNU nonstandard) strcasestr(), which isn't available on Windows
char *cisearch(const char *haystack, const char *needle) {
	if (strlen(needle) > strlen(haystack))
		return NULL;
	if (strlen(needle) == strlen(haystack) && strcasecmp(needle, haystack) != 0)
		return NULL;
	for (size_t i = 0; i <= strlen(haystack) - strlen(needle); ++i) {
		size_t j;
		for (j = 0; j < strlen(needle); ++j) {
			if (tolower(haystack[i + j]) != tolower(needle[j]))
				break;
		}
		if (j == strlen(needle)) {
			// no break
			return (char *) (haystack + i);
		}
	}
	return NULL;
}

// Return -1 for no matching device.
// Return a non-negative value that is an index into AudioDevices[].
int FindAudioDevice(char *devstr, bool iscapture) {
	// Search for an exact match of devstr in AudioDevices[]->name or
	// AudioDevices[]->alias.  A substring match is NOT accepted for name or
	// alias since hw:CARD=X,DEV=0 is a substring of plughw:CARD=X,DEV=0, so
	// that if a substring were accepted, there would be no way to ensure that
	// the hw: device was selected.  That device is not commonly used, but where
	// it supports the required sample rate and other parameters, it is the best
	// choice.
	int adevindex = -1;
	while (AudioDevices[++adevindex] != NULL) {
		if ((iscapture && !AudioDevices[adevindex]->capture)
			|| (!iscapture && !AudioDevices[adevindex]->playback)
		)
			continue;
		// Compare only the first DEVSTRSZ - 1 bytes (exclude terminating NUlL)
		if (strncmp(AudioDevices[adevindex]->name, devstr, DEVSTRSZ - 1) == 0)
			return adevindex;
		if (AudioDevices[adevindex]->alias != NULL
			&& strncmp(AudioDevices[adevindex]->alias, devstr, DEVSTRSZ - 1) == 0
		)
			return adevindex;
	}
	// No match found in AudioDevices[]->name, so search for the first case
	// insensitive substring match in AudioDevices[]->desc.
	adevindex = -1;
	while (AudioDevices[++adevindex] != NULL) {
		if ((iscapture && !AudioDevices[adevindex]->capture)
			|| (!iscapture && !AudioDevices[adevindex]->playback)
			|| (AudioDevices[adevindex]->desc == NULL)
		)
			continue;
		if (cisearch(AudioDevices[adevindex]->desc, devstr) != NULL)
			return adevindex;
	}
	return -1;
}

// Send updated info about audio system configuration to WebGui.  This should be
// called whenever a change to this configuration is made (or detected due to a
// failure).  If do_getdevices is true, then call GetDevices() first.  This
// should normally be true, unless GetDevices() was just called.
void updateWebGuiAudioConfig(bool do_getdevices) {
	wg_send_rxenabled(0, RXEnabled);
	wg_send_txenabled(0, TXEnabled);
	if (do_getdevices)
		GetDevices();
	wg_send_audiodevices(0, AudioDevices, CaptureDevice, PlaybackDevice,
		crestorable(), prestorable());
}
