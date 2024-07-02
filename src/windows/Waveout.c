//
// Passes audio samples to the sound interface

// Windows uses WaveOut

// Linux will use ALSA

// This is the Windows Version

#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>
#include <mmsystem.h>
#include <stdbool.h>

#include "common/wav.h"
#include "rockliff/rrs.h"

#pragma comment(lib, "winmm.lib")
void printtick(char * msg);
void PollReceivedSamples();

HANDLE OpenCOMPort(VOID * pPort, int speed, BOOL SetDTR, BOOL SetRTS, BOOL Quiet, int Stopbits);
VOID COMSetDTR(HANDLE fd);
VOID COMClearDTR(HANDLE fd);
VOID COMSetRTS(HANDLE fd);
VOID COMClearRTS(HANDLE fd);
VOID processargs(int argc, char * argv[]);
void DecodeCM108(char * ptr);

#include <math.h>

#include "common/ardopcommon.h"

void GetSoundDevices();

#ifdef LOGTOHOST

// Log output sent to host instead of File

#define LOGBUFFERSIZE 2048

char LogToHostBuffer[LOGBUFFERSIZE];
int LogToHostBufferLen;

#endif

// Windows works with signed samples +- 32767
// Currently use 1200 samples for TX but 480 for RX to reduce latency

short buffer[2][SendSize];  // Two Transfer/DMA buffers of 0.1 Sec
short inbuffer[5][ReceiveSize];  // Input Transfer/ buffers of 0.1 Sec

BOOL Loopback = FALSE;
// BOOL Loopback = TRUE;

char CaptureDevice[80] = "0";  // "2";
char PlaybackDevice[80] = "0";  // "1";


BOOL UseLeftRX = TRUE;
BOOL UseRightRX = TRUE;

BOOL UseLeftTX = TRUE;
BOOL UseRightTX = TRUE;

char LogDir[256] = "";

FILE *logfile[3] = {NULL, NULL, NULL};
char LogName[3][256] = {"ARDOPDebug", "ARDOPException", "ARDOPSession"};

#define DEBUGLOG 0
#define EXCEPTLOG 1
#define SESSIONLOG 2


char * CaptureDevices = NULL;
char * PlaybackDevices = NULL;

int CaptureCount = 0;
int PlaybackCount = 0;

int CaptureIndex = -1;  // Card number
int PlayBackIndex = -1;


char CaptureNames[16][MAXPNAMELEN + 2]= {""};
char PlaybackNames[16][MAXPNAMELEN + 2]= {""};

WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 1, 12000, 12000, 2, 16, 0 };

HWAVEOUT hWaveOut = 0;
HWAVEIN hWaveIn = 0;

WAVEHDR header[2] =
{
	{(char *)buffer[0], 0, 0, 0, 0, 0, 0, 0},
	{(char *)buffer[1], 0, 0, 0, 0, 0, 0, 0}
};

WAVEHDR inheader[5] =
{
	{(char *)inbuffer[0], 0, 0, 0, 0, 0, 0, 0},
	{(char *)inbuffer[1], 0, 0, 0, 0, 0, 0, 0},
	{(char *)inbuffer[2], 0, 0, 0, 0, 0, 0, 0},
	{(char *)inbuffer[3], 0, 0, 0, 0, 0, 0, 0},
	{(char *)inbuffer[4], 0, 0, 0, 0, 0, 0, 0}
};

WAVEOUTCAPS pwoc;
WAVEINCAPS pwic;

int InitSound(BOOL Quiet);
void HostPoll();
void TCPHostPoll();
void SerialHostPoll();
BOOL WriteCOMBlock(HANDLE fd, char * Block, int BytesToWrite);
void WebguiPoll();
int wg_send_currentlevel(int cnum, unsigned char level);
int wg_send_pttled(int cnum, bool isOn);
int wg_send_pixels(int cnum, unsigned char *data, size_t datalen);

int Ticks;

LARGE_INTEGER Frequency;
LARGE_INTEGER StartTicks;
LARGE_INTEGER NewTicks;

int LastNow;

extern void Generate50BaudTwoToneLeaderTemplate();
extern BOOL blnDISCRepeating;
void Send5SecTwoTone();

extern struct sockaddr HamlibAddr;  // Dest for above
extern int useHamLib;


#define TARGET_RESOLUTION 1  // 1-millisecond target resolution

extern int WavNow;  // Time since start of WAV file being decoded
extern char DecodeWav[256];
extern BOOL WriteTxWav;
extern BOOL WriteRxWav;
extern BOOL TwoToneAndExit;
struct WavFile *rxwf = NULL;
struct WavFile *txwff = NULL;
// writing unfiltered tx audio to WAV disabled
// struct WavFile *txwfu = NULL;
#define RXWFTAILMS 10000;  // 10 seconds
unsigned int rxwf_EndNow = 0;

void extendRxwf()
{
	rxwf_EndNow = Now + RXWFTAILMS;
}

