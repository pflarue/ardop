// ardop_lib.h - Embeddable ARDOP modem library C API
//
// This is the platform-neutral C API used to embed the ardopcf modem core into
// an application without the TCP host interface, Web UI, or command-line
// front end.  It is designed to be called directly from C, from Swift/
// Objective-C (via a module map), and from Kotlin/Java (via a thin JNI bridge).
//
// The modem runs on its own background thread, started by ardop_start().  All
// API calls below are safe to invoke from any thread: control/data requests are
// queued and executed on the modem thread, and received data and events are
// delivered via the callbacks registered at ardop_create() time.  Those
// callbacks fire on the modem thread, so the binding layer is responsible for
// hopping to whatever context (Swift delegate queue, JNI attached thread) it
// needs.
//
// Because the underlying modem core uses process-wide global state, only one
// ardop_t instance may exist at a time.

#ifndef ARDOP_LIB_H
#define ARDOP_LIB_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque modem handle.
typedef struct ardop ardop_t;

// Classification of a received data buffer, derived from the 3-byte tag the
// modem core attaches to data passed to the host ("ARQ", "FEC", "ERR", "IDF").
typedef enum {
	ARDOP_DATA_ARQ = 0,  // data received over a connected ARQ session
	ARDOP_DATA_FEC,      // data received from a connectionless FEC frame
	ARDOP_DATA_ERR,      // data from a frame that failed to decode cleanly
	ARDOP_DATA_ID,       // decoded ID frame content
	ARDOP_DATA_OTHER,    // any other / unrecognized tag
} ardop_data_tag;

// Callbacks invoked by the modem.  All fire on the modem thread.  The buffers
// passed to on_data and the string passed to on_event are owned by the modem
// and are only valid for the duration of the call; copy what you need.
typedef struct {
	void *ctx;  // opaque, passed back to each callback

	// Received application data (ARQ payload, FEC datagram, errored frame, ...).
	void (*on_data)(void *ctx, ardop_data_tag tag, const uint8_t *data, int len);

	// Asynchronous status/event line from the modem command channel, e.g.
	// "PTT TRUE", "CONNECTED K7CALL 500", "DISCONNECTED", "STATUS ...",
	// "BUFFER 0", "NEWSTATE ...", "FAULT ...".  NUL-terminated, no trailing CR.
	void (*on_event)(void *ctx, const char *line);
} ardop_callbacks;

// ---- Lifecycle ------------------------------------------------------------

// Create the modem instance and register callbacks.  cb may be NULL (no
// callbacks).  Returns NULL on failure (including if an instance already
// exists).
ardop_t *ardop_create(const ardop_callbacks *cb);

// Stop (if running) and free the modem instance.
void ardop_destroy(ardop_t *a);

// Open the audio devices and start the modem thread.  Returns 0 on success, a
// negative value on failure (e.g. audio device could not be opened).  The
// caller is expected to have configured the OS audio session/route first.
int ardop_start(ardop_t *a);

// Signal the modem thread to stop and wait for it to exit.  Closes audio.
// Returns 0 on success.  Safe to call when not running.
int ardop_stop(ardop_t *a);

// True between a successful ardop_start() and ardop_stop().
bool ardop_is_running(const ardop_t *a);

// ---- Configuration --------------------------------------------------------
// These are thin wrappers that queue the corresponding host command to run on
// the modem thread.  They may be called before or after ardop_start(); pending
// commands are applied once the modem thread is running.  Each returns 0 if the
// request was queued, negative on argument error.

int ardop_set_callsign(ardop_t *a, const char *callsign);     // MYCALL
int ardop_set_gridsquare(ardop_t *a, const char *gridsquare); // GRIDSQUARE
int ardop_set_protocolmode(ardop_t *a, const char *mode);     // ARQ | FEC | RXO

// Send an arbitrary host command (see docs/Host_Interface_Commands.md), e.g.
// "DRIVELEVEL 80", "ARQBW 500MAX", "BUSYDET 0".  The string must not contain a
// carriage return.
int ardop_command(ardop_t *a, const char *cmd);

// ---- ARQ (connected, reliable) -------------------------------------------

// Initiate an ARQ connection to target.  attempts is the number of CONREQ
// frames to send (2-15); pass 0 for the modem default.  Requires the callsign
// to be set and PROTOCOLMODE ARQ.
int ardop_arq_connect(ardop_t *a, const char *target, int attempts);

// Gracefully end the current ARQ session.
int ardop_arq_disconnect(ardop_t *a);

// Dirty-disconnect / abort the current ARQ session.
int ardop_arq_abort(ardop_t *a);

// Queue application data for transmission over the (current or pending) ARQ
// session.  Data is appended to the modem's outgoing buffer and sent in turn
// while connected.
int ardop_arq_send(ardop_t *a, const uint8_t *data, int len);

// ---- FEC (connectionless datagrams) --------------------------------------

// Queue a datagram and trigger its FEC transmission using the current FECMODE
// and FECREPEATS settings.  Requires the callsign to be set.
int ardop_fec_send(ardop_t *a, const uint8_t *data, int len);

// ---- Buffer ---------------------------------------------------------------

// Empty the outgoing data buffer (PURGEBUFFER).
int ardop_purge_buffer(ardop_t *a);

// ---- Audio device selection ----------------------------------------------
// On iOS/Android the audio route is chosen through the OS audio session, and
// the platform backend ignores the device name (treats any value as the
// default route).  These remain for API symmetry and desktop reuse.
int ardop_set_capture_device(ardop_t *a, const char *devname);
int ardop_set_playback_device(ardop_t *a, const char *devname);

// ---- Status / metrics -----------------------------------------------------

typedef struct {
	int protocol_state;       // enum _ARDOPState: OFFLINE/DISC/ISS/IRS/IDLE/...
	int receive_state;        // enum _ReceiveState
	int arq_substate;         // enum _ARQSubStates
	int protocol_mode;        // enum _ProtocolMode: Undef/FEC/ARQ (+RXO)
	int buffer_bytes;         // bytes queued in the outgoing data buffer
	bool rx_enabled;          // input audio processing active
	bool tx_enabled;          // output audio enabled
	bool sound_playing;       // currently transmitting audio
	bool capturing;           // currently accepting received samples
	// Cumulative decode metrics since session start.
	int leader_detects;
	int frame_syncs;
	int good_data_decodes;
	int failed_data_decodes;
	float leader_snr;         // average leader signal-to-noise (dB)
	int avg_quality;          // average decoded frame quality (~30-100)
} ardop_status;

// Fill out with a snapshot of the current modem status.  Returns 0 on success.
int ardop_get_status(ardop_t *a, ardop_status *out);

#ifdef __cplusplus
}
#endif

#endif  // ARDOP_LIB_H
