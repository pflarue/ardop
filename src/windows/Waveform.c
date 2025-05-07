// Waveform audio, a legacy audio system for Windows

#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>
#include <mmsystem.h>
#include <stdbool.h>
#include <string.h>

#include "common/os_util.h"
#include "common/audio.h"
#include "common/ardopcommon.h"
#include "common/wav.h"
#include "common/ptt.h"
#include "common/Webgui.h"

#pragma comment(lib, "winmm.lib")

// Use LastGoodCaptureDevice and LastGoodPlaybackDevice to store the names of
// the last audio devices that were successfully opened.  These are used by
// OpenSoundCapture() and OpenSoundPlayback() if the special value of "RESTORE"
// is used.  This is used by the RESTORE option of the CAPTURE and PLAYBACK host
// commands as well as RXENABLED TRUE, TXENABLED TRUE, and CODEC TRUE.
// See also LastGoodCATstr and and LastGoodPTTstr defined in ptt.c
char LastGoodCaptureDevice[DEVSTRSZ] = "";  // same size as CaptureDevice
char LastGoodPlaybackDevice[DEVSTRSZ] = "";  // same size as PlaybackDevice

extern struct WavFile *txwff;  // For recording of filtered TX audio
// extern struct WavFile *txwfu;  // For recording of unfiltered TX audio
extern bool WriteRxWav;  // Record RX controlled by Command line/TX/Timer
extern bool HWriteRxWav;  // Record RX controlled by host command RECRX

void StartRxWav();

// TX and RX audio are signed short integers: +- 32767
extern int SampleNo;  // Total number of samples for this transmission.
extern int Number;  // Number of samples waiting to be sent

// txbuffer and TxIndex are globals shared with Modulate.c via audio.h
// Two buffers of 0.1 sec duration for TX audio.
short txbuffer[2][SendSize];
// index to next txbuffer to be filled..
int TxIndex = 0;

// TODO: Explore whether 5 inbuffer are really required.  ALSA uses only 2.
short inbuffer[5][ReceiveSize];  // Two buffers of 0.1 Sec duration for RX audio.
int inIndex = 0;  // inbuffer being used 0 or 1
unsigned int lastRXnow = 0;

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
	{(char *)txbuffer[0], 0, 0, 0, 0, 0, 0, 0},
	{(char *)txbuffer[1], 0, 0, 0, 0, 0, 0, 0}
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

int nderrcnt = 0;

WAVEOUTCAPS pwoc;
WAVEINCAPS pwic;

char **PlaybackDevices;
int PlaybackDevicesCount;
char **CaptureDevices;
int CaptureDevicesCount;


// dev->name and dev->alias shall be truncated to DEVSTRSZ bytes including a
// terminating null.
void GetDevices() {
	unsigned int i;
	int devindex;
	DeviceInfo *dev;

	// Clean up previous results
	FreeDevices(&AudioDevices);
	InitDevices(&AudioDevices);

	// Unlike Linux (ALSA), windows audio devices have rather long names that
	// include spaces.  These are more like the descriptions provided for Linux
	// (ALSA) devices.  Because of this, and in accordance with the
	// waveOutOpen() and waveInOpen() commands, windows audio devices were
	// typically specified by their index numbers.  There are two problems with
	// this.  First, these index numbers are unique to either capture or
	// playback.  Index i for capture does not necessarily represent the same
	// hardware as index i for playback, and each has a unique name/description.
	// Second, the relationship between these numbers and the name/description
	// and hardware associated with them can change as OS audio settings are
	// changed.  So:
	// Short names consisting of 'i' for input/capture and 'o' for
	// output/playback followed by the index number will be used/listed.
	// However if a name that does not match one of these is given, the first
	// device in AudioDevices whose description (the Windows device name)
	// contains the specified name as a substring will be used.

	// Windows does not use dev->alias.

	CaptureDevicesCount = 0;
	for (i = 0; i < waveInGetNumDevs(); ++i) {
		waveInGetDevCaps(i, &pwic, sizeof(WAVEINCAPS));
		devindex = ExtendDevices(&AudioDevices);
		dev = AudioDevices[devindex];
		dev->name = strdup("i999");  // Accomodate a 3-digit index.
		sprintf(dev->name + 1, "%i", i);
		dev->desc = strdup(pwic.szPname);
		dev->capture = true;
	}
	for (i = 0; i < waveOutGetNumDevs(); ++i) {
		waveOutGetDevCaps(i, &pwoc, sizeof(WAVEOUTCAPS));
		devindex = ExtendDevices(&AudioDevices);
		dev = AudioDevices[devindex];
		dev->name = strdup("o999");  // Accomodate a 3-digit index.
		sprintf(dev->name + 1, "%i", i);
		dev->desc = strdup(pwoc.szPname);
		dev->playback = true;
	}
	// Always include NOSOUND as the last device suitable for both capture
	// and playback.  Thus, AudioDevices[] is never completely empty.
	devindex = ExtendDevices(&AudioDevices);
	dev = AudioDevices[devindex];
	dev->name = strdup("NOSOUND");
	dev->desc = strdup("A dummy audio device for diagnostic use.");
	dev->capture = true;
	dev->playback = true;
}


