// CoreAudio audio for macOS
//
// This is the macOS audio backend for ardopcf.  It implements the platform
// audio interface declared in common/audio.h, modeled on the Linux ALSA
// backend in linux/ALSA.c.
//
// ardopcf uses a polling model for audio (PollReceivedSamples() drains
// captured audio, SendtoCard()/SoundFlush() stage audio for transmission).
// CoreAudio is callback driven.  This file bridges the two models using a pair
// of thread-safe ring buffers (one for capture, one for playback) which are
// filled/drained by AudioQueue callbacks and drained/filled by the polling
// code.  The ring buffers are protected by pthread mutexes and the callbacks
// are kept lightweight.

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

#include "common/os_util.h"
#include "common/audio.h"
#include "common/log.h"
#include "common/wav.h"
#include "common/ardopcommon.h"
#include "common/ptt.h"
#include "common/Webgui.h"

// Use LastGoodCaptureDevice and LastGoodPlaybackDevice to store the names of
// the last audio devices that were successfully opened.  These are used by
// OpenSoundCapture() and OpenSoundPlayback() if the special value of "RESTORE"
// is used.  This is used by the RESTORE option of the CAPTURE and PLAYBACK host
// commands as well as RXENABLED TRUE, TXENABLED TRUE, and CODEC TRUE.
// See also LastGoodCATstr and and LastGoodPTTstr defined in ptt.c
char LastGoodCaptureDevice[DEVSTRSZ] = "";  // same size as CaptureDevice
char LastGoodPlaybackDevice[DEVSTRSZ] = "";  // same size as PlaybackDevice

extern bool WriteRxWav;  // Record RX controlled by Command line/TX/Timer
extern bool HWriteRxWav;  // Record RX controlled by host command RECRX
extern struct WavFile *txwff;  // For recording of filtered TX audio
// extern struct WavFile *txwfu;  // For recording of unfiltered TX audio


void txSleep(unsigned int mS);
void StartRxWav();

// TX and RX audio are signed short integers: +- 32767
extern int SampleNo;  // Total number of samples for this transmission.
extern int Number;  // Number of samples waiting to be sent

// txbuffer and TxIndex are globals shared with Modulate.c via audio.h
// Two buffers of 0.1 sec duration for TX audio.
short txbuffer[2][SendSize];
// index to next txbuffer to be filled..
int TxIndex = 0;

short inbuffer[2][ReceiveSize];  // Two buffers of 0.1 Sec duration for RX audio.
int inIndex = 0;  // inbuffer being used 0 or 1

int m_sampleRate = 12000;  // Ardopcf always uses 12000 samples per second

// Number of AudioQueue buffers to use for capture and playback.  AudioQueue
// requires several buffers in flight to play/record without gaps.
#define NUM_AQ_BUFFERS 4
// Number of mono sample frames per AudioQueue buffer.  240 frames = 20 ms at
// 12 kHz, which matches ReceiveSize and gives reasonable callback latency.
#define AQ_FRAMES_PER_BUFFER 240

// Ring buffer capacity in mono samples (shorts).  At 12 kHz, 48000 samples is
// 4 seconds of audio, which is plenty of slack for the polling code to keep up
// with the callbacks (and vice versa).
#define RINGSIZE 48000

// A simple single-producer/single-consumer ring buffer of mono int16 samples,
// protected by a mutex.  The capture ring is written by the input callback and
// read by PollReceivedSamples().  The playback ring is written by
// SoundCardWrite()/SendtoCard() and read by the output callback.
typedef struct {
	short buf[RINGSIZE];
	int head;  // next write position
	int tail;  // next read position
	int count;  // number of samples currently stored
	pthread_mutex_t mutex;
} RingBuffer;

static RingBuffer captureRing;
static RingBuffer playbackRing;

static void ring_init(RingBuffer *r) {
	r->head = 0;
	r->tail = 0;
	r->count = 0;
	pthread_mutex_init(&r->mutex, NULL);
}

// Write up to n samples into the ring buffer.  If the ring is full, excess
// samples are dropped (this should not happen in normal operation since the
// ring is large compared to the audio rate).  Returns the number written.
static int ring_write(RingBuffer *r, const short *data, int n) {
	pthread_mutex_lock(&r->mutex);
	int written = 0;
	while (written < n && r->count < RINGSIZE) {
		r->buf[r->head] = data[written];
		r->head = (r->head + 1) % RINGSIZE;
		++r->count;
		++written;
	}
	pthread_mutex_unlock(&r->mutex);
	return written;
}

// Read up to n samples from the ring buffer.  Returns the number actually read
// (may be less than n if fewer are available).
static int ring_read(RingBuffer *r, short *data, int n) {
	pthread_mutex_lock(&r->mutex);
	int rd = 0;
	while (rd < n && r->count > 0) {
		data[rd] = r->buf[r->tail];
		r->tail = (r->tail + 1) % RINGSIZE;
		--r->count;
		++rd;
	}
	pthread_mutex_unlock(&r->mutex);
	return rd;
}

static int ring_count(RingBuffer *r) {
	pthread_mutex_lock(&r->mutex);
	int c = r->count;
	pthread_mutex_unlock(&r->mutex);
	return c;
}

