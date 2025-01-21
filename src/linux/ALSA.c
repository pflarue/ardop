// ALSA audio for Linux

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#include <alsa/asoundlib.h>

#include "common/os_util.h"
#include "linux/ALSA.h"
#include "common/log.h"
#include "common/wav.h"
#include "common/ardopcommon.h"

extern bool blnClosing;

extern bool UseLeftRX;
extern bool UseRightRX;

extern bool UseLeftTX;
extern bool UseRightTX;

extern bool FixTiming;

extern char CaptureDevice[80];
extern char PlaybackDevice[80];

extern bool WriteRxWav;  // Record RX controlled by Command line/TX/Timer
extern bool HWriteRxWav;  // Record RX controlled by host command RECRX
extern struct WavFile *txwff;  // For recording of filtered TX audio
// extern struct WavFile *txwfu;  // For recording of unfiltered TX audio


void txSleep(int mS);
int wg_send_pixels(int cnum, unsigned char *data, size_t datalen);
void KeyPTT(bool state);
void StartRxWav();

// TX and RX audio are signed short integers: +- 32767
extern int SampleNo;  // Total number of samples for this transmission.
extern int Number;  // Number of samples waiting to be sent

short buffer[2][SendSize];  // Two buffers of 0.1 sec duration for TX audio.
short inbuffer[2][ReceiveSize];  // Two buffers of 0.1 Sec duration for RX audio.
int Index = 0;  // buffer being used 0 or 1
int inIndex = 0;  // inbuffer being used 0 or 1

snd_pcm_sframes_t MaxAvail;

snd_pcm_t *	playhandle = NULL;
snd_pcm_t *	rechandle = NULL;

// TODO: re-evaluate how these variables are used
int Savedplaychannels = 1;
int m_playchannels = 1;
int m_recchannels = 1;
snd_pcm_uframes_t fpp;  // Frames per period.
int dir;
char SavedCaptureDevice[256];  // Saved so we can reopen
char SavedPlaybackDevice[256];
int SavedPlaybackRate;
int SavedCaptureRate;
char **PlaybackDevices;
int PlaybackDevicesCount;
char **CaptureDevices;
int CaptureDevicesCount;

void CloseSoundCard() {
	if (rechandle) {
		snd_pcm_close(rechandle);
		rechandle = NULL;
	}

	if (playhandle) {
		snd_pcm_close(playhandle);
		playhandle = NULL;
	}
}