void StartCapture() {
	Capturing = true;
	DiscardOldSamples();
	ClearAllMixedSamples();
	State = SearchingForLeader;
}

bool AudioInit = false;

void InitAudio(bool quiet) {
	GetDevices();
	if (ZF_LOG_ON_VERBOSE && !quiet) {
		// LogDevices() uses ZF_LOGI(), but log the full set of audio devices
		// only if CONSOLELOG or LOGLEVEL is Verbose=1
		LogDevices(AudioDevices, "All audio devices", false, false);
	} else if (!quiet){
		LogDevices(AudioDevices, "Capture (input) Devices", true, false);
		LogDevices(AudioDevices, "Playback (output) Devices", false, true);
	}
	AudioInit = true;
}

// This lower level function is called by OpenSoundPlayback using an integer
// devindex derived from a user provided devstr.  devstr is included here
// only for use in error messages
bool open_audio_playback(int devindex, int ch, char *devstr) {
	int ret;
	header[0].dwFlags = WHDR_DONE;
	header[1].dwFlags = WHDR_DONE;
	stereoheader[0].dwFlags = WHDR_DONE;
	stereoheader[1].dwFlags = WHDR_DONE;

	if (ch == 1) {
		// For transmit, if a stereo device is opened this way, it sends the
		// audio to both channels.
		ret = waveOutOpen(&hWaveOut, devindex, &wfx, 0, 0, CALLBACK_NULL);
		if (ret) {
			if (ret == WAVERR_BADFORMAT) {
				// All of the stereo devices that I've tested can be successfully
				// opened as mono.  However, this allows for the possibility that
				// some devices (or their drivers) don't accomodate this.
				ZF_LOGW("Neither the -y nor -z command line option was used to indicate"
					" that the specified audio playback device (%s) should be opened"
					" as a stereo device, and to select which of its channels should be"
					" used.  Thus, the device was opened as a single channel (mono)"
					" audio device, but this failed.  This probably means that the"
					" device can only be opened as a two channel (stereo) device.  So,"
					" please try again using either the -y or -z command line option to"
					" indicate which channel to use.",
					devstr);
				return false;
			}
			if (ret == MMSYSERR_NODRIVER) {
				// Intermittantly, when attempting to open device immediately after
				// connecting (or reconnecting it), this error (No driver) is produces.
				// I don't know why, but waiting briefly and trying again sometimes
				// fixes it.
				if (nderrcnt++ < 3) {
					ZF_LOGE("MMSYSERR_NODRIVER attemping to open playback device %s. "
						" retry %i.", devstr, nderrcnt);
					txSleep(10);
					return open_audio_playback(devindex, ch, devstr);
				}
				ZF_LOGE("nderrcnt %i", nderrcnt);
				return false;
			}
			ZF_LOGE("ERROR %i opening playback device %s.", ret, devstr);
			return false;
		}
		return true;
	} else if (ch == 2) {
		// For transmit, this allows sending audio to either left or right, by
		// setting either odd or even numbered samples to zero.
		ret = waveOutOpen(&hWaveOut, devindex, &wfxs, 0, 0, CALLBACK_NULL);
		if (ret) {
			if (ret == WAVERR_BADFORMAT) {
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
					devstr,
					UseLeftTX ? "y" : "z");
				return false;
			}
			if (ret == MMSYSERR_NODRIVER) {
				// Intermittantly, when attempting to open device immediately after
				// connecting (or reconnecting it), this error (No driver) is produces.
				// I don't know why, but waiting briefly and trying again sometimes
				// fixes it.
				if (nderrcnt++ < 3) {
					ZF_LOGE("MMSYSERR_NODRIVER attemping to open playback device %s. "
						" retry %i.", devstr, nderrcnt);
					txSleep(10);
					return open_audio_playback(devindex, ch, devstr);
				}
				ZF_LOGE("nderrcnt %i", nderrcnt);
				return false;
			}
			ZF_LOGE("ERROR %i opening playback device %s.", ret, devstr);
			return false;
		}
		return true;
	}
	ZF_LOGE("Invalid ch=%i in open_audio_playback.", ch);
	return false;
}