static void ring_clear(RingBuffer *r) {
	pthread_mutex_lock(&r->mutex);
	r->head = 0;
	r->tail = 0;
	r->count = 0;
	pthread_mutex_unlock(&r->mutex);
}

// AudioQueue handles.  NULL when not open.
static AudioQueueRef playQueue = NULL;
static AudioQueueRef recQueue = NULL;
static AudioQueueBufferRef playBuffers[NUM_AQ_BUFFERS];
static AudioQueueBufferRef recBuffers[NUM_AQ_BUFFERS];
static bool playRunning = false;
static bool recRunning = false;

// End-of-transmission PTT timing.  The output AudioQueue runs continuously
// (playing silence between transmissions), so an empty playback ring only means
// the last samples have been handed to the queue, not that they have left the
// audio hardware.  To avoid dropping PTT before the modulated audio has
// actually been transmitted, track the output queue play position (in frames)
// at the end of the last real (non-silence) audio.  SoundFlush() then waits for
// the hardware play head (AudioQueueGetCurrentTime()) to reach it before
// unkeying.  Both counters are in output frames on the same timeline as
// AudioQueueGetCurrentTime()'s mSampleTime, which starts at 0 when the queue is
// started.
static pthread_mutex_t playPosMutex = PTHREAD_MUTEX_INITIALIZER;
static double outFramesEnqueued = 0;  // frames handed to the output queue
static double lastRealOutFrame = 0;   // play position at end of last real audio

void StartCapture() {
	Capturing = true;
	DiscardOldSamples();
	ClearAllMixedSamples();
	State = SearchingForLeader;
}


// A map between the AudioDevices[] list built by GetDevices() and the CoreAudio
// AudioDeviceID / device UID needed to bind an AudioQueue to a specific device.
// The index into these parallel arrays matches the index into AudioDevices[].
// NOSOUND entries have a deviceID of kAudioObjectUnknown and a NULL uid.
#define MAX_AUDIO_DEVICES 256
static AudioDeviceID deviceIDs[MAX_AUDIO_DEVICES];
static CFStringRef deviceUIDs[MAX_AUDIO_DEVICES];  // owned references, may be NULL
static int deviceMapLen = 0;

// Release any CFStringRefs in deviceUIDs and reset the device map.
static void clear_device_map() {
	for (int i = 0; i < deviceMapLen; ++i) {
		if (deviceUIDs[i] != NULL) {
			CFRelease(deviceUIDs[i]);
			deviceUIDs[i] = NULL;
		}
		deviceIDs[i] = kAudioObjectUnknown;
	}
	deviceMapLen = 0;
}

// Return the number of channels in the given scope (input or output) for a
// device, by querying its stream configuration.  Returns 0 on error or if the
// device has no streams in that scope.
static int device_channels_in_scope(AudioDeviceID devid, AudioObjectPropertyScope scope) {
	AudioObjectPropertyAddress addr;
	addr.mSelector = kAudioDevicePropertyStreamConfiguration;
	addr.mScope = scope;
	addr.mElement = kAudioObjectPropertyElementMain;

	UInt32 size = 0;
	if (AudioObjectGetPropertyDataSize(devid, &addr, 0, NULL, &size) != noErr
		|| size == 0
	)
		return 0;

	AudioBufferList *bl = (AudioBufferList *) malloc(size);
	if (bl == NULL)
		return 0;
	int channels = 0;
	if (AudioObjectGetPropertyData(devid, &addr, 0, NULL, &size, bl) == noErr) {
		for (UInt32 i = 0; i < bl->mNumberBuffers; ++i)
			channels += bl->mBuffers[i].mNumberChannels;
	}
	free(bl);
	return channels;
}

// Get a device's human readable name as a freshly allocated UTF-8 C string, or
// NULL on failure.  The caller must free() the result.
static char *device_name(AudioDeviceID devid) {
	AudioObjectPropertyAddress addr;
	addr.mSelector = kAudioDevicePropertyDeviceNameCFString;
	addr.mScope = kAudioObjectPropertyScopeGlobal;
	addr.mElement = kAudioObjectPropertyElementMain;

	CFStringRef cfname = NULL;
	UInt32 size = sizeof(cfname);
	if (AudioObjectGetPropertyData(devid, &addr, 0, NULL, &size, &cfname) != noErr
		|| cfname == NULL
	)
		return NULL;

	CFIndex len = CFStringGetMaximumSizeForEncoding(
		CFStringGetLength(cfname), kCFStringEncodingUTF8) + 1;
	char *out = (char *) malloc(len);
	if (out != NULL) {
		if (!CFStringGetCString(cfname, out, len, kCFStringEncodingUTF8)) {
			free(out);
			out = NULL;
		}
	}
	CFRelease(cfname);
	return out;
}

// Get a device's UID as an owned CFStringRef, or NULL on failure.  The caller
// is responsible for CFRelease()ing the result.
static CFStringRef device_uid(AudioDeviceID devid) {
	AudioObjectPropertyAddress addr;
	addr.mSelector = kAudioDevicePropertyDeviceUID;
	addr.mScope = kAudioObjectPropertyScopeGlobal;
	addr.mElement = kAudioObjectPropertyElementMain;

	CFStringRef uid = NULL;
	UInt32 size = sizeof(uid);
	if (AudioObjectGetPropertyData(devid, &addr, 0, NULL, &size, &uid) != noErr)
		return NULL;
	return uid;  // owned by caller
}

