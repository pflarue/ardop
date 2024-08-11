#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "common/ardopcommon.h"
#include "ws_server/ws_server.h"

/////////////////////////////////////////////////////////////////////////////
//
// The protocol used to communicate with Webgui clients via a WebSocket
// connection should be considered unstable and undocumented.  It is subject
// to significant change without notice in future releases of ardopcf.
//
/////////////////////////////////////////////////////////////////////////////

#define MAX_WG_CLIENTS 4  // maximum number of simultaneous clients
#define WG_RSIZE 512  // maximum length of messages received from Webgui clients
#define WG_SSIZE 2048  // maximum length of messages sent to Webgui clients
#define MAX_AVGLEN 10  // maximum supported length of FFT averaging

extern int wg_port;  // Port number of WebGui.  If 0, no WebGui
extern char webgui_html[];
extern char webgui_js[];
extern BOOL blnBusyStatus;
extern BOOL blnARQConnected;
extern BOOL blnPending;
extern BOOL NeedTwoToneTest;
extern BOOL NeedID;
extern BOOL WG_DevMode;
extern char Callsign[CALL_BUF_SIZE];
extern char strRemoteCallsign[CALL_BUF_SIZE];
extern float wS1;
int ExtractARQBandwidth();
void ProcessCommandFromHost(char * strCMD);

bool WebguiActive = false;
int WebGuiNumConnected = 0;
unsigned char AvgLen = 1;
// oldmags is used as a circular buffer containing the MAX_AVGLEN most recent
// FFT magnitudes, but only the AvgLen most recent values are averaged each
// time.  oldmags[oldindex] holds the most recent values, with the next most
// recent at oldmags[oldindex - 1]
float oldmags[MAX_AVGLEN][206];
int oldindex = 0;  // index in oldmags of the
void WebguiInit();
void WebguiPoll();

// A structure to hold/buffer data recieved from Webgui clients.
struct wg_receive_data {
	// Assuming a single message may have a length up to WG_RSIZE, create buf
	// large enough to hold up to two complete messages.  Thus, allowing for
	// wg_poll() for RSIZE to get the last few bytes of one message to also
	// return all of the following message as well.
	unsigned char buf[2 * WG_RSIZE];
	int len;
	int offset;
};
// wg_rdata[i - 1] is used for data from cnum=i
// cnum is never 0.
struct wg_receive_data wg_rdata[MAX_WG_CLIENTS];

// wg_reset_rdata(cnum) is called by ws_poll() when the WebSocket connection
// to client cnum is closed.
void wg_reset_rdata(const int cnum) {
	// wg_rdata[i - 1] is used for cnum=i
	wg_rdata[cnum - 1].len = 0;
	wg_rdata[cnum - 1].offset = 0;
	// Probably could just decrement WebGuiNumConnected by 1,
	// but this prevents perpetuating an error if it occurs.
	WebGuiNumConnected = ws_get_client_count(NULL);
}

// discard already parsed data from rdata->buf.
void condense(struct wg_receive_data *rdata) {
	if (rdata->offset == 0)
		return;
	if (rdata->offset == rdata->len)
		rdata->len = 0;
	else {
		memmove(rdata->buf, rdata->buf + rdata->offset, rdata->len - rdata->offset);
		rdata->len -= rdata->offset;
	}
	rdata->offset = 0;
}

// debug_for_ws() and error_for_ws() are designed to be
// passed as callbacks to ws_set_debug() and ws_set_error()
int debug_for_ws(const char * format, ...) {
	if (! ZF_LOG_ON_DEBUG)
		return 0;

	char Mess[1024] = "";
	va_list(arglist);
	va_start(arglist, format);
	int rv = vsnprintf(Mess, sizeof(Mess), format, arglist);
	ZF_LOGD_STR(Mess);
	va_end(arglist);
	return rv;
}

int error_for_ws(const char * format, ...) {
	if (!ZF_LOG_ON_ERROR)
		return 0;

	char Mess[1024] = "";
	va_list(arglist);
	va_start(arglist, format);
	int rv = vsnprintf(Mess, sizeof(Mess), format, arglist);
	ZF_LOGE_STR(Mess);
	va_end(arglist);
	return rv;
}


