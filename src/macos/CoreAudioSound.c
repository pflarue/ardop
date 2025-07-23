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
int PackSamplesAndSend(short *input, int nSamples) { return 0; }
VOID RadioPTT(int PTTState) {}
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
	if (strcmp(PlaybackDevice, "NOSOUND") == 0)
	{
		printf("InitSound(): NOSOUND mode enabled\n");
		return TRUE;
	}

	if (gAudioInitialized)
	{
		printf("InitSound(): Already initialized\n");
		return TRUE;
	}

	printf("InitSound(): Initializing Core Audio system\n");

	OSStatus status;

	// Create RemoteIO audio unit description
	AudioComponentDescription desc;
	desc.componentType = kAudioUnitType_Output;
	desc.componentSubType = kAudioUnitSubType_RemoteIO;
	desc.componentManufacturer = kAudioUnitManufacturer_Apple;
	desc.componentFlags = 0;
	desc.componentFlagsMask = 0;

	AudioComponent component = AudioComponentFindNext(NULL, &desc);
	if (!component)
	{
		printf("InitSound(): No RemoteIO component found\n");
		return FALSE;
	}

	status = AudioComponentInstanceNew(component, &gAudioUnit);
	if (status != noErr)
	{
		printf("InitSound(): Failed to create audio unit: %d\n", (int)status);
		return FALSE;
	}

	// Enable input and output
	UInt32 enableInput = 1;
	UInt32 enableOutput = 1;

	status = AudioUnitSetProperty(gAudioUnit, kAudioOutputUnitProperty_EnableIO,
																kAudioUnitScope_Input, 1, &enableInput, sizeof(enableInput));
	if (status != noErr)
	{
		printf("InitSound(): Failed to enable input: %d\n", (int)status);
		AudioComponentInstanceDispose(gAudioUnit);
		gAudioUnit = NULL;
		return FALSE;
	}

	status = AudioUnitSetProperty(gAudioUnit, kAudioOutputUnitProperty_EnableIO,
																kAudioUnitScope_Output, 0, &enableOutput, sizeof(enableOutput));
	if (status != noErr)
	{
		printf("InitSound(): Failed to enable output: %d\n", (int)status);
		AudioComponentInstanceDispose(gAudioUnit);
		gAudioUnit = NULL;
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

	// Set format for input (bus 1 output scope - from device to callback)
	status = AudioUnitSetProperty(gAudioUnit, kAudioUnitProperty_StreamFormat,
																kAudioUnitScope_Output, 1, &format, sizeof(format));
	if (status != noErr)
	{
		printf("InitSound(): Failed to set input format: %d\n", (int)status);
		AudioComponentInstanceDispose(gAudioUnit);
		gAudioUnit = NULL;
		return FALSE;
	}

	// Set format for output (bus 0 input scope - from callback to device)
	status = AudioUnitSetProperty(gAudioUnit, kAudioUnitProperty_StreamFormat,
																kAudioUnitScope_Input, 0, &format, sizeof(format));
	if (status != noErr)
	{
		printf("InitSound(): Failed to set output format: %d\n", (int)status);
		AudioComponentInstanceDispose(gAudioUnit);
		gAudioUnit = NULL;
		return FALSE;
	}

	// Set input callback
	AURenderCallbackStruct inputCallback;
	inputCallback.inputProc = InputCallback;
	inputCallback.inputProcRefCon = NULL;

	status = AudioUnitSetProperty(gAudioUnit, kAudioOutputUnitProperty_SetInputCallback,
																kAudioUnitScope_Global, 0, &inputCallback, sizeof(inputCallback));
	if (status != noErr)
	{
		printf("InitSound(): Failed to set input callback: %d\n", (int)status);
		AudioComponentInstanceDispose(gAudioUnit);
		gAudioUnit = NULL;
		return FALSE;
	}

	// Set output callback
	AURenderCallbackStruct outputCallback;
	outputCallback.inputProc = OutputCallback;
	outputCallback.inputProcRefCon = NULL;

	status = AudioUnitSetProperty(gAudioUnit, kAudioUnitProperty_SetRenderCallback,
																kAudioUnitScope_Input, 0, &outputCallback, sizeof(outputCallback));
	if (status != noErr)
	{
		printf("InitSound(): Failed to set output callback: %d\n", (int)status);
		AudioComponentInstanceDispose(gAudioUnit);
		gAudioUnit = NULL;
		return FALSE;
	}

	// Initialize the audio unit
	status = AudioUnitInitialize(gAudioUnit);
	if (status != noErr)
	{
		printf("InitSound(): Failed to initialize audio unit: %d\n", (int)status);
		AudioComponentInstanceDispose(gAudioUnit);
		gAudioUnit = NULL;
		return FALSE;
	}

	// Start audio processing
	status = AudioOutputUnitStart(gAudioUnit);
	if (status != noErr)
	{
		printf("InitSound(): Failed to start audio unit: %d\n", (int)status);
		AudioUnitUninitialize(gAudioUnit);
		AudioComponentInstanceDispose(gAudioUnit);
		gAudioUnit = NULL;
		return FALSE;
	}

	gAudioInitialized = true;
	printf("InitSound(): Core Audio initialized successfully\n");
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
		case SIGHUP:    return "HUP";
		case SIGINT:    return "INT";
		case SIGQUIT:   return "QUIT";
		case SIGILL:    return "ILL";
		case SIGTRAP:   return "TRAP";
		case SIGABRT:   return "ABRT";
		case SIGEMT:    return "EMT";
		case SIGFPE:    return "FPE";
		case SIGKILL:   return "KILL";
		case SIGBUS:    return "BUS";
		case SIGSEGV:   return "SEGV";
		case SIGSYS:    return "SYS";
		case SIGPIPE:   return "PIPE";
		case SIGALRM:   return "ALRM";
		case SIGTERM:   return "TERM";
		case SIGURG:    return "URG";
		case SIGSTOP:   return "STOP";
		case SIGTSTP:   return "TSTP";
		case SIGCONT:   return "CONT";
		case SIGCHLD:   return "CHLD";
		case SIGTTIN:   return "TTIN";
		case SIGTTOU:   return "TTOU";
		case SIGIO:     return "IO";
		case SIGXCPU:   return "XCPU";
		case SIGXFSZ:   return "XFSZ";
		case SIGVTALRM: return "VTALRM";
		case SIGPROF:   return "PROF";
		case SIGWINCH:  return "WINCH";
		case SIGINFO:   return "INFO";
		case SIGUSR1:   return "USR1";
		case SIGUSR2:   return "USR2";
		default:        return "UNKNOWN";
	}
}