// Populate AudioDevices with both Playback and Capture devices.
// Enumerates all audio devices known to the CoreAudio HAL.  For each device the
// name is read, and capture/playback capability is determined by querying the
// number of channels in the input and output stream configurations.  A device
// is usable for capture if it has input channels, and for playback if it has
// output channels.  Include all enumerated devices since this list may be
// useful for diagnostic purposes.
// A parallel device map (deviceIDs / deviceUIDs) is built alongside
// AudioDevices so that OpenSoundCapture()/OpenSoundPlayback() can translate a
// device index back into the AudioDeviceID/UID required to bind an AudioQueue.
// dev->name and dev->desc shall be truncated to DEVSTRSZ bytes including a
// terminating null.
void GetDevices() {
	int devindex;
	DeviceInfo *dev;

	// Clean up previous results
	FreeDevices(&AudioDevices);
	InitDevices(&AudioDevices);
	clear_device_map();

	// Query the list of all audio device IDs known to the HAL.
	AudioObjectPropertyAddress addr;
	addr.mSelector = kAudioHardwarePropertyDevices;
	addr.mScope = kAudioObjectPropertyScopeGlobal;
	addr.mElement = kAudioObjectPropertyElementMain;

	UInt32 size = 0;
	AudioDeviceID *ids = NULL;
	int ndevices = 0;
	if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr,
		0, NULL, &size) == noErr && size > 0
	) {
		ids = (AudioDeviceID *) malloc(size);
		if (ids != NULL) {
			if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr,
				0, NULL, &size, ids) == noErr
			) {
				ndevices = size / sizeof(AudioDeviceID);
			} else {
				free(ids);
				ids = NULL;
				ndevices = 0;
			}
		}
	}

	for (int i = 0; i < ndevices && i < MAX_AUDIO_DEVICES; ++i) {
		AudioDeviceID devid = ids[i];
		char *name = device_name(devid);
		if (name == NULL) {
			ZF_LOGV("Skipping audio device with no readable name (id=%u)",
				(unsigned int) devid);
			continue;
		}
		int inch = device_channels_in_scope(devid,
			kAudioObjectPropertyScopeInput);
		int outch = device_channels_in_scope(devid,
			kAudioObjectPropertyScopeOutput);

		// ExtendDevices() adds a new unused device and returns its index.  It
		// also initializes the string pointers to be NULL, and the booleans to
		// be false.
		devindex = ExtendDevices(&AudioDevices);
		dev = AudioDevices[devindex];
		// dev->name shall be truncated to DEVSTRSZ bytes including a
		// terminating null.
		if (strlen(name) > (DEVSTRSZ - 1))
			dev->name = strndup(name, DEVSTRSZ - 1);
		else
			dev->name = strdup(name);
		// On macOS there is no useful separate description, so reuse the name
		// (truncated) as the description.
		dev->desc = strdup(dev->name);
		dev->alias = NULL;  // No numeric-alias scheme on macOS.
		dev->capture = (inch > 0);
		dev->playback = (outch > 0);

		// Record the device id and UID in the parallel map at the same index.
		if (devindex < MAX_AUDIO_DEVICES) {
			deviceIDs[devindex] = devid;
			deviceUIDs[devindex] = device_uid(devid);  // owned, may be NULL
			if (devindex + 1 > deviceMapLen)
				deviceMapLen = devindex + 1;
		}
		free(name);
	}
	free(ids);

	// Always include NOSOUND as the last device suitable for both capture and
	// playback.  Thus, AudioDevices[] is never completely empty.
	devindex = ExtendDevices(&AudioDevices);
	dev = AudioDevices[devindex];
	dev->name = strdup("NOSOUND");
	dev->desc = strdup("A dummy audio device for diagnostic use.");
	dev->capture = true;
	dev->playback = true;
	if (devindex < MAX_AUDIO_DEVICES) {
		deviceIDs[devindex] = kAudioObjectUnknown;
		deviceUIDs[devindex] = NULL;
		if (devindex + 1 > deviceMapLen)
			deviceMapLen = devindex + 1;
	}
}


// AudioQueue input (capture) callback.  Called on an AudioQueue-managed thread
// when a buffer of captured audio is available.  The buffer holds interleaved
// int16 samples (mono or stereo per Cch).  Extract the desired channel and push
// the mono samples into the capture ring, then re-enqueue the buffer.
static void inputCallback(
	void *inUserData,
	AudioQueueRef inAQ,
	AudioQueueBufferRef inBuffer,
	const AudioTimeStamp *inStartTime,
	UInt32 inNumPackets,
	const AudioStreamPacketDescription *inPacketDescs
) {
	(void) inUserData;
	(void) inStartTime;
	(void) inNumPackets;
	(void) inPacketDescs;

	const short *samples = (const short *) inBuffer->mAudioData;
	int nframes = inBuffer->mAudioDataByteSize / sizeof(short);

	if (Cch == 2) {
		// Interleaved stereo.  Pick the configured channel.
		nframes /= 2;
		int start = UseLeftRX ? 0 : 1;
		short mono[AQ_FRAMES_PER_BUFFER * 2];
		int count = 0;
		for (int n = 0; n < nframes && count < (int) (sizeof(mono) / sizeof(short)); ++n)
			mono[count++] = samples[start + 2 * n];
		ring_write(&captureRing, mono, count);
	} else {
		// Mono.
		ring_write(&captureRing, samples, nframes);
	}

	if (recRunning)
		AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, NULL);
}

