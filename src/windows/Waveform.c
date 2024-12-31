// Waveform audio, a legacy audio system for Windows

#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>
#include <mmsystem.h>
#include <stdbool.h>
#include <string.h>

#include "common/os_util.h"
#include "windows/Waveform.h"
#include "common/ardopcommon.h"
#include "common/wav.h"

#pragma comment(lib, "winmm.lib")

extern bool UseLeftRX;
extern bool UseRightRX;

extern bool UseLeftTX;
extern bool UseRightTX;

extern char CaptureDevice[80];
extern char PlaybackDevice[80];

extern struct WavFile *txwff;  // For recording of filtered TX audio

extern bool WriteRxWav;  // Record RX controlled by Command line/TX/Timer
extern bool HWriteRxWav;  // Record RX controlled by host command RECRX

// extern struct WavFile *txwfu;  // For recording of unfiltered TX audio

void txSleep(int mS);
int wg_send_pixels(int cnum, unsigned char *data, size_t datalen);
void KeyPTT(bool state);
void StartRxWav();

// TX and RX audio are signed short integers: +- 32767
extern int SampleNo;  // Total number of samples for this transmission.
extern int Number;  // Number of samples waiting to be sent

short buffer[2][SendSize];  // Two buffers of 0.1 sec duration for TX audio.
// TODO: Explore whether 5 inbuffer are really required.  ALSA uses only 2.
short inbuffer[5][ReceiveSize];  // Two buffers of 0.1 Sec duration for RX audio.
int Index = 0;  // buffer being used 0 or 1
int inIndex = 0;  // inbuffer being used 0 or 1

int CaptureIndex = -1;  // Card number
int PlayBackIndex = -1;

// This opens a single audio channel.  Command line options to use
// the left or right channel of a stereo device are currently ignored.
// TODO: For a stereo device, correctly handle user selection of left
//   or right channel.
WAVEFORMATEX wfx = {
	WAVE_FORMAT_PCM,  // wFormatTag (Format type)
	1,  // nChannels
	12000,  // nSamplesPerSec
	24000,  // nAvgBytesPerSec (nSamplesPerSec * nBlockAlign)
	2,  // nBlockAlign (nChannels * wBitsPerSample / 8)
	16,  // wBitsPerSample
	0  // cbSize (extra format info.  0 for WAVE_FORMAT_PCM)
};


HWAVEOUT hWaveOut = 0;
HWAVEIN hWaveIn = 0;

WAVEHDR header[2] = {
	{(char *)buffer[0], 0, 0, 0, 0, 0, 0, 0},
	{(char *)buffer[1], 0, 0, 0, 0, 0, 0, 0}
};

// TODO: See note about 5 inbuffer compared to 2 in ALSA.
WAVEHDR inheader[5] = {
	{(char *)inbuffer[0], 0, 0, 0, 0, 0, 0, 0},
	{(char *)inbuffer[1], 0, 0, 0, 0, 0, 0, 0},
	{(char *)inbuffer[2], 0, 0, 0, 0, 0, 0, 0},
	{(char *)inbuffer[3], 0, 0, 0, 0, 0, 0, 0},
	{(char *)inbuffer[4], 0, 0, 0, 0, 0, 0, 0}
};

WAVEOUTCAPS pwoc;
WAVEINCAPS pwic;

char **PlaybackDevices;
int PlaybackDevicesCount;
char **CaptureDevices;
int CaptureDevicesCount;


