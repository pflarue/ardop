//
// Audio interface Routine for macOS
//
// This is CoreAudioSound.c for macOS using Core Audio
// Based on ALSASound.c (Linux) and Waveout.c (Windows)
//

#include <CoreAudio/CoreAudio.h>
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDDevice.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <CoreFoundation/CoreFoundation.h>

// For iOS/macOS RemoteIO constants - fallback definitions if not available
#ifndef kAudioUnitSubType_RemoteIO
#define kAudioUnitSubType_RemoteIO kAudioUnitSubType_HALOutput
#endif

// MIN macro for IOKit compatibility
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>

#define HANDLE int

#include "common/ardopcommon.h"
#include "common/wav.h"
#include "common/log.h"
#include "rockliff/rrs.h"

// Core Audio variables
static AudioUnit gAudioUnit = NULL;
static bool gAudioInitialized = false;
static pthread_mutex_t gAudioMutex = PTHREAD_MUTEX_INITIALIZER;

// External variables
extern BOOL WriteTxWav;
extern int port;
extern char * CM108Device;
extern void DecodeCM108(char * ptr);
extern void CM108_set_ptt(int PTTState);
extern int PTTMode;
extern char PTTPort[80];

// WAV file for writing transmitted audio
struct WavFile *txwff = NULL;

// Audio buffer management
#define SAMPLE_RATE 12000
#define BUFFER_SIZE 1200 // 100ms at 12kHz
#define NUM_BUFFERS 4

static short gInputBuffer[NUM_BUFFERS][BUFFER_SIZE];
static short gOutputBuffer[NUM_BUFFERS][BUFFER_SIZE];
static int gInputWriteIndex = 0;
static int gInputReadIndex = 0;
static int gOutputWriteIndex = 0;
static int gOutputReadIndex = 0;
static int gInputSamplesAvailable = 0;
static int gOutputSamplesQueued = 0;

// Audio level variable
UCHAR CurrentLevel = 0;

// Audio system variables
float InputNoiseStdDev = 1.0;

// DMA-style buffer for transmission - used by the modulation code
static short gDMABuffer[BUFFER_SIZE];

// Platform and timing variables
unsigned int PKTLEDTimer = 0;

// Forward declarations from common code
int _memicmp(unsigned char *a, unsigned char *b, int n);
int stricmp(const unsigned char *pStr1, const unsigned char *pStr2);
VOID processargs(int argc, char *argv[]);
void ProcessNewSamples(short *Samples, int nSamples);
void ardopmain();

// Forward declarations for local functions
static OSStatus InputCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
															const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
															UInt32 inNumberFrames, AudioBufferList *ioData);
static OSStatus OutputCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
															 const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
															 UInt32 inNumberFrames, AudioBufferList *ioData);

// Implementation of ARDOP_Main - calls the common ardopmain function
void ARDOP_Main()
{
	ardopmain();
}

int CloseSoundCard() { return 0; }

int PackSamplesAndSend(short *input, int nSamples)
{
	// Convert and send audio samples to Core Audio output
	// This is called from SampleSink when DMA buffer is full

	ZF_LOGD("PackSamplesAndSend: Called with %d samples", nSamples);

	if (strcmp(PlaybackDevice, "NOSOUND") == 0 || !gAudioInitialized)
	{
		ZF_LOGD("PackSamplesAndSend: NOSOUND mode or audio not initialized");
		return nSamples; // Pretend success in NOSOUND mode
	}

	pthread_mutex_lock(&gAudioMutex);

	int samplesQueued = 0;
	// Queue samples for output (similar to SendtoCard but more direct)
	for (int i = 0; i < nSamples; i++)
	{
		if (gOutputSamplesQueued < BUFFER_SIZE * NUM_BUFFERS)
		{
			gOutputBuffer[gOutputWriteIndex / BUFFER_SIZE][gOutputWriteIndex % BUFFER_SIZE] = input[i];
			gOutputWriteIndex = (gOutputWriteIndex + 1) % (BUFFER_SIZE * NUM_BUFFERS);
			gOutputSamplesQueued++;
			samplesQueued++;
		}
		else
		{
			// Buffer full - this shouldn't happen often but can in burst transmissions
			ZF_LOGW("PackSamplesAndSend: Output buffer full, dropping samples");
			break;
		}
	}

	pthread_mutex_unlock(&gAudioMutex);

	ZF_LOGD("PackSamplesAndSend: Queued %d samples, total queued: %d", samplesQueued, gOutputSamplesQueued);
	return nSamples;
}
VOID RadioPTT(int PTTState) {
	// Handle CM108 PTT control
	if (PTTMode & PTTCM108)
		CM108_set_ptt(PTTState);
}
VOID SerialHostPoll() {}