// AudioQueue output (playback) callback.  Called when the queue needs more
// audio to play.  Fill the buffer from the playback ring, packing mono samples
// into the interleaved channel layout selected by UseLeftTX/UseRightTX.  Output
// silence when the ring is empty (an underrun, which is normal at the start and
// end of a transmission).
static void outputCallback(
	void *inUserData,
	AudioQueueRef inAQ,
	AudioQueueBufferRef inBuffer
) {
	(void) inUserData;

	int frames = AQ_FRAMES_PER_BUFFER;
	short mono[AQ_FRAMES_PER_BUFFER];
	int got = ring_read(&playbackRing, mono, frames);
	// Pad with silence if the ring did not have enough samples.
	for (int n = got; n < frames; ++n)
		mono[n] = 0;

	short *out = (short *) inBuffer->mAudioData;
	if (Pch == 2) {
		for (int n = 0; n < frames; ++n) {
			out[2 * n] = UseLeftTX ? mono[n] : 0;
			out[2 * n + 1] = UseRightTX ? mono[n] : 0;
		}
		inBuffer->mAudioDataByteSize = frames * 2 * sizeof(short);
	} else {
		for (int n = 0; n < frames; ++n)
			out[n] = mono[n];
		inBuffer->mAudioDataByteSize = frames * sizeof(short);
	}

	// Advance the play position.  This buffer is re-enqueued at the current
	// write head; if it carried real audio (got > 0), the last real sample of
	// this transmission ends at that position + got.  SoundFlush() waits for the
	// hardware to play up to lastRealOutFrame before dropping PTT.
	pthread_mutex_lock(&playPosMutex);
	if (got > 0)
		lastRealOutFrame = outFramesEnqueued + got;
	outFramesEnqueued += frames;
	pthread_mutex_unlock(&playPosMutex);

	if (playRunning)
		AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, NULL);
}

// Build the AudioStreamBasicDescription for a 16-bit signed PCM stream with the
// given number of channels at 12 kHz.
static void fill_asbd(AudioStreamBasicDescription *asbd, int ch) {
	memset(asbd, 0, sizeof(*asbd));
	asbd->mSampleRate = m_sampleRate;
	asbd->mFormatID = kAudioFormatLinearPCM;
	asbd->mFormatFlags = kAudioFormatFlagIsSignedInteger
		| kAudioFormatFlagIsPacked;
	asbd->mBitsPerChannel = 16;
	asbd->mChannelsPerFrame = ch;
	asbd->mFramesPerPacket = 1;
	asbd->mBytesPerFrame = ch * sizeof(short);
	asbd->mBytesPerPacket = asbd->mBytesPerFrame * asbd->mFramesPerPacket;
}

// Open and configure an AudioQueue for the device with the given AudioQueue
// queue handle slot.  For capture, sets up an input queue; for playback, an
// output queue.  Binds the queue to the device identified by uid (unless uid is
// NULL, in which case the system default device is used).  On success, returns
// 0 with *queue set and the AudioQueue buffers allocated and (for capture)
// enqueued.  On failure, returns a non-zero OSStatus and leaves *queue == NULL.
static OSStatus open_audio(
	AudioQueueRef *queue,
	AudioQueueBufferRef *buffers,
	CFStringRef uid,
	bool iscapture,
	int ch
) {
	OSStatus status;
	AudioStreamBasicDescription asbd;
	const char *forstr = iscapture ? "capture" : "playback";
	fill_asbd(&asbd, ch);

	*queue = NULL;

	if (iscapture) {
		status = AudioQueueNewInput(&asbd, inputCallback, NULL,
			NULL, kCFRunLoopCommonModes, 0, queue);
	} else {
		status = AudioQueueNewOutput(&asbd, outputCallback, NULL,
			NULL, kCFRunLoopCommonModes, 0, queue);
	}
	if (status != noErr) {
		ZF_LOGE("Error creating AudioQueue for %s (OSStatus %d)",
			forstr, (int) status);
		*queue = NULL;
		return status;
	}

	// Bind the queue to the chosen device by UID.  If uid is NULL, the queue
	// uses the system default device.
	if (uid != NULL) {
		status = AudioQueueSetProperty(*queue,
			kAudioQueueProperty_CurrentDevice, &uid, sizeof(uid));
		if (status != noErr) {
			ZF_LOGE("Error binding AudioQueue to device for %s (OSStatus %d)",
				forstr, (int) status);
			AudioQueueDispose(*queue, true);
			*queue = NULL;
			return status;
		}
	}

	int bytesPerBuffer = AQ_FRAMES_PER_BUFFER * ch * sizeof(short);
	for (int i = 0; i < NUM_AQ_BUFFERS; ++i) {
		status = AudioQueueAllocateBuffer(*queue, bytesPerBuffer, &buffers[i]);
		if (status != noErr) {
			ZF_LOGE("Error allocating AudioQueue buffer %i for %s (OSStatus %d)",
				i, forstr, (int) status);
			AudioQueueDispose(*queue, true);
			*queue = NULL;
			return status;
		}
		if (iscapture) {
			// Capture buffers must be enqueued to receive audio.
			AudioQueueEnqueueBuffer(*queue, buffers[i], 0, NULL);
		} else {
			// Playback buffers are primed with silence and enqueued so that
			// the queue can start.
			memset(buffers[i]->mAudioData, 0, bytesPerBuffer);
			buffers[i]->mAudioDataByteSize = bytesPerBuffer;
			AudioQueueEnqueueBuffer(*queue, buffers[i], 0, NULL);
		}
	}

	if (!iscapture) {
		// The primed silence buffers occupy the first frames of the queue
		// timeline, which starts at 0 when AudioQueueStart() is called.
		pthread_mutex_lock(&playPosMutex);
		outFramesEnqueued = (double)(NUM_AQ_BUFFERS * AQ_FRAMES_PER_BUFFER);
		lastRealOutFrame = 0;
		pthread_mutex_unlock(&playPosMutex);
	}

	ZF_LOGD("Opened AudioQueue for %s with %i channel(s).", forstr, ch);
	return noErr;
}