void StartRxWav()
{
	// Open a new WAV file if not already recording.
	// If already recording, then just extend the time before
	// recording will end.
	//
	// Wav files will use a filename that includes port, UTC date,
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
	char rxwf_pathname[1024];
	SYSTEMTIME st;

	if (rxwf != NULL)
	{
		// Already recording, so just extend recording time.
		extendRxwf();
		return;
	}

	GetSystemTime(&st);

	if (LogDir[0])
	{
		if (HostPort[0])
			sprintf(rxwf_pathname, "%s/ARDOP_rxaudio_%s_%04d%02d%02d_%02d%02d%02d.wav",
				LogDir, HostPort, st.wYear, st.wMonth, st.wDay,
				st.wHour, st.wMinute, st.wSecond);
		else
			sprintf(rxwf_pathname, "%s/ARDOP_rxaudio_%04d%02d%02d_%02d%02d%02d.wav",
				LogDir, st.wYear, st.wMonth, st.wDay,
				st.wHour, st.wMinute, st.wSecond);
	}
	else
	{
		if (HostPort[0])
			sprintf(rxwf_pathname, "ARDOP_rxaudio_%s_%04d%02d%02d_%02d%02d%02d.wav",
				HostPort, st.wYear, st.wMonth, st.wDay,
				st.wHour, st.wMinute, st.wSecond);
		else
			sprintf(rxwf_pathname, "ARDOP_rxaudio_%04d%02d%02d_%02d%02d%02d.wav",
				st.wYear, st.wMonth, st.wDay,
				st.wHour, st.wMinute, st.wSecond);
	}

	rxwf = OpenWavW(rxwf_pathname);
	extendRxwf();
}

// writing unfiltered tx audio to WAV disabled.  Only filtered
// tx audio will be written.  However, the code for unfiltered
// audio is left in place but commented out so that it can eaily
// be restored if desired.
void StartTxWav()
{
	// Open two new WAV files for filtered and unfiltered Tx audio.
	//
	// Wav files will use a filename that includes port, UTC date,
	// and UTC time, similar to log files but with added time to
	// the nearest second.  Like Log files, these Wav files will be
	// written to the Log directory if defined, else to the current
	// directory
	char txwff_pathname[1024];
	// char txwfu_pathname[1024];
	SYSTEMTIME st;

	if (txwff != NULL)  // || txwfu != NULL)
	{
		WriteDebugLog(LOGWARNING, "WARNING: Trying to open Tx WAV file, but already open.");
		return;
	}

	GetSystemTime(&st);

	if (LogDir[0])
	{
		if (HostPort[0])
		{
			sprintf(txwff_pathname, "%s/ARDOP_txfaudio_%s_%04d%02d%02d_%02d%02d%02d.wav",
				LogDir, HostPort, st.wYear, st.wMonth, st.wDay,
				st.wHour, st.wMinute, st.wSecond);
			// sprintf(txwfu_pathname, "%s/ARDOP_txuaudio_%s_%04d%02d%02d_%02d%02d%02d.wav",
				// LogDir, HostPort, st.wYear, st.wMonth, st.wDay,
				// st.wHour, st.wMinute, st.wSecond);
		}
		else
		{
			sprintf(txwff_pathname, "%s/ARDOP_txfaudio_%04d%02d%02d_%02d%02d%02d.wav",
				LogDir, st.wYear, st.wMonth, st.wDay,
				st.wHour, st.wMinute, st.wSecond);
			// sprintf(txwfu_pathname, "%s/ARDOP_txuaudio_%04d%02d%02d_%02d%02d%02d.wav",
				// LogDir, st.wYear, st.wMonth, st.wDay,
				// st.wHour, st.wMinute, st.wSecond);
		}
	}
	else
	{
		if (HostPort[0])
		{
			sprintf(txwff_pathname, "ARDOP_txfaudio_%s_%04d%02d%02d_%02d%02d%02d.wav",
				HostPort, st.wYear, st.wMonth, st.wDay,
				st.wHour, st.wMinute, st.wSecond);
			// sprintf(txwfu_pathname, "ARDOP_txuaudio_%s_%04d%02d%02d_%02d%02d%02d.wav",
				// HostPort, st.wYear, st.wMonth, st.wDay,
				// st.wHour, st.wMinute, st.wSecond);
		}
		else
		{
			sprintf(txwff_pathname, "ARDOP_txfaudio_%04d%02d%02d_%02d%02d%02d.wav",
				st.wYear, st.wMonth, st.wDay,
				st.wHour, st.wMinute, st.wSecond);
			// sprintf(txwfu_pathname, "ARDOP_txuaudio_%04d%02d%02d_%02d%02d%02d.wav",
				// st.wYear, st.wMonth, st.wDay,
				// st.wHour, st.wMinute, st.wSecond);
		}
	}
	txwff = OpenWavW(txwff_pathname);
	// txwfu = OpenWavW(txwfu_pathname);
}


VOID __cdecl Debugprintf(const char * format, ...)
{
	char Mess[10000];
	va_list(arglist);

	va_start(arglist, format);
	vsprintf(Mess, format, arglist);
	WriteDebugLog(LOGDEBUG, Mess);

	return;
}

BOOL CtrlHandler(DWORD fdwCtrlType)
{
	switch( fdwCtrlType ) {
	// Handle the CTRL-C signal.
	case CTRL_C_EVENT:
		printf( "Ctrl-C event\n\n" );
		blnClosing = TRUE;
		Beep( 750, 300 );
		Sleep(1000);
		return( TRUE );

	// CTRL-CLOSE: confirm that the user wants to exit.

	case CTRL_CLOSE_EVENT:

		blnClosing = TRUE;
		printf( "Ctrl-Close event\n\n" );
		Sleep(20000);
		Beep( 750, 300 );
		return( TRUE );

	// Pass other signals to the next handler.
	case CTRL_BREAK_EVENT:
		Beep( 900, 200 );
		printf( "Ctrl-Break event\n\n" );
		blnClosing = TRUE;
		Beep( 750, 300 );
		return FALSE;

	case CTRL_LOGOFF_EVENT:
		Beep( 1000, 200 );
		printf( "Ctrl-Logoff event\n\n" );
		return FALSE;

	case CTRL_SHUTDOWN_EVENT:
		Beep( 750, 500 );
		printf( "Ctrl-Shutdown event\n\n" );
		blnClosing = TRUE;
		Beep( 750, 300 );
	return FALSE;

	default:
		return FALSE;
	}
}