// Display/GUI function stubs - these will be no-ops for now
void DrawAxes(int Qual, char *Mode) {}
void DrawDecode(char *Decode) {}
void DrawRXFrame(int State, const char *Frame) {}
void DrawTXFrame(const char *Frame) {}
void DrawTXMode(char *Mode) {}
void clearDisplay() {}
void mySetPixel(unsigned char x, unsigned char y, unsigned int Colour) {}
void updateDisplay() {}

// Audio system functions
BOOL InitSound()
{
	ZF_LOGD("InitSound(): Called - PlaybackDevice='%s'", PlaybackDevice);

	if (strcmp(PlaybackDevice, "NOSOUND") == 0)
	{
		ZF_LOGI("InitSound(): NOSOUND mode enabled");
		return TRUE;
	}

	if (gAudioInitialized)
	{
		ZF_LOGD("InitSound(): Already initialized");
		return TRUE;
	}

	ZF_LOGD("InitSound(): Initializing Core Audio system");

	OSStatus status;

	// Create DefaultOutput audio unit description for macOS
	AudioComponentDescription desc;
	desc.componentType = kAudioUnitType_Output;
	desc.componentSubType = kAudioUnitSubType_DefaultOutput;
	desc.componentManufacturer = kAudioUnitManufacturer_Apple;
	desc.componentFlags = 0;
	desc.componentFlagsMask = 0;

	AudioComponent component = AudioComponentFindNext(NULL, &desc);
	if (!component)
	{
		ZF_LOGE("InitSound(): No RemoteIO component found");
		return FALSE;
	}

	status = AudioComponentInstanceNew(component, &gAudioUnit);
	if (status != noErr)
	{
		ZF_LOGE("InitSound(): Failed to create audio unit: %d", (int)status);
		return FALSE;
	}

	// Set up audio format (16-bit mono at 12kHz)
	AudioStreamBasicDescription format;
	memset(&format, 0, sizeof(format));
	format.mSampleRate = SAMPLE_RATE;
	format.mFormatID = kAudioFormatLinearPCM;
	format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
	format.mBytesPerPacket = sizeof(short);
	format.mFramesPerPacket = 1;
	format.mBytesPerFrame = sizeof(short);
	format.mChannelsPerFrame = 1;
	format.mBitsPerChannel = 16;

	// Set format for output (bus 0 input scope - from callback to device)
	// DefaultOutput only supports output, no input configuration needed
	status = AudioUnitSetProperty(gAudioUnit, kAudioUnitProperty_StreamFormat,
																kAudioUnitScope_Input, 0, &format, sizeof(format));
	if (status != noErr)
	{
		ZF_LOGE("InitSound(): Failed to set output format: %d", (int)status);
		AudioComponentInstanceDispose(gAudioUnit);
		gAudioUnit = NULL;
		return FALSE;
	}

	// Set output callback (DefaultOutput doesn't support input callbacks)
	AURenderCallbackStruct outputCallback;
	outputCallback.inputProc = OutputCallback;
	outputCallback.inputProcRefCon = NULL;

	status = AudioUnitSetProperty(gAudioUnit, kAudioUnitProperty_SetRenderCallback,
																kAudioUnitScope_Input, 0, &outputCallback, sizeof(outputCallback));
	if (status != noErr)
	{
		ZF_LOGE("InitSound(): Failed to set output callback: %d", (int)status);
		AudioComponentInstanceDispose(gAudioUnit);
		gAudioUnit = NULL;
		return FALSE;
	}

	// Initialize the audio unit
	status = AudioUnitInitialize(gAudioUnit);
	if (status != noErr)
	{
		ZF_LOGE("InitSound(): Failed to initialize audio unit: %d", (int)status);
		AudioComponentInstanceDispose(gAudioUnit);
		gAudioUnit = NULL;
		return FALSE;
	}

	// Start audio processing
	ZF_LOGD("InitSound(): Starting Core Audio unit...");
	status = AudioOutputUnitStart(gAudioUnit);
	if (status != noErr)
	{
		ZF_LOGE("InitSound(): Failed to start audio unit: %d", (int)status);
		AudioUnitUninitialize(gAudioUnit);
		AudioComponentInstanceDispose(gAudioUnit);
		gAudioUnit = NULL;
		return FALSE;
	}

	gAudioInitialized = true;
	ZF_LOGI("InitSound(): SUCCESS - Core Audio initialized and started successfully");
	ZF_LOGD("InitSound(): gAudioInitialized = %s", gAudioInitialized ? "true" : "false");
	return TRUE;
}

// Platform-specific functions
BOOL KeyPTT(BOOL State)
{
	// PTT keying is handled through serial port RTS/DTR or CM108 HID
	// This function is called when PTT state needs to change

	// RadioPTT() in common code handles the actual PTT control
	// based on configured PTTMode (PTTCI-V, PTTRTS, PTTDTR, PTTCM108)
	RadioPTT(State);

	return TRUE;
}

