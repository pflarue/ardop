// ardop_lib.c - embeddable ARDOP modem library facade
//
// Implements the public ardop_lib.h API on top of the unmodified modem core.
// Owns the modem thread, the inbound command/data queue drained on that thread
// by the host shim (host_shim.c), and the dispatch of received data and events
// to the registered application callbacks.
//
// The modem core uses process-wide globals, so there is at most one instance.

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ardop_lib.h"
#include "embed_internal.h"

#include "common/ardopcommon.h"
#include "common/audio.h"
#include "common/log.h"
#include "common/os_util.h"
#include "rockliff/rrs.h"

// Audio enable flags, defined in the platform audio backend.
extern bool RXEnabled;
extern bool TXEnabled;

// Defined in HostInterface.c; not declared in a shared header.
void ProcessCommandFromHost(char *strCMD);

struct ardop {
	ardop_callbacks cb;
	pthread_t thread;
	bool thread_started;
	bool running;
	// Audio device names opened by ardop_start().  Default "default" (the OS
	// audio-session route on iOS); "NOSOUND" selects the dummy device.
	char capture_dev[DEVSTRSZ];
	char playback_dev[DEVSTRSZ];
};

// Single instance (the core is global-state based).
static ardop_t *g_ardop = NULL;

// ---- Inbound queue --------------------------------------------------------
// Commands and TX data are queued here from arbitrary threads and applied to
// the core on the modem thread inside ardop_embed_drain_inbound().

typedef enum { IN_CMD, IN_DATA } in_kind;

typedef struct in_item {
	struct in_item *next;
	in_kind kind;
	int len;             // payload length (IN_DATA)
	unsigned char *buf;  // NUL-terminated string (IN_CMD) or bytes (IN_DATA)
} in_item;

static in_item *q_head = NULL;
static in_item *q_tail = NULL;
static pthread_mutex_t q_mutex = PTHREAD_MUTEX_INITIALIZER;

static int queue_push(in_kind kind, const void *data, int len) {
	in_item *it = malloc(sizeof(*it));
	if (it == NULL)
		return -1;
	it->next = NULL;
	it->kind = kind;
	it->len = len;
	it->buf = malloc(kind == IN_CMD ? (size_t)len + 1 : (size_t)len);
	if (it->buf == NULL) {
		free(it);
		return -1;
	}
	memcpy(it->buf, data, len);
	if (kind == IN_CMD)
		it->buf[len] = 0x00;

	pthread_mutex_lock(&q_mutex);
	if (q_tail == NULL)
		q_head = q_tail = it;
	else {
		q_tail->next = it;
		q_tail = it;
	}
	pthread_mutex_unlock(&q_mutex);
	return 0;
}

static int queue_push_cmd(const char *cmd) {
	if (cmd == NULL)
		return -1;
	if (strchr(cmd, '\r') != NULL) {
		ZF_LOGW("ardop_command: carriage return not allowed in command");
		return -1;
	}
	return queue_push(IN_CMD, cmd, (int)strlen(cmd));
}

// Called from host_shim's TCPHostPoll() on the modem thread.
void ardop_embed_drain_inbound(void) {
	for (;;) {
		pthread_mutex_lock(&q_mutex);
		in_item *it = q_head;
		if (it != NULL) {
			q_head = it->next;
			if (q_head == NULL)
				q_tail = NULL;
		}
		pthread_mutex_unlock(&q_mutex);
		if (it == NULL)
			break;

		// Mirror the TCP host path: hold the modem semaphore while applying
		// commands and data to the core.
		GetSemaphore();
		if (it->kind == IN_CMD)
			ProcessCommandFromHost((char *)it->buf);
		else
			AddDataToDataToSend(it->buf, it->len);
		FreeSemaphore();

		free(it->buf);
		free(it);
	}
}

static void queue_free_all(void) {
	pthread_mutex_lock(&q_mutex);
	in_item *it = q_head;
	while (it != NULL) {
		in_item *next = it->next;
		free(it->buf);
		free(it);
		it = next;
	}
	q_head = q_tail = NULL;
	pthread_mutex_unlock(&q_mutex);
}