void WebguiInit() {
	if (wg_port == 0)
		// No WebGui
		return;
	ws_set_error(&error_for_ws);
	ws_set_debug(&debug_for_ws);
	ws_set_max_clients(MAX_WG_CLIENTS);
	ws_set_port(wg_port);
	ws_set_uri("/ws");
	ws_set_close_notify(wg_reset_rdata);  // callback

	// Use root uri "/" for the webgui.html that users are likely
	// to manually enter into a web browser (with the specified port
	// number)
	// These resources are included into the executable as literal strings
	// so that ardopcf can be run without explicit installation that would
	// put the corresponding files into predictable directories where they
	// can easily be found.  See the /webgui directory for the
	// original source files and information about how the corresponding
	// c source files are generated.
	ws_add_httptarget_data("/", webgui_html, strlen(webgui_html), "text/html");
	ws_add_httptarget_data("/webgui.js", webgui_js, strlen(webgui_js), "text/javascript");
	// For development/debugging, it may be more convenient to serve these
	// resources from the files in the webgui directory.  To do so, uncomment
	// the next two lines, and comment out the two lines above
	//ws_add_httptarget_file("/", "webgui/webgui.html", "text/html");
	//ws_add_httptarget_file("/webgui.js", "webgui/webgui.js", "text/javascript");
	if (ws_init() < 0) {
		ZF_LOGE("Webgui start failure.");
	} else {
		WebguiActive = true;
		WebGuiNumConnected = 0;
		ZF_LOGI("Webgui available at port %d.", wg_port);
	}
}

// return the number of bytes used to encode uvalue
// Return -1 on failure
int encodeUvint(char *buf, int size, unsigned int uvalue) {
	int i = 0;
	while (uvalue >= 0x80) {
		buf[i++] = (uvalue & 0x7f) + 0x80;
		if (i >= size)
			return (-1);
		uvalue >>= 7;
	}
	buf[i++] = uvalue;
	return (i);
}

// Decode a variable length unsigned integer (uvint)
// Return the decoded value and update rdata->offset.
// But, if unable to decode a uvint from rdata because it does not contain
// sufficient data, then return -1 and make no changes to rdata.
int decodeUvint(struct wg_receive_data *rdata) {
	unsigned char byte;
	int result = 0;
	int shift = 0;
	int original_offset = rdata->offset;
	while (true) {
		if (rdata->offset >= rdata->len) {
			// unable to decode uvint
			rdata->offset = original_offset;
			return (-1);
		}
		byte = (unsigned char)(rdata->buf[rdata->offset]);
		rdata->offset++;
		result += (byte & 0x7f) << shift;
		shift += 7;
		if ((0x80 & byte) == 0) {
			return result;
		}
	}
}

// Write an uppercase space separated hexidecimal representation of
// data to outputStr, creating a null terminated string.  datalen is
// length of data.  count is the maximum length of outputStr including
// the terminating NULL.  If count is too small to write all of data,
// write as much as will fit.
// Return outputStr.
char* toHex(char *outputStr, size_t count, unsigned char *data, size_t datalen) {
	outputStr[0] = 0x00;  // create a zero length NULL terminated string;
	count -= 1;  // for the NULL
	for (unsigned int i=0; i<datalen; i++) {
		if (count < 3)
			return outputStr;
		sprintf(outputStr, "%02X ", data[i]);
		count -=3;
	}
	return outputStr;
}


// Two protocol layers are defined for messages to Webgui clients.
// The lower level protocol prefaces each message with a variable
// length unsigned integer (uvint) which specifies the length of the
// message excluding the length of this header.
// The higher level protocol specifies that the first byte of each
// message indicates the message type, and this shall always be
// followed by a pipe '|' 0x7C.  This printable and easily typed
// character that is not widely used provides an imperfect but
// reasonably reliable way to detect a message handling error that
// has caused the start of a message to be incorrectly located.
// Each message type specifies the requried format for the remainder
// of the message, if any.  Where a message includes a string, no
// terminating NULL will be included at the end of the string.  The
// length of the message provided by the lower level protocol may
// be used to determine the length of a string or other variable
// length data at the end of a message.
//
// To simplify the printing of raw messages for debugging purposes,
// message type bytes that represent printable ASCII characters in
// the range of 0x20 (space) to 0x7D (closing brace) shall be used
// for messages whose entire raw content may be printed as human
// readable text (though they will not include a NULL terminator),
// while the remaining type bytes indicate that for logging
// purposes, the message should be displayed as a string of
// hexidecimal values when printing of raw message content is
// desired.
// "a|" followed by string: Alert for user.
// "B|" no additional data: BUSY true
// "b|" no additional data: BUSY false
// "C|" followed by a string: My callsign
//   If string has zero length, clear my callsign.
// "c|" followed by a string: remote callsign
//   If string has zero length, clear remote callsign.
// "D|" no additional data: Enable Dev Mode
// "F|" followed by a string: TX Frame type
// "f|"
//   followed by a character:
//    'P' for pending, 'O' for OK, or 'F' for fail
//   followed by a string: RX Frame state and type
// "H|"
//   followed by a character:
//    'Q' for QueueCommandToHost
//    'C' for SendCommandToHost
//    'T' for SendCommandToHostQuiet
//	  'R' for SendReplyToHost
//    'F' for Command from Host.
//	 followed by a string: Message to host
// "h|" followed by a string: Host text data (to host)
// "m|" followed by a string: Protocol Mode
// "P|" no additional data: PTT true
// "p|" no additional data: PTT false
// "R|" no additional data: IRS true
// "r|" no additional data: IRS false
// "S|" no additional data: ISS true
// "s|" no additional data: ISS false
// "t|" followed by a string: Protocol State
// 0x817C followed by one additional byte interpreted as an unsigned
//   char in the range of 0 to 150: Set CurrentLevel
// 0x8A7C
//   followed by an unsigned char: Quality (0-100)
//   followed by a uvint: The total number of errors detected by RS
//   followed by a uvint: The max number of errors correctable by RS
//   (For a failed decode, total will be set equal to max + 1)
// 0x8B7C followed by one unsigned char: Bandwidth in Hz divided by 10.
// 0x8C7C followed by 103 bytes of data: FFT power (dBfs 4 bits each).
// 0x8D7C followed by one additional byte interpreted as an unsigned
//   char in the range 0 to 100: Set DriveLevel
// 0x8E7C followed by unsigned char data: Pixel data (x, y, color)
//   color is 1, 2, or 3 for poor, ok, good.
// 0x8F7C followed by unsigned char data: Host non-text data (to host)
//	 The first three characters of data are text data indicating
//	 data type 'FEC', 'ARQ', 'RXO', 'ERR', etc.
// 0x9A7C followed by one additional byte interpreted as an unsigned
//   char in the range 1 to MAX_AVGLEN: Set display averaging length