const char *PlatformSignalAbbreviation(int sig)
{
	// Return signal name abbreviation for logging/debugging
	switch (sig)
	{
	case SIGHUP:
		return "HUP";
	case SIGINT:
		return "INT";
	case SIGQUIT:
		return "QUIT";
	case SIGILL:
		return "ILL";
	case SIGTRAP:
		return "TRAP";
	case SIGABRT:
		return "ABRT";
	case SIGEMT:
		return "EMT";
	case SIGFPE:
		return "FPE";
	case SIGKILL:
		return "KILL";
	case SIGBUS:
		return "BUS";
	case SIGSEGV:
		return "SEGV";
	case SIGSYS:
		return "SYS";
	case SIGPIPE:
		return "PIPE";
	case SIGALRM:
		return "ALRM";
	case SIGTERM:
		return "TERM";
	case SIGURG:
		return "URG";
	case SIGSTOP:
		return "STOP";
	case SIGTSTP:
		return "TSTP";
	case SIGCONT:
		return "CONT";
	case SIGCHLD:
		return "CHLD";
	case SIGTTIN:
		return "TTIN";
	case SIGTTOU:
		return "TTOU";
	case SIGIO:
		return "IO";
	case SIGXCPU:
		return "XCPU";
	case SIGXFSZ:
		return "XFSZ";
	case SIGVTALRM:
		return "VTALRM";
	case SIGPROF:
		return "PROF";
	case SIGWINCH:
		return "WINCH";
	case SIGINFO:
		return "INFO";
	case SIGUSR1:
		return "USR1";
	case SIGUSR2:
		return "USR2";
	default:
		return "UNKNOWN";
	}
}

// Utility functions
void printtick(char *msg)
{
	// Debug function - print timestamp with message
	ZF_LOGI("[%u] %s", (unsigned int)time(NULL), msg);
}

void PlatformSleep(int ms)
{
	usleep(ms * 1000); // Convert ms to microseconds
}

// Timing functions
unsigned int getTicks()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000; // Return milliseconds
}

void txSleep(int ms)
{
	PlatformSleep(ms);
}

// Audio I/O functions
void PollReceivedSamples()
{
	if (strcmp(PlaybackDevice, "NOSOUND") == 0 || !gAudioInitialized)
	{
		return;
	}

	pthread_mutex_lock(&gAudioMutex);

	// Process available input samples in chunks
	while (gInputSamplesAvailable >= BUFFER_SIZE)
	{
		short tempBuffer[BUFFER_SIZE];

		// Copy a buffer's worth of samples
		for (int i = 0; i < BUFFER_SIZE; i++)
		{
			tempBuffer[i] = gInputBuffer[gInputReadIndex / BUFFER_SIZE][gInputReadIndex % BUFFER_SIZE];
			gInputReadIndex = (gInputReadIndex + 1) % (BUFFER_SIZE * NUM_BUFFERS);
			gInputSamplesAvailable--;
		}

		pthread_mutex_unlock(&gAudioMutex);

		// Process samples (this calls the common ARDOP processing code)
		ProcessNewSamples(tempBuffer, BUFFER_SIZE);

		// Update level indicator
		short max = 0, min = 0;
		for (int i = 0; i < BUFFER_SIZE; i++)
		{
			if (tempBuffer[i] > max)
				max = tempBuffer[i];
			if (tempBuffer[i] < min)
				min = tempBuffer[i];
		}
		CurrentLevel = ((max - min) * 75) / 32768; // Scale to 0-150

		pthread_mutex_lock(&gAudioMutex);
	}

	pthread_mutex_unlock(&gAudioMutex);
}