// Close the playback audio device if one is open and set TXEnabled=false.
// do_getdevices is passed to updateWebGuiAudioConfig()
void CloseSoundPlayback(bool do_getdevices) {
	if (hWaveOut != 0) {
		// If audio is playing, waveOutClose() will fail.  So, call waveOutReset()
		// first to prevent this from happending.
		waveOutReset(hWaveOut);
		waveOutClose(hWaveOut);
		hWaveOut = 0;
	}
	PlaybackDevice[0] = 0x00;  // empty string
	SoundIsPlaying = false;
	TXEnabled = false;
	KeyPTT(false);  // In case PTT is engaged.
	updateWebGuiAudioConfig(do_getdevices);
}

// If devstr matches PlaybackDevice and ch matches Pch and TXEnbled is true,
// then do nothing but write a debug log message.  Otherwise, if TXEnabled is
// true, then CloseSoundPlayback() is called before attempting to open
// devstr.  devstr is first checked for an exact match to the name of a
// playback audio device.  If that fails, the first case insensitive substring
// match in the descriptions of playback audio devices is used.
// If no match is found, return false with TXEnabled=false and PlaybackDevice
// set to an empty string.  On success, return true with TXEnabled=true and
// PlaybackDevice set to devstr or altdevstr, which applies OS-specific rules
// to convert a numerical devstr into something that can be found by
// FindAudioDevices().  devstr/altdevstr, rather than the name found by
// FindAudioDevice() is used because in some instances, the device name may
// change while the description will be the same after that device has been
// unplugged and plugged back in.  So, in that case, more consistent behavior is
// achieved when later using "RESTORE" by using a devstr/altdevstr that will
// match the description, and retaining this devstr as PlaybackDevice and
// LastGoodPlaybackDevice.
// If devstr is the special device name "RESTORE", do nothing if TXEnabled is
// true.  However, if TXEnabled is false, but LastGoodPlaybackDevice is not an
// empty string (because some Playback device has previously been successfully
// opened), then try to reopen that device.
// If devstr is an empty string "", then close the existing playback device if
// one is open, and then always return false.
bool OpenSoundPlayback(char *devstr, int ch) {
	int aindex;  // index to a device in AudioDevices[]
	long devnum;
	char altdevstr[14] = "";
	int devindex;  // index suitable to pass to waveOutOpen()
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
		if ((strcmp(devstr, PlaybackDevice) == 0 && ch == Pch)
			|| strcmp(devstr, "RESTORE") == 0
		) {
			ZF_LOGD("OpenSoundPlayback(%s) matches already open device.",
				devstr);
			return true;
		}
		// Close the existing device.  This also sets TXEnabled=false
		// and calls updateWebGuiAudioConfig(false).  So, call
		// updateWebGuiAudioConfig(false) again before returning true, but it
		// is not needed before returning false.
		CloseSoundPlayback(false);  // calls updateWebGuiAudioConfig(false);
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

	if (try_parse_long(devstr, &devnum) && devnum >= 0 && devnum < 100) {
		// If devstr is a small positive integer X, use oX
		snprintf(altdevstr, sizeof(altdevstr), "o%li", devnum);
	}

	// FindAudioDevice searches for an exact match of device name, and if this
	// fails, searches for a case insensitive match of device description.
	if ((aindex = FindAudioDevice(altdevstr[0] != 0x00 ? altdevstr : devstr,
		false)) < 0
	) {
		ZF_LOGW("Error opening playback audio device %s.  This does not appear"
			" to be a valid audio device name, nor was a match found using a"
			" case insensitive substring search in the descriptions of"
			" available playback devices.",
			altdevstr[0] != 0x00 ? altdevstr : devstr);
		return false;
	}

	if (!try_parse_long(AudioDevices[aindex]->name + 1, &devnum)) {
		ZF_LOGW("Error extracting a device index from %s for an audio"
			" playback device.", AudioDevices[aindex]->name);
		return false;
	}
	devindex = (int) devnum;

	// nderrcnt is used as a counter in open_audio_playback() to retry if a
	// NODRIVER error is encountered.
	nderrcnt = 0;
	if (!open_audio_playback(devindex, ch, devstr))
		return false;  // an error was already logged.
	TXEnabled = true;
	snprintf(PlaybackDevice, DEVSTRSZ, "%s",
		altdevstr[0] != 0x00 ? altdevstr : devstr);
	strcpy(LastGoodPlaybackDevice, PlaybackDevice);
	Pch = ch;
	updateWebGuiAudioConfig(false);
	return true;
}