// For all of wg_send_*
// cnum = 0 to send to all Webgui clients.
// cnum > 0 to send to a single Webgui client
// cnum < 0 to send to all Webgui clients except for client number -cnum.

// Send data of length data_len to the specified clients.
// Return the number of clients that it was sent to.
// This function implements the lower level protocol of adding a
// uvint header to the start of each message indicating the
// length of its body (excluding the header length).
int wg_send_msg(int cnum, const char *data, unsigned int data_len) {
	if (WebGuiNumConnected == 0)
		return 0;
	int headlen;
	char header[2];
	if (data[1] != '|') {
		ZF_LOGE(
			"ERROR: Invalid message to send of length %u.  Msg begins with %02hhx %02hhx",
			data_len, data[0], data[1]);
		return (0);
	}
	if ((headlen = encodeUvint(header, 2, (unsigned int)(data_len))) == -1)
		return (0);
	if (data_len + headlen > WG_SSIZE) {
		ZF_LOGW(
			"Data to send to Webgui client is too big.  Discarding.");
		return (0);
	}
	// This message is sent with two calls to ws_send(), which will
	// thus send two WebSocket frames..
	ws_send(cnum, header, headlen);
	return ws_send(cnum, data, data_len);
}

// For text data
int wg_send_hostdatat(int cnum, char *prefix, unsigned char *data, int datalen) {
	if (!WG_DevMode)
		// don't send these to Webgui except in DevMode
		return (0);
	char Mess[WG_SSIZE - 2] = "h|";
	memcpy(Mess + 2, prefix, 3);
	if (datalen + 5 < WG_SSIZE - 2) {
		memcpy(Mess + 5, data, datalen);
		return wg_send_msg(cnum, Mess, datalen + 5);
	} else {
		ZF_LOGW("WARNING: datalen (%d) in wg_send_hostdatat()"
			" too big.  Truncated data sent.", datalen);
		memcpy(Mess + 5, data, WG_SSIZE - 7);
		return wg_send_msg(cnum, Mess, WG_SSIZE - 2);
	}
}

// For non-text data
int wg_send_hostdatab(int cnum, char *prefix, unsigned char *data, int datalen) {
	if (!WG_DevMode)
		// don't send these to Webgui except in DevMode
		return (0);
	char Mess[WG_SSIZE - 2];
	Mess[0] = 0x8F;
	Mess[1] = 0x7C;
	memcpy(Mess + 2, prefix, 3);
	if (datalen + 5 < WG_SSIZE - 2) {
		memcpy(Mess + 5, data, datalen);
		return wg_send_msg(cnum, Mess, datalen + 5);
	} else {
		ZF_LOGW("WARNING: datalen (%d) in wg_send_hostdatab()"
			" too big.  Truncated data sent.", datalen);
		memcpy(Mess + 5, data, WG_SSIZE - 7);
		return wg_send_msg(cnum, Mess, WG_SSIZE - 2);
	}
}