// Close the playback audio device if one is open and set TXEnabled=false.
// do_getdevices is passed to updateWebGuiAudioConfig()
void CloseSoundPlayback(bool do_getdevices) {
	if (playQueue != NULL) {
		playRunning = false;
		AudioQueueStop(playQueue, true);
		AudioQueueDispose(playQueue, true);
		playQueue = NULL;
	}
	ring_clear(&playbackRing);
	PlaybackDevice[0] = 0x00;  // empty string
	SoundIsPlaying = false;
	TXEnabled = false;
	KeyPTT(false);  // In case PTT is engaged.
	updateWebGuiAudioConfig(do_getdevices);
}

// GetDevices is always called at the start of this function to ensure that
// AudioDevices is accurate.
// If devstr matches PlaybackDevice and ch matches Pch and TXEnabled is true,
// then do nothing but write a debug log message.  Otherwise, if TXEnabled is
// true, then CloseSoundPlayback() is called before attempting to open devstr.
// FindAudioDevice() searches for an exact match for a device name using devstr
// truncated to DEVSTRSZ - 1 bytes, and if this fails, searches for a case
// insensitive match of device description.  If a match is found, it is opened
// and configured.
// If no match is found, return false with TXEnabled=false and PlaybackDevice
// set to an empty string.  On success, return true with TXEnabled=true and
// PlaybackDevice set to devstr.
// If devstr is the special device name "RESTORE", do nothing if TXEnabled is
// true.  However, if TXEnabled is false, but LastGoodPlaybackDevice is not an
// empty string, then try to reopen that device.
// If devstr is an empty string "", then close the existing playback device if
// one is open, and then always return false.
bool OpenSoundPlayback(char *devstr, int ch) {
	int aindex;
	// Always update AudioDevices so that next wg_send_audiodevices() will have
	// updated info, even if not used within this function.
	GetDevices();
	if (devstr[0] == 0x00) {
		// devstr is an empty string.  This would always match the first entry
		// in AudioDevices[].  So, instead it is used to indicate that the
		// current Playback device (if there is one) should be closed, and then
		// return false.
		if (TXEnabled)
			CloseSoundPlayback(false);  // calls updateWebGuiAudioConfig(false);
		return false;
	}
	if (TXEnabled) {
		// Compare only the first DEVSTRSZ - 1 bytes (exclude terminating null)
		if ((strncmp(devstr, PlaybackDevice, DEVSTRSZ - 1) == 0 && ch == Pch)
			|| strcmp(devstr, "RESTORE") == 0
		) {
			ZF_LOGD("OpenSoundPlayback(%s) matches already open device.",
				devstr);
			return true;
		}
		// Close the existing device.  This also sets TXEnabled=false and calls
		// updateWebGuiAudioConfig(false).
		CloseSoundPlayback(false);
	}
	// make PlaybackDevice an empty string.  If devstr cannot be opened, then
	// TXEnabled will remain false and PlaybackDevice will remain empty.
	PlaybackDevice[0] = 0x00;
	Pch = -1;

	if (strcmp(devstr, "RESTORE") == 0) {
		if (LastGoodPlaybackDevice[0] == 0x00)
			return false;  // no LastGoodPlaybackDevice to open.
		devstr = LastGoodPlaybackDevice;
		// This will succeed or fail depending on whether this device is
		// currently available.
	}

	if (strcmp(devstr, "NOSOUND") == 0 || strcmp(devstr, "-1") == 0) {
		// For testing/diagnostic purposes, NOSOUND uses no audio device,
		TXEnabled = true;
		strcpy(PlaybackDevice, "NOSOUND");
		strcpy(LastGoodPlaybackDevice, PlaybackDevice);
		Pch = ch;
		updateWebGuiAudioConfig(false);
		return true;
	}

	// FindAudioDevice searches for an exact match of device name, and if this
	// fails, searches for a case insensitive match of device description.
	if ((aindex = FindAudioDevice(devstr, false)) < 0) {
		ZF_LOGW("Error opening playback audio device %s.  This does not appear"
			" to be a valid audio device name, nor was a match found using a"
			" case insensitive substring search in the descriptions of"
			" available playback devices.",
			devstr);
		return false;
	}
	ZF_LOGV("FindAudioDevice(%s) in OpenSoundPlayback() -> %s",
		devstr, AudioDevices[aindex]->name);

	// Translate the matched AudioDevices index into a CoreAudio device UID via
	// the parallel device map, which is indexed identically to AudioDevices[].
	// aindex therefore selects the UID for the exact entry FindAudioDevice()
	// matched, regardless of whether it matched on name, alias, or description.
	// This also keeps the scope correct when a USB CODEC enumerates as two
	// AudioDeviceIDs sharing a name (one input, one output): FindAudioDevice()
	// already filtered by scope, so aindex points at the right half.
	CFStringRef uid = (aindex < deviceMapLen) ? deviceUIDs[aindex] : NULL;

	ring_clear(&playbackRing);
	if (open_audio(&playQueue, playBuffers, uid, false, ch) != noErr) {
		// Error already logged.
		playQueue = NULL;
		return false;
	}

	playRunning = true;
	OSStatus status = AudioQueueStart(playQueue, NULL);
	if (status != noErr) {
		ZF_LOGE("Error starting playback AudioQueue (OSStatus %d)",
			(int) status);
		playRunning = false;
		AudioQueueDispose(playQueue, true);
		playQueue = NULL;
		return false;
	}

	TXEnabled = true;
	snprintf(PlaybackDevice, DEVSTRSZ, "%s", devstr);
	strcpy(LastGoodPlaybackDevice, PlaybackDevice);
	Pch = ch;
	updateWebGuiAudioConfig(false);
	return true;
}


