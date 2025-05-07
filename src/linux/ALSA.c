// ALSA audio for Linux

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#include <alsa/asoundlib.h>

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

snd_pcm_t * playhandle = NULL;
snd_pcm_t * rechandle = NULL;

int m_sampleRate = 12000;  // Ardopcf always uses 12000 samples per second
snd_pcm_uframes_t fpp;  // Frames per period.
int dir;


// If set to true, then alsa_errhandler() will return without writing anything
// to the log.  This may be set temporarily when doing something that is
// expected to produce an error.
bool mute_alsa_errhandler = false;

void StartCapture() {
	Capturing = true;
	DiscardOldSamples();
	ClearAllMixedSamples();
	State = SearchingForLeader;
}

// Some string constants, to avoid repetition
const char * STREAM_OUTPUT = "Output";
const char * STREAM_INPUT = "Input";
const char * SURROUND = "surround";  // Surround Sound devices are skipped

// Populate AudioDevices with both Playback and Capture devices.
// Scans all available PCM objects using ALSA name hint API. This provides a
// much more complete list than if only hardware devices were found using
// snd_next_card().  Since it may be useful for diagnostic purposes, include
// all devices in this list, including those which cannot be opened for capture
// or playback.  However, omit "surround" devices that are not appropriate for
// this program.
// dev->name and dev->alias shall be truncated to DEVSTRSZ bytes including a
// terminating null.
void GetDevices() {
	void **hints, **nexthint;  // hint pointers
	char *name, *desc, *io;  // extracted name, description and i/o stream type
	int err;  // error indicator where necessary
	snd_pcm_t * pcm;  // pcm device handle
	int devindex;
	DeviceInfo *dev;
	int cardnumber;

	// Temporarily disable ALSA error logging to avoid logging errors while
	// testing whether audio devices can be opened.
	mute_alsa_errhandler = true;

	// Many device names as generated from hints take the form
	// prefix:CARD=cardname,DEV=devnumber.  However, when entering one of these
	// from the command line it is more convenient to write
	// prefix:cardnumber,devnumber where these two numbers are small integers.
	// This is also the way that devices have usually been specifed for earlier
	// versions of ardopcf (and ardopc).
	// So, use cardmap to store a map from cardnumber to cardname, which is then
	// used to generate dev->alias
	int cardmaplen = 0;
	int oldcardmaplen = 0;
	char **cardmap = NULL;
	snd_ctl_t * ctlhandle = NULL;
	char tmpdevstr[8];
	snd_ctl_card_info_t *info;
	snd_ctl_card_info_alloca(&info);
	cardnumber = -1;
	snd_card_next(&cardnumber);
	while (cardnumber >= 0) {
		cardmaplen = cardnumber + 1;
		if (cardmap == NULL)
			cardmap = (char **) malloc(cardmaplen * sizeof(char*));
		else
			cardmap = (char **) realloc(cardmap, cardmaplen * sizeof(char*));
		while (oldcardmaplen < cardmaplen) {
			// Set all newly allocated elements of cardmap to NULL.
			// If there is no next card, then snd_card_next() sets cardnumber
			// to -1.  I think that otherwise snd_card_next() always increments
			// cardnumber by 1, but I am not sure of this.  The documentation of
			// for snd_card_next doesn't say this explicitly.  So, this loop
			// handles the possibilty of skipped cardnumber values.  For skipped
			// cardnumber=i, cardmap[i] remains null.  cardmap[i] also remains
			// null if a card cannot be opened or getting its name/info fails.
			cardmap[oldcardmaplen++] = NULL;
		}
		sprintf(tmpdevstr, "hw:%i", cardnumber);
		if ((err = snd_ctl_open(&ctlhandle, tmpdevstr, 0)) != 0) {
			ZF_LOGV("Error from snd_ctl_open(%s) to get name for this number."
				" (%s)", tmpdevstr, snd_strerror(err));
			snd_card_next(&cardnumber);
			continue;
		}
		if ((err = snd_ctl_card_info(ctlhandle, info)) != 0) {
			ZF_LOGI("Error from snd_ctl_card_info() for %s (%s).",
				tmpdevstr, snd_strerror(err));
			snd_ctl_close(ctlhandle);
			snd_card_next(&cardnumber);
			continue;
		}
		cardmap[cardnumber] = strdup(snd_ctl_card_info_get_id(info));
		snd_ctl_close(ctlhandle);
		snd_card_next(&cardnumber);
	}

	// Clean up previous results
	FreeDevices(&AudioDevices);
	InitDevices(&AudioDevices);

	if (snd_device_name_hint(-1, "pcm", &hints) != 0) {
		// Always include NOSOUND as the last device suitable for both capture
		// and playback.  Thus, AudioDevices[] is never completely empty.
		devindex = ExtendDevices(&AudioDevices);
		dev = AudioDevices[devindex];
		dev->name = strdup("NOSOUND");
		dev->desc = strdup("A dummy audio device for diagnostic use.");
		dev->capture = true;
		dev->playback = true;
		for (int i = 0; i < cardmaplen; ++i)
			if (cardmap[i] != NULL)
				free(cardmap[i]);
		free(cardmap);
		return;
	}
	// hints is preserved so that it can be freed when finished.  nexthint is
	// advanced to cycle through values.
	nexthint = hints;
	while (*nexthint != NULL) {
		name = snd_device_name_get_hint(*nexthint, "NAME");
		// Skip any 'surroundXX:' items (not appropriate for radio use)
		if (strncmp(name, SURROUND, strlen(SURROUND)) == 0) {
			free(name);
			++nexthint;
			continue;
		}

		// ExtendDevices() adds a new unused device and returns its index.
		// It also initializes the string pointers to be NULL, and the booleans
		// to be false.
		devindex = ExtendDevices(&AudioDevices);
		dev = AudioDevices[devindex];
		// dev->name and dev->alias shall be truncated to MAXDEVLEN bytes
		// including a terminating null.
		if (strlen(name) > (DEVSTRSZ - 1)) {
			dev->name = strndup(name, DEVSTRSZ - 1);
			free(name);
		} else {
			dev->name = name;
		}
		char *cardptr;
		if ((cardptr = strstr(name, ":CARD=")) != NULL) {
			// dev->name has the form prefix:CARD=...
			// If what comes after ":CARD=" matches a string in cardmap, then
			// create dev->alias
			char *cardinfo = cardptr + strlen(":CARD=");
			for (int i = 0; i < cardmaplen; ++i) {
				if (cardmap[i] == NULL || strlen(cardmap[i]) > strlen(cardinfo))
					continue;  // not a match
				if (strncmp(cardinfo, cardmap[i], strlen(cardmap[i])) != 0)
					continue;  // not a match
				if (strlen(cardinfo) == strlen(cardmap[i])) {
					// match for CARD not followed by ",DEV="
					// aliaslen is the length of the string to be created for
					// dev->alias including the terminating null
					int aliaslen = cardptr - name + 3;  // prefix:i if i < 10
					if (i >= 10)
						++aliaslen;  // i requires 2 digits.
					dev->alias = (char *) malloc(aliaslen * sizeof(char*));
					snprintf(dev->alias, aliaslen, "%.*s:%i",
						(int) (cardptr - name), name, i);
					break;
				}
				// Check to see if name has the form prefix:CARD=i,DEV=k
				long devnum = 0;
				if (strncmp(cardinfo + strlen(cardmap[i]), ",DEV=",
						strlen(",DEV=")) == 0
					&& try_parse_long(cardinfo + strlen(cardmap[i])
						+ strlen(",DEV="), &devnum)
				) {
					int aliaslen = cardptr - name + 5;  // prefix:i,N
					if (i >= 10)
						++aliaslen;  // i requires 2 digits.
					if (devnum >= 10)
						++aliaslen;  // devnum requires 2 digits.
					dev->alias = (char *) malloc(aliaslen * sizeof(char*));
					snprintf(dev->alias, aliaslen, "%.*s:%i,%li",
						(int) (cardptr - name), name, i, devnum);
					break;
				}
			}
		}
		desc = snd_device_name_get_hint(*nexthint, "DESC");
		// If desc has more than one line, use only the first line.
		strlop(desc, '\n');
		dev->desc = strdup(desc);
		free(desc);
		io = snd_device_name_get_hint(*nexthint, "IOID");

		// Test to determine whether some or all of the booleans should be
		// changed to true
		// If a device is reported to be busy upon attempting to open it for
		// capture or playback, then assume that it is valid for that use.
		// TODO: Consider doing additional filtering to omit from AudioDevices
		// those that are not useful to ardopcf, such as some (but not all) hw:
		// devices that do not support a 16-bit 12kHz audio.  This would also
		// make use of a substring match for description more useful since it
		// would not match a plughw: device rather than the often unusable
		// corresponding hw: device.
		if (io == NULL || strcmp(io, STREAM_OUTPUT) == 0) {
			// Try to open this as an playback/output device
			if ((err = snd_pcm_open(&pcm, name,
				SND_PCM_STREAM_PLAYBACK, 0)) == 0
			) {
				dev->playback = true;
				snd_pcm_close(pcm);
			} else if (err == -EBUSY) {
				dev->playbackbusy = true;
				dev->playback = true;
			}
		}
		if (io == NULL || strcmp(io, STREAM_INPUT) == 0) {
			// Try to open this as an capture/input device
			if ((err = snd_pcm_open(&pcm, name,
				SND_PCM_STREAM_CAPTURE, 0)) == 0
			) {
				dev->capture = true;
				snd_pcm_close(pcm);
			} else if (err == -EBUSY) {
				dev->capturebusy = true;
				dev->capture = true;
			}
		}
		free(io);  // may be NULL
		++nexthint;
	}
	snd_device_name_free_hint(hints);
	mute_alsa_errhandler = false;  // return this to its normal condition
	for (int i = 0; i < cardmaplen; ++i)
		if (cardmap[i] != NULL)
			free(cardmap[i]);
	free(cardmap);

	// Always include NOSOUND as the last device suitable for both capture
	// and playback.
	devindex = ExtendDevices(&AudioDevices);
	dev = AudioDevices[devindex];
	dev->name = strdup("NOSOUND");
	dev->desc = strdup("A dummy audio device for diagnostic use.");
	dev->capture = true;
	dev->playback = true;
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


// Open audio device, setting *handle.  If successful, return 0.  On failure,
// return a non-zero value, usually the negative error value from a snd_*()
// function.
// Device is prepared, but not started.
int open_audio(snd_pcm_t **handle, const char *devstr, bool iscapture, int ch) {
	const int rate = 12000;  // Sample rate is always 12000 for ardopcf
	int bsize = 24000;  // buffer size (near is OK) for buffer time of 2 seconds
	int psize = 240;  // period size (near is OK)
	int mode = iscapture ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK;
	char forstr[9];
	//  bsize and psize of type required for snd_pcm functions
	snd_pcm_uframes_t pcm_bsize = (snd_pcm_uframes_t) bsize;
	snd_pcm_uframes_t pcm_psize = (snd_pcm_uframes_t) psize;

	int period_size_dir = 0;

	int err;
	snd_pcm_info_t *info;
	const char *name = NULL;
	snd_pcm_hw_params_t *params;
	char em[] = "##############################";

	snprintf(forstr, sizeof(forstr), "%s", iscapture ? "capture" : "playback");

	if ((err = snd_pcm_open(handle, devstr, mode, 0)) < 0) {
		ZF_LOGE("Error opening audio device %s for %s (%s)",
			devstr, forstr, snd_strerror(err));
		return err;
	}

	if ((err = snd_pcm_info_malloc(&info)) < 0) {
		ZF_LOGE("Error allocating audio info for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}
	if ((err = snd_pcm_info(*handle, info)) < 0) {
		ZF_LOGE("Error getting audio info for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_info_free(info);
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}
	if ((name = snd_pcm_info_get_name(info)) == NULL) {
		ZF_LOGE("Error getting name from audio info for %s.", forstr);
		snd_pcm_info_free(info);
		snd_pcm_close(*handle);
		*handle = NULL;
		return -1;
	}
	ZF_LOGD("Opening audio device %s for %s with device name = %s",
		devstr, forstr, name);
	snd_pcm_info_free(info);
	info = NULL;

	if ((err = snd_pcm_hw_params_malloc(&params)) < 0) {
		ZF_LOGE("Error allocating audio parameters for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}
	if ((err = snd_pcm_hw_params_any(*handle, params)) < 0) {
		ZF_LOGE("Error initializing audio parameters for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}

	log_all_pcm_hw_params_multi(params);

	// Set parameters
	if ((err = snd_pcm_hw_params_set_access(*handle, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
		ZF_LOGE("Error setting audio access to interleaved read/write for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}
	snd_pcm_access_t access;
	if ((err = snd_pcm_hw_params_get_access(params, &access)) < 0) {
		ZF_LOGE("Error from snd_pcm_hw_params_get_access() for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}
	if (access != SND_PCM_ACCESS_RW_INTERLEAVED) {
		ZF_LOGE("Tried to set access to %u (SND_PCM_ACCESS_RW_INTERLEAVED) for"
			" %s but snd_pcm_hw_params_get_access() -> access=%u without"
			" returning an error",
			SND_PCM_ACCESS_RW_INTERLEAVED, forstr, access);
		snd_pcm_hw_params_free(params);
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}

	// This explicitly sets data to be read/written as little-endian.  If this
	// code is ever used on a big-endian machine, this may need to be changed.
	if ((err = snd_pcm_hw_params_set_format(*handle, params, SND_PCM_FORMAT_S16_LE)) < 0) {
		ZF_LOGE("Error setting audio access to 16-bit LE for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}
	snd_pcm_format_t format;
	if ((err = snd_pcm_hw_params_get_format(params, &format)) < 0) {
		ZF_LOGE("Error from snd_pcm_hw_params_get_format() for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}
	if (format != SND_PCM_FORMAT_S16_LE) {
		ZF_LOGE("Tried to set format to %u (SND_PCM_FORMAT_S16_LE) for %s but"
			" snd_pcm_hw_params_get_format() -> format=%u without returning"
			" an error",
			SND_PCM_FORMAT_S16_LE, forstr, format);
		snd_pcm_hw_params_free(params);
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}

	if ((err = snd_pcm_hw_params_set_channels(*handle, params, ch)) < 0) {
		ZF_LOGE("Error setting number of audio channels to %i for %s (%s)",
			ch, forstr, snd_strerror(err));
		if (ch == 2) {
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
		} else if (ch == 1) {
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
		} else {
			ZF_LOGE("Invalid ch=%i in open_audio.", ch);
		}
		snd_pcm_hw_params_free(params);
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}
	unsigned int channels;
	if ((err = snd_pcm_hw_params_get_channels(params, &channels)) < 0) {
		ZF_LOGE("Error from snd_pcm_hw_params_get_channels() for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}
	if ((int) channels != ch) {
		ZF_LOGE("Tried to set channels to %u for %s but"
			" snd_pcm_hw_params_get_channels() -> channels=%u without returning"
			" an error",
			ch, forstr, channels);
		snd_pcm_hw_params_free(params);
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}

	if ((err = snd_pcm_hw_params_set_rate(*handle, params, rate, 0)) < 0) {
		if (strncmp(devstr, "hw:", 3) == 0) {
			ZF_LOGE("Error setting audio sample rate to 12 kHz for %s (%s)\n"
				"This occurs because, like many devices, this one does not"
				" natively supports the required 12 kHz sample rate.  Using"
				" plug%s instead is likely to work.",
				forstr, snd_strerror(err), devstr);
		} else {
			ZF_LOGE("Error setting audio sample rate to 12 kHz for %s (%s)\n",
			forstr, snd_strerror(err));
		}
		snd_pcm_hw_params_free(params);
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}
	unsigned int approx_rate;
	int rate_dir;
	if ((err = snd_pcm_hw_params_get_rate(params, &approx_rate, &rate_dir)) < 0) {
		ZF_LOGE("Error from snd_pcm_hw_params_get_rate() for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
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
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}

	// According to a comment dated 20200913 by "borine" at
	// https://github.com/Arkq/bluez-alsa/issues/376:
	// "It is not well documented, but when configuring the hardware params you
	// should set the period size before setting the buffer size."
	// Otherwise, snd_pcm_hw_params_get_periods() may fail with (Invalid argument)

	if ((err = snd_pcm_hw_params_set_period_size_near(*handle, params, &pcm_psize, &period_size_dir)) < 0) {
		ZF_LOGE("Error setting period size to near %i (frames) for %s. (%s)",
			psize, forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}
	if (psize != (int) pcm_psize) {
		ZF_LOGW("%s\nWARNING: Setting period_size near %i resulted in %i (frames)"
			" for %s.\n%s",
			em, psize, (int) pcm_psize, forstr, em);
	}
	psize = (int) pcm_psize;

	if ((err = snd_pcm_hw_params_set_buffer_size_near(*handle, params, &pcm_bsize)) < 0) {
		ZF_LOGE("Error setting audio buffer size near %lu (frames) for %s (%s)",
			pcm_bsize, forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}
	if (bsize != (int) pcm_bsize) {
		ZF_LOGW("%s\nWARNING: Setting buffer_size near %i resulted in %i (frames)"
			" for %s \n%s",
			em, bsize, (int) pcm_bsize, forstr, em);
	}
	bsize = (int) pcm_bsize;
	ZF_LOGD("For %s: buffer_size = %i (frames) gives a buffer time of about %i ms.",
		forstr, bsize, 1000 * bsize / rate);

	if ((err = snd_pcm_hw_params(*handle, params)) < 0) {
		ZF_LOGE("Error setting audio parameters for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}

	log_all_pcm_hw_params_final(*handle, params);

	snd_pcm_hw_params_free(params);
	if ((err = snd_pcm_prepare(*handle)) < 0) {
		ZF_LOGE("Error preparing audio for %s (%s)",
			forstr, snd_strerror(err));
		snd_pcm_close(*handle);
		*handle = NULL;
		return err;
	}
	return 0;
}

// Close the playback audio device if one is open and set TXEnabled=false.
// do_getdevices is passed to updateWebGuiAudioConfig()
void CloseSoundPlayback(bool do_getdevices) {
	if (playhandle != NULL) {
		snd_pcm_close(playhandle);
		playhandle = NULL;
	}
	PlaybackDevice[0] = 0x00;  // empty string
	SoundIsPlaying = false;
	TXEnabled = false;
	KeyPTT(false);  // In case PTT is engaged.
	updateWebGuiAudioConfig(do_getdevices);
}

// GetDevices is always called at the start of this function to ensure that
// AudioDevices is accurate.
// If devstr matches PlaybackDevice and ch matches Pch and TXEnbled is true,
// then do nothing but write a debug log message.  Otherwise, if TXEnabled is
// true, then CloseSoundPlayback() is called before attempting to open
// devstr.  FindAudioDevice() searches for an exact match for a device name or
// alias using devstr truncated to DEVSTRSZ - 1 bytes, and if this fails,
// searches for a case insensitive match of device description.  If a match is
// found, it is opened and configured.
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
	int err;
	// maxwait is the maximum time (in ms) to wait before assuming that -EBUSY
	// will not be fixed by further waiting.  This has proven necessary in
	// OpenSoundCapture().  So, it is also implented here in case it is useful.
	// Allow a wait of up to 10 sec (10000 ms).
	unsigned int maxwait = 10000;  // ms
	int aindex;
	long devnum;
	char altdevstr[14] = "";
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
		// Compare only the first DEVSTRSZ - 1 bytes (exclude terminating NUlL)
		if ((strncmp(devstr, PlaybackDevice, DEVSTRSZ - 1) == 0 && ch == Pch)
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

	if (try_parse_long(devstr, &devnum) && devnum >= 0 && devnum < 100) {
		// If devstr is a small positive integer X, use plughw:X,0
		snprintf(altdevstr, sizeof(altdevstr), "plughw:%li,0", devnum);
	}

	// FindAudioDevice searches for an exact match of device name or
	// alias using devstr truncated to DEVSTRSZ - 1 bytes, and if this
	// fails, searches for a case insensitive match of device
	// description.
	if ((aindex = FindAudioDevice(
		altdevstr[0] != 0x00 ? altdevstr : devstr, false)) < 0
	) {
		ZF_LOGW("Error opening playback audio device %s.  This does not"
			" appear to be a valid audio device name, nor was a match"
			" found using a case insensitive substring search in the"
			" descriptions of available playback devices.",
			altdevstr[0] != 0x00 ? altdevstr : devstr);
		return false;
	}
	ZF_LOGV("FindAudioDevice(%s) in OpenSoundPlayback() -> %s",
		altdevstr[0] != 0x00 ? altdevstr : devstr, AudioDevices[aindex]->name);

	if ((err = open_audio(&playhandle, AudioDevices[aindex]->name,
		false, ch)) != 0
	) {
		unsigned int openstart = Now;
		while (err == -EBUSY && Now < openstart + maxwait) {
			// This has proven necessary in OpenSoundCapture().
			// So, it is implemented here as well, in case it is useful.
			ZF_LOGV("retry open_audio() after %f sec in"
				" OpenSoundPlayback() due to -EBUSY",
				(Now - openstart) / 1000.0);
			txSleep(2000);
			err = open_audio(&playhandle, AudioDevices[aindex]->name,
				false, ch);
		}
		if (err == 0) {
			ZF_LOGV("Success retrying open_audio(%s) after %f sec in"
				" OpenSoundPlayback() due to -EBUSY",
				AudioDevices[aindex]->name, (Now - openstart) / 1000.0);
		} else if (err == -EBUSY && Now >= openstart + maxwait) {
			ZF_LOGW("Error opening playback audio device %s because it"
				" is already in use.  Mutliple retries were attempted. "
				" Some devices can be opened multiple times, but"
				" apparently not this one.",
				AudioDevices[aindex]->name);
			return false;
		} else {
			// Some error other than -EBUSY produced.  Error already logged.
			return false;
		}
	}
	TXEnabled = true;
	snprintf(PlaybackDevice, DEVSTRSZ, "%s",
		altdevstr[0] != 0x00 ? altdevstr : devstr);
	strcpy(LastGoodPlaybackDevice, PlaybackDevice);
	Pch = ch;
	updateWebGuiAudioConfig(false);
	return true;
}


// Close the capture audio device if one is open and set RXEnabled=false.
// do_getdevices is passed to updateWebGuiAudioConfig()
void CloseSoundCapture(bool do_getdevices) {
	if (rechandle != NULL) {
		snd_pcm_close(rechandle);
		rechandle = NULL;
	}
	CaptureDevice[0] = 0x00;  // empty string
	RXEnabled = false;
	updateWebGuiAudioConfig(do_getdevices);
}

// GetDevices is always called at the start of this function to ensure that
// AudioDevices is accurate.
// If devstr matches CaptureDevice and ch matches Cch and RXEnbled is true,
// then do nothing but write a debug log message.  Otherwise, if RXEnabled is
// true, then CloseSoundCapture() is called before attempting to open
// devstr.  FindAudioDevice() searches for an exact match for a device name or
// alias using devstr truncated to DEVSTRSZ - 1 bytes, and if this fails,
// searches for a case insensitive match of device description.  If a match is
// found, it is opened and configured.
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
	int err;
	// maxwait is the maximum time (in ms) to wait before assuming that -EBUSY
	// will not be fixed by further waiting.  I have observed values up to 6
	// seconds (6000 ms).  Allow 10 sec (10000 ms) to allow for additional
	// margin.
	unsigned int maxwait = 10000;  // ms
	int aindex;
	long devnum;
	char altdevstr[14] = "";
	// Always update AudioDevices so that next wg_send_audiodevices() will have
	// updated info, even if not used within this function.
	GetDevices();
	if (devstr[0] == 0x00) {
		// devstr is an empty string.  This would always match the first entry
		// in AudioDevices[].  So, instead it is used to indicate that the
		// current Playback device (if there is one) should be closed, and then
		// return false.
		if (RXEnabled)
			CloseSoundCapture(false);  // calls updateWebGuiAudioConfig(false);
		return false;
	}
	if (RXEnabled) {
		// Compare only the first DEVSTRSZ - 1 bytes (exclude terminating NUlL)
		if ((strncmp(devstr, CaptureDevice, DEVSTRSZ - 1) == 0 && ch == Cch)
			|| strcmp(devstr, "RESTORE") == 0
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
		wg_send_rxenabled(0, RXEnabled);
		strcpy(CaptureDevice, "NOSOUND");
		strcpy(LastGoodCaptureDevice, CaptureDevice);
		Cch = ch;
		updateWebGuiAudioConfig(false);
		return true;
	}

	if (try_parse_long(devstr, &devnum) && devnum >= 0 && devnum < 100) {
		// If devstr is a small positive integer X, use plughw:X,0
		snprintf(altdevstr, sizeof(altdevstr), "plughw:%li,0", devnum);
	}

	// FindAudioDevice searches for an exact match of device name or
	// alias, and if this fails, searches for a case insensitive match
	// of device description.
	if ((aindex = FindAudioDevice(
		altdevstr[0] != 0x00 ? altdevstr : devstr, true)) < 0
	) {
		ZF_LOGW("Error opening capture audio device %s.  This does not"
			" appear to be a valid audio device name, nor was a match"
			" found using a case insensitive substring search in the"
			" descriptions of available capture devices.",
			altdevstr[0] != 0x00 ? altdevstr : devstr);
		return false;
	}
	ZF_LOGV("FindAudioDevice(%s) in OpenSoundCapture() -> %s",
		altdevstr[0] != 0x00 ? altdevstr : devstr, AudioDevices[aindex]->name);

	if ((err = open_audio(&rechandle, AudioDevices[aindex]->name,
		true, ch)) != 0
	) {
		unsigned int openstart = Now;
		while (err == -EBUSY && Now < openstart + maxwait) {
			// Retrying for 4-6 seconds is found to be commonly required
			// when trying to reopen a capture device after it was
			// disconnected and reconnected when openstart is a plughw:
			// device on a computer running Pulse Audio.
			ZF_LOGV("retry open_audio() after %f sec in"
				" OpenSoundCapture() due to -EBUSY",
				(Now - openstart) / 1000.0);
			txSleep(2000);
			err = open_audio(&rechandle, AudioDevices[aindex]->name,
				true, ch);
		}
		if (err == 0) {
			ZF_LOGV("Success retrying open_audio(%s) after %f sec in"
				" OpenSoundCapture() due to -EBUSY",
				AudioDevices[aindex]->name, (Now - openstart) / 1000.0);
		} else if (err == -EBUSY && Now >= openstart + maxwait) {
			ZF_LOGW("Error opening capture audio device %s because it"
				" is already in use.  Mutliple retries were attempted. "
				" Some devices can be opened multiple times, but"
				" apparently not this one.",
				AudioDevices[aindex]->name);
			return false;
		} else {
			// Some error other than -EBUSY produced.  Error already logged.
			return false;
		}
	}
	if ((err = snd_pcm_start(rechandle)) < 0) {
		ZF_LOGE("Error starting audio for capture. (%s)",
			snd_strerror(err));
		snd_pcm_close(rechandle);
		return false;
	}
	RXEnabled = true;
	RXSilent = false;
	snprintf(CaptureDevice, DEVSTRSZ, "%s",
		altdevstr[0] != 0x00 ? altdevstr : devstr);
	strcpy(LastGoodCaptureDevice, CaptureDevice);
	Cch = ch;
	if (!SoundIsPlaying)
		StartCapture();
	updateWebGuiAudioConfig(false);
	return true;
}

// Return true on success, false on failure
bool attempt_recovery(bool iscapture, int snderr) {
	bool opened;
	if (iscapture && strcmp(CaptureDevice, "NOSOUND") == 0) {
		ZF_LOGE("Should not be using attempt_recovery() for capture NOSOUND");
		RXEnabled = false;
		updateWebGuiAudioConfig(true);
		return false;
	}
	if (!iscapture && strcmp(PlaybackDevice, "NOSOUND") == 0) {
		ZF_LOGE("Should not be using attempt_recovery() for playback NOSOUND");
		SoundIsPlaying = false;
		TXEnabled = false;
		wg_send_txenabled(0, TXEnabled);
		KeyPTT(false);  // In case PTT is engaged.
		updateWebGuiAudioConfig(true);
		return false;
	}
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
		// The following implements all the the contents of CloseSoundCapture()
		// and CloseSoundPlayback().  However, it does not call these functions
		// directly because, the steps other than actually closing the device
		// (especially in CloseSoundPlayback) are only implemented if recovery
		// by closing and reopening the device fails.
		if (*handleptr != NULL) {
			snd_pcm_close(*handleptr);
			*handleptr = NULL;
		}
		if (iscapture) {
			RXEnabled = false;  // Without this, OpenSoundCapture does nothing.
			opened = OpenSoundCapture("RESTORE", Cch);
		} else {
			TXEnabled = false;  // Without this, OpenSoundPlayback does nothing.
			opened = OpenSoundPlayback("RESTORE", Pch);
		}
		if (!opened) {
			// Unable to continue.
			if (iscapture) {
				// No additional steps required to finish implementation of
				// CloseSoundCapture.  CaptureDevice string has been cleared.
				ZF_LOGW("Error re-opening audio capture device.  RXEnabled is"
					" now false.");
				if (ZF_LOG_ON_VERBOSE)
					// For testing with testhost.py
					SendCommandToHost("STATUS RXENABLED FALSE");
			} else {
				// remainder of CloseSoundPlayback
				SoundIsPlaying = false;
				KeyPTT(false);  // In case PTT is engaged.
				ZF_LOGW("Error re-opening audio playback device.  TXEnabled is"
					" now false.");
				if (ZF_LOG_ON_VERBOSE)
					// For testing with testhost.py
					SendCommandToHost("STATUS TXENABLED FALSE");
			}
			updateWebGuiAudioConfig(true);
			return false;
		}
	} else if (iscapture) {
		// For a capture device, snd_pcm_avail() will always return 0 after
		// successful recovery until snd_pcm_start() is called.
		int err;
		if ((err = snd_pcm_start(rechandle)) < 0) {
			ZF_LOGE("Error restarting audio after snd_pcm_recover() for"
				" capture device. (%s)  Setting RXEnabled to false.",
				snd_strerror(err));
			if (ZF_LOG_ON_VERBOSE)
				// For testing with testhost.py
				SendCommandToHost("STATUS RXENABLED FALSE");
			CloseSoundCapture(true);  // calls updateWebGuiAudioConfig(true);
			return false;
		}
	}
	return true;
}


// Write samples to a mono or stereo audio device.
// If a failure to write occurs, retry up to 10 times.  If still unsuccessful,
// set TXEnabled to false and return false.
// return true on success and false on failure
bool PackSamplesAndSend(short * input, int nSamples) {
	if (!TXEnabled) {
		ZF_LOGW("PackSamplesAndSend() called when not TXEnabled. Ignoring.");
		return false;
	}

	unsigned short samples[256000];
	unsigned short * sampptr = samples;
	int ret;

	// Convert byte stream to int16 (watch endianness)
	if (Pch == 1) {
		for (int n = 0; n < nSamples; ++n) {
			*(sampptr++) = input[0];
			input ++;
		}
	} else if (Pch == 2) {
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
	} else {
		ZF_LOGE("Invalid Pch=%i in PackSamplesAndSend()", Pch);
		CloseSoundPlayback(true);  // calls updateWebGuiAudioConfig(true);
		return false;
	}

	int errcount = 0;
	while ((ret = snd_pcm_writei(playhandle, samples, nSamples)) < 0 && TXEnabled) {
		if (++errcount > 10) {
			ZF_LOGE("TXEnabled set to false due to repeated audio write errors.");
			CloseSoundPlayback(true);  // calls updateWebGuiAudioConfig(true);
			return false;
		}
		if (!attempt_recovery(false, ret)) {
			// Unable to recover. TXEnabled set to false, Error message logged.
			return false;
		}
	}
	return true;
}


// If a failure to write occurs, set TXEnabled to false.
// Block until samples can be staged for playback, calling txSleep() while
// waiting.
// return true on success and false on failure
bool SoundCardWrite(short * input, unsigned int nSamples) {
	if (!TXEnabled) {
		ZF_LOGW("SoundCardWrite() called when not TXEnabled. Ignoring.");
		return false;
	}
	if (strcmp(PlaybackDevice, "NOSOUND") == 0)
		return true;  // Do nothing, indicate success.
	snd_pcm_sframes_t avail;
	int errcount = 0;
	while ((avail = snd_pcm_avail(playhandle)) < nSamples && TXEnabled) {
		if (avail >= 0 || avail == -EAGAIN) {
			txSleep(100);  // Wait until samples can be staged for playback
		} else {
			// Error
			if (++errcount > 10) {
				ZF_LOGE("Setting TXEnabled to false due to repeated audio"
					" write errors.");
				CloseSoundPlayback(true);  // calls updateWebGuiAudioConfig(true);
				return false;
			}
			if (!attempt_recovery(false, avail)) {
				// Unable to recover.  TXEnabled set to false. Error message logged.
				return false;
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

	if (!RXEnabled) {
		ZF_LOGD("SoundCardRead() called when not  RXEnabled.  Ignoring.");
		return 0;
	}

	int errcount = 0;
	///////////////////////////////////////////////////////////////////////////
	// The following call to snd_pcm_avail() has been found to erratically
	// never return on certain systems (including one 32-bit ARM system tested)
	// if a pulse audio device is disconnected or turned off while in use.  When
	// this occurs, CTRL-C also fails to exit the program.  To exit, from
	// another console connection run ps -e | grep ardopcf to get a process id,
	// and then run kill -9 NNN, where NNN is the process id found.  A way to
	// detect this problem and respond more appropriately has not been found.
	///////////////////////////////////////////////////////////////////////////
	while ((avail = snd_pcm_avail(rechandle)) < (int) nSamples && RXEnabled) {
		if (avail >= 0 || avail == -EAGAIN) {
			return 0;  // Insufficient samples available.  Do nothing.
		}
		// An error occured.  Try to recover
		if (++errcount > 10) {
			ZF_LOGE("RXEnabled set to false due to repeated audio read errors.");
			CloseSoundCapture(true);  // calls updateWebGuiAudioConfig(true);
			return 0;
		}
		if (!attempt_recovery(true, avail)) {
			// Unable to recover. RXEnabled set to false. Error message logged.
			return 0;
		}
	}

	errcount = 0;
	while ((ret = snd_pcm_readi(rechandle, samples, nSamples)) < 0 && RXEnabled) {
		if (++errcount > 10) {
			ZF_LOGE("RXEnabled set to false due to repeated audio read errors.");
			CloseSoundCapture(true);  // calls updateWebGuiAudioConfig(true);
			return 0;
		}
		if (!attempt_recovery(true, ret)) {
			// Unable to recover. RXEnabled set to false. Error message logged.
			return 0;
		}
	}

	if (Cch == 1) {
		for (n = 0; n < ret; n++) {
			memcpy(input, samples, nSamples*sizeof(short));
		}
	} else if (Cch == 2) {
		if (UseLeftRX)
			start = 0;
		else
			start = 1;

		for (n = start; n < (ret * 2); n += 2) {  // return alternate
			*(input++) = samples[n];
		}
	} else {
		ZF_LOGE("Invalid Cch=%i in SoundCardRead()", Cch);
		CloseSoundCapture(true);  // calls updateWebGuiAudioConfig(true);
		return 0;
	}
	return ret;
}

// TODO: Consider calling SoundCardWrite() directly instead
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

// This mininal error handler to be passed to snd_lib_error_set_handler()
// prevents ALSA from printing errors directly to the console.  Instead,
// write an appropriate log message.  This ensures that the error is
// written to the log file, rather than only to the console.  It also prevents
// undesirable console output when using the --syslog command line option.
void alsa_errhandler(const char *file, int line, const char *function, int err, const char *fmt, ...) {
	static unsigned int errtime = 0;  // ms resolution
	static unsigned int errcount = 0;
	(void) fmt;  // This prevents an unused variable warning.

	if (mute_alsa_errhandler)
		return;

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
// TODO: Handle case of a rapid string of ALSA errors?  There doesn't seem to
// be a reliable way to determine whether the error was from the playback or
// capture device.  So, close/disable both?  An earlier version of ardopcf
// set blnClosing = true here.  However, ardopcf no longer exits when an audio
// device fails.
}

bool AudioInit = false;

void InitAudio(bool quiet) {
	snd_lib_error_set_handler(alsa_errhandler);

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

// Adds a trailer to the audio samples that have been staged with SendtoCard(),
// and then block until all of the staged audio has been played.  Before
// returning, it calls KeyPTT(false) and enables the Capturing state.
// return true on success and false on failure.  On failure, still set
// KeyPTT(false) and enables the Capturing state
bool SoundFlush() {
	int txlenMs = 0;
	// Append Trailer then send remaining samples
	// if AddTrailer() or SendtoCard() fail, TXEnabled will be set to false
	if (TXEnabled && AddTrailer() && SendtoCard(Number)) {
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
	} else {
		ZF_LOGW("SoundFlush() called when not TXEnabled. Ignoring.");
	}
	if (strcmp(PlaybackDevice, "NOSOUND") != 0
		&& pttOnTime + txlenMs > Now
	)
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