void GetSoundDevices() {
	unsigned int i;

	ZF_LOGI("Capture Devices");
	// free old set of devices if called again
	if (CaptureDevices != NULL) {
		for (int i = 0; i < CaptureDevicesCount; ++i)
			free(CaptureDevices[i]);
		free(CaptureDevices);
	}
	CaptureDevicesCount = 0;
	for (i = 0; i < waveInGetNumDevs(); ++i) {
		waveInGetDevCaps(i, &pwic, sizeof(WAVEINCAPS));

		CaptureDevices = realloc(CaptureDevices,
			(CaptureDevicesCount + 1) * sizeof(char *));
		CaptureDevices[CaptureDevicesCount] = malloc(strlen(pwic.szPname) + 16);
		snprintf(CaptureDevices[CaptureDevicesCount], strlen(pwic.szPname) + 15,
			"%i %s %i-channel", i, pwic.szPname, pwic.wChannels);
		ZF_LOGI("%s", CaptureDevices[CaptureDevicesCount++]);
	}

	ZF_LOGI("Playback Devices");
	// free old set of devices if called again
	if (PlaybackDevices != NULL) {
		for (int i = 0; i < PlaybackDevicesCount; ++i)
			free(PlaybackDevices[i]);
		free(PlaybackDevices);
	}
	PlaybackDevicesCount = 0;

	for (i = 0; i < waveOutGetNumDevs(); ++i) {
		waveOutGetDevCaps(i, &pwoc, sizeof(WAVEOUTCAPS));

		PlaybackDevices = realloc(PlaybackDevices,
			(PlaybackDevicesCount + 1) * sizeof(char *));
		PlaybackDevices[PlaybackDevicesCount] = malloc(strlen(pwoc.szPname) + 16);
		snprintf(PlaybackDevices[PlaybackDevicesCount], strlen(pwoc.szPname) + 15,
			"%i %s %i-channel", i, pwoc.szPname, pwoc.wChannels);
		ZF_LOGI("%s", PlaybackDevices[PlaybackDevicesCount++]);
	}
}

// Windows doesn't have strcasestr, so create it
char *strcasestr(const char *haystack, const char *needle) {
	for (size_t i = 0; i < strlen(haystack) - strlen(needle); ++i) {
		size_t j = 0;
		for (; j < strlen(needle); ++j) {
			if (tolower(haystack[i + j]) != tolower(needle[j]))
				break;
		}
		if (j == strlen(needle))
			return (char *) (haystack + i);
	}
	return NULL;
}

bool OpenSoundPlayback(char * devstr) {
	int ret;
	int devindex = -1;

	if (strcmp(devstr, "NOSOUND") == 0)
		return true;

	header[0].dwFlags = WHDR_DONE;
	header[1].dwFlags = WHDR_DONE;

	if (strlen(devstr) <= 2) {
		// devstr is the integer index of a device in PlaybackDevices
		devindex = atoi(devstr);
	} else {
		// Name instead of number. Look for a substring match in PlaybackDevices
		for (int i = 0; i < PlaybackDevicesCount; ++i) {
			if (strcasestr(PlaybackDevices[i], devstr)) {
				devindex = i;
				break;
			}
		}
	}
	if (devindex == -1) {
		ZF_LOGE("ERROR: playbackdevice = '%s' not found.  Try using one of the"
			" names or numbers (0-%d) listed above.",
			devstr,
			PlaybackDevicesCount - 1
		);
		return false;
	}

	// As noted above, currently this opens a single default channel.
	// TODO: If this is a stereo device, choose correct channel (probably
	//   by opening as stereo device, and then setting only half the
	//   samples.)
	ret = waveOutOpen(&hWaveOut, devindex, &wfx, 0, 0, CALLBACK_NULL);
	if (ret) {
		ZF_LOGF("Failed to open WaveOut Device %s Error %d", devstr, ret);
		return false;
	}
	ret = waveOutGetDevCaps((UINT_PTR)hWaveOut, &pwoc, sizeof(WAVEOUTCAPS));
	ZF_LOGI("Opened WaveOut Device %s", pwoc.szPname);
	return true;
}