// Close the capture audio device if one is open and set RXEnabled=false.
// do_getdevices is passed to updateWebGuiAudioConfig()
void CloseSoundCapture(bool do_getdevices) {
	if (recQueue != NULL) {
		recRunning = false;
		AudioQueueStop(recQueue, true);
		AudioQueueDispose(recQueue, true);
		recQueue = NULL;
	}
	ring_clear(&captureRing);
	CaptureDevice[0] = 0x00;  // empty string
	RXEnabled = false;
	updateWebGuiAudioConfig(do_getdevices);
}

// GetDevices is always called at the start of this function to ensure that
// AudioDevices is accurate.
// If devstr matches CaptureDevice and ch matches Cch and RXEnabled is true,
// then do nothing but write a debug log message.  Otherwise, if RXEnabled is
// true, then CloseSoundCapture() is called before attempting to open devstr.
// FindAudioDevice() searches for an exact match for a device name using devstr
// truncated to DEVSTRSZ - 1 bytes, and if this fails, searches for a case
// insensitive match of device description.  If a match is found, it is opened
// and configured.
// If no match is found, return false with RXEnabled=false and CaptureDevice
// set to an empty string.  On success, return true with RXEnabled=true and
// CaptureDevice set to devstr.
// If devstr is the special device name "RESTORE", do nothing if RXEnabled is
// true.  However, if RXEnabled is false, but LastGoodCaptureDevice is not an
// empty string, then try to reopen that device.
// If devstr is an empty string "", then close the existing capture device if
// one is open, and then always return false.
bool OpenSoundCapture(char *devstr, int ch) {
	int aindex;
	// Always update AudioDevices so that next wg_send_audiodevices() will have
	// updated info, even if not used within this function.
	GetDevices();
	if (devstr[0] == 0x00) {
		// devstr is an empty string.  This would always match the first entry
		// in AudioDevices[].  So, instead it is used to indicate that the
		// current Capture device (if there is one) should be closed, and then
		// return false.
		if (RXEnabled)
			CloseSoundCapture(false);  // calls updateWebGuiAudioConfig(false);
		return false;
	}
	if (RXEnabled) {
		// Compare only the first DEVSTRSZ - 1 bytes (exclude terminating null)
		if ((strncmp(devstr, CaptureDevice, DEVSTRSZ - 1) == 0 && ch == Cch)
			|| strcmp(devstr, "RESTORE") == 0
		) {
			ZF_LOGD("OpenSoundCapture(%s) matches already open device.",
				devstr);
			return true;
		}
		// Close the existing device.  This also sets RXEnabled=false and calls
		// updateWebGuiAudioConfig(false).
		CloseSoundCapture(false);
	}
	// make CaptureDevice an empty string.  If devstr cannot be opened, then
	// RXEnabled will remain false and CaptureDevice will remain empty.
	CaptureDevice[0] = 0x00;
	Cch = -1;

	if (strcmp(devstr, "RESTORE") == 0) {
		if (LastGoodCaptureDevice[0] == 0x00)
			return false;  // no LastGoodCaptureDevice to open.
		devstr = LastGoodCaptureDevice;
		// This will succeed or fail depending on whether this device is
		// currently available.
	}

	if (strcmp(devstr, "NOSOUND") == 0 || strcmp(devstr, "-1") == 0) {
		// For testing/diagnostic purposes, NOSOUND uses no audio device,
		RXEnabled = true;
		RXSilent = false;  // NOSOUND is always silent, so ignore silence.
		wg_send_rxenabled(0, RXEnabled);
		strcpy(CaptureDevice, "NOSOUND");
		strcpy(LastGoodCaptureDevice, CaptureDevice);
		Cch = ch;
		updateWebGuiAudioConfig(false);
		return true;
	}

	// FindAudioDevice searches for an exact match of device name, and if this
	// fails, searches for a case insensitive match of device description.
	if ((aindex = FindAudioDevice(devstr, true)) < 0) {
		ZF_LOGW("Error opening capture audio device %s.  This does not appear"
			" to be a valid audio device name, nor was a match found using a"
			" case insensitive substring search in the descriptions of"
			" available capture devices.",
			devstr);
		return false;
	}
	ZF_LOGV("FindAudioDevice(%s) in OpenSoundCapture() -> %s",
		devstr, AudioDevices[aindex]->name);

	// Translate the matched AudioDevices index into a CoreAudio device UID via
	// the parallel device map, which is indexed identically to AudioDevices[].
	// aindex therefore selects the UID for the exact entry FindAudioDevice()
	// matched, regardless of whether it matched on name, alias, or description.
	// This also keeps the scope correct when a USB CODEC enumerates as two
	// AudioDeviceIDs sharing a name (one input, one output): FindAudioDevice()
	// already filtered by scope, so aindex points at the right half.
	CFStringRef uid = (aindex < deviceMapLen) ? deviceUIDs[aindex] : NULL;

	ring_clear(&captureRing);
	if (open_audio(&recQueue, recBuffers, uid, true, ch) != noErr) {
		// Error already logged.
		recQueue = NULL;
		return false;
	}

	recRunning = true;
	OSStatus status = AudioQueueStart(recQueue, NULL);
	if (status != noErr) {
		ZF_LOGE("Error starting capture AudioQueue (OSStatus %d)",
			(int) status);
		recRunning = false;
		AudioQueueDispose(recQueue, true);
		recQueue = NULL;
		return false;
	}

	RXEnabled = true;
	RXSilent = false;
	snprintf(CaptureDevice, DEVSTRSZ, "%s", devstr);
	strcpy(LastGoodCaptureDevice, CaptureDevice);
	Cch = ch;
	if (!SoundIsPlaying)
		StartCapture();
	updateWebGuiAudioConfig(false);
	return true;
}