int wg_send_hostmsg(int cnum, char msgtype, unsigned char *strText) {
	if (!WG_DevMode)
		// don't send these to Webgui except in DevMode
		return (0);
	char Mess[256];
	snprintf(Mess, sizeof(Mess), "H|%c%s", msgtype, strText);
	return wg_send_msg(cnum, Mess, strlen(Mess));
}

int wg_send_alert(int cnum, const char * format, ...) {
	char Mess[1024] = "a|";
	va_list(arglist);
	va_start(arglist, format);
	// alert will be truncated to fit in Mess
	vsnprintf(Mess + 2, sizeof(Mess) - 2, format, arglist);
	va_end(arglist);
	return wg_send_msg(cnum, Mess, strlen(Mess));
}

int wg_send_protocolmode(int cnum) {
	const char ProtocolMode_msgs[3][5] = {"m|FEC", "m|ARQ", "m|RXO"};
	if (ProtocolMode < 1 || ProtocolMode > 3) {
		ZF_LOGW("Unexpected ProtocolMode=%d.  Unable to send to Webgui Client.", ProtocolMode);
		return (0);
	}
	return wg_send_msg(cnum, ProtocolMode_msgs[ProtocolMode - 1], 5);
}

int wg_send_state(int cnum) {
	// maximum length of Protocol State is 8 plus 1 for NULL
	char msg[11] = "t|";
	memcpy(msg + 2, ARDOPStates[ProtocolState], strlen(ARDOPStates[ProtocolState]));
	return wg_send_msg(cnum, msg, strlen(ARDOPStates[ProtocolState]) + 2);
}

int wg_send_bandwidth(int cnum) {
	// Use unsigned char for bandwidth in Hz divided by 10
	char msg[3];
	msg[0] = 0x8B;
	msg[1] = 0x7C;
	msg[2] = (unsigned char)(ExtractARQBandwidth() / 10);  // 20 to 200
	return wg_send_msg(cnum, msg, 3);
}

// linear fraction of fullscale audio scale scaled to 0-150
// TODO: Consider changing to to a log (dBfs) scale?
int wg_send_currentlevel(int cnum, unsigned char level) {
	char msg[3];
	msg[0] = 0x81;
	msg[1] = 0x7C;
	msg[2] = (unsigned char)(level);  // 0 to 150
	return wg_send_msg(cnum, msg, 3);
}

int wg_send_quality(int cnum, unsigned char quality,
	unsigned int totalRSErrors, unsigned int maxRSErrors)
{
	// msg has enough capacity for totalRSErrors and maxRSErrors to each
	// require two bytes
	char msg[7];
	int msglen = 3;  // length before adding two Uvint values
	int thislen;
	msg[0] = 0x8A;
	msg[1] = 0x7C;
	msg[2] = quality;  // 0 to 100
	if ((thislen = encodeUvint(msg + msglen, 2, totalRSErrors)) == -1) {
		ZF_LOGE(
			"ERROR: Failure encoding totalRSErrors=%d for Webgui.",
			totalRSErrors);
	}
	msglen += thislen;
	if ((thislen = encodeUvint(msg + msglen, 2, maxRSErrors)) == -1) {
		ZF_LOGE(
			"ERROR: Failure encoding maxRSErrors=%d for Webgui.",
			maxRSErrors);
	}
	msglen += thislen;
	return wg_send_msg(cnum, msg, msglen);
}

int wg_send_drivelevel(int cnum) {
	char msg[3];
	msg[0] = 0x8D;
	msg[1] = 0x7C;
	msg[2] = (unsigned char)(DriveLevel);  // 0 to 100
	return wg_send_msg(cnum, msg, 3);
}

int wg_send_avglen(int cnum) {
	char msg[3];
	msg[0] = 0x9A;
	msg[1] = 0x7C;
	msg[2] = AvgLen;  // 1 to MAX_AVGLEN
	return wg_send_msg(cnum, msg, 3);
}

// Provide a zero length string to clear remote callsign.
int wg_send_rcall(int cnum, char *call) {
	char msg[CALL_BUF_SIZE + 2];
	if (strlen(call) >= CALL_BUF_SIZE) {
		ZF_LOGW("Remote callsign (%s) too long for wg_send_rcall().", call);
		return (0);
	}
	snprintf(msg, sizeof(msg), "c|%s", call);
	return wg_send_msg(cnum, msg, strlen(msg));
}

// Provide a zero length string to clear my callsign.
int wg_send_mycall(int cnum, char *call) {
	char msg[CALL_BUF_SIZE + 2];
	if (strlen(call) >= CALL_BUF_SIZE) {
		ZF_LOGW("My callsign (%s) too long for wg_send_mycall().", call);
		return (0);
	}
	snprintf(msg, sizeof(msg), "C|%s", call);
	return wg_send_msg(cnum, msg, strlen(msg));
}

