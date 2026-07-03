// ardop-demo - a tiny, hardware-free smoke test for the embedded ARDOP library.
//
// It drives the CArdop C API end to end using the dummy "NOSOUND" audio device,
// so it needs no microphone, speaker, or radio: create the modem, configure it,
// start the modem thread, transmit a FEC datagram, observe the status/events,
// then stop.  Useful as a CI sanity check that the whole stack links and runs.
//
// Build & run from host/ios:   swift run ardop-demo

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ardop_lib.h"

static void on_data(void *ctx, ardop_data_tag tag, const uint8_t *data, int len) {
	(void)ctx;
	(void)data;
	static const char *names[] = {"ARQ", "FEC", "ERR", "ID", "OTHER"};
	printf("[data ] tag=%s len=%d\n", names[tag], len);
}

static void on_event(void *ctx, const char *line) {
	(void)ctx;
	printf("[event] %s\n", line);
}

int main(void) {
	ardop_callbacks cb = {0};
	cb.on_data = on_data;
	cb.on_event = on_event;

	ardop_t *a = ardop_create(&cb);
	if (a == NULL) {
		fprintf(stderr, "ardop_create failed\n");
		return 1;
	}

	// Hardware-free: use the dummy NOSOUND device for both directions.
	ardop_set_capture_device(a, "NOSOUND");
	ardop_set_playback_device(a, "NOSOUND");

	// Configuration is queued and applied on the modem thread once started.
	ardop_set_callsign(a, "N0CALL");
	ardop_set_gridsquare(a, "CN87");
	ardop_set_protocolmode(a, "FEC");

	if (ardop_start(a) != 0) {
		fprintf(stderr, "ardop_start failed\n");
		ardop_destroy(a);
		return 1;
	}
	printf("modem started (running=%d)\n", ardop_is_running(a));

	// Let the modem thread drain the queued configuration commands.
	usleep(300 * 1000);

	const char *msg = "Hello ARDOP from the demo";
	printf("sending FEC datagram: \"%s\" (%zu bytes)\n", msg, strlen(msg));
	ardop_fec_send(a, (const uint8_t *)msg, (int)strlen(msg));

	// Run for a couple of seconds while the FEC frame is transmitted (instantly,
	// since NOSOUND discards audio) and events are emitted.
	for (int i = 0; i < 10; ++i) {
		usleep(200 * 1000);
		if (i % 3 == 0) {
			ardop_status st;
			if (ardop_get_status(a, &st) == 0)
				printf("[stat ] state=%d mode=%d buffer=%d tx=%d rx=%d\n",
					st.protocol_state, st.protocol_mode, st.buffer_bytes,
					st.tx_enabled, st.rx_enabled);
		}
	}

	ardop_stop(a);
	ardop_destroy(a);
	printf("done\n");
	return 0;
}