// Stage nSamples mono samples for playback by pushing them into the playback
// ring buffer.  The output AudioQueue callback packs them into the configured
// channel layout.  Block (yielding with txSleep()) until there is room in the
// ring for the samples.
// return true on success and false on failure
bool SoundCardWrite(short *input, unsigned int nSamples) {
	if (!TXEnabled) {
		ZF_LOGW("SoundCardWrite() called when not TXEnabled. Ignoring.");
		return false;
	}
	if (strcmp(PlaybackDevice, "NOSOUND") == 0)
		return true;  // Do nothing, indicate success.

	unsigned int written = 0;
	int waitcount = 0;
	while (written < nSamples && TXEnabled) {
		int n = ring_write(&playbackRing, input + written,
			(int) (nSamples - written));
		written += n;
		if (written < nSamples) {
			// Ring is full.  Wait for the output callback to drain it.
			txSleep(20);
			// Guard against waiting forever if the queue has stalled.
			if (++waitcount > 500) {
				ZF_LOGE("Timeout waiting to stage playback samples. "
					" Setting TXEnabled to false.");
				CloseSoundPlayback(true);  // calls updateWebGuiAudioConfig(true);
				return false;
			}
		}
	}
	return true;
}

// Stage for playback the contents (n samples) of the txbuffer[TxIndex]
// return true on success and false on failure.
// This will block until samples can be staged.
bool SendtoCard(int n) {
	if (!TXEnabled) {
		ZF_LOGW("SendtoCard() called when not TXEnabled. Ignoring.");
		return false;
	}
	if (!SoundCardWrite(&txbuffer[TxIndex][0], n))
		return false;

	if (txwff != NULL)
		WriteWav(&txbuffer[TxIndex][0], n, txwff);
	return true;
}

// Read up to nSamples mono samples from the capture ring buffer into input.
// Returns the number of samples actually read.  If fewer than nSamples are
// available, returns 0 without consuming any samples (mirroring ALSA's
// SoundCardRead() returning 0 when insufficient samples are available).
int SoundCardRead(short *input, unsigned int nSamples) {
	if (!RXEnabled) {
		ZF_LOGD("SoundCardRead() called when not RXEnabled.  Ignoring.");
		return 0;
	}
	if (ring_count(&captureRing) < (int) nSamples)
		return 0;  // Insufficient samples available.  Do nothing.
	return ring_read(&captureRing, input, (int) nSamples);
}

bool AudioInit = false;