// For some frame types (ACK, NAK, PingAck)), the `frame` string
// may include a small amount of data that was encoded in that frame.
int wg_send_txframet(int cnum, const char *frame) {
	char msg[64];
	if (strlen(frame) > sizeof(msg) - 3) {
		ZF_LOGW("Frame type (%s) too long for wg_send_txframe().", frame);
		return (0);
	}
	snprintf(msg, sizeof(msg), "F|%s", frame);
	return wg_send_msg(cnum, msg, strlen(msg));
}

// `state` argument indicates whether frame is still being received (0),
// has been successfully received (1) or failure to successfully receive
// has occured (2).
// For some frame types (ACK, NAK, PingAck, IDFrame, etc.)), the `frame` string
// may include a small amount of data that was encoded in that frame.
int wg_send_rxframet(int cnum, unsigned char state, const char *frame) {
	char msg[64];
	unsigned char st[3] = {'P', 'O', 'F'};
	if (state > 2) {
		ZF_LOGW("Invalid state arument for wg_send_rxframe().");
		return(0);
	}
	if (strlen(frame) > sizeof(msg) - 3) {
		ZF_LOGW("Frame type (%s) too long for wg_send_rxframe().", frame);
		return (0);
	}
	snprintf(msg, sizeof(msg), "f|%c%s", st[state], frame);
	return wg_send_msg(cnum, msg, strlen(msg));
}

int wg_send_pttled(int cnum, bool isOn) {
	if (isOn)
		return wg_send_msg(cnum, "P|", 2);
	else
		return wg_send_msg(cnum, "p|", 2);
}

int wg_send_irsled(int cnum, bool isOn) {
	if (isOn)
		return wg_send_msg(cnum, "R|", 2);
	else
		return wg_send_msg(cnum, "r|", 2);
}

int wg_send_issled(int cnum, bool isOn) {
	if (isOn)
		return wg_send_msg(cnum, "S|", 2);
	else
		return wg_send_msg(cnum, "s|", 2);
}

int wg_send_busy(int cnum, bool isBusy) {
	if (isBusy)
		return wg_send_msg(cnum, "B|", 2);
	else
		return wg_send_msg(cnum, "b|", 2);
}

int wg_send_pixels(int cnum, unsigned char *data, size_t datalen) {
	char msg[10000];  // Large enough for 4FSK.2000.600
	if (datalen > sizeof(msg)) {
		ZF_LOGW(
			"WARNING: Too much pixel data (%lu bytes) provided to wg_send_pixels()",
			datalen);
		datalen = sizeof(msg) - 2;
	}
	msg[0] = 0x8E;
	msg[1] = 0x7C;
	memcpy(msg + 2, data, datalen);
	return wg_send_msg(cnum, msg, datalen + 2);
}

