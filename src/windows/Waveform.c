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

int m_playchannels = 1;  // 1 for mono, 2 for stereo
int m_recchannels = 1;  // 1 for mono, 2 for stereo

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

// Single channel.
// For receive, if a stereo device is opened this way, it uses the left chennal.
// For transmit, if a stereo device is opened this way, it sends the audio to both channels.
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


// Open stereo device, but use only one channel.
// For receive, this allows use of the right channel, by discarding even
// numbered audio samples.
// For transmit, this allows sending audio to either left or right, by
// setting either odd or even numbered samples to zero.
WAVEFORMATEX wfxs = {
	WAVE_FORMAT_PCM,  // wFormatTag (Format type)
	2,  // nChannels
	12000,  // nSamplesPerSec
	24000,  // nAvgBytesPerSec (nSamplesPerSec * nBlockAlign)
	4,  // nBlockAlign (nChannels * wBitsPerSample / 8)
	16,  // wBitsPerSample
	0  // cbSize (extra format info.  0 for WAVE_FORMAT_PCM)
};


short stereobuffer[2][SendSize * 2];  // Two buffers of 0.1 sec duration for TX audio
// TODO: Explore whether 5 inbuffer are really required.  ALSA uses only 2.
short stereoinbuffer[5][ReceiveSize * 4];  // Two buffers of 0.1 Sec duration for RX audio.
WAVEHDR stereoheader[2] =
{
	{(char *)stereobuffer[0], 0, 0, 0, 0, 0, 0, 0},
	{(char *)stereobuffer[1], 0, 0, 0, 0, 0, 0, 0}
};
WAVEHDR stereoinheader[5] =
{
	{(char *)stereoinbuffer[0], 0, 0, 0, 0, 0, 0, 0},
	{(char *)stereoinbuffer[1], 0, 0, 0, 0, 0, 0, 0},
	{(char *)stereoinbuffer[2], 0, 0, 0, 0, 0, 0, 0},
	{(char *)stereoinbuffer[3], 0, 0, 0, 0, 0, 0, 0},
	{(char *)stereoinbuffer[4], 0, 0, 0, 0, 0, 0, 0}
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

bool OpenSoundPlayback(int devindex) {
	int ret;

	if (devindex == -1)  // OK.  Special NOSOUND device
		return true;
	if (devindex < -1 || devindex >= PlaybackDevicesCount) {
		ZF_LOGE("Invalid device index %i in OpenSoundPlayback.", devindex);
		return false;
	}

	header[0].dwFlags = WHDR_DONE;
	header[1].dwFlags = WHDR_DONE;
	stereoheader[0].dwFlags = WHDR_DONE;
	stereoheader[1].dwFlags = WHDR_DONE;

	if (m_playchannels == 1) {
		// For transmit, if a stereo device is opened this way, it sends the
		// audio to both channels.
		ret = waveOutOpen(&hWaveOut, devindex, &wfx, 0, 0, CALLBACK_NULL);
		if (ret) {
			// All of the stereo devices that I've tested can be successfully
			// opened as mono.  However, this allows for the possibility that
			// some devices (or their drivers) don't accomodate this.
			ZF_LOGE("Neither the -y nor -z command line option was used to indicate"
				" that the specified audio playback device (%s) should be opened"
				" as a stereo device, and to select which of its channels should be"
				" used.  Thus, the device was opened as a single channel (mono)"
				" audio device, but this failed.  This probably means that the"
				" device can only be opened as a two channel (stereo) device.  So,"
				" please try again using either the -y or -z command line option to"
				" indicate which channel to use.",
				PlaybackDevices[devindex]);
			return false;
		}
	} else {
		// m_playchannels == 2
		// For transmit, this allows sending audio to either left or right, by
		// setting either odd or even numbered samples to zero.
		ret = waveOutOpen(&hWaveOut, devindex, &wfxs, 0, 0, CALLBACK_NULL);
		if (ret) {
			// All of the mono devices that I've tested can be successfully
			// opened as stereo.  However, this allows for the possibility that
			// some devices (or their drivers) don't accomodate this.
			ZF_LOGE("The -%s command line option was used to indicate that the"
				" %s channel of a stereo audio playback device should be used."
				" However, the audio playback device specified (%s) could not be"
				" opened as a two channel (stereo) device.  Try again without"
				" the -%s command line option to open it as a single channel"
				" (mono) device.",
				UseLeftTX ? "y" : "z",
				UseLeftTX ? "left" : "right",
				PlaybackDevices[devindex],
				UseLeftTX ? "y" : "z");
			return false;
		}
	}
	return true;
}

bool OpenSoundCapture(int devindex) {
	int ret;

	if (devindex == -1)  // OK.  Special NOSOUND device
		return true;
	if (devindex < -1 || devindex >= CaptureDevicesCount) {
		ZF_LOGE("Invalid device index %i in OpenSoundCapture.", devindex);
		return false;
	}

	if (m_recchannels == 1) {
		// Open single channel audio capture and prepare for use
		ret = waveInOpen(&hWaveIn, devindex, &wfx, 0, 0, CALLBACK_NULL);
		if (ret) {
			// All of the stereo devices that I've tested can be successfully
			// opened as mono.  However, this allows for the possibility that
			// some devices (or their drivers) don't accomodate this.
			ZF_LOGE("Neither the -L nor -R command line option was used to indicate"
				" that the specified audio capture device (%s) should be opened"
				" as a stereo device, and to select which of its channels should be"
				" used.  Thus, the device was opened as a single channel (mono)"
				" audio device, but this failed.  This probably means that the"
				" device can only be opened as a two channel (stereo) device.  So,"
				" please try again using either the -L or -R command line option to"
				" indicate which channel to use.",
				CaptureDevices[devindex]);
			return false;
		}
		for (int i = 0; i < NumberofinBuffers; ++i) {
			inheader[i].dwBufferLength = ReceiveSize * 2;  // 2 bytes per sample
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
	} else { // m_recchannels == 2
		// Open two channel (stereo) audio capture and prepare for use
		ret = waveInOpen(&hWaveIn, devindex, &wfxs, 0, 0, CALLBACK_NULL);
		if (ret) {
			// All of the mono devices that I've tested can be successfully
			// opened as stereo.  However, this allows for the possibility that
			// some devices (or their drivers) don't accomodate this.
			ZF_LOGE("The -%s command line option was used to indicate that the"
				" %s channel of a stereo audio capture device should be used."
				" However, the audio capture device specified (%s) could not be"
				" opened as a two channel (stereo) device.  Try again without"
				" the -%s command line option to open it as a single channel"
				" (mono) device.",
				UseLeftRX ? "L" : "R",
				UseLeftRX ? "left" : "right",
				CaptureDevices[devindex],
				UseLeftRX ? "L" : "R");
			return false;
		}
		for (int i = 0; i < NumberofinBuffers; ++i) {
			stereoinheader[i].dwBufferLength = ReceiveSize * 2 * 2;  // 2 bytes per sample, 2 channels
			ret = waveInPrepareHeader(hWaveIn, &stereoinheader[i], sizeof(WAVEHDR));
			if (ret) {
				ZF_LOGF("Failure of waveInPrepareHeader() %i: Error %d", i, ret);
				waveInClose(hWaveIn);
				return false;
			}
			ret = waveInAddBuffer(hWaveIn, &stereoinheader[i], sizeof(WAVEHDR));
			if (ret) {
				ZF_LOGF("Failure of waveInAddBuffer() %i: Error %d", i, ret);
				waveInClose(hWaveIn);
				return false;
			}
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
	char * endptr;

	int idevindex = strtol(CaptureDevice, &endptr, 10);
	if (strcmp(CaptureDevice, "NOSOUND") == 0) {
		idevindex = -1;  // special CaptureDevice
	} else if (*endptr != '\0') {
		// CaptureDevice is not a number. Look for a substring match of
		// CaptureDevice in CaptureDevices[]
		idevindex = -2;  // indicate no match found
		for (int i = 0; i < CaptureDevicesCount; ++i) {
			if (strcasestr(CaptureDevices[i], CaptureDevice)) {
				idevindex = i;
				break;
			}
		}
	}
	if (idevindex < -1 || idevindex >= CaptureDevicesCount) {
		ZF_LOGE("ERROR: CaptureDevice = \"%s\" not found.  Try using one of the"
			" names or numbers (0-%d) listed above.",
			CaptureDevice,
			CaptureDevicesCount - 1);
		return false;
	}

	int odevindex = strtol(PlaybackDevice, &endptr, 10);
	if (strcmp(PlaybackDevice, "NOSOUND") == 0) {
		odevindex = -1;  // special PlaybackDevice
	} else if (*endptr != '\0') {
		// PlaybackDevice is not a number. Look for a substring match of
		// PlaybackDevice in PlaybackDevices[]
		odevindex = -2;  // indicate no match found
		for (int i = 0; i < PlaybackDevicesCount; ++i) {
			if (strcasestr(PlaybackDevices[i], PlaybackDevice)) {
				odevindex = i;
				break;
			}
		}
	}
	if (odevindex < -1 || odevindex >= PlaybackDevicesCount) {
		ZF_LOGE("ERROR: PlaybackDevice = \"%s\" not found.  Try using one of the"
			" names or numbers (0-%d) listed above.",
			PlaybackDevice,
			PlaybackDevicesCount - 1);
		return false;
	}

	if (UseLeftRX == 1 && UseRightRX == 1) {
		m_recchannels = 1;
		ZF_LOGI("Opening %s for RX as a single channel (mono) device",
			idevindex == -1 ? "NOSOUND" : CaptureDevices[idevindex]);
	} else {
		m_recchannels = 2;
		if (UseLeftRX == 0)
			ZF_LOGI("Opening %s for RX as a stereo device and using Right channel",
				idevindex == -1 ? "NOSOUND" : CaptureDevices[idevindex]);
		if (UseRightRX == 0)
			ZF_LOGI("Opening %s for RX as a stereo device and using Left channel",
				idevindex == -1 ? "NOSOUND" : CaptureDevices[idevindex]);
	}

	if (UseLeftTX == 1 && UseRightTX == 1) {
		m_playchannels = 1;
		ZF_LOGI("Opening %s for TX as a single channel (mono) device",
			odevindex == -1 ? "NOSOUND" : PlaybackDevices[odevindex]);
	} else {
		m_playchannels = 2;
		if (UseLeftTX == 0)
			ZF_LOGI("Opening %s for TX as a stereo device and using Right channel",
				odevindex == -1 ? "NOSOUND" : PlaybackDevices[odevindex]);
		if (UseRightTX == 0)
			ZF_LOGI("Opening %s for TX as a stereo device and using Left channel",
				odevindex == -1 ? "NOSOUND" : PlaybackDevices[odevindex]);
	}

	if (!OpenSoundPlayback(odevindex))
		return false;
	if (!OpenSoundCapture(idevindex))
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

	if (m_playchannels == 2) {
		// fill buffer with zeros to ensure silence on unused channel
		memset(stereobuffer[Index], 0x00, SendSize * 2 * 2);

		int j = UseLeftTX ? 0 : 1;  // Select use of Left or Right channel
		for (int i=0; i < n; ++i, j += 2)
			stereobuffer[Index][j] = buffer[Index][i];
		stereoheader[Index].dwBufferLength = n * 2 * 2;  // 2 bytes per sample, 2 channels
		waveOutPrepareHeader(hWaveOut, &stereoheader[Index], sizeof(WAVEHDR));
		waveOutWrite(hWaveOut, &stereoheader[Index], sizeof(WAVEHDR));

		// wait till previous buffer is complete
		while (!(stereoheader[!Index].dwFlags & WHDR_DONE))
			txSleep(10);  // Run background while waiting
		waveOutUnprepareHeader(hWaveOut, &stereoheader[!Index], sizeof(WAVEHDR));
	} else {
		header[Index].dwBufferLength = n * 2;
		waveOutPrepareHeader(hWaveOut, &header[Index], sizeof(WAVEHDR));
		waveOutWrite(hWaveOut, &header[Index], sizeof(WAVEHDR));

		// wait till previous buffer is complete
		while (!(header[!Index].dwFlags & WHDR_DONE))
			txSleep(10);  // Run background while waiting
		waveOutUnprepareHeader(hWaveOut, &header[!Index], sizeof(WAVEHDR));
	}
	Index = !Index;  // toggle Index between 0 and 1
	return &buffer[Index][0];  // even when using stereo, return mono buffer.
}


void PollReceivedSamples() {
	if (strcmp(PlaybackDevice, "NOSOUND") == 0)
		return;
	// Process any captured samples
	// Ideally call at least every 100 mS, more than 200 will loose data
	if (m_recchannels == 2) {
		if (stereoinheader[inIndex].dwFlags & WHDR_DONE) {
			// Copy samples from the user specified channel into the corresponding
			// inbuffer[inIndex] so that it can be passed to ProcessNewSamples(),
			// which expects a mono signal.
			if (Capturing) {
				int j = UseLeftRX ? 0 : 1;  // Select use of Left or Right channel
				for (int i = 0; i < ReceiveSize; ++i, j += 2)
					inbuffer[inIndex][i] = stereoinbuffer[inIndex][j];
				ProcessNewSamples(&inbuffer[inIndex][0],
					stereoinheader[inIndex].dwBytesRecorded / 4);
			}
			waveInPrepareHeader(hWaveIn, &stereoinheader[inIndex], sizeof(WAVEHDR));
			waveInAddBuffer(hWaveIn, &stereoinheader[inIndex], sizeof(WAVEHDR));
			inIndex++;
			if (inIndex == NumberofinBuffers)
				inIndex = 0;
		}
		return;
	}
	// m_recchannels == 2 // Mono
	if (inheader[inIndex].dwFlags & WHDR_DONE) {
		if (Capturing) {
			ProcessNewSamples(&inbuffer[inIndex][0],
				inheader[inIndex].dwBytesRecorded / 2);
		}
		waveInUnprepareHeader(hWaveIn, &inheader[inIndex], sizeof(WAVEHDR));
		inheader[inIndex].dwFlags = 0;
		waveInPrepareHeader(hWaveIn, &inheader[inIndex], sizeof(WAVEHDR));
		waveInAddBuffer(hWaveIn, &inheader[inIndex], sizeof(WAVEHDR));
		inIndex++;
		if (inIndex == NumberofinBuffers)
			inIndex = 0;
	}
	return;
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
		if (m_playchannels == 2) {
			while (!(stereoheader[0].dwFlags & WHDR_DONE))
				txSleep(10);
			while (!(stereoheader[1].dwFlags & WHDR_DONE))
				txSleep(10);
		} else {
			while (!(header[0].dwFlags & WHDR_DONE))
				txSleep(10);
			while (!(header[1].dwFlags & WHDR_DONE))
				txSleep(10);
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