// Populate PlaybackDevices and Log the available output audio devices
int GetOutputDeviceCollection() {
	snd_ctl_t *handle= NULL;
	snd_pcm_t *pcm= NULL;
	snd_ctl_card_info_t *info;
	snd_pcm_info_t *pcminfo;
	snd_pcm_hw_params_t *pars;
	snd_pcm_format_mask_t *fmask;
	char NameString[256];

	ZF_LOGI("Playback Devices\n");

	CloseSoundCard();

	// free old set of devices if called again
	if (PlaybackDevices != NULL) {
		for (int i = 0; i < PlaybackDevicesCount; i++)
			free(PlaybackDevices[i]);
		free(PlaybackDevices);
	}
	PlaybackDevicesCount = 0;

	// Get Device List from ALSA
	snd_ctl_card_info_alloca(&info);
	snd_pcm_info_alloca(&pcminfo);
	snd_pcm_hw_params_alloca(&pars);
	snd_pcm_format_mask_alloca(&fmask);

	char hwdev[80];
	unsigned int min, max, ratemin, ratemax;
	int card, err, dev, nsubd;
	snd_pcm_stream_t stream = SND_PCM_STREAM_PLAYBACK;

	card = -1;
	if (snd_card_next(&card) < 0) {
		ZF_LOGI("No Devices");
		return 0;
	}

	if (playhandle)
		snd_pcm_close(playhandle);

	playhandle = NULL;
	while (card >= 0) {
		sprintf(hwdev, "hw:%d", card);
		err = snd_ctl_open(&handle, hwdev, 0);
		err = snd_ctl_card_info(handle, info);

		ZF_LOGI("Card %d, ID `%s', name `%s'", card, snd_ctl_card_info_get_id(info),
			snd_ctl_card_info_get_name(info));

		dev = -1;
		if (snd_ctl_pcm_next_device(handle, &dev) < 0) {
			// Card has no devices
			snd_ctl_close(handle);
			goto nextcard;
		}

		// TODO: Refactor to eliminate use of goto
		while (dev >= 0) {
			snd_pcm_info_set_device(pcminfo, dev);
			snd_pcm_info_set_subdevice(pcminfo, 0);
			snd_pcm_info_set_stream(pcminfo, stream);
			err = snd_ctl_pcm_info(handle, pcminfo);

			if (err == -ENOENT)
				goto nextdevice;
			nsubd = snd_pcm_info_get_subdevices_count(pcminfo);

			ZF_LOGI("  Device hw:%d,%d ID `%s', name `%s', %d subdevices (%d available)",
				card, dev, snd_pcm_info_get_id(pcminfo), snd_pcm_info_get_name(pcminfo),
				nsubd, snd_pcm_info_get_subdevices_avail(pcminfo));

			sprintf(hwdev, "hw:%d,%d", card, dev);
			if ((err = snd_pcm_open(&pcm, hwdev, stream, SND_PCM_NONBLOCK)) != 0) {
				ZF_LOGW("Error %d opening output device", err);
				goto nextdevice;
			}

			// Get parameters for this device
			err = snd_pcm_hw_params_any(pcm, pars);
			snd_pcm_hw_params_get_channels_min(pars, &min);
			snd_pcm_hw_params_get_channels_max(pars, &max);
			snd_pcm_hw_params_get_rate_min(pars, &ratemin, NULL);
			snd_pcm_hw_params_get_rate_max(pars, &ratemax, NULL);
			if (min == max) {
				if (min == 1)
					ZF_LOGI("    1 channel,  sampling rate %u..%u Hz", ratemin, ratemax);
				else
					ZF_LOGI("    %d channels,  sampling rate %u..%u Hz", min, ratemin, ratemax);
			} else
				ZF_LOGI("    %u..%u channels, sampling rate %u..%u Hz", min, max, ratemin, ratemax);

			// Add device to list
			sprintf(NameString, "hw:%d,%d %s(%s)", card, dev,
				snd_pcm_info_get_name(pcminfo), snd_ctl_card_info_get_name(info));
			PlaybackDevices = realloc(PlaybackDevices,
				(PlaybackDevicesCount + 1) * sizeof(char *));
			PlaybackDevices[PlaybackDevicesCount++] = strdup(NameString);

			snd_pcm_close(pcm);
			pcm= NULL;

nextdevice:
			if (snd_ctl_pcm_next_device(handle, &dev) < 0)
				break;
			snd_ctl_close(handle);
		}


nextcard:
		ZF_LOGI("%s", "");
		if (snd_card_next(&card) < 0)  // No more cards
			break;
	}
	return 0;
}