// This lower level function is called by OpenSoundCapture using an integer
// devindex derived from a user provided devstr.  devstr is included here
// only for use in error messages.
bool open_audio_capture(int devindex, int ch, char *devstr) {
	int ret;
	if (ch == 1) {
		// Open single channel audio capture and prepare for use
		ret = waveInOpen(&hWaveIn, devindex, &wfx, 0, 0, CALLBACK_NULL);
		if (ret) {
			if (ret == WAVERR_BADFORMAT) {
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
					devstr);
				return false;
			}
			if (ret == MMSYSERR_NODRIVER) {
				// Intermittantly, when attempting to open device immediately after
				// connecting (or reconnecting it), this error (No driver) is produces.
				// I don't know why, but waiting briefly and trying again sometimes
				// fixes it.
				if (nderrcnt++ < 3) {
					ZF_LOGE("MMSYSERR_NODRIVER attemping to open capture device %s. "
						" retry %i.", devstr, nderrcnt);
					txSleep(10);
					return open_audio_capture(devindex, ch, devstr);
				}
				ZF_LOGE("nderrcnt %i", nderrcnt);
				return false;
			}
			ZF_LOGE("ERROR %i opening capture device %s.", ret, devstr);
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
	} else if (ch == 2) {
		// Open two channel (stereo) audio capture and prepare for use
		ret = waveInOpen(&hWaveIn, devindex, &wfxs, 0, 0, CALLBACK_NULL);
		if (ret) {
			if (ret == WAVERR_BADFORMAT) {
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
					devstr,
					UseLeftRX ? "L" : "R");
				return false;
			}
			if (ret == MMSYSERR_NODRIVER) {
				// Intermittantly, when attempting to open device immediately after
				// connecting (or reconnecting it), this error (No driver) is produces.
				// I don't know why, but waiting briefly and trying again sometimes
				// fixes it.
				if (nderrcnt++ < 3) {
					ZF_LOGE("MMSYSERR_NODRIVER attemping to open capture device %s. "
						" retry %i.", devstr, nderrcnt);
					txSleep(10);
					return open_audio_capture(devindex, ch, devstr);
				}
				ZF_LOGE("nderrcnt %i", nderrcnt);
				return false;
			}
			ZF_LOGE("ERROR %i opening capture device %s.", ret, devstr);
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
	} else {
		ZF_LOGE("Invalid ch=%i in open_audio_capture.", ch);
		return false;
	}
	ret = waveInStart(hWaveIn);
	if (ret) {
		ZF_LOGF("Failure of waveInStart(): Error %d", ret);
		waveInClose(hWaveIn);
		return false;
	}
	return true;
}

// Close the capture audio device if one is open and set RXEnabled=false.
// do_getdevices is passed to updateWebGuiAudioConfig()
void CloseSoundCapture(bool do_getdevices) {
	if (hWaveIn != 0) {
		waveInReset(hWaveIn);
		waveInClose(hWaveIn);
		hWaveIn = 0;
	}
	CaptureDevice[0] = 0x00;  // empty string
	RXEnabled = false;
	updateWebGuiAudioConfig(do_getdevices);
}