// Somewhat arbitrary choice of initial setting for minimum dBfs
// value that will be visible on the waterfall plot.  This is adjusted
// below to track somewhat above the minimum calculated dBfs.
float lowerLimitdBfs = -80;
// The ability to differentiate between very strong signal levels is
// probably not important, so group all such signals together,
// allowing greater resolution for lower power signals.
float upperLimitdBfs = -10;
// mags are actually powf(Re, 2) + powf(Im, 2), no sqrt() used.
// index is offset such that mags[0] corresponds to about 293 Hz,
// mags[103] is at 1500 Hz, and mags[205] is at about 2695 Hz.
int wg_send_fftdata(float *mags, int magsLen) {
	if (WebGuiNumConnected == 0)
		// wg_send_msg() does this check, but doing it here saves
		// work if data will not be sent.
		return (0);

	float pdBfs[206];
	float mindBfs = 0.0;
	float maxdBfs = -200.0;
	unsigned char msgData[105];
	msgData[0] = 0x8C;  // message type for 4-bit spectral data
	msgData[1] = 0x7C;

	if (magsLen != 206) {
		ZF_LOGW("wg_send_fftdata() expects 206 values, but received %d,", magsLen);
		return (0);
	}

	// mags[] are magnitude squared of the 25th to 230th result from a 1024-point windowed
	// FFT whose inputs were signed 16-bit real values sampled at 12000 samples per second.
	// 2*sqrt(mags[i])/wS1 is the calculated magnitude of a signal with a frequency
	// of (25 + i)12000/1024.  Since the inputs are 16-bit signed integers, the maximum
	// possible calculated magnitude is 32768.  So, calling this fs (full scale), convert
	// mags to dBfs based on the ratio of magnitude squared, so as to compare power rather
	// than amplitude/magnitude.
	// dBfs = 10 * log10(power/powerfs)
	// dBfs = 10 * log10(pow(2*sqrt(mags[i])/wS1, 2)/pow(32768, 2))
	// dBfs = 10 * log10(pow(2*sqrt(mags[i])/wS1, 2)) - 10 * log10(pow(32768, 2))
	// dBfs = 20 * log10(2*sqrt(mags[i])/wS1) - 20 * log10(32768)
	// dBfs = 20 * log10(sqrt(mags[i])) - 20*log(wS1/2) - 20 * log10(32768)
	// dBfs = 10 * log10(mags[i]) - 20 * log10(wS1/2 * 32768)
	float ref_dBfs = 20 * log10(wS1/2 * 32768);

	// To reduce the sample to sample variation in the displayed power at frequencies
	// that contain only noise, optionally average the results from multiple sequential
	// result sets.  This doesn't reduce the noise at any frequency, but it makes it more
	// steady.  The steadier value makes its value easier to see on a spectrum or
	// waterfall plot.  The downsize to averaging is that the results become less
	// reponsive to rapid changes in the data.
	// The user can adjust the number of samples averaged.  To avoid false discontinuities
	// when AvgLen is changed, always maintain the full MAX_AVGLEN data sets in oldmags,
	// but only average over the AvgLen most recent sets.
	oldindex = (oldindex + 1) % MAX_AVGLEN;
	memcpy(oldmags[oldindex], mags, sizeof(oldmags[oldindex]));
	for (int fbin = 0; fbin < 206; fbin++) {
		mags[fbin] = 0;
		// 1 <= AvgLen <= MAX_AVGLEN.  AvgLen=1 means no averaging
		for (int i = 0; i < AvgLen; i++)
			mags[fbin] += oldmags[(oldindex - i + MAX_AVGLEN) % MAX_AVGLEN][fbin];
		mags[fbin] /= AvgLen;
	}

	// ref_dBfs is about 135.6, so data may potentially range from -135.6 to 0.
	// However, the ability to distinguish between strong and very strong is
	// usually not too important.  Nor is it useful to use too much of the
	// visible scale showing only noise.
	// So, scale to 4 bits (0-15), but set anything less than lowerLimitdBfs
	// to 0, and anything above upperLimitdBfs to 15.
	for (int i=0; i<magsLen; i++) {
		// convert magsitude squared to power dBfs, and find min and max.
		// While mags[i] is unlikely to be 0, add one to avoid any possibility
		// of trying to do log10(0).  This is a very small change.
		pdBfs[i] = (10 * log10(mags[i] + 1)) - ref_dBfs;
		// find mindBfs and maxdBfs for later use in updating lowerLimitdBfs and uperLimitdBfs
		if (pdBfs[i] > maxdBfs)
			maxdBfs = pdBfs[i];
		else if (pdBfs[i] < mindBfs)
			mindBfs = pdBfs[i];
		// Rescale to 4 bits (0-15) using old values of upperLimitdBfs and lowerLimitdBfs.
		if (pdBfs[i] > upperLimitdBfs)
			pdBfs[i] = upperLimitdBfs;
		else if (pdBfs[i] < lowerLimitdBfs)
			pdBfs[i] = lowerLimitdBfs;
		pdBfs[i] = roundf((pdBfs[i] - lowerLimitdBfs) * (15.0/(upperLimitdBfs - lowerLimitdBfs)));
		if ((i & 0x01) == 0)
			// Even i are encoded in upper bits of data byte
			msgData[(i >> 1) + 2] = (unsigned char)(pdBfs[i] * 16);
		else
			// Odd i are encoded in lower bits of data byte
			msgData[(i >> 1) + 2] += (unsigned char)pdBfs[i];
	}

	// Implement a very slow AGC for waterfall/spectrum data by slowly adjusting
	// lowerLimitdBfs to about (10 - AvgLevel) dB above mindBfs,  This assumes at least
	// 10dB of variation in noise level for unaveraged data that can be ignored.
	// Increasing averaging should reduce this variability in noise, so attempt to
	// compensate for this.  There is probably room for improvement in this heuristic.
	// The adjustment is intentionally slow so that appearance of background noise level
	// remains relatively steady during short breaks when no signal is present.  Thus,
	// any sudden changes in the appearance of signal or noise level is present in the
	// received audio (though it may be an aritifact of the sound card's or radio's AGC.)
	//
	// Current values will be used to calculate scaling factors for future data.  Curent
	// data has already been rescaled.
	lowerLimitdBfs = lowerLimitdBfs + 0.02 * ((mindBfs + (10 - AvgLen)) - lowerLimitdBfs);
	// Similarly adjust upperLimitdBfs to about maxdBfs.
	// upperLimitdBfs = upperLimitdBfs + 0.02 * (maxdBfs - upperLimitdBfs);
	// if (upperLimitdBfs - lowerLimitdBfs < 40.0)
	//   upperLimitdBfs = lowerLimitdBfs + 40.0;

	return wg_send_msg(0, (char *)msgData, 105);
}


