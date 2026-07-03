// Webgui_stub.c - no-op WebGUI for embedded library builds
//
// The modem core calls a large set of wg_send_*() notification functions plus
// WebguiInit()/WebguiPoll() throughout its operation.  In the embedded library
// build there is no Web UI and no embedded web/WebSocket server, so this file
// provides inert implementations of the entire Webgui.h interface.  Status and
// data reach the application through the ardop_lib callbacks instead.
//
// Replaces src/common/Webgui.c (excluded from embedded builds, along with the
// generated webgui HTML/JS and the lib/ws_server WebSocket server).

#include <stdbool.h>
#include <stddef.h>

#include "common/Webgui.h"

// Defined in Webgui.c in normal builds; the core reads it elsewhere.
int WebGuiNumConnected = 0;

void WebguiInit(void) {}
void WebguiPoll(void) {}

int encodeUvint(char *buf, int size, unsigned int uvalue) {
	(void)buf; (void)size; (void)uvalue;
	return 0;
}

int wg_send_hostdatat(int cnum, char *prefix, unsigned char *data, int datalen) {
	(void)cnum; (void)prefix; (void)data; (void)datalen;
	return 0;
}
int wg_send_hostdatab(int cnum, char *prefix, unsigned char *data, int datalen) {
	(void)cnum; (void)prefix; (void)data; (void)datalen;
	return 0;
}
int wg_send_hostmsg(int cnum, char msgtype, unsigned char *strText) {
	(void)cnum; (void)msgtype; (void)strText;
	return 0;
}
int wg_send_alert(int cnum, const char *format, ...) {
	(void)cnum; (void)format;
	return 0;
}
int wg_send_protocolmode(int cnum) { (void)cnum; return 0; }
int wg_send_state(int cnum) { (void)cnum; return 0; }
int wg_send_bandwidth(int cnum) { (void)cnum; return 0; }
int wg_send_currentlevel(int cnum, unsigned char level) {
	(void)cnum; (void)level;
	return 0;
}
int wg_send_quality(int cnum, unsigned char quality,
	unsigned int totalRSErrors, unsigned int maxRSErrors) {
	(void)cnum; (void)quality; (void)totalRSErrors; (void)maxRSErrors;
	return 0;
}
int wg_send_drivelevel(int cnum) { (void)cnum; return 0; }
int wg_send_avglen(int cnum) { (void)cnum; return 0; }
int wg_send_rcall(int cnum, const char *call) { (void)cnum; (void)call; return 0; }
int wg_send_mycall(int cnum, char *call) { (void)cnum; (void)call; return 0; }
int wg_send_txframet(int cnum, const char *frame) { (void)cnum; (void)frame; return 0; }
int wg_send_rxframet(int cnum, unsigned char state, const char *frame) {
	(void)cnum; (void)state; (void)frame;
	return 0;
}
int wg_send_pttled(int cnum, bool isOn) { (void)cnum; (void)isOn; return 0; }
int wg_send_irsled(int cnum, bool isOn) { (void)cnum; (void)isOn; return 0; }
int wg_send_issled(int cnum, bool isOn) { (void)cnum; (void)isOn; return 0; }
int wg_send_busy(int cnum, bool isBusy) { (void)cnum; (void)isBusy; return 0; }
int wg_send_wavrx(int cnum, bool isRecording) { (void)cnum; (void)isRecording; return 0; }
int wg_send_pttenabled(int cnum, bool enabled) { (void)cnum; (void)enabled; return 0; }
int wg_send_rxenabled(int cnum, bool enabled) { (void)cnum; (void)enabled; return 0; }
int wg_send_rxsilent(int cnum) { (void)cnum; return 0; }
int wg_send_txenabled(int cnum, bool enabled) { (void)cnum; (void)enabled; return 0; }
int wg_send_capturechannel(int cnum) { (void)cnum; return 0; }
int wg_send_playbackchannel(int cnum) { (void)cnum; return 0; }
int wg_send_devices(int cnum, char **ss, char **cs) { (void)cnum; (void)ss; (void)cs; return 0; }
int wg_send_audiodevices(int cnum, DeviceInfo **devices, char *cdevice,
	char *pdevice, bool crestore, bool prestore) {
	(void)cnum; (void)devices; (void)cdevice; (void)pdevice;
	(void)crestore; (void)prestore;
	return 0;
}
int wg_send_ptton(int cnum, char *hexstr) { (void)cnum; (void)hexstr; return 0; }
int wg_send_pttoff(int cnum, char *hexstr) { (void)cnum; (void)hexstr; return 0; }
int wg_send_pixels(int cnum, unsigned char *data, size_t datalen) {
	(void)cnum; (void)data; (void)datalen;
	return 0;
}
int wg_send_fftdata(float *mags, int magsLen) { (void)mags; (void)magsLen; return 0; }