// If devstr matches CaptureDevice and ch matches Cch and RXEnbled is true,
// then do nothing but write a debug log message.  Otherwise, if RXEnabled is
// true, then CloseSoundCapture() is called before attempting to open
// devstr.  devstr is first checked for an exact match to the name of a
// capture audio device.  If that fails, the first case insensitive substring
// match in the descriptions of playback audio devices is used.
// If no match is found, return false with RXEnabled=false and CaptureDevice
// set to an empty string.  On success, return true with RXEnabled=true and
// CaptureDevice set to devstr or altdevstr, which applies OS-specific rules
// to convert a numerical devstr into something that can be found by
// FindAudioDevices().  devstr/altdevstr, rather than the name found by
// FindAudioDevice() is used because in some instances, the device name may
// change while the description will be the same after that device has been
// unplugged and plugged back in.  So, in that case, more consistent behavior is
// achieved when later using "RESTORE" by using a devstr/altdevstr that will
// match the description, and retaining this devstr as PlaybackDevice and
// LastGoodPlaybackDevice.
// If devstr is the special device name "RESTORE", do nothing if RXEnabled is
// true.  However, if RXEnabled is false, but LastGoodCaptureDevice is not an
// empty string (because some Capture device has previously been successfully
// opened), then try to reopen that device.
// If devstr is an empty string "", then close the existing capture device if
// one is open, and then always return false.
bool OpenSoundCapture(char *devstr, int ch) {
	int aindex;  // index to a device in AudioDevices[]
	long devnum;
	char altdevstr[14] = "";
	int devindex;  // index suitable to pass to waveInOpen()
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
		if ((strcmp(devstr, CaptureDevice) == 0 && ch == Cch)
			|| strcmp(devstr, "RESTORE")
		) {
			ZF_LOGD("OpenSoundCapture(%s) matches already open device.",
				devstr);
			return true;
		}
		// Close the existing device.  This also sets RXEnabled=false
		// and calls updateWebGuiAudioConfig(false).  So, call
		// updateWebGuiAudioConfig(false) again before returning true, but it
		// is not needed before returning false.
		CloseSoundCapture(false);  // calls updateWebGuiAudioConfig(false);
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
		strcpy(CaptureDevice, "NOSOUND");
		strcpy(LastGoodCaptureDevice, CaptureDevice);
		Cch = ch;
		updateWebGuiAudioConfig(false);
		return true;
	}

	if (try_parse_long(devstr, &devnum) && devnum >= 0 && devnum < 100) {
		// If devstr is a small positive integer X, use iX
		snprintf(altdevstr, sizeof(altdevstr), "i%li", devnum);
	}

	// FindAudioDevice searches for an exact match of device name, and if this
	// fails, searches for a case insensitive match of device description.
	if ((aindex = FindAudioDevice(devstr, true)) < 0) {
		ZF_LOGW("Error opening capture audio device %s.  This does not appear"
			" to be a valid audio device name, nor was a match found using a"
			" case insensitive substring search in the descriptions of"
			" available capture devices.",
			altdevstr[0] != 0x00 ? altdevstr : devstr);
		return false;
	}


	if (!try_parse_long(AudioDevices[aindex]->name + 1, &devnum)) {
		ZF_LOGW("Error extracting a device index from %s for an audio"
			" capture device.", AudioDevices[aindex]->name);
		return false;
	}
	devindex = (int) devnum;

	// nderrcnt is used as a counter in open_audio_capture() to retry if a
	// NODRIVER error is encountered.
	nderrcnt = 0;
	if (!open_audio_capture(devindex, ch, devstr))
		return false;  // an error was already logged.
	RXEnabled = true;
	RXSilent = false;
	snprintf(CaptureDevice, DEVSTRSZ, "%s",
		altdevstr[0] != 0x00 ? altdevstr : devstr);
	strcpy(LastGoodCaptureDevice, CaptureDevice);
	lastRXnow = Now;  // Used to detect failure of CaptureDevice
	Cch = ch;
	if (!SoundIsPlaying)
		StartCapture();
	updateWebGuiAudioConfig(false);
	return true;
}