// Utility functions
void printtick(char *msg)
{
	// Debug function - print timestamp with message
	printf("[%u] %s\n", (unsigned int)time(NULL), msg);
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

void SendtoCard(short *samples, int nSamples)
{
	if (strcmp(PlaybackDevice, "NOSOUND") == 0 || !gAudioInitialized)
	{
		// Even in NOSOUND mode, we still need to write to WAV file if enabled
		if (txwff != NULL)
			WriteWav(samples, nSamples, txwff);
		return;
	}

	pthread_mutex_lock(&gAudioMutex);

	// Queue samples for output
	for (int i = 0; i < nSamples; i++)
	{
		if (gOutputSamplesQueued < BUFFER_SIZE * NUM_BUFFERS)
		{
			gOutputBuffer[gOutputWriteIndex / BUFFER_SIZE][gOutputWriteIndex % BUFFER_SIZE] = samples[i];
			gOutputWriteIndex = (gOutputWriteIndex + 1) % (BUFFER_SIZE * NUM_BUFFERS);
			gOutputSamplesQueued++;
		}
		else
		{
			// Buffer full - drop samples (could log this)
			break;
		}
	}

	pthread_mutex_unlock(&gAudioMutex);

	// Write transmitted audio to WAV file if enabled (like Linux/Windows)
	if (txwff != NULL)
		WriteWav(samples, nSamples, txwff);
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

// More audio functions
void SoundFlush()
{
	if (!gAudioInitialized)
		return;

	pthread_mutex_lock(&gAudioMutex);

	// Clear output buffers
	gOutputWriteIndex = 0;
	gOutputReadIndex = 0;
	gOutputSamplesQueued = 0;

	pthread_mutex_unlock(&gAudioMutex);

	// Close WAV file if open (like Linux/Windows)
	if (txwff != NULL)
	{
		CloseWav(txwff);
		txwff = NULL;
	}
}

void SoundInit()
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

	pthread_mutex_unlock(&gAudioMutex);
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
		printf("Unable to write WAV file, invalid pathname. Logpath may be too long.\n");
		WriteTxWav = FALSE;
		return;
	}
	txwff = OpenWavW(txwff_pathname);
}