unsigned short *SendtoCard(unsigned short *samples, int nSamples)
{
	ZF_LOGD("SendtoCard: Called with %d samples", nSamples);
	ZF_LOGD("SendtoCard: PlaybackDevice='%s', gAudioInitialized=%s",
				 PlaybackDevice, gAudioInitialized ? "true" : "false");

	if (strcmp(PlaybackDevice, "NOSOUND") == 0 || !gAudioInitialized)
	{
		ZF_LOGD("SendtoCard: NOSOUND mode or audio not initialized - returning early");
		// Even in NOSOUND mode, we still need to write to WAV file if enabled
		if (txwff != NULL)
			WriteWav((short *)samples, nSamples, txwff);
		// Return the DMA buffer for next use
		return (unsigned short *)gDMABuffer;
	}

	// Implement proper flow control like Linux/Windows - wait for buffer space instead of dropping
	// This mimics the blocking behavior of ALSA and WaveOut implementations

	int retryCount = 0;
	const int maxRetries = 100; // Maximum 100ms wait (100 * 1ms sleeps)

	while (retryCount < maxRetries)
	{
		pthread_mutex_lock(&gAudioMutex);

		// Check if we have space for all samples (leave 20% headroom like ALSA does)
		int availableSpace = (BUFFER_SIZE * NUM_BUFFERS) - gOutputSamplesQueued;
		int spaceThreshold = (BUFFER_SIZE * NUM_BUFFERS) * 0.8; // 80% threshold

		if (gOutputSamplesQueued <= spaceThreshold)
		{
			// We have space - queue all samples
			int samplesQueued = 0;
			for (int i = 0; i < nSamples; i++)
			{
				if (gOutputSamplesQueued < BUFFER_SIZE * NUM_BUFFERS)
				{
					gOutputBuffer[gOutputWriteIndex / BUFFER_SIZE][gOutputWriteIndex % BUFFER_SIZE] = (short)samples[i];
					gOutputWriteIndex = (gOutputWriteIndex + 1) % (BUFFER_SIZE * NUM_BUFFERS);
					gOutputSamplesQueued++;
					samplesQueued++;
				}
				else
				{
					break; // Should not happen with our threshold check above
				}
			}

			ZF_LOGD("SendtoCard: Successfully queued %d samples, total queued: %d", samplesQueued, gOutputSamplesQueued);
			pthread_mutex_unlock(&gAudioMutex);
			break; // Success - exit retry loop
		}
		else
		{
			// Buffer too full - wait like Linux/Windows do
			pthread_mutex_unlock(&gAudioMutex);
			ZF_LOGD("SendtoCard: Buffer %d%% full, waiting for space (retry %d/%d)",
						 (gOutputSamplesQueued * 100) / (BUFFER_SIZE * NUM_BUFFERS), retryCount + 1, maxRetries);
			usleep(1000); // Wait 1ms like Linux txSleep
			retryCount++;
		}
	}

	if (retryCount >= maxRetries)
	{
		ZF_LOGW("SendtoCard: WARNING - Timeout waiting for buffer space, may drop samples");
		// As last resort, try to queue what we can without waiting
		pthread_mutex_lock(&gAudioMutex);
		int samplesQueued = 0;
		for (int i = 0; i < nSamples; i++)
		{
			if (gOutputSamplesQueued < BUFFER_SIZE * NUM_BUFFERS)
			{
				gOutputBuffer[gOutputWriteIndex / BUFFER_SIZE][gOutputWriteIndex % BUFFER_SIZE] = samples[i];
				gOutputWriteIndex = (gOutputWriteIndex + 1) % (BUFFER_SIZE * NUM_BUFFERS);
				gOutputSamplesQueued++;
				samplesQueued++;
			}
			else
			{
				break;
			}
		}
		ZF_LOGD("SendtoCard: Queued %d/%d samples after timeout", samplesQueued, nSamples);
		pthread_mutex_unlock(&gAudioMutex);
	}

	// Log final state
	pthread_mutex_lock(&gAudioMutex);
	ZF_LOGD("SendtoCard: Final state - total queued: %d samples", gOutputSamplesQueued);
	pthread_mutex_unlock(&gAudioMutex);

	// Write transmitted audio to WAV file if enabled (like Linux/Windows)
	if (txwff != NULL)
		WriteWav((short *)samples, nSamples, txwff);

	// Return the DMA buffer for next use (like Linux/Windows)
	return (unsigned short *)gDMABuffer;
}

// LED/Status functions
void SetLED(int LED, BOOL State)
{
	// On macOS, we don't have direct hardware LED control like on Raspberry Pi
	// This function is mainly used for status indication on embedded systems
	//
	// LED values typically used:
	// 0 = PTT LED (transmit indicator)
	// 1 = Data LED (activity indicator)
	// 2 = Status LED (connection state)
	//
	// For macOS, we could potentially:
	// - Update the UI if a GUI is implemented
	// - Write to a status file
	// - Send notifications
	// - Update the dock icon badge
	//
	// For now, we'll just log significant state changes for debugging

	static BOOL lastPTTState = FALSE;
	static BOOL lastDataState = FALSE;

	switch (LED)
	{
	case 0: // PTT LED
		if (State != lastPTTState)
		{
			lastPTTState = State;
			if (State)
				ZF_LOGD("PTT ON - Transmitting");
			else
				ZF_LOGD("PTT OFF - Receiving");
		}
		break;

	case 1: // Data LED
		if (State != lastDataState)
		{
			lastDataState = State;
			// Data activity is too frequent to log every change
		}
		break;

	case 2: // Status LED
		// Connection status changes are already logged elsewhere
		break;
	}
}

// Forward declaration for AddTrailer from common code
void AddTrailer();