// Return true if recovery is successful, else return false
bool attempt_playback_recovery() {
	ZF_LOGE("An error occured while attempting play audio to %s.  Attempting to"
		" close and re-open audio this Playback device.",
		PlaybackDevice);
	// The following implements all the the contents of CloseSoundPlayback().
	// However, it does not call that function directly because, the steps other
	// than actually closing the device are only implemented if recovery by
	// closing and reopening the device fails.
	if (hWaveOut != 0) {
		// If audio is playing, waveOutClose() will fail.  So, call waveOutReset()
		// first to prevent this from happending.
		waveOutReset(hWaveOut);
		waveOutClose(hWaveOut);
		hWaveOut = 0;
	}
	TXEnabled = false;  // Without this, OpenSoundPlayback does nothing.
	if (!OpenSoundPlayback("RESTORE", Pch)) {
		// remainder of CloseSoundPlayback()
		PlaybackDevice[0] = 0x00;  // empty string
		SoundIsPlaying = false;
		KeyPTT(false);  // In case PTT is engaged.
		ZF_LOGW("Error re-opening audio capture device.  TXEnabled is"
			" now false.");
		if (ZF_LOG_ON_VERBOSE)
			// For testing with testhost.py
			SendCommandToHost("STATUS TXENABLED FALSE");
		// Without at least a short pause, GetDevices() in
		// updateWebGuiAudioConfig() sometimes still shows the now
		// invalid device.
		txSleep(10);
		updateWebGuiAudioConfig(true);
		return false;
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

	// ALSA.c has WriteWav() after writing to soundcard, but skipping actual write
	// if PlaybackDevice is NOSOUND is handled in another function, so that WriteWav()
	// is still done in that case.
	if (txwff != NULL)
		WriteWav(&txbuffer[TxIndex][0], n, txwff);

	if (strcmp(PlaybackDevice, "NOSOUND") == 0)
		return true;  // Do nothing, indicate success

	if (Pch == 2) {
		// fill buffer with zeros to ensure silence on unused channel
		memset(stereobuffer[TxIndex], 0x00, SendSize * 2 * 2);

		int j = UseLeftTX ? 0 : 1;  // Select use of Left or Right channel
		for (int i=0; i < n; ++i, j += 2)
			stereobuffer[TxIndex][j] = txbuffer[TxIndex][i];
		stereoheader[TxIndex].dwBufferLength = n * 2 * 2;  // 2 bytes per sample, 2 channels
		waveOutPrepareHeader(hWaveOut, &stereoheader[TxIndex], sizeof(WAVEHDR));
		if (waveOutWrite(hWaveOut, &stereoheader[TxIndex], sizeof(WAVEHDR)) != 0) {
			if (!attempt_playback_recovery())
				return false;
		}
		// wait till previous buffer is complete
		while (!(stereoheader[!TxIndex].dwFlags & WHDR_DONE))
			txSleep(10);  // Run background while waiting
		waveOutUnprepareHeader(hWaveOut, &stereoheader[!TxIndex], sizeof(WAVEHDR));
	} else if (Pch == 1) {
		header[TxIndex].dwBufferLength = n * 2;
		waveOutPrepareHeader(hWaveOut, &header[TxIndex], sizeof(WAVEHDR));
		if (waveOutWrite(hWaveOut, &header[TxIndex], sizeof(WAVEHDR)) != 0) {
			if (!attempt_playback_recovery())
				return false;
		}
		// wait till previous buffer is complete
		while (!(header[!TxIndex].dwFlags & WHDR_DONE))
			txSleep(10);  // Run background while waiting
		waveOutUnprepareHeader(hWaveOut, &header[!TxIndex], sizeof(WAVEHDR));
	} else {
		ZF_LOGE("Invalid Pch=%i in SendToCard()", Pch);
		CloseSoundPlayback(true);  // calls updateWebGuiAudioConfig(true);
		return false;
	}
	TxIndex = !TxIndex;  // toggle TxIndex between 0 and 1
	return true;
}


void PollReceivedSamples() {
	if (strcmp(PlaybackDevice, "NOSOUND") == 0)
		return;
	// Process any captured samples
	// Ideally call at least every 100 mS, more than 200 will loose data
	if (Now > lastRXnow + 2000) {
		ZF_LOGW("No received audio samples for more than 2 seconds.  Assume"
			" that CaptureDevice=%s has failed.  Attempting to close and re-open"
			" audio capture device",
			CaptureDevice);
		// The following implements all the the contents of CloseSoundCapture().
		// However, it does not call this function directly because, the steps
		// other than actually closing the device are only implemented if recovery
		// by closing and reopening the device fails.
		if (hWaveIn != 0) {
			waveInReset(hWaveIn);
			waveInClose(hWaveIn);
			hWaveIn = 0;
		}
		RXEnabled = false;  // Without this, OpenSoundCapture does nothing.
		if (!OpenSoundCapture("RESTORE", Cch)) {
			CaptureDevice[0] = 0x00;  // empty string
			ZF_LOGW("Error re-opening audio capture device.  RXEnabled is"
				" now false.");
			if (ZF_LOG_ON_VERBOSE)
				// For testing with testhost.py
				SendCommandToHost("STATUS RXENABLED FALSE");
			// Without at least a short pause, GetDevices() in
			// updateWebGuiAudioConfig() sometimes still shows the now
			// invalid device.
			txSleep(10);
			updateWebGuiAudioConfig(true);
			return;
		}
		// success, continue
	}
	if (Cch == 2) {
		if (stereoinheader[inIndex].dwFlags & WHDR_DONE) {
			lastRXnow = Now;
			// Copy samples from the user specified channel into the corresponding
			// inbuffer[inIndex] so that it can be passed to ProcessNewSamples(),
			// which expects a mono signal.
			int j = UseLeftRX ? 0 : 1;  // Select use of Left or Right channel
			for (int i = 0; i < ReceiveSize; ++i, j += 2)
				inbuffer[inIndex][i] = stereoinbuffer[inIndex][j];
			if (Capturing) {
				ProcessNewSamples(&inbuffer[inIndex][0],
					stereoinheader[inIndex].dwBytesRecorded / 4);
			} else {
				// PreprocessNewSamples() writes these samples to the RX wav
				// file when specified, even though not Capturing.  This
				// produces a time continuous Wav file which can be useful for
				// diagnostic purposes.  In earlier versions of ardopcf, this
				// RX wav file could not be time continuous due to the way that
				// ALSA audio devices were opened, closed, and configured.
				// If Capturing, this is called from ProcessNewSamples().
				PreprocessNewSamples(&inbuffer[inIndex][0],
					stereoinheader[inIndex].dwBytesRecorded / 4);
			}
			waveInPrepareHeader(hWaveIn, &stereoinheader[inIndex], sizeof(WAVEHDR));
			waveInAddBuffer(hWaveIn, &stereoinheader[inIndex], sizeof(WAVEHDR));
			inIndex++;
			if (inIndex == NumberofinBuffers)
				inIndex = 0;
		}
		return;
	} else if (Cch == 1) {  // Mono
		if (inheader[inIndex].dwFlags & WHDR_DONE) {
			lastRXnow = Now;
			if (Capturing) {
				ProcessNewSamples(&inbuffer[inIndex][0],
					inheader[inIndex].dwBytesRecorded / 2);
			} else {
				// PreprocessNewSamples() writes these samples to the RX wav
				// file when specified, even though not Capturing.  This
				// produces a time continuous Wav file which can be useful for
				// diagnostic purposes.  In earlier versions of ardopcf, this
				// RX wav file could not be time continuous due to the way that
				// ALSA audio devices were opened, closed, and configured.
				// If Capturing, this is called from ProcessNewSamples().
				PreprocessNewSamples(&inbuffer[inIndex][0],
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
	ZF_LOGE("Invalid Cch=%i in PollReceivedSamples()", Cch);
	CloseSoundCapture(true);  // calls updateWebGuiAudioConfig(true);
	return;
}

void StopCapture() {
	Capturing = false;
}


// Adds a trailer to the audio samples that have been staged with SendtoCard(),
// and then block until all of the staged audio has been played.  Before
// returning, it calls KeyPTT(false) and enables the Capturing state.
// return true on success and false on failure.  On failure, still set
// KeyPTT(false) and enables the Capturing state
bool SoundFlush() {
	// Append Trailer then wait for TX to complete
	// if AddTrailer() or SendtoCard() fail, TXEnabled will be set to false
	if (TXEnabled && AddTrailer() && SendtoCard(Number)) {
		// Wait for all sound output to complete
		if (strcmp(PlaybackDevice, "NOSOUND") != 0) {
			if (Pch == 2) {
				while (!(stereoheader[0].dwFlags & WHDR_DONE))
					txSleep(10);
				while (!(stereoheader[1].dwFlags & WHDR_DONE))
					txSleep(10);
			} else if (Pch == 1) {
				while (!(header[0].dwFlags & WHDR_DONE))
					txSleep(10);
				while (!(header[1].dwFlags & WHDR_DONE))
					txSleep(10);
			} else {
				ZF_LOGE("Error: unexpected Pch=%i in SoundFlush()", Pch);
				// sets TXEnabled = false
				CloseSoundPlayback(true);  // calls updateWebGuiAudioConfig(false);
			}
		}
	} else {
		ZF_LOGW("SoundFlush() called when not TXEnabled. Ignoring.");
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