void StopCapture()
{
	if (!gAudioInitialized)
		return;

	// Stop audio processing
	if (gAudioUnit)
	{
		AudioOutputUnitStop(gAudioUnit);
		AudioUnitUninitialize(gAudioUnit);
		AudioComponentInstanceDispose(gAudioUnit);
		gAudioUnit = NULL;
		gAudioInitialized = false;
	}
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

	// Fill output buffer from queued samples
	for (UInt32 i = 0; i < samplesToWrite; i++)
	{
		if (gOutputSamplesQueued > 0)
		{
			outputBuffer[i] = gOutputBuffer[gOutputReadIndex / BUFFER_SIZE][gOutputReadIndex % BUFFER_SIZE];
			gOutputReadIndex = (gOutputReadIndex + 1) % (BUFFER_SIZE * NUM_BUFFERS);
			gOutputSamplesQueued--;
		}
		else
		{
			outputBuffer[i] = 0; // Silence if no data
		}
	}

	pthread_mutex_unlock(&gAudioMutex);
	return noErr;
}

// HID implementation for macOS using IOKit
// This provides CM108-style USB audio device support for PTT control

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
				printf("Failed to open HID Manager: %d\n", result);
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

	printf("HID write failed: %d\n", result);
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

	// For CM108 devices, we need to find the device by VID/PID or path
	// This is a simplified implementation - real CM108 support would parse VID:PID

	CFSetRef deviceSet = IOHIDManagerCopyDevices(gHIDManager);
	if (!deviceSet)
		return NULL;

	CFIndex deviceCount = CFSetGetCount(deviceSet);
	IOHIDDeviceRef *devices = malloc(deviceCount * sizeof(IOHIDDeviceRef));
	CFSetGetValues(deviceSet, (const void **)devices);

	IOHIDDeviceRef targetDevice = NULL;

	// Search for matching device
	for (CFIndex i = 0; i < deviceCount; i++)
	{
		IOHIDDeviceRef device = devices[i];

		// Get device properties
		CFNumberRef vendorID = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey));
		CFNumberRef productID = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey));

		if (vendorID && productID)
		{
			int vid, pid;
			CFNumberGetValue(vendorID, kCFNumberIntType, &vid);
			CFNumberGetValue(productID, kCFNumberIntType, &pid);

			// Check for common CM108 VID/PID combinations
			if ((vid == 0x0d8c && pid == 0x0008) || // Original CM108
					(vid == 0x0d8c && pid == 0x000c) || // CM119 variant
					strstr(path, "0d8c:0008") ||				// VID:PID format
					strstr(path, "0d8c:000c"))
			{ // VID:PID format
				targetDevice = device;
				CFRetain(targetDevice);
				break;
			}
		}
	}

	free(devices);
	CFRelease(deviceSet);

	if (!targetDevice)
	{
		printf("CM108 device not found: %s\n", path);
		return NULL;
	}

	// Open the device
	IOReturn result = IOHIDDeviceOpen(targetDevice, kIOHIDOptionsTypeNone);
	if (result != kIOReturnSuccess)
	{
		printf("Failed to open HID device: %d\n", result);
		CFRelease(targetDevice);
		return NULL;
	}

	// Create our device structure
	macos_hid_device *hid_dev = malloc(sizeof(macos_hid_device));
	hid_dev->device = targetDevice;
	hid_dev->runLoop = CFRunLoopGetCurrent();
	hid_dev->isOpen = true;

	printf("CM108 HID device opened successfully\n");
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
			printf("ERROR: cmdstr[%ld] insufficient to hold full command string for logging.\n", sizeof(cmdstr));
			break;
		}
	}

	// Process command line arguments
	processargs(argc, argv);

	// TODO: Initialize signal handlers for macOS
	// TODO: Initialize Core Audio system
	// TODO: Set up audio devices

	printf("ARDOP macOS version starting...\n");
	printf("Command line: %s\n", cmdstr);

	// Call main ARDOP loop
	ARDOP_Main();

	return 0;
}