// Populate CaptureDevices and Log the available input audio devices
int GetInputDeviceCollection() {
	snd_ctl_t *handle= NULL;
	snd_pcm_t *pcm= NULL;
	snd_ctl_card_info_t *info;
	snd_pcm_info_t *pcminfo;
	snd_pcm_hw_params_t *pars;
	snd_pcm_format_mask_t *fmask;
	char NameString[256];

	ZF_LOGI("Capture Devices\n");
	// Get Device List from ALSA
	snd_ctl_card_info_alloca(&info);
	snd_pcm_info_alloca(&pcminfo);
	snd_pcm_hw_params_alloca(&pars);
	snd_pcm_format_mask_alloca(&fmask);

	char hwdev[80];
	unsigned int min, max, ratemin, ratemax;
	int card, err, dev, nsubd;
	snd_pcm_stream_t stream = SND_PCM_STREAM_CAPTURE;

	// free old set of devices if called again
	if (CaptureDevices != NULL) {
		for (int i = 0; i < CaptureDevicesCount; i++)
			free(CaptureDevices[i]);
		free(CaptureDevices);
	}
	CaptureDevicesCount = 0;

	card = -1;
	if(snd_card_next(&card) < 0) {
		ZF_LOGI("No Devices");
		return 0;
	}

	if (rechandle)
		snd_pcm_close(rechandle);

	rechandle = NULL;
	while(card >= 0) {
		sprintf(hwdev, "hw:%d", card);
		err = snd_ctl_open(&handle, hwdev, 0);
		err = snd_ctl_card_info(handle, info);

		ZF_LOGI("Card %d, ID `%s', name `%s'", card, snd_ctl_card_info_get_id(info),
			snd_ctl_card_info_get_name(info));

		dev = -1;
		if (snd_ctl_pcm_next_device(handle, &dev) < 0) {  // No Devices
			snd_ctl_close(handle);
			goto nextcard;
		}

		// TODO: Refactor to eliminate use of goto
		while(dev >= 0) {
			snd_pcm_info_set_device(pcminfo, dev);
			snd_pcm_info_set_subdevice(pcminfo, 0);
			snd_pcm_info_set_stream(pcminfo, stream);
			err= snd_ctl_pcm_info(handle, pcminfo);

			if (err == -ENOENT)
				goto nextdevice;

			nsubd= snd_pcm_info_get_subdevices_count(pcminfo);
			ZF_LOGI("  Device hw:%d,%d ID `%s', name `%s', %d subdevices (%d available)",
				card, dev, snd_pcm_info_get_id(pcminfo), snd_pcm_info_get_name(pcminfo),
				nsubd, snd_pcm_info_get_subdevices_avail(pcminfo));

			sprintf(hwdev, "hw:%d,%d", card, dev);

			if ((err = snd_pcm_open(&pcm, hwdev, stream, SND_PCM_NONBLOCK)) != 0) {
				ZF_LOGW("Error %d opening input device", err);
				goto nextdevice;
			}

			err = snd_pcm_hw_params_any(pcm, pars);
			snd_pcm_hw_params_get_channels_min(pars, &min);
			snd_pcm_hw_params_get_channels_max(pars, &max);
			snd_pcm_hw_params_get_rate_min(pars, &ratemin, NULL);
			snd_pcm_hw_params_get_rate_max(pars, &ratemax, NULL);
			if (min == max) {
				if (min == 1)
					ZF_LOGI("    1 channel,  sampling rate %u..%u Hz", ratemin, ratemax);
				else
					ZF_LOGI("    %d channels,  sampling rate %u..%u Hz", min, ratemin, ratemax);
			} else
				ZF_LOGI("    %u..%u channels, sampling rate %u..%u Hz", min, max, ratemin, ratemax);

			sprintf(NameString, "hw:%d,%d %s(%s)", card, dev,
				snd_pcm_info_get_name(pcminfo), snd_ctl_card_info_get_name(info));
			CaptureDevices = realloc(CaptureDevices,
				(CaptureDevicesCount + 1) * sizeof(char *));
			CaptureDevices[PlaybackDevicesCount++] = strdup(NameString);

			snd_pcm_close(pcm);
			pcm= NULL;

nextdevice:
			if (snd_ctl_pcm_next_device(handle, &dev) < 0)
				break;
		}
		snd_ctl_close(handle);
nextcard:

		ZF_LOGI("%s", "");
		if (snd_card_next(&card) < 0 )
			break;
	}
	return 0;
}


bool FirstOpenSoundPlayback = true;  // used to only log warning about -A option once.