// ---- Outbound dispatch (called by host_shim on the modem thread) ----------

void ardop_embed_emit_event(const char *line) {
	if (g_ardop != NULL && g_ardop->cb.on_event != NULL && line != NULL)
		g_ardop->cb.on_event(g_ardop->cb.ctx, line);
}

void ardop_embed_emit_data(const char *tag, const unsigned char *data, int len) {
	if (g_ardop == NULL || g_ardop->cb.on_data == NULL)
		return;
	ardop_data_tag t = ARDOP_DATA_OTHER;
	if (tag != NULL) {
		if (strncmp(tag, "ARQ", 3) == 0)
			t = ARDOP_DATA_ARQ;
		else if (strncmp(tag, "FEC", 3) == 0)
			t = ARDOP_DATA_FEC;
		else if (strncmp(tag, "ERR", 3) == 0)
			t = ARDOP_DATA_ERR;
		else if (strncmp(tag, "IDF", 3) == 0)
			t = ARDOP_DATA_ID;
	}
	g_ardop->cb.on_data(g_ardop->cb.ctx, t, data, len);
}

// ---- Lifecycle ------------------------------------------------------------

ardop_t *ardop_create(const ardop_callbacks *cb) {
	if (g_ardop != NULL) {
		ZF_LOGE("ardop_create: an instance already exists");
		return NULL;
	}
	ardop_t *a = calloc(1, sizeof(*a));
	if (a == NULL)
		return NULL;
	if (cb != NULL)
		a->cb = *cb;
	snprintf(a->capture_dev, sizeof(a->capture_dev), "default");
	snprintf(a->playback_dev, sizeof(a->playback_dev), "default");
	g_ardop = a;
	return a;
}

static void *modem_thread(void *arg) {
	(void)arg;
	ardopmain();  // blocks until blnClosing is set
	return NULL;
}

int ardop_start(ardop_t *a) {
	if (a == NULL || a->running)
		return -1;

	// One-time Reed-Solomon setup (normally done in main() before ardopmain()).
	static bool rs_ready = false;
	if (!rs_ready) {
		int rslen_set[] = {2, 4, 8, 16, 32, 36, 50, 64};
		init_rs(rslen_set, 8);
		rs_ready = true;
	}

	platform_init();
	InitAudio(true);

	// Open the configured audio I/O.  On iOS the platform backend follows the
	// OS audio session route for any name other than "NOSOUND".
	bool cap = OpenSoundCapture(a->capture_dev, 1);
	bool play = OpenSoundPlayback(a->playback_dev, 1);
	if (!cap && !play) {
		ZF_LOGE("ardop_start: failed to open any audio device");
		return -2;
	}
	if (!cap)
		ZF_LOGW("ardop_start: capture (RX) audio device not opened");
	if (!play)
		ZF_LOGW("ardop_start: playback (TX) audio device not opened");

	blnClosing = false;
	if (pthread_create(&a->thread, NULL, modem_thread, a) != 0) {
		ZF_LOGE("ardop_start: failed to create modem thread");
		return -3;
	}
	a->thread_started = true;
	a->running = true;
	return 0;
}

int ardop_stop(ardop_t *a) {
	if (a == NULL || !a->running)
		return 0;
	blnClosing = true;
	if (a->thread_started)
		pthread_join(a->thread, NULL);
	a->thread_started = false;
	a->running = false;
	return 0;
}

bool ardop_is_running(const ardop_t *a) {
	return a != NULL && a->running;
}

void ardop_destroy(ardop_t *a) {
	if (a == NULL)
		return;
	ardop_stop(a);
	queue_free_all();
	if (g_ardop == a)
		g_ardop = NULL;
	free(a);
}

// ---- Configuration --------------------------------------------------------

int ardop_command(ardop_t *a, const char *cmd) {
	(void)a;
	return queue_push_cmd(cmd);
}

