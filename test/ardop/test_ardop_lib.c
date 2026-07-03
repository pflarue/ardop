// Unit tests for the embeddable modem library facade (src/embed/ardop_lib.c).
//
// These exercise the platform-neutral validate / queue / dispatch layer in
// isolation: only ardop_lib.o is linked, and this file supplies its own fakes
// for the modem-core seam (ProcessCommandFromHost, AddDataToDataToSend, the
// semaphore, the audio/start-path functions, and the status globals).  Because
// every intercepted symbol is first-party, no ld --wrap is needed, so the test
// builds with Apple's ld64 as well as GNU ld.
//
// The running modem thread and real audio are deliberately out of scope here;
// ardop_start()/ardop_stop() are covered end-to-end by the NOSOUND smoke test
// in the iOS SwiftPM package (host/ios ardop-demo).

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include <math.h>
#include <cmocka.h>

#include <stdbool.h>

#include "setup.h"

#include "embed/ardop_lib.h"
#include "embed/embed_internal.h"
#include "common/ardopcommon.h"

// ---- Fakes for the modem-core seam ----------------------------------------
// ardop_lib.o references these; we provide recording/no-op definitions instead
// of the real core, so the linker resolves them here.

// Commands handed to the core by the inbound-queue drain.
#define MAXREC 16
static char rec_cmds[MAXREC][256];
static int rec_cmd_n;
static unsigned char rec_data[MAXREC][256];
static int rec_data_len[MAXREC];
static int rec_data_n;

void ProcessCommandFromHost(char *strCMD) {
	if (rec_cmd_n < MAXREC)
		snprintf(rec_cmds[rec_cmd_n], sizeof(rec_cmds[0]), "%s", strCMD);
	rec_cmd_n++;
}

void AddDataToDataToSend(UCHAR *bytNewData, int Len) {
	if (rec_data_n < MAXREC) {
		int n = Len < (int)sizeof(rec_data[0]) ? Len : (int)sizeof(rec_data[0]);
		if (n > 0)
			memcpy(rec_data[rec_data_n], bytNewData, n);
		rec_data_len[rec_data_n] = Len;
	}
	rec_data_n++;
}

void GetSemaphore(void) {}
void FreeSemaphore(void) {}

// Referenced by ardop_start()/the modem thread; never called by these tests,
// but the symbols must resolve at link time.
void ardopmain(void) {}
int init_rs(int *lengths, int count) { (void)lengths; (void)count; return 0; }
void InitAudio(bool quiet) { (void)quiet; }
bool OpenSoundCapture(char *devstr, int ch) { (void)devstr; (void)ch; return false; }
bool OpenSoundPlayback(char *devstr, int ch) { (void)devstr; (void)ch; return false; }
int platform_init(void) { return 0; }

// Modem-core status globals sampled by ardop_get_status().
enum _ReceiveState State;
enum _ARDOPState ProtocolState;
enum _ARQSubStates ARQState;
enum _ProtocolMode ProtocolMode;
int bytDataToSendLength;
bool RXEnabled;
bool TXEnabled;
bool SoundIsPlaying;
bool Capturing;
bool blnClosing;
int intLeaderDetects;
int intFrameSyncs;
int intGoodFSKFrameDataDecodes;
int intFailedFSKFrameDataDecodes;
int intAvgFSKQuality;
float dblLeaderSNAvg;

// zf_log seam: suppress output and no-op the writer (level is high so nothing
// would be emitted even if the writer did anything).
int _zf_log_global_output_lvl = 0xFFFF;
void _zf_log_write(const int lvl, const char *const tag, const char *const fmt, ...) {
	(void)lvl;
	(void)tag;
	(void)fmt;
}

// ---- Callback recording ---------------------------------------------------

static int ctx_marker;  // sentinel address passed as the callback ctx

static int on_data_calls;
static ardop_data_tag last_tag;
static unsigned char last_data[256];
static int last_data_len;
static void *last_data_ctx;

static int on_event_calls;
static char last_event[256];
static void *last_event_ctx;

static void cb_on_data(void *ctx, ardop_data_tag tag, const uint8_t *data, int len) {
	on_data_calls++;
	last_data_ctx = ctx;
	last_tag = tag;
	last_data_len = len;
	int n = len < (int)sizeof(last_data) ? len : (int)sizeof(last_data);
	if (data != NULL && n > 0)
		memcpy(last_data, data, n);
}

static void cb_on_event(void *ctx, const char *line) {
	on_event_calls++;
	last_event_ctx = ctx;
	snprintf(last_event, sizeof(last_event), "%s", line != NULL ? line : "");
}

static const ardop_callbacks CB = {
	.ctx = &ctx_marker,
	.on_data = cb_on_data,
	.on_event = cb_on_event,
};

// The instance under test, (re)created per test by setup().
static ardop_t *A;

static int setup(void **state) {
	(void)state;
	rec_cmd_n = rec_data_n = 0;
	on_data_calls = on_event_calls = 0;
	last_data_len = 0;
	last_data_ctx = last_event_ctx = NULL;
	last_event[0] = '\0';
	A = ardop_create(&CB);
	assert_non_null(A);
	return 0;
}