// More audio functions
void SoundFlush()
{
	// Always close WAV files, even if audio isn't initialized (NOSOUND mode)
	if (!gAudioInitialized)
	{
		ZF_LOGD("SoundFlush: Audio not initialized, but closing WAV files if open");
		if (txwff != NULL)
		{
			CloseWav(txwff);
			txwff = NULL;
		}
		return;
	}

	// Add trailer to complete transmission (like Linux implementation)
	AddTrailer();

	// Let the audio system finish playing any queued samples
	// On Core Audio, we need to wait for the output buffer to drain
	pthread_mutex_lock(&gAudioMutex);

	// Calculate approximate time to wait based on queued samples
	int samplesToWait = gOutputSamplesQueued;

	pthread_mutex_unlock(&gAudioMutex);

	if (samplesToWait > 0)
	{
		// Wait for samples to play out (12000 samples per second = 12 samples per ms)
		int waitTimeMs = (samplesToWait / 12) + 20; // Add 20ms TXTAIL like Linux

		ZF_LOGD("SoundFlush: Waiting %d ms for %d samples to complete", waitTimeMs, samplesToWait);

		// Wait for transmission to complete
		int elapsed = 0;
		while (elapsed < waitTimeMs)
		{
			txSleep(10);
			elapsed += 10;

			// Check if buffer has drained
			pthread_mutex_lock(&gAudioMutex);
			if (gOutputSamplesQueued == 0)
			{
				pthread_mutex_unlock(&gAudioMutex);
				break;
			}
			pthread_mutex_unlock(&gAudioMutex);
		}
	}

	// Close WAV file if open (like Linux/Windows)
	if (txwff != NULL)
	{
		CloseWav(txwff);
		txwff = NULL;
	}

	ZF_LOGD("SoundFlush: Transmission completed");
}

unsigned short *SoundInit()
{
	// Initialize buffer indices
	pthread_mutex_lock(&gAudioMutex);

	gInputWriteIndex = 0;
	gInputReadIndex = 0;
	gInputSamplesAvailable = 0;
	gOutputWriteIndex = 0;
	gOutputReadIndex = 0;
	gOutputSamplesQueued = 0;

	// Clear buffers
	memset(gInputBuffer, 0, sizeof(gInputBuffer));
	memset(gOutputBuffer, 0, sizeof(gOutputBuffer));
	memset(gDMABuffer, 0, sizeof(gDMABuffer));

	pthread_mutex_unlock(&gAudioMutex);

	// Return the DMA buffer for modulation code to use
	return (unsigned short *)gDMABuffer;
}

void StartTxWav()
{
	// Open a new WAV file for filtered Tx audio.
	//
	// WAV files will use a filename that includes port, UTC date,
	// and UTC time, similar to log files but with added time to
	// the nearest second. Like log files, these WAV files will be
	// written to the log directory if defined, else to the current
	// directory
	char txwff_pathname[1024];
	int pnflen;

	if (txwff != NULL)
	{
		ZF_LOGW("WARNING: Trying to open Tx WAV file, but already open.");
		return;
	}

	struct tm *tm;
	time_t T;

	T = time(NULL);
	tm = gmtime(&T);

	struct timespec tp;
	int ss, hh, mm;
	clock_gettime(CLOCK_REALTIME, &tp);
	ss = tp.tv_sec % 86400; // Seconds in a day
	hh = ss / 3600;
	mm = (ss - (hh * 3600)) / 60;
	ss = ss % 60;

	if (ardop_log_get_directory()[0])
	{
		pnflen = snprintf(txwff_pathname, sizeof(txwff_pathname),
											"%s/ARDOP_txfaudio_%d_%04d%02d%02d_%02d%02d%02d.wav",
											ardop_log_get_directory(), port, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
											hh, mm, ss);
	}
	else
	{
		pnflen = snprintf(txwff_pathname, sizeof(txwff_pathname),
											"ARDOP_txfaudio_%d_%04d%02d%02d_%02d:%02d:%02d.wav",
											port, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
											hh, mm, ss);
	}
	if (pnflen == -1 || pnflen > sizeof(txwff_pathname))
	{
		// Logpath too long likely to also prevent writing to log files.
		// So, print this error directly to console instead of using
		// WriteDebugLog.
		ZF_LOGE("Unable to write WAV file, invalid pathname. Logpath may be too long.");
		WriteTxWav = FALSE;
		return;
	}
	txwff = OpenWavW(txwff_pathname);
}

void StartCapture()
{
	// Set receiving state - called when starting to listen for incoming signals
	// This is mainly for compatibility with Linux/Windows implementations
	// On macOS, Core Audio is always running when initialized
	if (gAudioInitialized)
	{
		ZF_LOGD("StartCapture: Audio capture started");
	}
}