void WebguiPoll() {
	int cnum;
	char tmpdata[WG_RSIZE];
	int tmpdata_len = 0;
	struct wg_receive_data *rdata;
	int startoffset;
	int msglen;
	char errstr[128];

	if (!WebguiActive)
		return;
	if ((tmpdata_len = ws_poll(&cnum, tmpdata, WG_RSIZE)) <= 0)
		// If == 0, No data available.
		// If < 0, there was an arror, which ws_poll() already logged.
		return;
	// Recall that wg_rdata[i-1] is used for cnum=i
	rdata = &(wg_rdata[cnum - 1]);
	memcpy(rdata->buf + rdata->len, tmpdata, tmpdata_len);
	rdata->len += tmpdata_len;
	// Process messages from Webgui clients
	// Two protocol layers are defined for messages from Webgui clients.
	// The lower level protocol prefaces each message with a variable
	// length unsigned integer (uvint) which specifies the length of the
	// message excluding the length of this header.
	// The higher level protocol specifies that the first byte of each
	// message indicates the message type, and this shall always be
	// followed by a tilde '~' 0x7E.  This printable and easily typed
	// character that is not widely used provides an imperfect but
	// reasonably reliable way to detect a message handling error that
	// has caused the start of a message to be incorrectly located.
	// Each message type specifies the requried format for the remainder
	// of the message, if any.  Where a message includes a string, no
	// terminating NULL will be included at the end of the string.  The
	// length of the message provided by the lower level protocol shall
	// be used to determine the length of a string or other variable
	// length data at the end of a message.
	//
	// To simplify the printing of raw messages for debugging purposes,
	// message type bytes that represent printable ASCII characters in
	// the range of 0x20 (space) to 0x7D (closing brace) shall be used
	// for messages whose entire raw content may be printed as human
	// readable text (though they will not include a NULL terminator),
	// while the remaining type bytes indicate that for logging
	// purposes, the message should be displayed as a string of
	// hexidecimal values.
	// "0~" no additional data: Client connected/reconnected
	// "2~" no additional data: Request ardopcf to send 5 send two tone signal.
	// "I~" no additional data: Request ardopcf to send ID frame.
	// 0x8D7E followed by one additional byte interpreted as an unsigned
	//   char in the range 0 to 100: Set DriveLevel
	// 0x9A7E followed by one additional byte interpreted as an unsigned
	//   char in the range 1 to max_avglen: Set display averaging length

	startoffset = rdata->offset;
	while((msglen = decodeUvint(rdata)) >= 0) {
		if (msglen == 0) {
			// An empty message.  Do nothing.
			// update startoffset before parsing next msglen
			startoffset = rdata->offset;
			continue;
		}
		if ((rdata->len - rdata->offset) < msglen) {
			// The complete message has not yet been recieved.
			// rewind parsing of msglen
			rdata->offset = startoffset;
			break;
		}
		// higher level protocol
		if (rdata->buf[rdata->offset + 1] != '~') {
			ZF_LOGE(
				"Webgui protocol error.  All messages sent by Webgui clients"
				" are expected to begin with a variable length unsigned integer,"
				" followed by a one byte message type indicator, followed by a"
				" tilde '~' character.  No tilde was found.  Closing this client"
				" connection (cnum=%d).", cnum);
			ws_close(cnum, 0);
			wg_reset_rdata(cnum);
			break;
		}
		switch(rdata->buf[rdata->offset]) {
		case '0':
			// Client connected/reconnected (no additional data)
			// This is sent by a client upon connecting or reconnecting to
			// the server.  Send current status values.
			if (msglen != 2) {
				ZF_LOGW(
					"WARNING: Invalid msglen=%d received from cnum=%d with type='0'"
					" (Client connected/reconnected).  Expected msglen=2.  Ignoring.",
					msglen, cnum);
				break;
			}
			// Probably could just increment WebGuiNumConnected by 1,
			// but this prevents perpetuating an error if it occurs.
			WebGuiNumConnected = ws_get_client_count(NULL);

			wg_send_protocolmode(cnum);
			wg_send_state(cnum);
			// TODO: Also display ARQState?
			// To work well, create SetARQState() like SetARDOPProtocolState() in
			// ARQ.c so that it is only set changed in one place.
			wg_send_drivelevel(cnum);
			wg_send_avglen(cnum);
			wg_send_bandwidth(cnum);
			wg_send_busy(cnum, blnBusyStatus);

			if (Callsign[0] != 0x00)
				wg_send_mycall(cnum, Callsign);
			if (strRemoteCallsign[0] != 0x00 && (blnARQConnected || blnPending))
				wg_send_rcall(cnum, strRemoteCallsign);
			if (WG_DevMode)
				// This is an "undocumented" feature that may be discontinued in
				// future releases
				wg_send_msg(cnum, "D|", 2);
			break;
		case '2':
			if (ProtocolMode == RXO)
				wg_send_alert(cnum, "Cannot transmit in RXO ProtocolMode.");
			else if (ProtocolState == DISC)
				NeedTwoToneTest = true;
			else
				wg_send_alert(cnum, "Cannot send Two Tone Test from ProtocolState = %s.",
					ARDOPStates[ProtocolState]);
			break;
		case 'H':
			// Host command
			if (!WG_DevMode) {
				ZF_LOGW(
					"Host commands from Webgui are not supported when ardopcf"
					" and the Webgui are not in Dev Mode.");
				break;
			}
			memcpy(tmpdata, rdata->buf + rdata->offset + 2, msglen - 2);
			// add NULL terminator
			tmpdata[msglen - 2] = 0x00;
			ZF_LOGD("Host command (from Webgui): '%s'", tmpdata);
			ProcessCommandFromHost(tmpdata);
			break;
		case 'I':
			if (ProtocolMode == RXO)
				wg_send_alert(cnum, "Cannot transmit in RXO ProtocolMode.");
			else if (ProtocolState != DISC)
				wg_send_alert(cnum, "Cannot send ID from ProtocolState = %s.",
					ARDOPStates[ProtocolState]);
			else if (Callsign[0] == 0x00)
				wg_send_alert(cnum, "Cannot send ID.  Callsign not set.");
			else
				NeedID = true;
			break;
		case 0x8D:
			// Client command to change DriveLevel
			if (msglen != 3) {
				ZF_LOGW(
					"WARNING: Invalid msglen=%d received from cnum=%d with type=0x8D"
					" (Change DriveLevel).  Expected msglen=3.  Ignoring.",
					msglen, cnum);
				break;
			}
			if (rdata->buf[rdata->offset + 2] > 100) {
				ZF_LOGW(
					"WARNING: Invalid value=%d received from cnum=%d with type=0x8D"
					" (Change DriveLevel).  Expected 0 <= value <= 100.  Ignoring.",
					rdata->buf[rdata->offset + 2], cnum);
				break;
			}
			DriveLevel = (unsigned char)(rdata->buf[rdata->offset + 2]);
			ZF_LOGW("DriveLevel changed to %d by Webgui client cnum=%d",
				DriveLevel, cnum);
			wg_send_drivelevel(-cnum);  // notify all other Webgui clients
			break;
		case 0x9A:
			// Client command to change AvgLen
			if (msglen != 3) {
				ZF_LOGW(
					"WARNING: Invalid msglen=%d received from cnum=%d with type=0x8A"
					" (Change AvgLen).  Expected msglen=3.  Ignoring.",
					msglen, cnum);
				break;
			}
			if ((rdata->buf[rdata->offset + 2] > MAX_AVGLEN) ||
				(rdata->buf[rdata->offset + 2] < 1)) {
				ZF_LOGW(
					"WARNING: Invalid value=%d received from cnum=%d with type=0x8D"
					" (Change AvgLen).  Expected 1 <= value <= %d.  Ignoring.",
					rdata->buf[rdata->offset + 2], cnum, MAX_AVGLEN);
				break;
			}
			AvgLen = (unsigned char)(rdata->buf[rdata->offset + 2]);
			wg_send_avglen(-cnum);  // notify all other Webgui clients
			break;
		default:
			ZF_LOGW(
				"WARNING: Received an unexpected message of type=%d and length=%d"
				" from Webgui client number %d.",
				rdata->buf[rdata->offset], msglen, cnum);
			if (rdata->buf[rdata->offset] >= 0x20 && rdata->buf[rdata->offset] <= 0x7D)
				// This message can be printed as text (though it doesn't have a terminating NULL)
				ZF_LOGD("message (text): %.*s", msglen, rdata->buf + rdata->offset);
			else
				// This message should be printed as a string of hex values.
				ZF_LOGD("message (hex): %s...",
					toHex(errstr, sizeof(errstr),
					(unsigned char*)(rdata->buf + rdata->offset), (size_t)(msglen)));
			break;
		}
		rdata->offset += msglen;
		// update startoffset before parsing next msglen
		startoffset = rdata->offset;
	}
	condense(rdata);  // discard parsed data
}