bool OpenSoundCapture(char * devstr) {
	int ret;
	int devindex = -1;

	if (strcmp(devstr, "NOSOUND") == 0)
		return true;

	if (strlen(devstr) <= 2) {
		// devstr is the integer index of a device in CaptureDevices
		devindex = atoi(devstr);
	} else {
		// Name instead of number. Look for a substring match
		for (int i = 0; i < CaptureDevicesCount; ++i) {
			if (strcasestr(CaptureDevices[i], devstr)) {
				devindex = i;
				break;
			}
		}
	}
	if (devindex == -1) {
		ZF_LOGE("ERROR: capturedevice = '%s' not found.  Try using one of the"
			" names or numbers (0-%d) listed above.",
			devstr,
			CaptureDevicesCount - 1);
		return false;
	}

	// As noted above, currently this opens a single default channel.
	// TODO: If this is a stereo device, choose correct channel (probably
	//   by opening as stereo device, and then discarding half the samples.)
	ret = waveInOpen(&hWaveIn, devindex, &wfx, 0, 0, CALLBACK_NULL);  // WAVE_MAPPER
	if (ret) {
		ZF_LOGF("Failed to open WaveIn Device %s Error %d", devstr, ret);
		return false;
	} else {
		waveInGetDevCaps((UINT_PTR)hWaveIn, &pwic, sizeof(WAVEINCAPS));
		ZF_LOGI("Opened WaveIn Device %s", pwic.szPname);
	}

	for (int i = 0; i < NumberofinBuffers; ++i) {
		inheader[i].dwBufferLength = ReceiveSize * 2;
		ret = waveInPrepareHeader(hWaveIn, &inheader[i], sizeof(WAVEHDR));
		if (ret) {
			ZF_LOGF("Failure of waveInPrepareHeader() %i: Error %d", i, ret);
			waveInClose(hWaveIn);
			return false;
		}
		ret = waveInAddBuffer(hWaveIn, &inheader[i], sizeof(WAVEHDR));
		if (ret) {
			ZF_LOGF("Failure of waveInAddBuffer() %i: Error %d", i, ret);
			waveInClose(hWaveIn);
			return false;
		}
	}
	ret = waveInStart(hWaveIn);
	if (ret) {
		ZF_LOGF("Failure of waveInStart(): Error %d", ret);
		waveInClose(hWaveIn);
		return false;
	}
	return true;
}

bool InitSound() {
	GetSoundDevices();
	if (!OpenSoundPlayback(PlaybackDevice))
		return false;
	if (!OpenSoundCapture(CaptureDevice))
		return false;
	return true;
}


short * SendtoCard(int n) {
	if (txwff != NULL)
		WriteWav(&buffer[Index][0], n, txwff);

	if (strcmp(PlaybackDevice, "NOSOUND") == 0) {
		Index = !Index;  // toggle Index between 0 and 1
		return &buffer[Index][0];
	}

	header[Index].dwBufferLength = n * 2;
	waveOutPrepareHeader(hWaveOut, &header[Index], sizeof(WAVEHDR));
	waveOutWrite(hWaveOut, &header[Index], sizeof(WAVEHDR));

	// wait till previous buffer is complete
	while (!(header[!Index].dwFlags & WHDR_DONE)) {
		txSleep(10);  // Run background while waiting
	}

	waveOutUnprepareHeader(hWaveOut, &header[!Index], sizeof(WAVEHDR));
	Index = !Index;  // toggle Index between 0 and 1
	return &buffer[Index][0];
}


void PollReceivedSamples() {
	if (strcmp(PlaybackDevice, "NOSOUND") == 0)
		return;
	// Process any captured samples
	// Ideally call at least every 100 mS, more than 200 will loose data
	if (inheader[inIndex].dwFlags & WHDR_DONE) {
		// TODO: To extract one channel from a stereo system, do so here.

//		ZF_LOGD("Process %d %d", inIndex, inheader[inIndex].dwBytesRecorded/2);
		if (Capturing)
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

void StopCapture() {
	Capturing = false;
}

void StartCapture() {
	Capturing = true;
	DiscardOldSamples();
	ClearAllMixedSamples();
	State = SearchingForLeader;
}
void CloseSound() {
	waveInClose(hWaveIn);
	waveOutClose(hWaveOut);
}


void SoundFlush() {
	// Append Trailer then wait for TX to complete
	AddTrailer();  // add the trailer.

	SendtoCard(Number);

	// Wait for all sound output to complete
	if (strcmp(PlaybackDevice, "NOSOUND") != 0) {
		while (!(header[0].dwFlags & WHDR_DONE))
			txSleep(10);
		while (!(header[1].dwFlags & WHDR_DONE))
			txSleep(10);
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
	return;
}

// TODO: Why does this return an array of UNSIGNED shorts when the sound samples.
// to be sent to device are signed?   Review use in Modulate.c
// TODO: Consider renaming this function
// get a buffer to fill to start transmitting.
unsigned short * SoundInit() {
	Index = 0;
	return (unsigned short *) &buffer[0][0];
}

// TODO: Enable CODEC host command to start/stop audio processing
// int StartCodec() {
//	OpenSoundCard(CaptureDevice, PlaybackDevice, 12000, 12000);
//}
//
//int StopCodec() {
//	CloseSoundCard();
//}