void StopCapture()
{
	ZF_LOGD("StopCapture: Called! gAudioInitialized=%s", gAudioInitialized ? "true" : "false");

	if (!gAudioInitialized)
		return;

	// On macOS, StopCapture should NOT destroy the entire audio system
	// It should only stop audio capture, but keep the audio unit available for transmission
	ZF_LOGD("StopCapture: Stopping capture only (keeping audio system for transmission)");

	// TODO: If we implement separate input/output audio units in the future,
	// we would stop only the input unit here. For now, we keep everything running.

	// NOTE: We do NOT set gAudioInitialized = false here because that would
	// break transmission. The audio system should remain available for both
	// input and output operations.
}

// Audio channel selection variables/functions
BOOL UseLeftRX = FALSE;
BOOL UseRightRX = FALSE;
BOOL UseLeftTX = FALSE;
BOOL UseRightTX = FALSE;

// Core Audio callback functions
static OSStatus InputCallback(void *inRefCon,
															AudioUnitRenderActionFlags *ioActionFlags,
															const AudioTimeStamp *inTimeStamp,
															UInt32 inBusNumber,
															UInt32 inNumberFrames,
															AudioBufferList *ioData)
{
	// Use stack allocation for typical frame sizes to avoid malloc/free overhead
	// Maximum expected frames is typically 1024 for real-time audio
	short stackBuffer[1024];
	short *bufferData = NULL;

	// Use stack buffer if it's large enough, otherwise fall back to heap
	if (inNumberFrames <= 1024)
	{
		bufferData = stackBuffer;
	}
	else
	{
		bufferData = (short *)malloc(inNumberFrames * sizeof(short));
		if (!bufferData)
			return kAudioUnitErr_FailedInitialization;
	}

	AudioBufferList bufferList;
	bufferList.mNumberBuffers = 1;
	bufferList.mBuffers[0].mDataByteSize = inNumberFrames * sizeof(short);
	bufferList.mBuffers[0].mNumberChannels = 1;
	bufferList.mBuffers[0].mData = bufferData;

	OSStatus status = AudioUnitRender(gAudioUnit, ioActionFlags, inTimeStamp,
																		inBusNumber, inNumberFrames, &bufferList);

	if (status == noErr)
	{
		pthread_mutex_lock(&gAudioMutex);

		short *samples = (short *)bufferList.mBuffers[0].mData;

		// Copy samples to circular buffer
		for (UInt32 i = 0; i < inNumberFrames && gInputSamplesAvailable < BUFFER_SIZE * NUM_BUFFERS; i++)
		{
			gInputBuffer[gInputWriteIndex / BUFFER_SIZE][gInputWriteIndex % BUFFER_SIZE] = samples[i];
			gInputWriteIndex = (gInputWriteIndex + 1) % (BUFFER_SIZE * NUM_BUFFERS);
			gInputSamplesAvailable++;
		}

		pthread_mutex_unlock(&gAudioMutex);
	}

	// Only free if we allocated from heap
	if (inNumberFrames > 1024 && bufferData)
		free(bufferData);

	return status;
}

static OSStatus OutputCallback(void *inRefCon,
															 AudioUnitRenderActionFlags *ioActionFlags,
															 const AudioTimeStamp *inTimeStamp,
															 UInt32 inBusNumber,
															 UInt32 inNumberFrames,
															 AudioBufferList *ioData)
{
	pthread_mutex_lock(&gAudioMutex);

	short *outputBuffer = (short *)ioData->mBuffers[0].mData;
	UInt32 samplesToWrite = inNumberFrames;
	static int callbackCount = 0;
	int samplesWritten = 0;

	// Fill output buffer from queued samples
	for (UInt32 i = 0; i < samplesToWrite; i++)
	{
		if (gOutputSamplesQueued > 0)
		{
			outputBuffer[i] = gOutputBuffer[gOutputReadIndex / BUFFER_SIZE][gOutputReadIndex % BUFFER_SIZE];
			gOutputReadIndex = (gOutputReadIndex + 1) % (BUFFER_SIZE * NUM_BUFFERS);
			gOutputSamplesQueued--;
			samplesWritten++;
		}
		else
		{
			outputBuffer[i] = 0; // Silence if no data
		}
	}

	// Log more frequently for first few callbacks and when we have audio data
	if ((callbackCount < 10) || (samplesWritten > 0 && (callbackCount % 50 == 0)))
	{
		ZF_LOGD("OutputCallback: Frame %d, wrote %d samples, %d queued, requested %d", callbackCount, samplesWritten, gOutputSamplesQueued, samplesToWrite);
	}
	callbackCount++;

	pthread_mutex_unlock(&gAudioMutex);
	return noErr;
}

// HID implementation for macOS using IOKit
// This provides hidapi-compatible functions for CM108 PTT control
// Note: This is a macOS-specific implementation to avoid external dependencies

