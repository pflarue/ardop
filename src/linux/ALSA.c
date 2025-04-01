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

snd_pcm_t *	playhandle = NULL;
snd_pcm_t *	rechandle = NULL;

int m_sampleRate = 12000;  // Ardopcf always uses 12000 samples per second
int m_playchannels = 1;  // 1 for mono, 2 for stereo
int m_recchannels = 1;  // 1 for mono, 2 for stereo
snd_pcm_uframes_t fpp;  // Frames per period.
int dir;
char SavedCaptureDevice[256];  // Saved so we can reopen
char SavedPlaybackDevice[256];
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
	snd_ctl_t *handle = NULL;
	snd_pcm_t *pcm = NULL;
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
	// TODO: Refactor to eliminate use of goto?
	while (card >= 0) {
		sprintf(hwdev, "hw:%d", card);
		if ((err = snd_ctl_open(&handle, hwdev, 0)) != 0) {
			ZF_LOGI("Unable to open audio playback card %s (%s), skipping...",
				hwdev, snd_strerror(err));
			goto nextcard;
		}
		if ((err = snd_ctl_card_info(handle, info)) != 0) {
			ZF_LOGI("Unable to get audio playback card %s info (%s), skipping...",
				hwdev, snd_strerror(err));
			goto nextcard;
		}

		ZF_LOGI("Card %d, ID `%s', name `%s'", card, snd_ctl_card_info_get_id(info),
			snd_ctl_card_info_get_name(info));

		dev = -1;
		if (snd_ctl_pcm_next_device(handle, &dev) < 0) {
			// Card has no devices
			snd_ctl_close(handle);
			goto nextcard;
		}

		while (dev >= 0) {
			snd_pcm_info_set_device(pcminfo, dev);
			snd_pcm_info_set_subdevice(pcminfo, 0);
			snd_pcm_info_set_stream(pcminfo, stream);
			if ((err = snd_ctl_pcm_info(handle, pcminfo)) != 0) {
				ZF_LOGI("Unable to get audio playback device hw:%d,%d info (%s), skipping...",
					card, dev, snd_strerror(err));
				goto nextdevice;
			}
			nsubd = snd_pcm_info_get_subdevices_count(pcminfo);

			ZF_LOGI("  Device hw:%d,%d ID `%s', name `%s', %d subdevices (%d available)",
				card, dev, snd_pcm_info_get_id(pcminfo), snd_pcm_info_get_name(pcminfo),
				nsubd, snd_pcm_info_get_subdevices_avail(pcminfo));

			sprintf(hwdev, "hw:%d,%d", card, dev);
			if ((err = snd_pcm_open(&pcm, hwdev, stream, SND_PCM_NONBLOCK)) != 0) {
				ZF_LOGW("Error %d opening output device (%s)",
					err, snd_strerror(err));
				pcm = NULL;
				goto nextdevice;
			}

			// Get parameters for this device
			if ((err = snd_pcm_hw_params_any(pcm, pars)) != 0) {
				ZF_LOGI("Error initializing audio parameters (%s)",
					snd_strerror(err));
				snd_pcm_close(pcm);
				pcm = NULL;
				goto nextdevice;
			}
			if ((err = snd_pcm_hw_params_get_channels_min(pars, &min)) != 0) {
				ZF_LOGI("Error getting minimum channels for audio device (%s)",
					snd_strerror(err));
				snd_pcm_close(pcm);
				pcm = NULL;
				goto nextdevice;
			}
			if ((err = snd_pcm_hw_params_get_channels_max(pars, &max)) != 0) {
				ZF_LOGI("Error getting maximum channels for audio device (%s)",
					snd_strerror(err));
				snd_pcm_close(pcm);
				pcm = NULL;
				goto nextdevice;
			}
			if ((err = snd_pcm_hw_params_get_rate_min(pars, &ratemin, NULL)) != 0) {
				ZF_LOGI("Error getting minimum sample rate for audio device (%s)",
					snd_strerror(err));
				snd_pcm_close(pcm);
				pcm = NULL;
				goto nextdevice;
			}
			if ((err = snd_pcm_hw_params_get_rate_max(pars, &ratemax, NULL)) != 0) {
				ZF_LOGI("Error getting maximum sample rate for audio device (%s)",
					snd_strerror(err));
				snd_pcm_close(pcm);
				pcm = NULL;
				goto nextdevice;
			}
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
			pcm = NULL;

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
	snd_ctl_t *handle = NULL;
	snd_pcm_t *pcm = NULL;
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
	// TODO: Refactor to eliminate use of goto?
	while(card >= 0) {
		sprintf(hwdev, "hw:%d", card);
		if ((err = snd_ctl_open(&handle, hwdev, 0)) != 0) {
			ZF_LOGI("Unable to open audio capture card %s (%s), skipping...",
				hwdev, snd_strerror(err));
			goto nextcard;
		}
		if ((err = snd_ctl_card_info(handle, info)) != 0) {
			ZF_LOGI("Unable to get audio capture card %s info (%s), skipping...",
				hwdev, snd_strerror(err));
			goto nextcard;
		}

		ZF_LOGI("Card %d, ID `%s', name `%s'", card, snd_ctl_card_info_get_id(info),
			snd_ctl_card_info_get_name(info));

		dev = -1;
		if (snd_ctl_pcm_next_device(handle, &dev) < 0) {  // No Devices
			snd_ctl_close(handle);
			goto nextcard;
		}

		while(dev >= 0) {
			snd_pcm_info_set_device(pcminfo, dev);
			snd_pcm_info_set_subdevice(pcminfo, 0);
			snd_pcm_info_set_stream(pcminfo, stream);
			if ((err = snd_ctl_pcm_info(handle, pcminfo)) != 0) {
				ZF_LOGI("Unable to get audio capture device hw:%d,%d info (%s), skipping...",
					card, dev, snd_strerror(err));
				goto nextdevice;
			}

			nsubd = snd_pcm_info_get_subdevices_count(pcminfo);
			ZF_LOGI("  Device hw:%d,%d ID `%s', name `%s', %d subdevices (%d available)",
				card, dev, snd_pcm_info_get_id(pcminfo), snd_pcm_info_get_name(pcminfo),
				nsubd, snd_pcm_info_get_subdevices_avail(pcminfo));

			sprintf(hwdev, "hw:%d,%d", card, dev);

			if ((err = snd_pcm_open(&pcm, hwdev, stream, SND_PCM_NONBLOCK)) != 0) {
				ZF_LOGW("Error %d opening input device (%s)",
					err, snd_strerror(err));
				pcm = NULL;
				goto nextdevice;
			}

			// Get parameters for this device
			if ((err = snd_pcm_hw_params_any(pcm, pars)) != 0) {
				ZF_LOGI("Error initializing audio parameters (%s)",
					snd_strerror(err));
				snd_pcm_close(pcm);
				pcm = NULL;
				goto nextdevice;
			}
			if ((err = snd_pcm_hw_params_get_channels_min(pars, &min)) != 0) {
				ZF_LOGI("Error getting minimum channels for audio device (%s)",
					snd_strerror(err));
				snd_pcm_close(pcm);
				pcm = NULL;
				goto nextdevice;
			}
			if ((err = snd_pcm_hw_params_get_channels_max(pars, &max)) != 0) {
				ZF_LOGI("Error getting maximum channels for audio device (%s)",
					snd_strerror(err));
				snd_pcm_close(pcm);
				pcm = NULL;
				goto nextdevice;
			}
			if ((err = snd_pcm_hw_params_get_rate_min(pars, &ratemin, NULL)) != 0) {
				ZF_LOGI("Error getting minimum sample rate for audio device (%s)",
					snd_strerror(err));
				snd_pcm_close(pcm);
				pcm = NULL;
				goto nextdevice;
			}
			if ((err = snd_pcm_hw_params_get_rate_max(pars, &ratemax, NULL)) != 0) {
				ZF_LOGI("Error getting maximum sample rate for audio device (%s)",
					snd_strerror(err));
				snd_pcm_close(pcm);
				pcm = NULL;
				goto nextdevice;
			}
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
			CaptureDevices[CaptureDevicesCount++] = strdup(NameString);

			snd_pcm_close(pcm);
			pcm = NULL;

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


// This function and log_all_pcm_hw_params_final() which are called by
// open_audio() are not required for correct operation.  However, they are
// useful for debugging problems related to audio device configuration.
//
// This function writes the full range of parameters supported by an audio
// device before it has been configured, which may be useful to diagnose
// problems configuring that device.
void log_all_pcm_hw_params_multi(snd_pcm_hw_params_t *params) {
	int err;
	unsigned int channels_min, channels_max;
	char em[] = "##############################";
	// This function should do nothing unless ZF_LOGV() will write to console
	// or log file.
	if (!ZF_LOG_ON_VERBOSE)
		return;

	ZF_LOGV("log_all_pcm_hw_params_multi()");
	if ((err = snd_pcm_hw_params_get_channels_min(params, &channels_min)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_channels_min() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_channels_min() -> channels_min=%u",
			channels_min);
	}
	if ((err = snd_pcm_hw_params_get_channels_max(params, &channels_max)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_channels_max() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_channels_max() -> channels_max=%u",
			channels_max);
	}

	unsigned int approx_rate_min, approx_rate_max;
	int rate_min_dir, rate_max_dir;
	if ((err = snd_pcm_hw_params_get_rate_min(params, &approx_rate_min, &rate_min_dir)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_rate_min() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_rate_min() -> approx_rate_min=%u (Hz) rate_min_dir=%i",
			approx_rate_min, rate_min_dir);
	}
	if ((err = snd_pcm_hw_params_get_rate_max(params, &approx_rate_max, &rate_max_dir)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_rate_max() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_rate_max() -> approx_rate_max=%u (Hz) rate_max_dir=%i",
			approx_rate_max, rate_max_dir);
	}

	unsigned int approx_period_time_min, approx_period_time_max;
	int period_time_min_dir, period_time_max_dir;
	if ((err = snd_pcm_hw_params_get_period_time_min(params, &approx_period_time_min, &period_time_min_dir)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_period_time_min() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_period_time_min() -> approx_period_time_min=%u (us) period_time_min_dir=%i",
			approx_period_time_min, period_time_min_dir);
	}
	if ((err = snd_pcm_hw_params_get_period_time_max(params, &approx_period_time_max, &period_time_max_dir)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_period_time_max() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_period_time_max() -> approx_period_time_max=%u (us) period_time_max_dir=%i",
			approx_period_time_max, period_time_max_dir);
	}

	snd_pcm_uframes_t approx_period_size_min, approx_period_size_max;
	int period_size_min_dir, period_size_max_dir;
	if ((err = snd_pcm_hw_params_get_period_size_min(params, &approx_period_size_min, &period_size_min_dir)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_period_size_min() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_period_size_min() -> approx_period_size_min=%lu (frames) period_size_min_dir=%i",
			approx_period_size_min, period_size_min_dir);
	}
	if ((err = snd_pcm_hw_params_get_period_size_max(params, &approx_period_size_max, &period_size_max_dir)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_period_size_max() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_period_size_max() -> approx_period_size_max=%lu (frames) period_size_max_dir=%i",
			approx_period_size_max, period_size_max_dir);
	}

	unsigned int approx_periods_min, approx_periods_max;
	int periods_min_dir, periods_max_dir;
	if ((err = snd_pcm_hw_params_get_periods_min(params, &approx_periods_min, &periods_min_dir)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_periods_min() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_periods_min() -> approx_periods_min=%u (per buffer) periods_min_dir=%i",
			approx_periods_min, periods_min_dir);
	}
	if ((err = snd_pcm_hw_params_get_periods_max(params, &approx_periods_max, &periods_max_dir)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_periods_max() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_periods_max() -> approx_periods_max=%u (per buffer) periods_max_dir=%i",
			approx_periods_max, periods_max_dir);
	}

	unsigned int approx_buffer_time_min, approx_buffer_time_max;
	int buffer_time_min_dir, buffer_time_max_dir;
	if ((err = snd_pcm_hw_params_get_buffer_time_min(params, &approx_buffer_time_min, &buffer_time_min_dir)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_buffer_time_min() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_buffer_time_min() -> approx_buffer_time_min=%u (us) buffer_time_min_dir=%i",
			approx_buffer_time_min, buffer_time_min_dir);
	}
	if ((err = snd_pcm_hw_params_get_buffer_time_max(params, &approx_buffer_time_max, &buffer_time_max_dir)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_buffer_time_max() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_buffer_time_max() -> approx_buffer_time_max=%u (us) buffer_time_max_dir=%i",
			approx_buffer_time_max, buffer_time_max_dir);
	}

	snd_pcm_uframes_t buffer_size_min, buffer_size_max;
	if ((err = snd_pcm_hw_params_get_buffer_size_min(params, &buffer_size_min)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_buffer_size_min() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_buffer_size_min() -> buffer_size_min=%lu (frames)",
			buffer_size_min);
	}
	if ((err = snd_pcm_hw_params_get_buffer_size_max(params, &buffer_size_max)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_buffer_size_max() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_buffer_size_max() -> buffer_size_max=%lu (frames)",
			buffer_size_max);
	}
}

// This function and log_all_pcm_hw_params_multi() which are called by
// open_audio() are not required for correct operation.  However, they are
// useful for debugging problems related to audio device configuration.
//
// This function writes the parameters being used by a configured audio device,
// which may be useful to diagnose problems configuring that device.
void log_all_pcm_hw_params_final(snd_pcm_t *handle, snd_pcm_hw_params_t *params) {
	int err;
	unsigned int rate_num, rate_den;
	char em[] = "##############################";
	// This function should do nothing unless ZF_LOGV() will write to console
	// or log file.
	if (!ZF_LOG_ON_VERBOSE)
		return;

	ZF_LOGV("log_all_pcm_hw_params_final()");
	if ((err = snd_pcm_hw_params_get_rate_numden(params, &rate_num, &rate_den)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_rate_numden() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_rate_numden() -> rate_num=%u (Hz) rate_den=%u",
			rate_num, rate_den);
	}

	int sbits = snd_pcm_hw_params_get_sbits(params);
	ZF_LOGV("snd_pcm_hw_params_get_sbits()=%i", sbits);

	int fifo_size = snd_pcm_hw_params_get_fifo_size(params);
	ZF_LOGV("snd_pcm_hw_params_get_fifo_size()=%i", fifo_size);

	snd_pcm_access_t access;
	if ((err = snd_pcm_hw_params_get_access(params, &access)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_access() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_access() -> access=%u (expect"
			" SND_PCM_ACCESS_RW_INTERLEAVED=%u)",
			access, SND_PCM_ACCESS_RW_INTERLEAVED);
	}

	snd_pcm_format_t format;
	if ((err = snd_pcm_hw_params_get_format(params, &format)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_format() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_format() -> format=%u (expect"
		" SND_PCM_FORMAT_S16_LE=%u)",
		format, SND_PCM_FORMAT_S16_LE);
	}

	snd_pcm_subformat_t subformat;
	if ((err = snd_pcm_hw_params_get_subformat(params, &subformat)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_subformat() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_subformat() -> subformat=%u (expect"
		" SND_PCM_SUBFORMAT_STD=%u)",
		subformat, SND_PCM_SUBFORMAT_STD);
	}

	unsigned int channels;
	if ((err = snd_pcm_hw_params_get_channels(params, &channels)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_channels() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_channels() -> channels=%u", channels);
	}

	unsigned int approx_rate;
	int rate_dir;
	if ((err = snd_pcm_hw_params_get_rate(params, &approx_rate, &rate_dir)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_rate() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_rate() -> approx_rate=%u (Hz) rate_dir=%i",
			approx_rate, rate_dir);
	}
	if (rate_dir != 0 || rate_num / rate_den != approx_rate) {
		ZF_LOGV("%s\nWARNING: sample rate may be inexact.  rate_num=%u (Hz),"
			" rate_den=%u, so rate_num/rate_den=%f (Hz). approx_rate=%u (Hz),"
			" rate_dir=%i\n%s",
			em, rate_num, rate_den, 1.0 * rate_num / rate_den, approx_rate,
			rate_dir, em);
	}

	unsigned int resample;
	if ((err = snd_pcm_hw_params_get_rate_resample(handle, params, &resample)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_rate_resample() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_rate_resample() -> resample=%u (bool)",
			resample);
	}

	unsigned int export_buffer;
	if ((err = snd_pcm_hw_params_get_export_buffer(handle, params, &export_buffer)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_export_buffer() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_export_buffer() -> export_buffer=%u (bool)",
			export_buffer);
	}

	unsigned int period_wakeup;
	if ((err = snd_pcm_hw_params_get_period_wakeup(handle, params, &period_wakeup)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_period_wakeup() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_period_wakeup() -> period_wakeup=%u (bool)",
			period_wakeup);
	}

	unsigned int approx_period_time, approx_period_time_max, approx_period_time_min;
	int period_time_dir, period_time_dir_max, period_time_dir_min;
	if ((err = snd_pcm_hw_params_get_period_time(params, &approx_period_time, &period_time_dir)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_period_time() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_period_time() -> approx_period_time=%u (us) period_time_dir=%i",
			approx_period_time, period_time_dir);
	}
	if (period_time_dir != 0) {
		ZF_LOGV("%s\nWARNING: (period_time_dir=%i) != 0.  So, approx_period_time=%u"
			" is inexact and it will be impossible to confirm consistency of"
			" sample rate, period size, and period time.\n%s",
			em, period_time_dir, approx_period_time, em);
		if ((err = snd_pcm_hw_params_get_period_time_min(params, &approx_period_time_min, &period_time_dir_min)) < 0) {
			ZF_LOGV("%s\n  Error from snd_pcm_hw_params_get_period_time_min() (%s)\n%s",
				em, snd_strerror(err), em);
		} else {
			ZF_LOGV("  snd_pcm_hw_params_get_period_time_min() -> approx_period_time_min=%u (us) period_time_min_dir=%i",
				approx_period_time_min, period_time_dir_min);
		}
		if ((err = snd_pcm_hw_params_get_period_time_max(params, &approx_period_time_max, &period_time_dir_max)) < 0) {
			ZF_LOGV("%s\n  Error from snd_pcm_hw_params_get_period_time_max() (%s)\n%s",
				em, snd_strerror(err), em);
		} else {
			ZF_LOGV("  snd_pcm_hw_params_get_period_time_max() -> approx_period_time_max=%u (us) period_time_max_dir=%i",
				approx_period_time_max, period_time_dir_max);
		}
	}


	snd_pcm_uframes_t approx_period_size, approx_period_size_min, approx_period_size_max;
	int period_size_dir, period_size_dir_min, period_size_dir_max;
	if ((err = snd_pcm_hw_params_get_period_size(params, &approx_period_size, &period_size_dir)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_period_size() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_period_size() -> approx_period_size=%lu (frames) period_size_dir=%i",
			approx_period_size, period_size_dir);
	}
	if (period_size_dir != 0) {
		ZF_LOGV("%s\nWARNING: (period_size_dir=%i) != 0.  So, approx_period_size=%lu"
			" is inexact and it will be impossible to confirm consistency of"
			" sample rate, period size, and period time.\n%s",
			em, period_size_dir, approx_period_size, em);
		if ((err = snd_pcm_hw_params_get_period_size_min(params, &approx_period_size_min, &period_size_dir_min)) < 0) {
			ZF_LOGV("%s\n  Error from snd_pcm_hw_params_get_period_size_min() (%s)\n%s",
				em, snd_strerror(err), em);
		} else {
			ZF_LOGV("  snd_pcm_hw_params_get_period_size_min() -> approx_period_size_min=%lu (us) period_size_min_dir=%i",
				approx_period_size_min, period_size_dir_min);
		}
		if ((err = snd_pcm_hw_params_get_period_size_max(params, &approx_period_size_max, &period_size_dir_max)) < 0) {
			ZF_LOGV("%s\n  Error from snd_pcm_hw_params_get_period_size_max() (%s)\n%s",
				em, snd_strerror(err), em);
		} else {
			ZF_LOGV("  snd_pcm_hw_params_get_period_size_max() -> approx_period_size_max=%lu (us) period_size_max_dir=%i",
				approx_period_size_max, period_size_dir_max);
		}
	}

	unsigned int approx_periods, approx_periods_max, approx_periods_min;
	int periods_dir, periods_dir_max, periods_dir_min;
	if ((err = snd_pcm_hw_params_get_periods(params, &approx_periods, &periods_dir)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_periods() (%s)\n"
			"This shouldn't happen unless periods has not been set. "
			" However, sometimes it does.  So, get and max and min values",
			em, snd_strerror(err));
		if ((err = snd_pcm_hw_params_get_periods_min(params, &approx_periods_min, &periods_dir_min)) < 0) {
			ZF_LOGV("%s\n  Error from snd_pcm_hw_params_get_periods_min() (%s)\n%s",
				em, snd_strerror(err), em);
		} else {
			ZF_LOGV("  snd_pcm_hw_params_get_periods_min() -> approx_min_periods=%u (per buffer) periods_dir=%i",
				approx_periods_min, periods_dir_min);
		}
		if ((err = snd_pcm_hw_params_get_periods_max(params, &approx_periods_max, &periods_dir_max)) < 0) {
			ZF_LOGV("%s\n  Error from snd_pcm_hw_params_get_periods_max() (%s)\n%s",
				em, snd_strerror(err), em);
		} else {
			ZF_LOGV("  snd_pcm_hw_params_get_periods_max() -> approx_max_periods=%u (per buffer) periods_dir=%i",
				approx_periods_max, periods_dir_max);
		}
		ZF_LOGV("%s", em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_periods() -> approx_periods=%u (per buffer) periods_dir=%i",
			approx_periods, periods_dir);
	}

	unsigned int approx_buffer_time;
	int buffer_time_dir;
	if ((err = snd_pcm_hw_params_get_buffer_time(params, &approx_buffer_time, &buffer_time_dir)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_buffer_time() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_buffer_time() -> approx_buffer_time=%u (us) buffer_time_dir=%i",
			approx_buffer_time, buffer_time_dir);
	}

	// Note that period size is approx, but buffer size is exact.
	snd_pcm_uframes_t buffer_size;
	if ((err = snd_pcm_hw_params_get_buffer_size(params, &buffer_size)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_buffer_size() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_buffer_size() -> buffer_size=%lu (frames)",
			buffer_size);
	}

	// ??
	snd_pcm_uframes_t min_align;
	if ((err = snd_pcm_hw_params_get_min_align(params, &min_align)) < 0) {
		ZF_LOGV("%s\nError from snd_pcm_hw_params_get_min_align() (%s)\n%s",
			em, snd_strerror(err), em);
	} else {
		ZF_LOGV("snd_pcm_hw_params_get_min_align() -> min_align=%lu",
			min_align);
	}


	if (approx_period_time * approx_rate != approx_period_size * 1000000) {
		if (period_time_dir == 0 && period_size_dir == 0) {
			double calc_rate = (approx_period_size * 1000000.0) / approx_period_time;
			ZF_LOGV("%s\n%s\nERROR: Inconsistent audio settings.\n"
				"(period size * 1,000,000) / period time = sample rate.\n"
				"(%i * 1,000,000) / %i = %.3f, but expected %i.\n"
				"This is an error of about %.3fppm.\n%s\n%s",
				em, em, (int) approx_period_size, approx_period_time,
				calc_rate, approx_rate,
				(calc_rate - approx_rate) / approx_rate * 1000000.0,
				em, em);
		} else if (period_time_dir != 0 && period_size_dir == 0) {
			double calc_rate1 = (approx_period_size * 1000000.0) / approx_period_time;
			double calc_rate2 = (approx_period_size * 1000000.0) / (approx_period_time + period_time_dir);
			ZF_LOGV("%s\n%s\nERROR: Inconsistent audio settings.\n"
				"Expect: (period size * 1,000,000) / period time = sample rate.\n"
				"(%i * 1,000,000) / (%i or %i) = %.3f or %.3f.  Expected %i.\n"
				"This is an error of about %.3f or %.3f ppm.\n%s\n%s",
				em, em, (int) approx_period_size, approx_period_time,
				approx_period_time + period_time_dir,
				calc_rate1, calc_rate2, approx_rate,
				(calc_rate1 - approx_rate) / approx_rate * 1000000.0,
				(calc_rate2 - approx_rate) / approx_rate * 1000000.0,
				em, em);
		} else if (period_time_dir == 0 && period_size_dir != 0) {
			double calc_rate1 = (approx_period_size * 1000000.0) / approx_period_time;
			double calc_rate2 = ((approx_period_size + period_size_dir) * 1000000.0) / approx_period_time;
			ZF_LOGV("%s\n%s\nERROR: Inconsistent audio settings.\n"
				"Expect: (period size * 1,000,000) / period time = sample rate.\n"
				"(%i * 1,000,000) / (%i or %i) = %.3f or %.3f.  Expected %i.\n"
				"This is an error of about %.3f or %.3f ppm.\n%s\n%s",
				em, em, (int) approx_period_size,
				approx_period_time, approx_period_time + period_time_dir,
				calc_rate1, calc_rate2, approx_rate,
				(calc_rate1 - approx_rate) / approx_rate * 1000000.0,
				(calc_rate2 - approx_rate) / approx_rate * 1000000.0,
				em, em);
		} else if (period_time_dir != 0 && period_size_dir != 0) {
			double calc_rate1, calc_rate2;
			if (period_time_dir == period_size_dir) {
				calc_rate1 = (approx_period_size * 1000000.0) / (approx_period_time + period_time_dir);
				calc_rate2 = ((approx_period_size + period_size_dir) * 1000000.0) / approx_period_time;
			} else {
				calc_rate1 = (approx_period_size * 1000000.0) / approx_period_time;
				calc_rate2 = ((approx_period_size + period_size_dir) * 1000000.0)
					/ (approx_period_time + period_time_dir);
			}
			ZF_LOGV("%s\n%s\nERROR: Inconsistent audio settings.\n"
				"Expect: (period size * 1,000,000) / period time = sample rate.\n"
				"((%i or %i) * 1,000,000) / (%i or %i) = %.3f or %.3f.  Expected %i.\n"
				"This is an error of about %.3f or %.3f ppm.\n%s\n%s",
				em, em, (int) approx_period_size, (int) approx_period_size + period_size_dir,
				approx_period_time, approx_period_time + period_time_dir,
				calc_rate1, calc_rate2, approx_rate,
				(calc_rate1 - approx_rate) / approx_rate * 1000000.0,
				(calc_rate2 - approx_rate) / approx_rate * 1000000.0,
				em, em);
		}
	} else {
		ZF_LOGV("Audio settings OK: %d * %d == %lu * 1000000.",
			approx_period_time, approx_rate, approx_period_size);
	}
}


// Open audio device.  If successful, return the handle to the configured
// ALSA audio device.  Device is prepared, but not started.
// On failure, return NULL.
snd_pcm_t * open_audio(const char *devstr, bool iscapture, int ch) {
	const int rate = 12000;  // Sample rate is always 12000 for ardopcf
	int bsize = 24000;  // buffer size (near is OK) for buffer time of 2 seconds
	int psize = 240;  // period size (near is OK)
	int mode = iscapture ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK;
	char forstr[9];
	//  bsize and psize of type required for snd_pcm functions
	snd_pcm_uframes_t pcm_bsize = (snd_pcm_uframes_t) bsize;
	snd_pcm_uframes_t pcm_psize = (snd_pcm_uframes_t) psize;

	int period_size_dir = 0;

	snd_pcm_t *handle;
	int err;
	snd_pcm_info_t *info;
	const char *name = NULL;
	snd_pcm_hw_params_t *params;
	char em[] = "##############################";

	snprintf(forstr, sizeof(forstr), "%s", iscapture ? "capture" : "playback");

	if ((err = snd_pcm_open(&handle, devstr, mode, 0)) < 0) {
		ZF_LOGE("Error opening audio device %s for %s (%s)",
			devstr, forstr, snd_strerror(err));
		return NULL;
	}

	if ((err = snd_pcm_info_malloc(&info)) < 0) {
		ZF_LOGE("Error allocating audio info for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_close(handle);
		return NULL;
	}
	if ((err = snd_pcm_info(handle, info)) < 0) {
		ZF_LOGE("Error getting audio info for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_info_free(info);
		snd_pcm_close(handle);
		return NULL;
	}
	if ((name = snd_pcm_info_get_name(info)) == NULL) {
		ZF_LOGE("Error getting name from audio info for %s.", forstr);
		snd_pcm_info_free(info);
		snd_pcm_close(handle);
		return NULL;
	}
	ZF_LOGD("Opening audio device for %s with device name = %s",
		forstr, name);
	snd_pcm_info_free(info);
	info = NULL;

	if ((err = snd_pcm_hw_params_malloc(&params)) < 0) {
		ZF_LOGE("Error allocating audio parameters for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_close(handle);
		return NULL;
	}
	if ((err = snd_pcm_hw_params_any(handle, params)) < 0) {
		ZF_LOGE("Error initializing audio parameters for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(handle);
		return NULL;
	}

	log_all_pcm_hw_params_multi(params);

	// Set parameters
	if ((err = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
		ZF_LOGE("Error setting audio access to interleaved read/write for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(handle);
		return NULL;
	}
	snd_pcm_access_t access;
	if ((err = snd_pcm_hw_params_get_access(params, &access)) < 0) {
		ZF_LOGE("Error from snd_pcm_hw_params_get_access() for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(handle);
		return NULL;
	}
	if (access != SND_PCM_ACCESS_RW_INTERLEAVED) {
		ZF_LOGE("Tried to set access to %u (SND_PCM_ACCESS_RW_INTERLEAVED) for"
			" %s but snd_pcm_hw_params_get_access() -> access=%u without"
			" returning an error",
			SND_PCM_ACCESS_RW_INTERLEAVED, forstr, access);
		snd_pcm_hw_params_free(params);
		snd_pcm_close(handle);
		return NULL;
	}

	// This explicitly sets data to be read/written as little-endian.  If this
	// code is ever used on a big-endian machine, this may need to be changed.
	if ((err = snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE)) < 0) {
		ZF_LOGE("Error setting audio access to 16-bit LE for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(handle);
		return NULL;
	}
	snd_pcm_format_t format;
	if ((err = snd_pcm_hw_params_get_format(params, &format)) < 0) {
		ZF_LOGE("Error from snd_pcm_hw_params_get_format() for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(handle);
		return NULL;
	}
	if (format != SND_PCM_FORMAT_S16_LE) {
		ZF_LOGE("Tried to set format to %u (SND_PCM_FORMAT_S16_LE) for %s but"
			" snd_pcm_hw_params_get_format() -> format=%u without returning"
			" an error",
			SND_PCM_FORMAT_S16_LE, forstr, format);
		snd_pcm_hw_params_free(params);
		snd_pcm_close(handle);
		return NULL;
	}

	if ((err = snd_pcm_hw_params_set_channels(handle, params, ch)) < 0) {
		ZF_LOGE("Error setting number of audio channels to %i for %s (%s)",
			ch, forstr, snd_strerror(err));
		if (m_playchannels  == 2) {
			ZF_LOGE("The -%s command line option was used to indicate that the"
				" %s channel of a stereo audio device should be used for %s."
				" However, the audio playback device specified (%s) could not"
				" be opened as a two channel (stereo) device.  Try again"
				" without the -%s command line option to open it as a single"
				" channel (mono) device.",
				iscapture ? (UseLeftRX ? "L" : "R") : (UseLeftTX ? "y" : "z"),
				(iscapture ? UseLeftTX : UseLeftRX) ? "left" : "right",
				forstr,
				iscapture ? CaptureDevice : PlaybackDevice,
				iscapture ? (UseLeftRX ? "L" : "R") : (UseLeftTX ? "y" : "z"));
			return false;
		}
		ZF_LOGE("Neither the %s command line option was used to indicate"
			" that the specified %s audio device (%s) should be opened"
			" as a stereo device, and to select which of its channels should be"
			" used.  Thus, the device was opened as a single channel (mono)"
			" audio device, but this failed.  This probably means that the"
			" device can only be opened as a two channel (stereo) device.  So,"
			" please try again using either the %s command line option to"
			" indicate which channel to use.",
			iscapture ? "-L nor -R" : "-y nor -z",
			forstr,
			PlaybackDevice,
			iscapture ? "-L or -R" : "-y or -z");
		snd_pcm_hw_params_free(params);
		snd_pcm_close(handle);
		return NULL;
	}
	unsigned int channels;
	if ((err = snd_pcm_hw_params_get_channels(params, &channels)) < 0) {
		ZF_LOGE("Error from snd_pcm_hw_params_get_channels() for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(handle);
		return NULL;
	}
	if ((int) channels != ch) {
		ZF_LOGE("Tried to set channels to %u for %s but"
			" snd_pcm_hw_params_get_channels() -> channels=%u without returning"
			" an error",
			ch, forstr, channels);
		snd_pcm_hw_params_free(params);
		snd_pcm_close(handle);
		return NULL;
	}

	if ((err = snd_pcm_hw_params_set_rate(handle, params, rate, 0)) < 0) {
		ZF_LOGE("Error setting audio sample rate to %i (Hz) for %s (%s)\n",
			rate, forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(handle);
		return NULL;
	}
	unsigned int approx_rate;
	int rate_dir;
	if ((err = snd_pcm_hw_params_get_rate(params, &approx_rate, &rate_dir)) < 0) {
		ZF_LOGE("Error from snd_pcm_hw_params_get_rate() for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(handle);
		return NULL;
	}
	if (rate_dir != 0) {
		ZF_LOGW("%s\nWARNING: Tried to set sample rate to %u for %s but"
			" snd_pcm_hw_params_get_rate() -> approx_rate=%u and rate_dir=%u"
			" The non-zero rate_dir suggests that approx_rate is inexact.\n%s",
			em, rate, forstr, approx_rate, rate_dir, em);
		// While perhaps undesirable, this is not necessarily an error, so don't
		// fail.
	} else if ((int) approx_rate != rate) {
		ZF_LOGE("Tried to set sample rate to %u for %s but"
			" snd_pcm_hw_params_get_rate() -> approx_rate=%u and rate_dir=%u"
			" without returning an error",
			rate, forstr, approx_rate, rate_dir);
		snd_pcm_hw_params_free(params);
		snd_pcm_close(handle);
		return NULL;
	}

	// According to a comment dated 20200913 by "borine" at
	// https://github.com/Arkq/bluez-alsa/issues/376:
	// "It is not well documented, but when configuring the hardware params you
	// should set the period size before setting the buffer size."
	// Otherwise, snd_pcm_hw_params_get_periods() may fail with (Invalid argument)

	if ((err = snd_pcm_hw_params_set_period_size_near(handle, params, &pcm_psize, &period_size_dir)) < 0) {
		ZF_LOGE("Error setting period size to near %i (frames) for %s. (%s)",
			psize, forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(handle);
		return NULL;
	}
	if (psize != (int) pcm_psize) {
		ZF_LOGW("%s\nWARNING: Setting period_size near %i resulted in %i (frames)"
			" for %s.\n%s",
			em, psize, (int) pcm_psize, forstr, em);
	}
	psize = (int) pcm_psize;

	if ((err = snd_pcm_hw_params_set_buffer_size_near(handle, params, &pcm_bsize)) < 0) {
		ZF_LOGE("Error setting audio buffer size near %lu (frames) for %s (%s)",
			pcm_bsize, forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(handle);
		return NULL;
	}
	if (bsize != (int) pcm_bsize) {
		ZF_LOGW("%s\nWARNING: Setting buffer_size near %i resulted in %i (frames)"
			" for %s \n%s",
			em, bsize, (int) pcm_bsize, forstr, em);
	}
	bsize = (int) pcm_bsize;
	ZF_LOGD("For %s: buffer_size = %i (frames) gives a buffer time of about %i ms.",
		forstr, bsize, 1000 * bsize / rate);

	if ((err = snd_pcm_hw_params(handle, params)) < 0) {
		ZF_LOGE("Error setting audio parameters for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(handle);
		return NULL;
	}

	log_all_pcm_hw_params_final(handle, params);

	snd_pcm_hw_params_free(params);
	if ((err = snd_pcm_prepare(handle)) < 0) {
		ZF_LOGE("Error preparing audio for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_close(handle);
		return NULL;
	}
	return handle;
}

bool OpenSoundPlayback(char * PlaybackDevice) {
	if (strcmp(PlaybackDevice, "NOSOUND") == 0)
		// For testing/diagnostic purposes, NOSOUND uses no audio device,
		return true;

	if (playhandle) {
		snd_pcm_close(playhandle);
		playhandle = NULL;
		ZF_LOGE("ERROR: OpenSoundPlayback() called, but device already open.");
		blnClosing = true;
		return false;
	}

	playhandle = open_audio(PlaybackDevice, false, m_playchannels);
	if (playhandle == NULL)
		return false;  // Error already logged
	return true;
}


bool OpenSoundCapture(char * CaptureDevice) {
	int err;
	if (strcmp(CaptureDevice, "NOSOUND") == 0)
		// For testing/diagnostic purposes, NOSOUND uses no audio device,
		return true;

	if (rechandle) {
		snd_pcm_close(rechandle);
		rechandle = NULL;
		ZF_LOGE("ERROR: OpenSoundCapture() called, but device already open.");
		blnClosing = true;
		return false;
	}

	rechandle = open_audio(CaptureDevice, true, m_recchannels);
	if (rechandle == NULL)
		return false;  // Error already logged
	if ((err = snd_pcm_start(rechandle)) < 0) {
		ZF_LOGE("Error starting audio for capture (%s)",
			snd_strerror(err));
		return false;
	}
	return true;
}

// Return true on success, false on failure
bool attempt_recovery(bool iscapture, int snderr) {
	bool opened;
	snd_pcm_t ** handleptr = iscapture ? &rechandle : &playhandle;
	if (snderr == -EPIPE && !iscapture) {
		ZF_LOGV("A Broken Pipe error occured while attempting audio device"
			" write.  For some sound devices such as ALSA dmix and its"
			" derivatives, this occurs at the start of each transmission. "
			" Attempting to recover.  The ALSA error in snd_pcm_recover() that"
			" follows is related to this.");
	} else {
		ZF_LOGE("An error occured while attempting audio device %s.  %s. "
			" Attempting to recover.",
			iscapture ? "read" : "write",
			snd_strerror(snderr));
	}
	if (snd_pcm_recover(*handleptr, snderr, 0) < 0) {
		ZF_LOGE("Unable to recover.  Attempting to close and re-open audio"
			" %s device.",
			iscapture ? "capture" : "playback");
		if (*handleptr != NULL) {
			snd_pcm_close(*handleptr);
			*handleptr = NULL;
		}
		if (iscapture)
			opened = OpenSoundCapture(CaptureDevice);
		else
			opened = OpenSoundPlayback(PlaybackDevice);
		if (!opened) {
			// Unable to continue.
			ZF_LOGE("Error re-opening audio %s device.",
				iscapture ? "capture" : "playback");
			blnClosing = true;  // This is detected in the ardop main loop.
			return false;
		}
	} else if (iscapture) {
		// For a capture device, snd_pcm_avail() will always return 0 after
		// successful recovery until snd_pcm_start() is called.
		int err;
		if ((err = snd_pcm_start(rechandle)) < 0) {
			ZF_LOGE("Error restarting audio after snd_pcm_recover() for"
				" capture device. (%s)",
				snd_strerror(err));
			blnClosing = true;  // This is detected in the ardop main loop.
			return false;
		}
	}
	return true;
}


// Write samples to a mono or stereo audio device.
// Return nSamples (number of samples written) on success, or 0 on failure with
// blnClosing also set to true.
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
			// Set left channel value
			if (UseLeftTX)
				*(sampptr++) = input[0];
			else
				*(sampptr++) = 0;

			// Set right channel value
			if (UseRightTX)
				*(sampptr++) = input[0];
			else
				*(sampptr++) = 0;

			input ++;
		}
	}

	int errcount = 0;
	while ((ret = snd_pcm_writei(playhandle, samples, nSamples)) < 0 && !blnClosing) {
		if (++errcount > 10) {
			ZF_LOGE("Exiting ardopcf due to repeated audio write errors..");
			blnClosing = true;  // This is detected in the ardop main loop.
			return 0;
		}
		if (!attempt_recovery(false, ret)) {
			// Unable to recover. blnClosing set, Error message logged.
			return 0;
		}
	}
	return ret;
}


// Return nSamples (number of samples written) on success, or 0 on failure with
// blnClosing also set to true.
// Block until samples can be staged for playback, calling txSleep() while
// waiting.
int SoundCardWrite(short * input, unsigned int nSamples) {
	snd_pcm_sframes_t avail;

	if (playhandle == NULL) {
		ZF_LOGE("ERROR: SoundCardWrite() called, but no audio playback device"
			" is open.");
		blnClosing = true;  // This is detected in the ardop main loop.
		return 0;
	}

	int errcount = 0;
	while ((avail = snd_pcm_avail(playhandle)) < nSamples && !blnClosing) {
		if (avail >= 0 || avail == -EAGAIN) {
			txSleep(100);  // Wait until samples can be staged for playback
		} else {
			// Error
			if (++errcount > 10) {
				ZF_LOGE("Exiting ardopcf due to repeated audio write errors..");
				blnClosing = true;  // This is detected in the ardop main loop.
				return 0;
			}
			if (!attempt_recovery(false, avail)) {
				// Unable to recover. blnClosing set, Error message logged.
				return 0;
			}
		}
	}
	return PackSamplesAndSend(input, nSamples);
}

int SoundCardRead(short * input, unsigned int nSamples) {
	short samples[65536];
	int n;
	int ret;
	int avail;
	int start;

	if (rechandle == NULL) {
		ZF_LOGE("ERROR: SoundCardRead() called, but no audio capture device"
			" is open.");
		blnClosing = true;  // This is detected in the ardop main loop.
		return 0;
	}

	int errcount = 0;
	while ((avail = snd_pcm_avail(rechandle)) < (int) nSamples && !blnClosing) {
		if (avail >= 0 || avail == -EAGAIN) {
			return 0;  // Insufficient samples available.  Do nothing.
		}
		// An error occured.  Try to recover
		if (++errcount > 10) {
			ZF_LOGE("Exiting ardopcf due to repeated audio read errors..");
			blnClosing = true;  // This is detected in the ardop main loop.
			return 0;
		}
		if (!attempt_recovery(true, avail)) {
			// Unable to recover. blnClosing set, Error message logged.
			return 0;
		}
	}

	errcount = 0;
	while ((ret = snd_pcm_readi(rechandle, samples, nSamples)) < 0 && !blnClosing) {
		if (++errcount > 10) {
			ZF_LOGE("Exiting ardopcf due to repeated audio read errors.");
			blnClosing = true;  // This is detected in the ardop main loop.
			return 0;
		}
		if (!attempt_recovery(true, ret)) {
			// Unable to recover. blnClosing set, Error message logged.
			return 0;
		}
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

		for (n = start; n < (ret * 2); n += 2) {  // return alternate
			*(input++) = samples[n];
		}
	}
	return ret;
}

// TODO: Consider calling SoundCardWrite() directly instead
short * SendtoCard(int n) {
	if (playhandle != NULL) {
		SoundCardWrite(&buffer[Index][0], n);
	} else if (strcmp(PlaybackDevice, "NOSOUND") != 0) {
		// if PlaybackDevice is NOSOUND, then playhandle==NULL is not an error
		ZF_LOGE("ERROR: SendtoCard() called, but no audio playback device"
			" is open.");
		// This error is likely to be repeated several times because blnClosing
		// is not detected/acted upon until control returns to the ardop main
		// loop after the end of this transmission.
		blnClosing = true;
	}

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

	if (blnClosing)
		return;  // This avoids spamming the logger with excessive error msgs

	if (Now - errtime > 2000) {
		// It has been more than 2 seconds since the last error
		errcount = 0;
	}
	errtime = Now;
	errcount += 1;

	if (strcmp(function, "snd_pcm_recover") == 0) {
		// For some audio devices such as ALSA dmix and its derivatives, this
		// occurs at the start of every transmission.  So, write this as ZF_LOGV
		// rather than ZF_LOGE.
		if (err) {
			ZF_LOGV("ALSA Error at %s:%i %s(): %s", file, line, function, snd_strerror(err));
		} else {
			ZF_LOGV("ALSA Error at %s:%i %s()", file, line, function);
		}
	} else {
		if (err) {
			ZF_LOGE("ALSA Error at %s:%i %s(): %s", file, line, function, snd_strerror(err));
		} else {
			ZF_LOGE("ALSA Error at %s:%i %s()", file, line, function);
		}
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
		m_recchannels = 1;
		ZF_LOGI("Opening %s for RX as a single channel (mono) device",
			CaptureDevice);
	} else {
		m_recchannels = 2;
		if (UseLeftRX == 0)
			ZF_LOGI("Opening %s for RX as a stereo device and using Right channel",
				CaptureDevice);
		if (UseRightRX == 0)
			ZF_LOGI("Opening %s for RX as a stereo device and using Left channel",
				CaptureDevice);
	}

	if (UseLeftTX == 1 && UseRightTX == 1) {
		m_playchannels = 1;
		ZF_LOGI("Opening %s for TX as a single channel (mono) device",
			PlaybackDevice);
	} else {
		m_playchannels = 2;
		if (UseLeftTX == 0)
			ZF_LOGI("Opening %s for TX as a stereo device and using Right channel",
				PlaybackDevice);
		if (UseRightTX == 0)
			ZF_LOGI("Opening %s for TX as a stereo device and using Left channel",
				PlaybackDevice);
	}

	if (!OpenSoundPlayback(PlaybackDevice)) {
		// OpenSoundPlayback() already logged an error message.
		return false;
	}
	if (!OpenSoundCapture(CaptureDevice)) {
		// OpenSoundCapture() already logged an error message.
		snd_pcm_close(playhandle);
		return false;
	}
	// Capturing is intialized to to be true at startup in ARDOPC.c.  It it were
	// not, StartCapture() would be called here.
	return true;  // Both audio devices successfully opened.
}

// Process any captured samples
// Ideally call at least every 100 mS, more than 200 will loose data
void PollReceivedSamples() {
	if (strcmp(PlaybackDevice, "NOSOUND") == 0)
		return;

	// TODO: If we always use inbuufer[0], why is there inbuffer[1]?
	if (SoundCardRead(&inbuffer[0][0], ReceiveSize) == 0)
		return;  // No samples to process

	if (Capturing) {
		ProcessNewSamples(&inbuffer[0][0], ReceiveSize);
	} else {
		// PreprocessNewSamples() writes these samples to the RX wav
		// file when specified, even though not Capturing.  This
		// produces a time continuous Wav file which can be useful for
		// diagnostic purposes.  In earlier versions of ardopcf, this
		// RX wav file could not be time continuous due to the way that
		// ALSA audio devices were opened, closed, and configured.
		// If Capturing, this is called from ProcessNewSamples().
		PreprocessNewSamples(&inbuffer[0][0], ReceiveSize);
	}
}


void StopCapture() {
	Capturing = false;
	return;
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


// Called at end of transmit to block until all staged audio has been played.
void SoundFlush() {
	// Append Trailer then send remaining samples
	int txlenMs = 0;

	AddTrailer();  // add the trailer.
	SendtoCard(Number);

	// Wait for tx to complete
	//
	// TODO: Can greater consistency be achieved by monitoring the tx audio
	// buffer, as is done in windows/Waveform.c rather than relying on
	// pttOnTime?  Would such an approach be consistent for all linux audio
	// devices including plughw, dmix, and pulse?
	//
	// samples sent is is in SampleNo, time started in mS is in pttOnTime.
	// calculate time to stop
	txlenMs = SampleNo / 12 + 20;  // 12000 samples per sec. 20 mS TXTAIL
	ZF_LOGD("Tx Time %d Time till end = %d", txlenMs, (pttOnTime + txlenMs) - Now);

	if (strcmp(PlaybackDevice, "NOSOUND") != 0)
		txSleep((pttOnTime + txlenMs) - Now);

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