int platform_main(int argc, char * argv[])
{
	TIMECAPS tc;
	unsigned int     wTimerRes;
	DWORD	t, lastt = 0;
	int i = 0;
	// rslen_set[] must list all of the rslen values used.
	int rslen_set[] = {2, 4, 8, 16, 32, 36, 50, 64};
	init_rs(rslen_set, 8);

	SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);

	if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) != TIMERR_NOERROR)
	{
		// Error; application can't continue.
	}

	wTimerRes = min(max(tc.wPeriodMin, TARGET_RESOLUTION), tc.wPeriodMax);
	timeBeginPeriod(wTimerRes);

	t = timeGetTime();

	processargs(argc, argv);

	if (LogDir[0])
	{
		sprintf(&LogName[0][0], "%s/%s", LogDir, "ARDOPDebug");
		sprintf(&LogName[1][0], "%s/%s", LogDir, "ARDOPException");
		sprintf(&LogName[2][0], "%s/%s", LogDir, "ARDOPSession");
	}

	WriteDebugLog(LOGALERT, "%s Version %s (https://www.github.com/pflarue/ardop)", ProductName, ProductVersion);
	WriteDebugLog(LOGALERT, "Copyright (c) 2014-2024 Rick Muething, John Wiseman, Peter LaRue");
	WriteDebugLog(LOGALERT,
		"See https://github.com/pflarue/ardop/blob/master/LICENSE for licence details including\n"
		"  information about authors of external libraries used and their licenses."
	);

	if (DecodeWav[0])
	{
		decode_wav();
		return (0);
	}

	if (HostPort[0])
		port = atoi(HostPort);

	_strupr(CaptureDevice);
	_strupr(PlaybackDevice);

	if (PTTPort[0])
	{
		// Can be port{:speed] for serial, a vid/pid pair or a device UID for CM108 or IPADDR:PORT for Hamlib

		if (_memicmp(PTTPort, "0x", 2) == 0 ||  _memicmp(PTTPort, "\\\\", 2) == 0)
		{
			// CM108 device

			DecodeCM108(PTTPort);
		}
		else
		{
			char * Baud = strlop(PTTPort, ':');

			if (Baud)
			{
				// Could be IPADDR:PORT or COMPORT:SPEED. See if first part is valid ip address

				struct sockaddr_in * destaddr = (struct sockaddr_in *)&HamlibAddr;

				destaddr->sin_family = AF_INET;
				destaddr->sin_addr.s_addr = inet_addr(PTTPort);
				destaddr->sin_port = htons(atoi(Baud));

				if (destaddr->sin_addr.s_addr != INADDR_NONE)
				{
					useHamLib = 1;
					WriteDebugLog(LOGALERT, "Using Hamlib at %s:%s for PTT", PTTPort, Baud);
					RadioControl = TRUE;
					PTTMode = PTTHAMLIB;
				}
				else
					PTTBAUD = atoi(Baud);
			}
			if (useHamLib == 0)
				hPTTDevice = OpenCOMPort(PTTPort, PTTBAUD, FALSE, FALSE, FALSE, 0);
		}
	}


	if (CATPort[0])
	{
		char * Baud = strlop(CATPort, ':');
		if (strcmp(CATPort, PTTPort) == 0)
		{
			hCATDevice = hPTTDevice;
		}
		else
		{
			if (Baud)
			CATBAUD = atoi(Baud);
			hCATDevice = OpenCOMPort(CATPort, CATBAUD, FALSE, FALSE, FALSE, 0);
		}
	}

	if (hCATDevice)
	{
		WriteDebugLog(LOGALERT, "CAT Control on port %s", CATPort);
		COMSetRTS(hPTTDevice);
		COMSetDTR(hPTTDevice);
		if (PTTOffCmdLen)
		{
			WriteDebugLog(LOGALERT, "PTT using CAT Port", CATPort);
			RadioControl = TRUE;
		}
	}
	else
	{
		// Warn of -u and -k defined but no CAT Port

		if (PTTOffCmdLen)
			WriteDebugLog(LOGALERT, "Warning PTT Off string defined but no CAT port", CATPort);
	}

	if (hPTTDevice)
	{
		WriteDebugLog(LOGALERT, "Using RTS on port %s for PTT", PTTPort);
		COMClearRTS(hPTTDevice);
		COMClearDTR(hPTTDevice);
		RadioControl = TRUE;
	}

	QueryPerformanceFrequency(&Frequency);
	Frequency.QuadPart /= 1000;  // Microsecs
	QueryPerformanceCounter(&StartTicks);

	GetSoundDevices();

	if(!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS))
		printf("Failed to set High Priority (%d)\n"), GetLastError();

	if (TwoToneAndExit)
	{
		if (!InitSound(TRUE))
		{
			WriteDebugLog(LOGCRIT, "Error in InitSound().  Stopping ardop.");
			return (0);
		}
		WriteDebugLog(LOGINFO, "Sending a 5 second 2-tone signal. Then exiting ardop.");
		Send5SecTwoTone();
		return (0);
	}
	ardopmain();
	return (0);
}

unsigned int getTicks()
{
	// When decoding a WAV file, return WavNow, a measure of the offset
	// in ms from the start of the WAV file.
	if (DecodeWav[0])
		return WavNow;

	return timeGetTime();
//		QueryPerformanceCounter(&NewTicks);
//		return (int)(NewTicks.QuadPart - StartTicks.QuadPart) / Frequency.QuadPart;
}