// HID device structure for macOS
typedef struct
{
	IOHIDDeviceRef device;
	CFRunLoopRef runLoop;
	bool isOpen;
} macos_hid_device;

// Global HID manager
static IOHIDManagerRef gHIDManager = NULL;

// Initialize IOKit HID system
static void init_hid_system()
{
	if (gHIDManager == NULL)
	{
		gHIDManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
		if (gHIDManager)
		{
			// Set matching criteria for all HID devices
			IOHIDManagerSetDeviceMatching(gHIDManager, NULL);
			IOHIDManagerScheduleWithRunLoop(gHIDManager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
			IOReturn result = IOHIDManagerOpen(gHIDManager, kIOHIDOptionsTypeNone);
			if (result != kIOReturnSuccess)
			{
				ZF_LOGE("Failed to open HID Manager: %d", result);
			}
		}
	}
}

// Read data from HID device with timeout
int hid_read_timeout(void *dev, unsigned char *data, size_t length, int milliseconds)
{
	if (!dev || !data)
		return -1;

	macos_hid_device *hid_dev = (macos_hid_device *)dev;
	if (!hid_dev->isOpen || !hid_dev->device)
		return -1;

	// IOKit HID read implementation
	// Note: This is a simplified implementation for CM108 devices
	// Real implementation would need proper report handling

	CFIndex reportLength = (CFIndex)length;
	uint8_t report[64]; // CM108 typically uses 64-byte reports

	IOReturn result = IOHIDDeviceGetReport(hid_dev->device,
																				 kIOHIDReportTypeInput,
																				 0, // Report ID
																				 report,
																				 &reportLength);

	if (result == kIOReturnSuccess && reportLength > 0)
	{
		memcpy(data, report, MIN(length, reportLength));
		return (int)reportLength;
	}

	return 0; // No data available or error
}

// Write data to HID device
int hid_write(void *dev, const unsigned char *data, size_t length)
{
	if (!dev || !data)
		return -1;

	macos_hid_device *hid_dev = (macos_hid_device *)dev;
	if (!hid_dev->isOpen || !hid_dev->device)
		return -1;

	// IOKit HID write implementation
	IOReturn result = IOHIDDeviceSetReport(hid_dev->device,
																				 kIOHIDReportTypeOutput,
																				 0, // Report ID
																				 data,
																				 length);

	if (result == kIOReturnSuccess)
	{
		return (int)length;
	}

	ZF_LOGE("HID write failed: %d", result);
	return -1;
}

// Close HID device
void hid_close(void *dev)
{
	if (!dev)
		return;

	macos_hid_device *hid_dev = (macos_hid_device *)dev;

	if (hid_dev->device && hid_dev->isOpen)
	{
		IOHIDDeviceClose(hid_dev->device, kIOHIDOptionsTypeNone);
		hid_dev->isOpen = false;
	}

	if (hid_dev->device)
	{
		CFRelease(hid_dev->device);
		hid_dev->device = NULL;
	}

	free(hid_dev);
}

// Open HID device by path (for CM108 compatibility)
void *hid_open_path(const char *path)
{
	if (!path)
		return NULL;

	init_hid_system();
	if (!gHIDManager)
		return NULL;

	// For CM108 devices, we need to find the device by VID/PID from the path
	// Path format expected: "0d8c:0008" or similar VID:PID format

	CFSetRef deviceSet = IOHIDManagerCopyDevices(gHIDManager);
	if (!deviceSet)
		return NULL;

	CFIndex deviceCount = CFSetGetCount(deviceSet);
	IOHIDDeviceRef *devices = malloc(deviceCount * sizeof(IOHIDDeviceRef));
	CFSetGetValues(deviceSet, (const void **)devices);

	IOHIDDeviceRef targetDevice = NULL;

	// Parse VID:PID from path if provided in that format
	int vid = 0, pid = 0;
	if (sscanf(path, "%x:%x", &vid, &pid) == 2)
	{
		// Search for matching device by VID:PID
		for (CFIndex i = 0; i < deviceCount; i++)
		{
			IOHIDDeviceRef device = devices[i];

			// Get device properties
			CFNumberRef vendorID = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey));
			CFNumberRef productID = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey));

			if (vendorID && productID)
			{
				int device_vid, device_pid;
				CFNumberGetValue(vendorID, kCFNumberIntType, &device_vid);
				CFNumberGetValue(productID, kCFNumberIntType, &device_pid);

				// Check if this matches our target VID:PID
				if (device_vid == vid && device_pid == pid)
				{
					targetDevice = device;
					CFRetain(targetDevice);
					break;
				}
			}
		}
	}
	else
	{
		// Also support common CM108 devices by checking known VID:PID combinations
		for (CFIndex i = 0; i < deviceCount; i++)
		{
			IOHIDDeviceRef device = devices[i];

			// Get device properties
			CFNumberRef vendorID = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey));
			CFNumberRef productID = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey));

			if (vendorID && productID)
			{
				int device_vid, device_pid;
				CFNumberGetValue(vendorID, kCFNumberIntType, &device_vid);
				CFNumberGetValue(productID, kCFNumberIntType, &device_pid);

				// Check for common CM108 VID/PID combinations
				if ((device_vid == 0x0d8c && device_pid == 0x0008) || // Original CM108
						(device_vid == 0x0d8c && device_pid == 0x000c))   // CM119 variant
				{
					targetDevice = device;
					CFRetain(targetDevice);
					break;
				}
			}
		}
	}

	free(devices);
	CFRelease(deviceSet);

	if (!targetDevice)
	{
		ZF_LOGE("CM108 device not found: %s", path);
		return NULL;
	}

	// Open the device
	IOReturn result = IOHIDDeviceOpen(targetDevice, kIOHIDOptionsTypeNone);
	if (result != kIOReturnSuccess)
	{
		ZF_LOGE("Failed to open HID device: %d", result);
		CFRelease(targetDevice);
		return NULL;
	}

	// Create our device structure
	macos_hid_device *hid_dev = malloc(sizeof(macos_hid_device));
	hid_dev->device = targetDevice;
	hid_dev->runLoop = CFRunLoopGetCurrent();
	hid_dev->isOpen = true;

	ZF_LOGI("CM108 HID device opened successfully");
	return hid_dev;
}