static int queue_cmdf(const char *fmt, const char *arg) {
	char cmd[256];
	if (arg == NULL)
		return -1;
	if (snprintf(cmd, sizeof(cmd), fmt, arg) >= (int)sizeof(cmd))
		return -1;
	return queue_push_cmd(cmd);
}

int ardop_set_callsign(ardop_t *a, const char *callsign) {
	(void)a;
	return queue_cmdf("MYCALL %s", callsign);
}

int ardop_set_gridsquare(ardop_t *a, const char *gridsquare) {
	(void)a;
	return queue_cmdf("GRIDSQUARE %s", gridsquare);
}

int ardop_set_protocolmode(ardop_t *a, const char *mode) {
	(void)a;
	return queue_cmdf("PROTOCOLMODE %s", mode);
}

int ardop_set_capture_device(ardop_t *a, const char *devname) {
	if (a == NULL || devname == NULL)
		return -1;
	snprintf(a->capture_dev, sizeof(a->capture_dev), "%s", devname);
	// If already running, apply immediately; otherwise it is used by
	// ardop_start().
	if (a->running)
		return queue_cmdf("CAPTURE %s", devname);
	return 0;
}

int ardop_set_playback_device(ardop_t *a, const char *devname) {
	if (a == NULL || devname == NULL)
		return -1;
	snprintf(a->playback_dev, sizeof(a->playback_dev), "%s", devname);
	if (a->running)
		return queue_cmdf("PLAYBACK %s", devname);
	return 0;
}

// ---- ARQ ------------------------------------------------------------------

int ardop_arq_connect(ardop_t *a, const char *target, int attempts) {
	(void)a;
	if (target == NULL)
		return -1;
	if (attempts < 2)
		attempts = 5;
	else if (attempts > 15)
		attempts = 15;
	char cmd[256];
	if (snprintf(cmd, sizeof(cmd), "ARQCALL %s %d", target, attempts)
		>= (int)sizeof(cmd))
		return -1;
	return queue_push_cmd(cmd);
}

int ardop_arq_disconnect(ardop_t *a) {
	(void)a;
	return queue_push_cmd("DISCONNECT");
}

int ardop_arq_abort(ardop_t *a) {
	(void)a;
	return queue_push_cmd("ABORT");
}

int ardop_arq_send(ardop_t *a, const uint8_t *data, int len) {
	(void)a;
	if (data == NULL || len <= 0)
		return -1;
	return queue_push(IN_DATA, data, len);
}

// ---- FEC ------------------------------------------------------------------

int ardop_fec_send(ardop_t *a, const uint8_t *data, int len) {
	(void)a;
	if (data == NULL || len <= 0)
		return -1;
	if (queue_push(IN_DATA, data, len) != 0)
		return -1;
	return queue_push_cmd("FECSEND TRUE");
}

// ---- Buffer ---------------------------------------------------------------

int ardop_purge_buffer(ardop_t *a) {
	(void)a;
	return queue_push_cmd("PURGEBUFFER");
}

// ---- Status ---------------------------------------------------------------

int ardop_get_status(ardop_t *a, ardop_status *out) {
	if (a == NULL || out == NULL)
		return -1;
	memset(out, 0, sizeof(*out));
	out->protocol_state = (int)ProtocolState;
	out->receive_state = (int)State;
	out->arq_substate = (int)ARQState;
	out->protocol_mode = (int)ProtocolMode;
	out->buffer_bytes = bytDataToSendLength;
	out->rx_enabled = RXEnabled;
	out->tx_enabled = TXEnabled;
	out->sound_playing = SoundIsPlaying;
	out->capturing = Capturing;
	out->leader_detects = intLeaderDetects;
	out->frame_syncs = intFrameSyncs;
	out->good_data_decodes = intGoodFSKFrameDataDecodes;
	out->failed_data_decodes = intFailedFSKFrameDataDecodes;
	out->leader_snr = dblLeaderSNAvg;
	out->avg_quality = intAvgFSKQuality;
	return 0;
}