void printtick(char * msg)
{
	QueryPerformanceCounter(&NewTicks);
	WriteDebugLog(LOGCRIT, "%s %i\r", msg, Now - LastNow);
	LastNow = Now;
}

void txSleep(int mS)
{
	// called while waiting for next TX buffer. Run background processes

	PollReceivedSamples();  // discard any received samples
	TCPHostPoll();
	WebguiPoll();

	if (strcmp(PlaybackDevice, "NOSOUND") != 0)
		Sleep(mS);

	if (PKTLEDTimer && Now > PKTLEDTimer)
	{
		PKTLEDTimer = 0;
		SetLED(PKTLED, 0);  // turn off packet rxed led
	}
}

int PriorSize = 0;

int Index = 0;  // DMA TX Buffer being used 0 or 1
int inIndex = 0;  // DMA Buffer being used


FILE * wavfp1;

BOOL DMARunning = FALSE;  // Used to start DMA on first write

short * SendtoCard(unsigned short * buf, int n)
{
	if (txwff != NULL)
		WriteWav(&buffer[Index][0], n, txwff);

	if (strcmp(PlaybackDevice, "NOSOUND") == 0) {
		Index = !Index;
		return &buffer[Index][0];
	}

	header[Index].dwBufferLength = n * 2;
	waveOutPrepareHeader(hWaveOut, &header[Index], sizeof(WAVEHDR));
	waveOutWrite(hWaveOut, &header[Index], sizeof(WAVEHDR));

	// wait till previous buffer is complete

	while (!(header[!Index].dwFlags & WHDR_DONE))
	{
		txSleep(10);  // Run buckground while waiting
	}

	waveOutUnprepareHeader(hWaveOut, &header[!Index], sizeof(WAVEHDR));
	Index = !Index;

	return &buffer[Index][0];
}


// This generates a nice musical pattern for sound interface testing
// for (t = 0; t < sizeof(buffer); ++t)
//  buffer[t] =((((t * (t >> 8 | t >> 9) & 46 & t >> 8)) ^ (t & t >> 13 | t >> 6)) & 0xFF);

void GetSoundDevices()
{
	int i;

	WriteDebugLog(LOGALERT, "Capture Devices");

	CaptureCount = waveInGetNumDevs();

	CaptureDevices = malloc((MAXPNAMELEN + 2) * CaptureCount);
	CaptureDevices[0] = 0;

	for (i = 0; i < CaptureCount; i++)
	{
		waveInOpen(&hWaveIn, i, &wfx, 0, 0, CALLBACK_NULL);  // WAVE_MAPPER
		waveInGetDevCaps((UINT_PTR)hWaveIn, &pwic, sizeof(WAVEINCAPS));

		if (CaptureDevices)
			strcat(CaptureDevices, ",");
		strcat(CaptureDevices, pwic.szPname);
		WriteDebugLog(LOGALERT, "%d %s", i, pwic.szPname);
		memcpy(&CaptureNames[i][0], pwic.szPname, MAXPNAMELEN);
		_strupr(&CaptureNames[i][0]);
	}

	WriteDebugLog(LOGALERT, "Playback Devices");

	PlaybackCount = waveOutGetNumDevs();

	PlaybackDevices = malloc((MAXPNAMELEN + 2) * PlaybackCount);
	PlaybackDevices[0] = 0;

	for (i = 0; i < PlaybackCount; i++)
	{
		waveOutOpen(&hWaveOut, i, &wfx, 0, 0, CALLBACK_NULL);  // WAVE_MAPPER
		waveOutGetDevCaps((UINT_PTR)hWaveOut, &pwoc, sizeof(WAVEOUTCAPS));

		if (PlaybackDevices[0])
			strcat(PlaybackDevices, ",");
		strcat(PlaybackDevices, pwoc.szPname);
		WriteDebugLog(LOGALERT, "%i %s", i, pwoc.szPname);
		memcpy(&PlaybackNames[i][0], pwoc.szPname, MAXPNAMELEN);
		_strupr(&PlaybackNames[i][0]);
		waveOutClose(hWaveOut);
	}
}