static int teardown(void **state) {
	(void)state;
	if (A != NULL) {
		ardop_destroy(A);
		A = NULL;
	}
	return 0;
}

// ---- Tests ----------------------------------------------------------------

// Only one instance may exist at a time; a second create is refused, and after
// destroy a fresh instance may be created again.
static void test_singleton(void **state) {
	(void)state;
	assert_non_null(A);  // created by setup()

	ardop_t *a2 = ardop_create(NULL);
	assert_null(a2);

	ardop_destroy(A);
	A = NULL;

	ardop_t *a3 = ardop_create(NULL);
	assert_non_null(a3);
	A = a3;  // let teardown() free it
}

// Bad arguments are rejected without queueing anything to the modem.
static void test_arg_validation(void **state) {
	(void)state;
	ardop_status st;
	unsigned char buf[4] = {1, 2, 3, 4};

	assert_int_equal(-1, ardop_get_status(NULL, &st));
	assert_int_equal(-1, ardop_get_status(A, NULL));
	assert_int_equal(-1, ardop_set_callsign(A, NULL));
	assert_int_equal(-1, ardop_arq_connect(A, NULL, 5));
	assert_int_equal(-1, ardop_arq_send(A, NULL, 4));
	assert_int_equal(-1, ardop_arq_send(A, buf, 0));
	assert_int_equal(-1, ardop_fec_send(A, NULL, 4));
	assert_int_equal(-1, ardop_fec_send(A, buf, 0));
	assert_int_equal(-1, ardop_set_capture_device(A, NULL));
	assert_int_equal(-1, ardop_set_playback_device(A, NULL));
	// Carriage returns are not allowed inside a host command.
	assert_int_equal(-1, ardop_command(A, "DRIVELEVEL 80\rMYCALL X"));

	ardop_embed_drain_inbound();
	assert_int_equal(0, rec_cmd_n);
	assert_int_equal(0, rec_data_n);
}

// Config setters and raw commands queue the expected host command strings, in
// order, and reach the core when the queue is drained.
static void test_command_queue_and_drain(void **state) {
	(void)state;
	assert_int_equal(0, ardop_set_callsign(A, "N0CALL"));
	assert_int_equal(0, ardop_set_gridsquare(A, "AB12cd"));
	assert_int_equal(0, ardop_set_protocolmode(A, "ARQ"));
	assert_int_equal(0, ardop_command(A, "DRIVELEVEL 80"));

	// Nothing reaches the core until the modem thread drains the queue.
	assert_int_equal(0, rec_cmd_n);

	ardop_embed_drain_inbound();
	assert_int_equal(4, rec_cmd_n);
	assert_string_equal("MYCALL N0CALL", rec_cmds[0]);
	assert_string_equal("GRIDSQUARE AB12cd", rec_cmds[1]);
	assert_string_equal("PROTOCOLMODE ARQ", rec_cmds[2]);
	assert_string_equal("DRIVELEVEL 80", rec_cmds[3]);
}

// ARQ connect formats an ARQCALL command and clamps the attempt count to 2..15
// (0 / below-range -> default 5, above-range -> 15).
static void test_arq_connect_clamp(void **state) {
	(void)state;
	assert_int_equal(0, ardop_arq_connect(A, "N0CALL", 0));
	assert_int_equal(0, ardop_arq_connect(A, "N0CALL", 3));
	assert_int_equal(0, ardop_arq_connect(A, "N0CALL", 99));

	ardop_embed_drain_inbound();
	assert_int_equal(3, rec_cmd_n);
	assert_string_equal("ARQCALL N0CALL 5", rec_cmds[0]);
	assert_string_equal("ARQCALL N0CALL 3", rec_cmds[1]);
	assert_string_equal("ARQCALL N0CALL 15", rec_cmds[2]);
}

// Disconnect / abort / purge map to their host commands.
static void test_arq_control_commands(void **state) {
	(void)state;
	assert_int_equal(0, ardop_arq_disconnect(A));
	assert_int_equal(0, ardop_arq_abort(A));
	assert_int_equal(0, ardop_purge_buffer(A));

	ardop_embed_drain_inbound();
	assert_int_equal(3, rec_cmd_n);
	assert_string_equal("DISCONNECT", rec_cmds[0]);
	assert_string_equal("ABORT", rec_cmds[1]);
	assert_string_equal("PURGEBUFFER", rec_cmds[2]);
}

// ARQ send queues raw bytes (no command) for the core's outgoing buffer.
static void test_arq_send_queues_data(void **state) {
	(void)state;
	unsigned char d[] = {0x01, 0x02, 0x03, 0x04, 0x05};
	assert_int_equal(0, ardop_arq_send(A, d, sizeof(d)));

	ardop_embed_drain_inbound();
	assert_int_equal(1, rec_data_n);
	assert_int_equal((int)sizeof(d), rec_data_len[0]);
	assert_memory_equal(d, rec_data[0], sizeof(d));
	assert_int_equal(0, rec_cmd_n);
}