bool OpenSoundPlayback(char * PlaybackDevice, unsigned int m_sampleRate, int channels) {
	unsigned int intRate;  // reported number of frames per second
	unsigned int intPeriodTime = 0;  // reported duration of one period in microseconds.
	snd_pcm_uframes_t periodSize = 0;  // reported number of frames per period
	int intDir;
	int err = 0;
	char buf1[100];
	char * ptr;

	if (strcmp(PlaybackDevice, "NOSOUND") == 0)
		// For testing/diagnostic purposes, NOSOUND uses no audio device,
		return true;

	// Choose the number of frames per period.  This avoids possible ALSA misconfiguration
	// errors that may result in a TX symbol timing error if the default value is accepted.
	// The value chosen is a tradeoff between avoiding excessive CPU load caused by too
	// small of a value and increased latency associated with too large a value.
	// TODO: Consider alternate methods of choosing this value
	snd_pcm_uframes_t setPeriodSize = m_sampleRate / 100;

	if (playhandle) {
		snd_pcm_close(playhandle);
		playhandle = NULL;
	}
	strcpy(buf1, PlaybackDevice);
	if ((ptr = strchr(buf1, ' ')) != NULL)
		*ptr = 0;  // Get Device part of name

	snd_pcm_hw_params_t *hw_params;

	if ((err = snd_pcm_open(&playhandle, buf1, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK)) < 0) {
		ZF_LOGE("cannot open playback audio device %s (%s)",  buf1, snd_strerror(err));
		return false;
	}
	if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
		ZF_LOGE("cannot allocate hardware parameter structure (%s)", snd_strerror(err));
		return false;
	}
	if ((err = snd_pcm_hw_params_any (playhandle, hw_params)) < 0) {
		ZF_LOGE("cannot initialize hardware parameter structure (%s)", snd_strerror(err));
		return false;
	}
	if ((err = snd_pcm_hw_params_set_access (playhandle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
		ZF_LOGE("cannot set playback access type (%s)", snd_strerror(err));
		return false;
	}
	if ((err = snd_pcm_hw_params_set_format (playhandle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0) {
		ZF_LOGE("cannot setplayback  sample format (%s)", snd_strerror(err));
		return false;
	}
	if ((err = snd_pcm_hw_params_set_rate (playhandle, hw_params, m_sampleRate, 0)) < 0) {
		ZF_LOGE("cannot set playback sample rate (%s)", snd_strerror(err));
		return false;
	}

	// Initial call has channels set to 1. Subequent ones set to what worked last time
	if ((err = snd_pcm_hw_params_set_channels (playhandle, hw_params, channels)) < 0) {
		ZF_LOGE("cannot set play channel count to %d (%s)", channels, snd_strerror(err));
		if (channels == 2)
			return false;  // Shouldn't happen as should have worked before
		channels = 2;

		if ((err = snd_pcm_hw_params_set_channels (playhandle, hw_params, 2)) < 0) {
			ZF_LOGE("cannot play set channel count to 2 (%s)", snd_strerror(err));
			return false;
		}
	}

	if (FixTiming) {
		if ((err = snd_pcm_hw_params_set_period_size (playhandle, hw_params, setPeriodSize, 0)) < 0) {
			ZF_LOGE("cannot set playback period size (%s)", snd_strerror(err));
			return false;
		}
	}

	if ((err = snd_pcm_hw_params (playhandle, hw_params)) < 0) {
		ZF_LOGE("cannot set parameters (%s)", snd_strerror(err));
		return false;
	}

	// Verify that key values were set as expected
	if ((err = snd_pcm_hw_params_get_rate(hw_params, &intRate, &intDir)) < 0) {
		ZF_LOGE("cannot verify playback rate (%s)", snd_strerror(err));
		return false;
	}
	if (m_sampleRate != intRate) {
		ZF_LOGE("Unable to correctly set playback rate.  Got %d instead of %d.",
			intRate, m_sampleRate);
		return false;
	}
	if ((err = snd_pcm_hw_params_get_period_size(hw_params, &periodSize, &intDir)) < 0) {
		ZF_LOGE("cannot verify playback period size (%s)", snd_strerror(err));
		return false;
	}
	if (FixTiming && (setPeriodSize != periodSize)) {
		ZF_LOGE("Unable to correctly set playback period size.  Got %ld instead of %ld.",
			periodSize, setPeriodSize);
		return false;
	}
	if ((err = snd_pcm_hw_params_get_period_time(hw_params, &intPeriodTime, &intDir)) < 0) {
		ZF_LOGE("cannot verify playback period time (%s)", snd_strerror(err));
		return false;
	}
	if (FixTiming && (intPeriodTime * intRate != periodSize * 1000000)) {
		ZF_LOGE("\n\nERROR: Inconsistent playback settings: %d * %d != %lu *"
			" 1000000.  Please report this error with a message to the ardop"
			" users group at ardop.groups.io or by creating an issue at"
			" github.com/pflarue/ardop.  You may find that ardopcf is usable"
			" with your hardware/operating system by using the -A command line"
			" option.\n\n",
			intPeriodTime, intRate, periodSize);
		return false;
	}
	// ZF_LOGI("snd_pcm_hw_params_get_period_time(hw_params, &intPeriodTime, &intDir) intPeriodTime=%d intDir=%d", intPeriodTime, intDir);

	if (!FixTiming && (intPeriodTime * intRate != periodSize * 1000000) && FirstOpenSoundPlayback) {
		ZF_LOGW("WARNING: Inconsistent ALSA playback configuration: %u * %u != %ld * 1000000.",
			intPeriodTime, intRate, periodSize);
		ZF_LOGW("This will result in a playblack sample rate of %f instead of %d.",
			periodSize * 1000000.0 / intPeriodTime, intRate);
		ZF_LOGW("This is an error of about %fppm.  Per the Ardop spec +/-100ppm"
			" should work well and +/-1000 ppm should work with some"
			" performance degredation.",
			(intRate - (periodSize * 1000000.0 / intPeriodTime))/intRate * 1000000);
		ZF_LOGW("\n\nWARNING: The -A option was specified.  So, ALSA"
			" misconfiguration will be accepted and ignored.  This option is"
			" primarily intended for testing/debuging.  However, it may also be"
			" useful if ardopcf will not run without it.  In this case, please"
			" report this problem to the ardop users group at ardop.groups.io"
			" or by creating an issue at www.github.com/pflarue/ardop.\n\n");
	}
	FirstOpenSoundPlayback = false;
	snd_pcm_hw_params_free(hw_params);

	if ((err = snd_pcm_prepare (playhandle)) < 0) {
		ZF_LOGE("cannot prepare audio interface for use (%s)", snd_strerror(err));
		return false;
	}

	Savedplaychannels = m_playchannels = channels;
	MaxAvail = snd_pcm_avail_update(playhandle);
	return true;
}


bool OpenSoundCapture(char * CaptureDevice, int m_sampleRate) {
	int err = 0;
	char buf1[100];
	char * ptr;
	snd_pcm_hw_params_t *hw_params;

	if (strcmp(CaptureDevice, "NOSOUND") == 0)
		// For testing/diagnostic purposes, NOSOUND uses no audio device,
		return true;

	if (rechandle) {
		snd_pcm_close(rechandle);
		rechandle = NULL;
	}

	strcpy(buf1, CaptureDevice);

	if ((ptr = strchr(buf1, ' ')) != NULL)
		*ptr = 0;  // Get Device part of name

	if ((err = snd_pcm_open (&rechandle, buf1, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
		ZF_LOGE("cannot open capture audio device %s (%s)", buf1, snd_strerror(err));
		return false;
	}
	if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
		ZF_LOGE("cannot allocate capture hardware parameter structure (%s)", snd_strerror(err));
		return false;
	}
	if ((err = snd_pcm_hw_params_any (rechandle, hw_params)) < 0) {
		ZF_LOGE("cannot initialize capture hardware parameter structure (%s)", snd_strerror(err));
		return false;
	}

	m_recchannels = 1;
	if (UseLeftRX == 0 || UseRightRX == 0)
		m_recchannels = 2;  // -L or -R was used.  So, open as a stereo device

	if ((err = snd_pcm_hw_params_set_channels (rechandle, hw_params, m_recchannels)) < 0) {
		if (m_recchannels  == 2) {
			ZF_LOGE("The -%s command line option was used to indicate that the"
				" %s channel of a stereo audio capture device should be used."
				" However, the audio capture device specified (%s) could not be"
				" opened as a stereo device.  Try again without the -%s command"
				" line option to open it as a single channel device.",
				UseLeftRX ? "L" : "R",
				UseLeftRX ? "left" : "right",
				UseLeftRX ? "L" : "R",
				CaptureDevice);
			return false;
		}
		ZF_LOGE("Neither the -L nor -R command line option was used to indicate"
			" that the specified audio capture device (%s) was a stereo device,"
			" and to select which of its channels should be used.  Thus, the"
			" device was opened as a single channel audio device, but this"
			" failed.  This usually means that the device can only be opened"
			" as a stereo device.  So, please try again using either the -L"
			" or -R command line option to indicate which channel to use.",
			CaptureDevice);
		return false;
	}

	// craiger add frames per period
	fpp = 600;
	dir = 0;

	if ((err = snd_pcm_hw_params_set_period_size_near (rechandle, hw_params, &fpp, &dir)) < 0) {
		ZF_LOGE("snd_pcm_hw_params_set_period_size_near failed (%s)", snd_strerror(err));
		return false;
	}
	if ((err = snd_pcm_hw_params_set_access (rechandle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
		ZF_LOGE("cannot set capture access type (%s)", snd_strerror(err));
		return false;
	}
	if ((err = snd_pcm_hw_params_set_format (rechandle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0) {
		ZF_LOGE("cannot set capture sample format (%s)", snd_strerror(err));
		return false;
	}
	if ((err = snd_pcm_hw_params_set_rate (rechandle, hw_params, m_sampleRate, 0)) < 0) {
		ZF_LOGE("cannot set capture sample rate (%s)", snd_strerror(err));
		return false;
	}
	if ((err = snd_pcm_hw_params (rechandle, hw_params)) < 0) {
		ZF_LOGE("cannot set parameters (%s)", snd_strerror(err));
		return false;
	}

	snd_pcm_hw_params_free(hw_params);
	if ((err = snd_pcm_prepare (rechandle)) < 0) {
		ZF_LOGE("cannot prepare audio interface for use (%s)", snd_strerror(err));
		return false;
	}

	snd_pcm_start(rechandle);  // without this avail stuck at 0
	return true;
}

bool OpenSoundCard(int c_sampleRate, int p_sampleRate) {
	int Channels = 1;

	if (strcmp(CaptureDevice, "0") == 0
		|| (atoi(CaptureDevice) > 0 && strlen(CaptureDevice) <= 2)
	) {
		// If CaptureDevice is a small positive integer X, use plughw:X,0
		// This includes the default value of "0" which becomes "plughw:0,0"
		snprintf(CaptureDevice, sizeof(CaptureDevice), "plughw:%i,0",
			atoi(CaptureDevice));
	}

	if (strcmp(PlaybackDevice, "0") == 0
		|| (atoi(PlaybackDevice) > 0 && strlen(PlaybackDevice) <= 2)
	) {
		// If Playback device is a small positive integer X, use plughw:X,0
		// This includes the default value of "0" which becomes "plughw:0,0"
		snprintf(PlaybackDevice, sizeof(PlaybackDevice), "plughw:%i,0",
			atoi(PlaybackDevice));
	}

	if (UseLeftRX == 1 && UseRightRX == 1) {
		// TODO: Is this wording correct?
		ZF_LOGI("Using Both Channels of soundcard for RX");
	} else {
		if (UseLeftRX == 0)
			ZF_LOGI("Using Right Channel of soundcard for RX");
		if (UseRightRX == 0)
			ZF_LOGI("Using Left Channel of soundcard for RX");
	}

	if (UseLeftTX == 1 && UseRightTX == 1) {
		// TODO: Is this wording correct?
		ZF_LOGI("Using Both Channels of soundcard for TX");
	} else {
		if (UseLeftTX == 0)
			ZF_LOGI("Using Right Channel of soundcard for TX");
		if (UseRightTX == 0)
			ZF_LOGI("Using Left Channel of soundcard for TX");
	}

	ZF_LOGI("Opening Playback Device %s Rate %d", PlaybackDevice, p_sampleRate);
	if (UseLeftTX == 0 || UseRightTX == 0)
		Channels = 2;  // L or R implies stereo
	strcpy(SavedPlaybackDevice, PlaybackDevice);  // Saved so we can reopen in error recovery
	SavedPlaybackRate = p_sampleRate;
	if (OpenSoundPlayback(PlaybackDevice, p_sampleRate, Channels)) {
		// TODO: Reconsider closing playback device here
		// Close playback device so it can be shared
		if (playhandle) {
			snd_pcm_close(playhandle);
			playhandle = NULL;
		}
		ZF_LOGI("Opening Capture Device %s Rate %d", CaptureDevice, c_sampleRate);

		strcpy(SavedCaptureDevice, CaptureDevice);  // Saved so we can reopen in error recovery
		SavedCaptureRate = c_sampleRate;
		return OpenSoundCapture(CaptureDevice, c_sampleRate);
	}
	else
		return false;
}

int PackSamplesAndSend(short * input, int nSamples) {
	unsigned short samples[256000];
	unsigned short * sampptr = samples;
	int ret;

	// Convert byte stream to int16 (watch endianness)
	if (m_playchannels == 1) {
		for (int n = 0; n < nSamples; ++n) {
			*(sampptr++) = input[0];
			input ++;
		}
	} else {
		for (int n = 0; n < nSamples; n++) {
			if (UseLeftTX)
				*(sampptr++) = input[0];
			else
				*(sampptr++) = 0;

			if (UseRightTX)
				*(sampptr++) = input[0];
			else
				*(sampptr++) = 0;

			input ++;
		}
	}

	if ((ret = snd_pcm_writei(playhandle, samples, nSamples)) < 0) {
		snd_pcm_recover(playhandle, ret, 1);
		ret = snd_pcm_writei(playhandle, samples, nSamples);
	}
	snd_pcm_avail_update(playhandle);
	return ret;
}


int SoundCardWrite(short * input, unsigned int nSamples) {
	snd_pcm_sframes_t avail;

	if (playhandle == NULL)
		return 0;

	// Stop Capture
	if (rechandle) {
		snd_pcm_close(rechandle);
		rechandle = NULL;
	}

	if ((avail = snd_pcm_avail_update(playhandle)) < 0) {
		if (avail != -32)
			ZF_LOGD("Playback Avail Recovering from %s ..", snd_strerror((int)avail));
		snd_pcm_recover(playhandle, avail, 1);

		if ((avail = snd_pcm_avail_update(playhandle)) < 0)
			ZF_LOGD("avail play after recovery returned %d", (int)avail);
	}

	while (avail < (int) nSamples) {
		txSleep(100);
		avail = snd_pcm_avail_update(playhandle);
	}

	return PackSamplesAndSend(input, nSamples);
}

int SoundCardRead(short * input, unsigned int nSamples) {
	short samples[65536];
	int n;
	int ret;
	int avail;
	int start;

	if (rechandle == NULL)
		return 0;

	if ((avail = snd_pcm_avail_update(rechandle)) < 0) {
		ZF_LOGD("avail Recovering from %s ..", snd_strerror((int)avail));
		if (rechandle) {
			snd_pcm_close(rechandle);
			rechandle = NULL;
		}

		OpenSoundCapture(SavedCaptureDevice, SavedCaptureRate);
//		snd_pcm_recover(rechandle, avail, 0);
		avail = snd_pcm_avail_update(rechandle);
		ZF_LOGD("After avail recovery %d ..", avail);
	}

	if (avail < (int) nSamples)
		return 0;

	if ((ret = snd_pcm_readi(rechandle, samples, nSamples)) < 0) {
		ZF_LOGE("RX Error %d", ret);
//		snd_pcm_recover(rechandle, avail, 0);
		if (rechandle) {
			snd_pcm_close(rechandle);
			rechandle = NULL;
		}

		OpenSoundCapture(SavedCaptureDevice, SavedCaptureRate);
//		snd_pcm_recover(rechandle, avail, 0);
		avail = snd_pcm_avail_update(rechandle);
		ZF_LOGD("After Read recovery Avail %d ..", avail);
		return 0;
	}

	if (m_recchannels == 1) {
		for (n = 0; n < ret; n++) {
			memcpy(input, samples, nSamples*sizeof(short));
		}
	} else {
		if (UseLeftRX)
			start = 0;
		else
			start = 1;

		for (n = start; n < (ret * 2); n+=2) {  // return alternate
			*(input++) = samples[n];
		}
	}
	return ret;
}

// TODO: Consider calling SoundCardWrite() directly instead
short * SendtoCard(int n) {
	if (playhandle)
		SoundCardWrite(&buffer[Index][0], n);
	if (txwff != NULL)
		WriteWav(&buffer[Index][0], n, txwff);
	return &buffer[Index][0];
}

// This mininal error handler to be passed to snd_lib_error_set_handler()
// prevents ALSA from printing errors directly to the console.  Instead,
// write an appropriate log message.  This ensures that the error is
// written to the log file, rather than only to the console.  It also prevents
// undesirable console output when using the --syslog command line option.
//
// If it detects a rapid string of ALSA errors, it signals for ardopcf to exit
// normally.  The scenario envisioned which will cause this is if a sound device
// is (accidentally?) disconnected or otherwise fails.  TODO: If/when the
// ability to stop and restart audio processing and change audio devices without
// restarting ardopcf are implemented, a better course of action for such a
// failure may be implemented.
void alsa_errhandler(const char *file, int line, const char *function, int err, const char *fmt, ...) {
	static unsigned int errtime = 0;  // ms resolution
	static unsigned int errcount = 0;
	(void) fmt;  // This prevents an unused variable warning.

	if (Now - errtime > 2000) {
		// It has been more than 2 seconds since the last error
		errcount = 0;
	}
	errtime = Now;
	errcount += 1;

	if (err) {
		ZF_LOGE("ALSA Error at %s:%i %s(): %s", file, line, function, snd_strerror(err));
	} else {
		ZF_LOGE("ALSA Error at %s:%i %s()", file, line, function);
	}
	if (errcount > 10) {
		ZF_LOGE("In response to a string of rapid ALSA errors.  Exit.");
		blnClosing = true;  // This is detected in the ardop main loop.
	}
}

bool InitSound() {
	// Using hw: where plughw: is required seems to be a common mistake.  So,
	// log a warning to help new users correct this problem.
	if (strncmp(CaptureDevice, "hw:", 3) == 0) {
		ZF_LOGW("WARNING: Use of %s as the audio capture device may cause"
			" problems.  This will only work if that device natively"
			" supports a 12 kHz sample rate, which most do not.  If ardopcf"
			" seems to work as expected, you can ignore this warning.  If"
			" it does not, try using plug%s instead.",
			CaptureDevice, CaptureDevice);
	}
	if (strncmp(PlaybackDevice, "hw:", 3) == 0) {
		ZF_LOGW("WARNING: Use of %s as the audio playback device may cause"
			" problems.  This will only work if that device natively"
			" supports a 12 kHz sample rate, which most do not.  If ardopcf"
			" seems to work as expected, you can ignore this warning.  If"
			" it does not, try using plug%s instead.",
			PlaybackDevice, PlaybackDevice);
	}

	snd_lib_error_set_handler(alsa_errhandler);
	GetInputDeviceCollection();
	GetOutputDeviceCollection();
	return OpenSoundCard(12000, 12000);
}

// Process any captured samples
// Ideally call at least every 100 mS, more than 200 will loose data
void PollReceivedSamples() {
	if (strcmp(PlaybackDevice, "NOSOUND") == 0)
		return;

	// TODO: If we always use inbuufer[0], why is there inbuffer[1]?
	if (SoundCardRead(&inbuffer[0][0], ReceiveSize) == 0)
		return;  // No samples to process

	if (Capturing)
		ProcessNewSamples(&inbuffer[0][0], ReceiveSize);
}


void StopCapture() {
	Capturing = false;

	if (strcmp(PlaybackDevice, "NOSOUND") == 0)
		return;

	// Stopcapture is only called when we are about to transmit, so use it to open plaback device. We don't keep
	// it open all the time to facilitate sharing.
	OpenSoundPlayback(SavedPlaybackDevice, SavedPlaybackRate, Savedplaychannels);
}

// TODO: Enable CODEC host command to start/stop audio processing
// int StartCodec() {
//	OpenSoundCard(12000, 12000);
//}
//
//int StopCodec() {
//	CloseSoundCard();
//}

void StartCapture() {
	Capturing = true;
	DiscardOldSamples();
	ClearAllMixedSamples();
	State = SearchingForLeader;
}

void CloseSound() {
	CloseSoundCard();
}

// TODO: Why does this return an array of UNSIGNED shorts when the sound samples.
// to be sent to ALSA are signed?   Review use in Modulate.c
// TODO: Consider renaming this function
// get a buffer to fill to start transmitting.
unsigned short * SoundInit() {
	Index = 0;
	return (unsigned short *) &buffer[0][0];
}


// Called at end of transmit
void SoundFlush() {
	// Append Trailer then send remaining samples
	int txlenMs = 0;

	AddTrailer();  // add the trailer.
	SendtoCard(Number);

	// Wait for tx to complete
	// samples sent is is in SampleNo, time started in mS is in pttOnTime.
	// calculate time to stop
	txlenMs = SampleNo / 12 + 20;  // 12000 samples per sec. 20 mS TXTAIL
	ZF_LOGD("Tx Time %d Time till end = %d", txlenMs, (pttOnTime + txlenMs) - Now);

	if (strcmp(PlaybackDevice, "NOSOUND") != 0) {
		while (Now < (pttOnTime + txlenMs))
			usleep(2000);

		if (playhandle) {
			snd_pcm_close(playhandle);
			playhandle = NULL;
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

	OpenSoundCapture(SavedCaptureDevice, SavedCaptureRate);
	StartCapture();

	if (WriteRxWav && !HWriteRxWav) {
		// Start recording if not already recording, else extend the recording time.
		// Note that this is disabled if HWriteRxWav is true.
		StartRxWav();
	}
	return;
}