// Set non-blocking mode (compatibility function)
void hid_set_nonblocking(void *dev, int nonblock)
{
	// IOKit HID is inherently non-blocking for most operations
	// This is mainly for compatibility with the existing hidapi interface
	if (dev)
	{
		macos_hid_device *hid_dev = (macos_hid_device *)dev;
		// Non-blocking mode is default behavior in IOKit
		(void)nonblock; // Suppress unused parameter warning
	}
}

// Get error string (compatibility function)
const wchar_t *hid_error(void *dev)
{
	// Return a generic error message for IOKit
	static const wchar_t error_msg[] = L"IOKit HID Error";
	return error_msg;
}

// Global variables
char *PortString = NULL;

// Audio device variables - following Linux pattern
char CaptureDevice[80] = "Built-in";
char PlaybackDevice[80] = "Built-in";
char *CaptureDevices = CaptureDevice;
char *PlaybackDevices = PlaybackDevice;

int platform_main(int argc, char *argv[])
{
	ZF_LOGD("platform_main: ENTRY with argc=%d", argc);

	// Initialize Reed-Solomon codec
	int rslen_set[] = {2, 4, 8, 16, 32, 36, 50, 64};
	init_rs(rslen_set, 8);

	// Build command string for logging
	char cmdstr[3000] = "";
	for (int i = 0; i < argc; i++)
	{
		if ((int)(sizeof(cmdstr) - strlen(cmdstr)) <= snprintf(
																											cmdstr + strlen(cmdstr),
																											sizeof(cmdstr) - strlen(cmdstr),
																											"%s ",
																											argv[i]))
		{
			ZF_LOGE("ERROR: cmdstr[%ld] insufficient to hold full command string for logging.", sizeof(cmdstr));
			break;
		}
	}

	// Process command line arguments
	ZF_LOGD("platform_main: About to call processargs()");
	processargs(argc, argv);
	ZF_LOGD("platform_main: processargs() completed");

	// Check if decoding WAV file(s) - if so, skip audio initialization
	extern char DecodeWav[5][256];
	if (DecodeWav[0][0])
	{
		ZF_LOGD("platform_main: DecodeWav mode detected, calling decode_wav() directly");
		decode_wav();
		return 0;
	}

	// Check for CM108 PTT device
	if (PTTPort[0])
	{
		// CM108 devices are specified as VID:PID (e.g., 0x0d8c:0x0008) or full device path
		if (_memicmp(PTTPort, "0x", 2) == 0 || strstr(PTTPort, ":"))
		{
			// CM108 device - decode VID:PID format
			DecodeCM108(PTTPort);
		}
	}

	ZF_LOGD("platform_main: About to print starting message");
	ZF_LOGI("ARDOP macOS version starting...");
	ZF_LOGI("Command line: %s", cmdstr);

	// Call main ARDOP loop
	ZF_LOGD("platform_main: About to call ARDOP_Main()");
	ARDOP_Main();
	ZF_LOGD("platform_main: ARDOP_Main() returned");

	// Ensure WAV files are properly closed on program exit
	ZF_LOGD("platform_main: Calling SoundFlush() to close any open WAV files");
	SoundFlush();

	return 0;
}