// FEC send queues the datagram bytes and then a FECSEND TRUE command to trigger
// transmission.
static void test_fec_send_data_then_command(void **state) {
	(void)state;
	unsigned char d[] = {0xde, 0xad, 0xbe, 0xef};
	assert_int_equal(0, ardop_fec_send(A, d, sizeof(d)));

	ardop_embed_drain_inbound();
	assert_int_equal(1, rec_data_n);
	assert_int_equal((int)sizeof(d), rec_data_len[0]);
	assert_memory_equal(d, rec_data[0], sizeof(d));
	assert_int_equal(1, rec_cmd_n);
	assert_string_equal("FECSEND TRUE", rec_cmds[0]);
}

// get_status copies the modem-core globals into the caller's snapshot struct.
static void test_status_snapshot(void **state) {
	(void)state;
	ProtocolState = IRS;
	State = AcquireFrameSync;
	ARQState = ISSData;
	ProtocolMode = ARQ;
	bytDataToSendLength = 123;
	RXEnabled = true;
	TXEnabled = false;
	SoundIsPlaying = true;
	Capturing = true;
	intLeaderDetects = 7;
	intFrameSyncs = 3;
	intGoodFSKFrameDataDecodes = 11;
	intFailedFSKFrameDataDecodes = 2;
	dblLeaderSNAvg = 12.5f;
	intAvgFSKQuality = 88;

	ardop_status st;
	assert_int_equal(0, ardop_get_status(A, &st));

	assert_int_equal((int)IRS, st.protocol_state);
	assert_int_equal((int)AcquireFrameSync, st.receive_state);
	assert_int_equal((int)ISSData, st.arq_substate);
	assert_int_equal((int)ARQ, st.protocol_mode);
	assert_int_equal(123, st.buffer_bytes);
	assert_true(st.rx_enabled);
	assert_false(st.tx_enabled);
	assert_true(st.sound_playing);
	assert_true(st.capturing);
	assert_int_equal(7, st.leader_detects);
	assert_int_equal(3, st.frame_syncs);
	assert_int_equal(11, st.good_data_decodes);
	assert_int_equal(2, st.failed_data_decodes);
	assert_int_equal(88, st.avg_quality);
	assert_true(fabs(st.leader_snr - 12.5f) < 0.001);
}

// Received-data dispatch maps the 3-byte core tag to the API enum and delivers
// the bytes and registered ctx to the on_data callback.
static void test_emit_data_tag_mapping(void **state) {
	(void)state;
	unsigned char p[] = {0x09, 0x08, 0x07};

	ardop_embed_emit_data("ARQ", p, sizeof(p));
	assert_int_equal(1, on_data_calls);
	assert_int_equal(ARDOP_DATA_ARQ, last_tag);
	assert_ptr_equal(&ctx_marker, last_data_ctx);
	assert_int_equal((int)sizeof(p), last_data_len);
	assert_memory_equal(p, last_data, sizeof(p));

	ardop_embed_emit_data("FEC", p, sizeof(p));
	assert_int_equal(ARDOP_DATA_FEC, last_tag);

	ardop_embed_emit_data("ERR", p, sizeof(p));
	assert_int_equal(ARDOP_DATA_ERR, last_tag);

	ardop_embed_emit_data("IDF", p, sizeof(p));
	assert_int_equal(ARDOP_DATA_ID, last_tag);

	ardop_embed_emit_data("XYZ", p, sizeof(p));
	assert_int_equal(ARDOP_DATA_OTHER, last_tag);

	ardop_embed_emit_data(NULL, p, sizeof(p));
	assert_int_equal(ARDOP_DATA_OTHER, last_tag);

	assert_int_equal(6, on_data_calls);
}

// Event lines are delivered to on_event with the registered ctx; a NULL line is
// ignored rather than forwarded.
static void test_emit_event(void **state) {
	(void)state;
	ardop_embed_emit_event("CONNECTED N0CALL 500");
	assert_int_equal(1, on_event_calls);
	assert_ptr_equal(&ctx_marker, last_event_ctx);
	assert_string_equal("CONNECTED N0CALL 500", last_event);

	ardop_embed_emit_event(NULL);
	assert_int_equal(1, on_event_calls);  // not forwarded
}

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup_teardown(test_singleton, setup, teardown),
		cmocka_unit_test_setup_teardown(test_arg_validation, setup, teardown),
		cmocka_unit_test_setup_teardown(test_command_queue_and_drain, setup, teardown),
		cmocka_unit_test_setup_teardown(test_arq_connect_clamp, setup, teardown),
		cmocka_unit_test_setup_teardown(test_arq_control_commands, setup, teardown),
		cmocka_unit_test_setup_teardown(test_arq_send_queues_data, setup, teardown),
		cmocka_unit_test_setup_teardown(test_fec_send_data_then_command, setup, teardown),
		cmocka_unit_test_setup_teardown(test_status_snapshot, setup, teardown),
		cmocka_unit_test_setup_teardown(test_emit_data_tag_mapping, setup, teardown),
		cmocka_unit_test_setup_teardown(test_emit_event, setup, teardown),
	};

	ardop_test_setup();
	return cmocka_run_group_tests(tests, NULL, NULL);
}