void InitAudio(bool quiet) {
	ring_init(&captureRing);
	ring_init(&playbackRing);

	GetDevices();
	if (ZF_LOG_ON_VERBOSE && !quiet) {
		// LogDevices() uses ZF_LOGI(), but log the full set of audio devices
		// only if CONSOLELOG or LOGLEVEL is Verbose=1
		LogDevices(AudioDevices, "All audio devices", false, false);
	} else if (!quiet) {
		LogDevices(AudioDevices, "Capture (input) Devices", true, false);
		LogDevices(AudioDevices, "Playback (output) Devices", false, true);
	}
	AudioInit = true;
}

// Process any captured samples
// Ideally call at least every 100 mS, more than 200 will loose data
void PollReceivedSamples() {
	if (strcmp(CaptureDevice, "NOSOUND") == 0)
		return;

	if (SoundCardRead(&inbuffer[0][0], ReceiveSize) == 0)
		return;  // No samples to process

	if (Capturing) {
		ProcessNewSamples(&inbuffer[0][0], ReceiveSize);
	} else {
		// PreprocessNewSamples() writes these samples to the RX wav file when
		// specified, even though not Capturing.  This produces a time
		// continuous Wav file which can be useful for diagnostic purposes.
		// If Capturing, this is called from ProcessNewSamples().
		PreprocessNewSamples(&inbuffer[0][0], ReceiveSize);
	}
}


void StopCapture() {
	Capturing = false;
	return;
}

// Adds a trailer to the audio samples that have been staged with SendtoCard(),
// and then block until all of the staged audio has been played.  Before
// returning, it calls KeyPTT(false) and enables the Capturing state.
// return true on success and false on failure.  On failure, still set
// KeyPTT(false) and enables the Capturing state
bool SoundFlush() {
	// Append Trailer then send remaining samples
	// if AddTrailer() or SendtoCard() fail, TXEnabled will be set to false
	if (TXEnabled && AddTrailer() && SendtoCard(Number)) {
		ZF_LOGD("SoundFlush(): %d samples staged for playout.", SampleNo);
	} else {
		ZF_LOGW("SoundFlush() called when not TXEnabled. Ignoring.");
	}
	// PTT must not be dropped until all of the modulated audio has actually been
	// transmitted.  The output AudioQueue buffers audio beyond the ring, so wait
	// in two stages: first until the ring has been drained into the queue, then
	// until the audio hardware has played through the end of the last real
	// audio.
	if (strcmp(PlaybackDevice, "NOSOUND") != 0 && TXEnabled) {
		// 1. Wait until the output callback has pulled all staged audio out of
		//    the ring and into the AudioQueue.
		int waitcount = 0;
		while (ring_count(&playbackRing) > 0 && TXEnabled) {
			txSleep(5);
			if (++waitcount > 2000) {  // ~10 sec safety timeout
				ZF_LOGW("SoundFlush(): timeout draining playback ring.");
				break;
			}
		}
		// 2. Wait until the hardware play head reaches the end of the last real
		//    audio.  The one-buffer margin covers the small race between the
		//    ring emptying and the callback recording the final play position.
		pthread_mutex_lock(&playPosMutex);
		double target = lastRealOutFrame + AQ_FRAMES_PER_BUFFER;
		pthread_mutex_unlock(&playPosMutex);
		waitcount = 0;
		while (TXEnabled && playQueue != NULL) {
			AudioTimeStamp ts;
			if (AudioQueueGetCurrentTime(playQueue, NULL, &ts, NULL) != noErr
				|| !(ts.mFlags & kAudioTimeStampSampleTimeValid)
			) {
				// Queue play time unavailable; fall back to an elapsed-time
				// estimate based on when PTT was keyed.
				int txlenMs = SampleNo / 12 + 20;  // 12 kHz, 20 ms TXTAIL
				if (pttOnTime + txlenMs > Now)
					txSleep((pttOnTime + txlenMs) - Now);
				break;
			}
			if (ts.mSampleTime >= target)
				break;
			txSleep(5);
			if (++waitcount > 2000) {  // ~10 sec safety timeout
				ZF_LOGW("SoundFlush(): timeout waiting for audio playout.");
				break;
			}
		}
	}

	SoundIsPlaying = false;

	if (blnEnbARQRpt > 0 || blnDISCRepeating)  // Start Repeat Timer if frame should be repeated
		dttNextPlay = Now + intFrameRepeatInterval + extraDelay;

	KeyPTT(false);  // Unkey the Transmitter
	if (txwff != NULL) {
		CloseWav(txwff);
		txwff = NULL;
	}
	// writing unfiltered tx audio to WAV disabled
	// if (txwfu != NULL)
	// {
		// CloseWav(txwfu);
		// txwfu = NULL;
	// }

	StartCapture();

	if (WriteRxWav && !HWriteRxWav) {
		// Start recording if not already recording, else extend the recording time.
		// Note that this is disabled if HWriteRxWav is true.
		StartRxWav();
	}
	return TXEnabled;
}

// Return true if OpenSoundCapture("RESTORE") might succeed, else false
bool crestorable() {
	return LastGoodCaptureDevice[0] != 0x00;
}

// Return true if OpenSoundPlayback("RESTORE") might succeed, else false
bool prestorable() {
	return LastGoodPlaybackDevice[0] != 0x00;
}