int InitSound(BOOL Report)
{
	int i, ret;
	if (strcmp(PlaybackDevice, "NOSOUND") == 0)
		return TRUE;

	header[0].dwFlags = WHDR_DONE;
	header[1].dwFlags = WHDR_DONE;

	if (strlen(PlaybackDevice) <= 2)
		PlayBackIndex = atoi(PlaybackDevice);
	else
	{
		// Name instead of number. Look for a substring match

		for (i = 0; i < PlaybackCount; i++)
		{
			if (strstr(&PlaybackNames[i][0], PlaybackDevice))
			{
				PlayBackIndex = i;
				break;
			}
		}
	}
	if (PlayBackIndex == -1) {
		WriteDebugLog(LOGERROR,
			"ERROR: playbackdevice = '%s' not found.  Try using one of the names or"
			" numbers (0-%d) listed above.",
			PlaybackDevice,
			PlaybackCount - 1
		);
		return FALSE;
	}

	ret = waveOutOpen(&hWaveOut, PlayBackIndex, &wfx, 0, 0, CALLBACK_NULL);  // WAVE_MAPPER

	if (ret)
		WriteDebugLog(LOGALERT, "Failed to open WaveOut Device %s Error %d", PlaybackDevice, ret);
	else
	{
		ret = waveOutGetDevCaps((UINT_PTR)hWaveOut, &pwoc, sizeof(WAVEOUTCAPS));
		if (Report)
			WriteDebugLog(LOGALERT, "Opened WaveOut Device %s", pwoc.szPname);
	}

	if (strlen(CaptureDevice) <= 2)
		CaptureIndex = atoi(CaptureDevice);
	else
	{
		// Name instead of number. Look for a substring match

		for (i = 0; i < CaptureCount; i++)
		{
			if (strstr(&CaptureNames[i][0], CaptureDevice))
			{
				CaptureIndex = i;
				break;
			}
		}
	}
	if (CaptureIndex == -1) {
		WriteDebugLog(LOGERROR,
			"ERROR: capturedevice = '%s' not found.  Try using one of the names or"
			" numbers (0-%d) listed above.",
			CaptureDevice,
			CaptureCount - 1
		);
		return FALSE;
	}

	ret = waveInOpen(&hWaveIn, CaptureIndex, &wfx, 0, 0, CALLBACK_NULL);  // WAVE_MAPPER
	if (ret)
		WriteDebugLog(LOGALERT, "Failed to open WaveIn Device %s Error %d", CaptureDevice, ret);
	else
	{
		ret = waveInGetDevCaps((UINT_PTR)hWaveIn, &pwic, sizeof(WAVEINCAPS));
		if (Report)
			WriteDebugLog(LOGALERT, "Opened WaveIn Device %s", pwic.szPname);
	}

//	wavfp1 = fopen("s:\\textxxx.wav", "wb");

	for (i = 0; i < NumberofinBuffers; i++)
	{
		inheader[i].dwBufferLength = ReceiveSize * 2;

		ret = waveInPrepareHeader(hWaveIn, &inheader[i], sizeof(WAVEHDR));
		ret = waveInAddBuffer(hWaveIn, &inheader[i], sizeof(WAVEHDR));
	}

	ret = waveInStart(hWaveIn);
	return TRUE;
}

int min = 0, max = 0, lastlevelGUI = 0, lastlevelreport = 0;
UCHAR CurrentLevel = 0;  // Peak from current samples


void PollReceivedSamples()
{
	if (strcmp(PlaybackDevice, "NOSOUND") == 0)
		return;
	// Process any captured samples
	// Ideally call at least every 100 mS, more than 200 will loose data

	// For level display we want a fairly rapir level average but only want to report
	// to log every 10 secs or so

	if (inheader[inIndex].dwFlags & WHDR_DONE)
	{
		short * ptr = &inbuffer[inIndex][0];
		int i;

		for (i = 0; i < ReceiveSize; i++)
		{
			if (*(ptr) < min)
				min = *ptr;
			else if (*(ptr) > max)
				max = *ptr;
			ptr++;
		}

		CurrentLevel = ((max - min) * 75) /32768;  // Scale to 150 max
		wg_send_currentlevel(0, CurrentLevel);

		if ((Now - lastlevelGUI) > 2000)  // 2 Secs
		{
			if (WaterfallActive == 0 && SpectrumActive == 0)  // Don't need to send as included in Waterfall Line
				SendtoGUI('L', &CurrentLevel, 1);  // Signal Level

			lastlevelGUI = Now;

			if ((Now - lastlevelreport) > 10000)  // 10 Secs
			{
				lastlevelreport = Now;
				// Report input peaks to host if in debug mode or if close to clipping
				if (max >= 32000 || ConsoleLogLevel >= LOGDEBUG)
				{
					char HostCmd[64];

					sprintf(HostCmd, "INPUTPEAKS %d %d", min, max);
					SendCommandToHostQuiet(HostCmd);
					WriteDebugLog(LOGINFO, "Input peaks = %d, %d", min, max);
					// A user NOT in debug mode will see this message if they are clipping
					if (ConsoleLogLevel <= LOGINFO)
					{
						WriteDebugLog(LOGINFO,
							"Your input signal is probably clipping.  If you"
							" see this message repeated in the next 20-30"
							" seconds, Turn down your RX input until this"
							" message stops repeating."
						);
					}
				}
			}
			min = max = 0;
		}

		if (rxwf != NULL)
		{
			// There is an open Wav file recording.
			// Either close it or write samples to it.
			if (rxwf_EndNow < Now)
			{
				CloseWav(rxwf);
				rxwf = NULL;
			}
			else
				WriteWav(&inbuffer[inIndex][0], inheader[inIndex].dwBytesRecorded/2, rxwf);
		}

//		WriteDebugLog(LOGDEBUG, "Process %d %d", inIndex, inheader[inIndex].dwBytesRecorded/2);
		if (Capturing && Loopback == FALSE)
			ProcessNewSamples(&inbuffer[inIndex][0], inheader[inIndex].dwBytesRecorded/2);

		waveInUnprepareHeader(hWaveIn, &inheader[inIndex], sizeof(WAVEHDR));
		inheader[inIndex].dwFlags = 0;
		waveInPrepareHeader(hWaveIn, &inheader[inIndex], sizeof(WAVEHDR));
		waveInAddBuffer(hWaveIn, &inheader[inIndex], sizeof(WAVEHDR));

		inIndex++;

		if (inIndex == NumberofinBuffers)
			inIndex = 0;
	}
}


void StopCapture()
{
	Capturing = FALSE;

//	waveInStop(hWaveIn);
//	WriteDebugLog(LOGDEBUG, "Stop Capture");
}

void StartCapture()
{
	Capturing = TRUE;
	DiscardOldSamples();
	ClearAllMixedSamples();
	State = SearchingForLeader;

//	WriteDebugLog(LOGDEBUG, "Start Capture");
}
void CloseSound()
{
	waveInClose(hWaveIn);
	waveOutClose(hWaveOut);
}

#include <stdarg.h>

VOID CloseDebugLog()
{
	if(logfile[0])
		fclose(logfile[0]);
	logfile[0] = NULL;
}

int WriteLog(char * msg, int log) {
	SYSTEMTIME st;
	UCHAR Value[100];
	char timebuf[128];
	int wavhh;
	int wavmm;
	float wavss;

	GetSystemTime(&st);
	if (logfile[log] == NULL)
	{
		if (HostPort[0])
			sprintf(Value, "%s%s_%04d%02d%02d.log",
				&LogName[log], HostPort, st.wYear, st.wMonth, st.wDay);
		else
			sprintf(Value, "%s%d_%04d%02d%02d.log",
				&LogName[log], port, st.wYear, st.wMonth, st.wDay);

		if ((logfile[log] = fopen(Value, "ab")) == NULL)
			return -1;
	}
	if (DecodeWav[0])
	{
		// When decoding a WAV file, include an approximate reference to the time offset
		// since the start of the WAV file.
		wavhh = Now/3600000;
		wavmm = ((Now/1000) - wavhh * 3600) / 60;
		wavss = (Now % 60000) / 1000.0;
		sprintf(timebuf, "%02d:%02d:%02d.%03d (WAV %02d:%02d:%05.2f) ",
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
			wavhh, wavmm, wavss);
	}
	else
	{
		sprintf(timebuf, "%02d:%02d:%02d.%03d ",
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	}
	fputs(timebuf, logfile[log]);
	fputs(msg, logfile[log]);
	return 0;
}

VOID WriteDebugLog(int LogLevel, const char * format, ...)
{
	char Mess[10000];
	va_list(arglist);

	va_start(arglist, format);
#ifdef LOGTOHOST
	vsnprintf(&Mess[1], sizeof(Mess), format, arglist);
	strcat(Mess, "\r\n");
	Mess[0] = LogLevel + '0';
	SendLogToHost(Mess, strlen(Mess));
#else
	vsnprintf(Mess, sizeof(Mess), format, arglist);
	strcat(Mess, "\r\n");

	if (LogLevel <= ConsoleLogLevel)
		printf(Mess);

	if (!DebugLog)
		return;

	if (LogLevel > FileLogLevel)
		return;
	WriteLog(Mess, DEBUGLOG);
#endif
}

VOID WriteExceptionLog(const char * format, ...)
{
	char Mess[10000];
	va_list(arglist);
	FILE *logfile = NULL;

	va_start(arglist, format);
	vsnprintf(Mess, sizeof(Mess), format, arglist);
	strcat(Mess, "\r\n");

	printf(Mess);
	WriteLog(Mess, EXCEPTLOG);
}

FILE *statslogfile = NULL;

VOID CloseStatsLog()
{
	fclose(statslogfile);
	statslogfile = NULL;
}
VOID Statsprintf(const char * format, ...)
{
	char Mess[10000];
	va_list(arglist);
	UCHAR Value[100];
	char timebuf[32];

	SYSTEMTIME st;

	va_start(arglist, format);
	vsnprintf(Mess, sizeof(Mess), format, arglist);
	strcat(Mess, "\r\n");

	if (statslogfile == NULL)
	{
		GetSystemTime(&st);
		if (HostPort[0])
			sprintf(Value, "%s%s_%04d%02d%02d.log",
				&LogName[2], HostPort, st.wYear, st.wMonth, st.wDay);
		else
			sprintf(Value, "%s%d_%04d%02d%02d.log",
				&LogName[2], port, st.wYear, st.wMonth, st.wDay);

		if ((statslogfile = fopen(Value, "ab")) == NULL)
			return;
		else
		{
			// Including the date is redundant with the filename for the session log
			// file, but is useful for also writing it to the console.
			sprintf(timebuf, "%04d/%02d/%02d %02d:%02d:%02d.%03dz\r\n",
				st.wYear, st.wMonth, st.wDay,
				st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
			fputs(timebuf, statslogfile);
			// Printing the UTC date and time to the console with the session stats
			// may be useful if sessions are logged manually.
			printf("%s\n", timebuf);
		}
	}

	fputs(Mess, statslogfile);
	printf(Mess);

	return;
}

VOID WriteSamples(short * buffer, int len)
{
	fwrite(buffer, 1, len * 2, wavfp1);
}

unsigned short * SoundInit()
{
	Index = 0;
	return &buffer[0][0];
}

// Called at end of transmission

extern int Number;  // Number of samples waiting to be sent

void SoundFlush()
{
	// Append Trailer then wait for TX to complete

	AddTrailer();  // add the trailer.

	if (Loopback)
		ProcessNewSamples(buffer[Index], Number);

	SendtoCard(buffer[Index], Number);

	// Wait for all sound output to complete
	if (strcmp(PlaybackDevice, "NOSOUND") != 0) {
		while (!(header[0].dwFlags & WHDR_DONE))
			txSleep(10);
		while (!(header[1].dwFlags & WHDR_DONE))
			txSleep(10);
	}

	// I think we should turn round the link here. I dont see the point in
	// waiting for MainPoll

	SoundIsPlaying = FALSE;

	// Debug.WriteLine("[tmrPoll.Tick] Play stop. Length = " & Format(Now.Subtract(dttTestStart).TotalMilliseconds, "#") & " ms")

//	WriteDebugLog(LOGDEBUG, "Play complete blnEnbARQRpt = %d", blnEnbARQRpt);

	if (blnEnbARQRpt > 0 || blnDISCRepeating)  // Start Repeat Timer if frame should be repeated
		dttNextPlay = Now + intFrameRepeatInterval;

//	WriteDebugLog(LOGDEBUG, "Now %d Now - dttNextPlay 1  = %d", Now, Now - dttNextPlay);

	KeyPTT(FALSE);  // Unkey the Transmitter
	if (txwff != NULL)
	{
		CloseWav(txwff);
		txwff = NULL;
	}
	// writing unfiltered tx audio to WAV disabled
	// if (txwfu != NULL)
	// {
		// CloseWav(txwfu);
		// txwfu = NULL;
	// }

	// Clear the capture buffers. I think this is only  needed when testing
	// with audio loopback.

//	memset(&buffer[0], 0, 2400);
//	memset(&buffer[1], 0, 2400);

	StartCapture();

	// clear the transmit label
	// stcStatus.BackColor = SystemColors.Control
	// stcStatus.ControlName = "lblXmtFrame"  // clear the transmit label
	// queTNCStatus.Enqueue(stcStatus)
	// stcStatus.ControlName = "lblRcvFrame"  // clear the Receive label
	// queTNCStatus.Enqueue(stcStatus)

	if (WriteRxWav)
		// Start recording if not already recording, else extend the recording time.
		StartRxWav();

	return;
}


void StartCodec(char * strFault)
{
	strFault[0] = 0;
	InitSound(FALSE);

}

void StopCodec(char * strFault)
{
	CloseSound();
	strFault[0] = 0;
}

VOID RadioPTT(BOOL PTTState)
{
	if (PTTMode & PTTRTS)
		if (PTTState)
			COMSetRTS(hPTTDevice);
		else
			COMClearRTS(hPTTDevice);

	if (PTTMode & PTTDTR)
		if (PTTState)
			COMSetDTR(hPTTDevice);
		else
			COMClearDTR(hPTTDevice);

	if (PTTMode & PTTCI_V)
		if (PTTState)
			WriteCOMBlock(hCATDevice, PTTOnCmd, PTTOnCmdLen);
		else
			WriteCOMBlock(hCATDevice, PTTOffCmd, PTTOffCmdLen);

	if (PTTMode & PTTCM108)
		CM108_set_ptt(PTTState);
}

// Function to send PTT TRUE or PTT FALSE comannad to Host or if local Radio control Keys radio PTT

const char BoolString[2][6] = {"FALSE", "TRUE"};

BOOL KeyPTT(BOOL blnPTT)
{
	// Returns TRUE if successful False otherwise

	if (blnLastPTT &&  !blnPTT)
		dttStartRTMeasure = Now;  // start a measurement on release of PTT.

	if (!RadioControl)
		if (blnPTT)
			SendCommandToHostQuiet("PTT TRUE");
		else
			SendCommandToHostQuiet("PTT FALSE");

	else
		RadioPTT(blnPTT);

	WriteDebugLog(LOGDEBUG, "[Main.KeyPTT]  PTT-%s", BoolString[blnPTT]);

	blnLastPTT = blnPTT;
	SetLED(0, blnPTT);
	wg_send_pttled(0, blnPTT);
	return TRUE;
}

void PlatformSleep(int mS)
{
	// Sleep to avoid using all cpu

	if (strcmp(PlaybackDevice, "NOSOUND") != 0)
		Sleep(mS);

	if (PKTLEDTimer && Now > PKTLEDTimer)
	{
		PKTLEDTimer = 0;
		SetLED(PKTLED, 0);  // turn off packet rxed led
	}
}

void DrawTXMode(const char * Mode)
{
	char Msg[80];

	strcpy(Msg, Mode);
	SendtoGUI('T', Msg, strlen(Msg) + 1);  // TX Frame
}

void DrawTXFrame(const char * Frame)
{
	char Msg[80];

	strcpy(Msg, Frame);
	SendtoGUI('T', Msg, strlen(Msg) + 1);  // TX Frame
}

void DrawRXFrame(int State, const char * Frame)
{
	unsigned char Msg[64];

	Msg[0] = State;  // Pending/Good/Bad
	strcpy(&Msg[1], Frame);
	SendtoGUI('R', Msg, strlen(Frame) + 2);  // RX Frame
}

char Leds[8]= {0};
unsigned int PKTLEDTimer = 0;

void SetLED(int LED, int State)
{
	// If GUI active send state

	Leds[LED] = State;
	SendtoGUI('D', Leds, 8);
}

HANDLE OpenCOMPort(VOID * pPort, int speed, BOOL SetDTR, BOOL SetRTS, BOOL Quiet, int Stopbits)
{
	char szPort[256];
	BOOL fRetVal ;
	COMMTIMEOUTS  CommTimeOuts ;
	int	Err;
	char buf[256];
	HANDLE fd;
	DCB dcb;

	// if Port Name starts COM, convert to \\.\COM or ports above 10 wont work

	if (atoi(pPort) != 0)  // just a com port number
		sprintf( szPort, "\\\\.\\COM%d", pPort);

	else if (_memicmp(pPort, "COM", 3) == 0)
	{
		char * pp = (char *)pPort;
		int p = atoi(&pp[3]);
		sprintf( szPort, "\\\\.\\COM%d", p);
	}
	else
		strcpy(szPort, pPort);

	// open COMM device

	fd = CreateFile( szPort, GENERIC_READ | GENERIC_WRITE,
		0,  // exclusive access
		NULL,  // no security attrs
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (fd == (HANDLE) -1)
	{
		if (Quiet == 0)
		{
			if (atoi(pPort) != 0)
				sprintf(buf," COM%d could not be opened \r\n ", atoi(pPort));
			else
				sprintf(buf," %s could not be opened \r\n ", pPort);

//			WritetoConsoleLocal(buf);
			OutputDebugString(buf);
		}
		return (FALSE);
	}

	Err = GetFileType(fd);

	// setup device buffers

	SetupComm(fd, 4096, 4096 ) ;

	// purge any information in the buffer

	PurgeComm(fd, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR ) ;

	// set up for overlapped I/O

	CommTimeOuts.ReadIntervalTimeout = 0xFFFFFFFF ;
	CommTimeOuts.ReadTotalTimeoutMultiplier = 0 ;
	CommTimeOuts.ReadTotalTimeoutConstant = 0 ;
	CommTimeOuts.WriteTotalTimeoutMultiplier = 0 ;
//	CommTimeOuts.WriteTotalTimeoutConstant = 0 ;
	CommTimeOuts.WriteTotalTimeoutConstant = 500 ;
	SetCommTimeouts(fd, &CommTimeOuts ) ;

	dcb.DCBlength = sizeof( DCB ) ;

	GetCommState(fd, &dcb ) ;

	dcb.BaudRate = speed;
	dcb.ByteSize = 8;
	dcb.Parity = 0;
	dcb.StopBits = TWOSTOPBITS;
	dcb.StopBits = Stopbits;

	// setup hardware flow control

	dcb.fOutxDsrFlow = 0;
	dcb.fDtrControl = DTR_CONTROL_DISABLE ;

	dcb.fOutxCtsFlow = 0;
	dcb.fRtsControl = RTS_CONTROL_DISABLE ;

	// setup software flow control

	dcb.fInX = dcb.fOutX = 0;
	dcb.XonChar = 0;
	dcb.XoffChar = 0;
	dcb.XonLim = 100 ;
	dcb.XoffLim = 100 ;

	// other various settings

	dcb.fBinary = TRUE ;
	dcb.fParity = FALSE;

	fRetVal = SetCommState(fd, &dcb);

	if (fRetVal)
	{
		if (SetDTR)
			EscapeCommFunction(fd, SETDTR);
		if (SetRTS)
			EscapeCommFunction(fd, SETRTS);
	}
	else
	{
		if (atoi(pPort) != 0)
			sprintf(buf,"COM%d Setup Failed %d ", atoi(pPort), GetLastError());
		else
			sprintf(buf,"%s Setup Failed %d ", pPort, GetLastError());

		printf(buf);
		OutputDebugString(buf);
		CloseHandle(fd);
		return 0;
	}

	return fd;

}

int ReadCOMBlock(HANDLE fd, char * Block, int MaxLength )
{
	BOOL       fReadStat ;
	COMSTAT    ComStat ;
	DWORD      dwErrorFlags;
	DWORD      dwLength;

	// only try to read number of bytes in queue

	ClearCommError(fd, &dwErrorFlags, &ComStat);

	dwLength = min((DWORD) MaxLength, ComStat.cbInQue);

	if (dwLength > 0)
	{
		fReadStat = ReadFile(fd, Block, dwLength, &dwLength, NULL) ;

		if (!fReadStat)
		{
			dwLength = 0 ;
			ClearCommError(fd, &dwErrorFlags, &ComStat ) ;
		}
	}

	return dwLength;
}

BOOL WriteCOMBlock(HANDLE fd, char * Block, int BytesToWrite)
{
	BOOL        fWriteStat;
	DWORD       BytesWritten;
	DWORD       ErrorFlags;
	COMSTAT     ComStat;

	fWriteStat = WriteFile(fd, Block, BytesToWrite, &BytesWritten, NULL );

	if ((!fWriteStat) || (BytesToWrite != BytesWritten))
	{
		int Err = GetLastError();
		ClearCommError(fd, &ErrorFlags, &ComStat);
		return FALSE;
	}
	return TRUE;
}



VOID CloseCOMPort(HANDLE fd)
{
	SetCommMask(fd, 0);

	// drop DTR

	COMClearDTR(fd);

	// purge any outstanding reads/writes and close device handle

	PurgeComm(fd, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR ) ;

	CloseHandle(fd);
}


VOID COMSetDTR(HANDLE fd)
{
	EscapeCommFunction(fd, SETDTR);
}

VOID COMClearDTR(HANDLE fd)
{
	EscapeCommFunction(fd, CLRDTR);
}

VOID COMSetRTS(HANDLE fd)
{
	EscapeCommFunction(fd, SETRTS);
}

VOID COMClearRTS(HANDLE fd)
{
	EscapeCommFunction(fd, CLRRTS);
}

void CatWrite(char * Buffer, int Len)
{
	if (hCATDevice)
		WriteCOMBlock(hCATDevice, Buffer, Len);
}


UCHAR Pixels[16384];
UCHAR * pixelPointer = Pixels;


void mySetPixel(unsigned char x, unsigned char y, unsigned int Colour)
{
	// Used on Windows for constellation. Save points and send to GUI at end

	*(pixelPointer++) = x;
	*(pixelPointer++) = y;
	*(pixelPointer++) = Colour;

	if ((pixelPointer - Pixels) > 16000)
		pixelPointer -= 3;

}
void clearDisplay()
{
	// Reset pixel pointer

	pixelPointer = Pixels;

}
void updateDisplay()
{
//	 SendtoGUI('C', Pixels, pixelPointer - Pixels);
}
void DrawAxes(int Qual, char * Mode)
{
	UCHAR Msg[80];
	SendtoGUI('C', Pixels, pixelPointer - Pixels);
	wg_send_pixels(0, Pixels, pixelPointer - Pixels);
	pixelPointer = Pixels;

	sprintf(Msg, "%s Quality: %d", Mode, Qual);
	SendtoGUI('Q', Msg, strlen(Msg) + 1);
}
void DrawDecode(char * Decode)
{}

