// TCP KISS server for ardopcf
//
// Provides a TCP server that speaks the KISS protocol
// (https://www.ax25.net/kiss.aspx) so that external AX.25/APRS software can
// use ardopcf as a simple FEC modem.  KISS data frames received from a
// connected client are decapsulated and the raw AX.25 frame they contain is
// transmitted using the ARDOP FEC modulator.  AX.25 frames decoded from
// received ARDOP FEC frames are KISS encapsulated and sent to all connected
// clients.
//
// An AX.25 frame too large for a single ARDOP FEC frame is fragmented over
// several FEC frames sent back to back within one transmission.  Each FEC frame
// payload begins with a 2-byte fragmentation header ([msgid][ (last << 7) |
// index ]) so the receiver can reassemble the original AX.25 frame.  Because the
// channel is half duplex, the fragments of one AX.25 frame always arrive
// contiguously (a colliding transmission destroys both), so a single receive
// reassembler suffices: a new fragment with index 0 starts a fresh frame, an
// out-of-sequence fragment discards the partial, and the last-fragment flag
// completes it.  There is no ARQ, so a lost fragment loses the whole AX.25 frame.
//
// On transmit the AX.25 frames pending transmission are held in a queue and fed
// to the FEC modulator one frame at a time, only while the modem is idle.  This
// keeps each AX.25 frame's fragments aligned to FEC-frame boundaries; pipelining
// a second frame into the FEC byte-stream queue while the first is still being
// carved would misalign the fragmentation headers.

#include <stdbool.h>

#ifdef WIN32
#define _CRT_SECURE_NO_DEPRECATE
#include <windows.h>
#pragma comment(lib, "WS2_32.Lib")
#define ioctl ioctlsocket
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#define SOCKET int
#define INVALID_SOCKET (SOCKET)(~0)
#define SOCKET_ERROR (-1)
#define WSAGetLastError() errno
#define closesocket close
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#include <stdlib.h>
#include <string.h>

#include "os_util.h"
#include "ARDOPC.h"
#include "ardopcommon.h"

// KISS special characters
#define FEND  0xC0  // Frame End
#define FESC  0xDB  // Frame Escape
#define TFEND 0xDC  // Transposed Frame End
#define TFESC 0xDD  // Transposed Frame Escape

#define KISS_MAX_CLIENTS 16

// Configuration (set by KISSConfig() from the --kiss command line option)
int KISSPort = 0;  // 0 means KISS server disabled
char KISSAddr[64] = "";  // empty means listen on loopback (127.0.0.1) only

extern char strFECMode[];
extern int FECRepeats;
extern const short FrameSize[256];
unsigned char FrameCode(char * bytFrameType);
bool StartFEC(UCHAR * bytData, int Len, char * strDataMode, int intRepeats, bool blnSendID);

static SOCKET KISSListenSock = INVALID_SOCKET;
static SOCKET KISSClients[KISS_MAX_CLIENTS];
static KISSDecoder KISSDecoders[KISS_MAX_CLIENTS];

// Receive-side reassembler for AX.25 frames fragmented over multiple FEC frames.
static KISSReassembler KISSRxReasm;

// Transmit-side FIFO of complete AX.25 frames awaiting transmission.  Frames are
// fed to the FEC modulator one at a time by KISSPumpTx() while the modem is idle,
// so each frame's fragments stay aligned to FEC-frame boundaries.
#define KISS_TXQ_MAX 64
typedef struct {
	UCHAR *data;
	int len;
} KISSTxEntry;
static KISSTxEntry KISSTxQ[KISS_TXQ_MAX];
static int KISSTxQHead = 0;  // index of the oldest queued frame
static int KISSTxQCount = 0;  // number of frames currently queued
static UCHAR KISSTxMsgId = 0;  // fragmentation msgid, increments per AX.25 frame

// Return the single-frame payload capacity (bytes) of the named FEC mode, or 0
// if the mode is unknown.
int KISSModeCapacity(const char *fecmode)
{
	char FullType[20];
	snprintf(FullType, sizeof(FullType), "%s.E", fecmode);
	int fc = FrameCode(FullType);
	if (fc <= 0)
		return 0;
	return FrameSize[fc];
}

// KISS encapsulate an AX.25 frame into out: FEND, type byte (0x00 = data,
// port 0), escaped payload, FEND.  Returns the number of bytes written, or -1
// if out is too small.
int KISSEncode(const UCHAR *axdata, int len, UCHAR *out, int outsize)
{
	int o = 0;

	if (outsize < 3)
		return -1;  // need at least FEND, type, FEND

	out[o++] = FEND;
	out[o++] = 0x00;  // data frame, port 0
	for (int i = 0; i < len; i++)
	{
		UCHAR b = axdata[i];
		if (b == FEND || b == FESC)
		{
			// An escape pair plus the trailing FEND must still fit.
			if (o + 3 > outsize)
				return -1;
			out[o++] = FESC;
			out[o++] = (b == FEND) ? TFEND : TFESC;
		}
		else
		{
			if (o + 2 > outsize)
				return -1;
			out[o++] = b;
		}
	}
	out[o++] = FEND;
	return o;
}

void KISSDecoderReset(KISSDecoder *d)
{
	d->len = 0;
	d->inEsc = false;
	d->overflow = false;
}

// Feed one received byte to the decoder.  Returns the length of a completed
// frame (with its de-escaped bytes in d->frame) when a frame terminates, or 0
// otherwise.
int KISSDecoderByte(KISSDecoder *d, UCHAR b)
{
	if (b == FEND)
	{
		// A frame terminates.  Report its length unless it was empty (a mere
		// delimiter) or overflowed.  Either way, start fresh afterwards.
		int ready = (!d->overflow && d->len > 0) ? d->len : 0;
		d->len = 0;
		d->inEsc = false;
		d->overflow = false;
		return ready;
	}

	if (d->inEsc)
	{
		d->inEsc = false;
		if (b == TFEND)
			b = FEND;
		else if (b == TFESC)
			b = FESC;
		// else protocol violation; store the byte as-is
	}
	else if (b == FESC)
	{
		d->inEsc = true;
		return 0;
	}

	if (!d->overflow)
	{
		if (d->len < KISS_FRAME_MAX)
			d->frame[d->len++] = b;
		else
			d->overflow = true;  // discard until the next FEND
	}
	return 0;
}

// Build the concatenated fragment buffer for one AX.25 frame.  See ardopcommon.h
// for the contract.  Every fragment except the last is exactly framecap bytes so
// the FEC layer's fixed-size carving reproduces the fragments exactly.
int KISSFragmentBuild(const UCHAR *axdata, int len, int framecap, UCHAR msgid,
	UCHAR *out, int outsize)
{
	int chunk = framecap - KISS_FRAG_HDR;
	if (chunk < 1 || len <= 0)
		return -1;

	int nfrags = (len + chunk - 1) / chunk;
	if (nfrags > KISS_FRAG_MAXFRAGS)
		return -1;

	int o = 0;
	for (int i = 0; i < nfrags; i++)
	{
		int off = i * chunk;
		int clen = len - off;
		if (clen > chunk)
			clen = chunk;
		bool last = (i == nfrags - 1);

		if (o + KISS_FRAG_HDR + clen > outsize)
			return -1;

		out[o++] = msgid;
		out[o++] = (last ? 0x80 : 0x00) | (UCHAR)(i & 0x7F);
		memcpy(out + o, axdata + off, clen);
		o += clen;
	}
	return o;
}

void KISSReassemblerReset(KISSReassembler *r)
{
	r->len = 0;
	r->active = false;
	r->expectedIndex = 0;
	r->msgid = 0;
}

// Feed one received FEC-frame payload (with its fragmentation header) to the
// reassembler.  See ardopcommon.h for the contract.
int KISSReassembleFrame(KISSReassembler *r, const UCHAR *frame, int frameLen,
	UCHAR *out, int outsize)
{
	if (frameLen < KISS_FRAG_HDR)
		return -1;  // runt; leave any partial untouched

	UCHAR msgid = frame[0];
	bool last = (frame[1] & 0x80) != 0;
	int index = frame[1] & 0x7F;
	const UCHAR *payload = frame + KISS_FRAG_HDR;
	int plen = frameLen - KISS_FRAG_HDR;

	if (index == 0)
	{
		// Start (or restart) a reassembly, discarding any partial in progress.
		r->active = true;
		r->len = 0;
		r->msgid = msgid;
	}
	else if (!r->active || msgid != r->msgid || index != r->expectedIndex)
	{
		// Out of sequence, or a fragment of a different frame: drop the partial.
		r->active = false;
		return -1;
	}

	if (r->len + plen > (int)sizeof(r->buf))
	{
		r->active = false;
		return -1;  // would overflow the reassembly buffer; give up
	}

	memcpy(r->buf + r->len, payload, plen);
	r->len += plen;
	r->expectedIndex = index + 1;

	if (!last)
		return 0;  // more fragments expected

	// Last fragment: the AX.25 frame is complete.
	int total = r->len;
	r->active = false;
	if (total <= 0)
		return 0;  // completed but empty; nothing to deliver
	if (total > outsize)
		return -1;  // caller's buffer too small
	memcpy(out, r->buf, total);
	return total;
}

// Parse the --kiss argument, which is "[addr:]port".  If addr is omitted, the
// server listens on loopback only.  Returns true on success.
bool KISSConfig(const char *arg)
{
	if (arg == NULL || arg[0] == 0x00)
		return false;

	const char *colon = strrchr(arg, ':');
	if (colon != NULL)
	{
		int n = (int)(colon - arg);
		if (n >= (int)sizeof(KISSAddr))
			n = sizeof(KISSAddr) - 1;
		memcpy(KISSAddr, arg, n);
		KISSAddr[n] = 0x00;
		KISSPort = atoi(colon + 1);
	}
	else
	{
		KISSAddr[0] = 0x00;
		KISSPort = atoi(arg);
	}

	if (KISSPort <= 0 || KISSPort > 65535)
	{
		KISSPort = 0;
		return false;
	}
	return true;
}

// Open the listening socket.  Returns true on success.
bool KISSInit()
{
	struct sockaddr_in local_sin;
	u_long param = 1;

	for (int i = 0; i < KISS_MAX_CLIENTS; i++)
	{
		KISSClients[i] = INVALID_SOCKET;
		KISSDecoderReset(&KISSDecoders[i]);
	}
	KISSReassemblerReset(&KISSRxReasm);
	for (int i = 0; i < KISS_TXQ_MAX; i++)
	{
		free(KISSTxQ[i].data);
		KISSTxQ[i].data = NULL;
	}
	KISSTxQHead = 0;
	KISSTxQCount = 0;

	if (KISSPort == 0)
		return false;

	// AX.25 frames larger than one FEC frame are fragmented across several FEC
	// frames and reassembled by the receiver, so any valid FEC mode works.  Only
	// a mode with no usable per-frame payload (smaller than the fragmentation
	// header) is a hard failure.
	int cap = KISSModeCapacity(strFECMode);
	if (cap <= KISS_FRAG_HDR)
	{
		ZF_LOGE("KISS: FEC mode %s has no usable per-frame capacity (%d bytes)."
			"  KISS server not started.  Set a valid FECMODE.", strFECMode, cap);
		KISSPort = 0;
		return false;
	}

#ifdef WIN32
	{
		WSADATA WsaData;
		WSAStartup(MAKEWORD(2, 0), &WsaData);
	}
#endif

	memset(&local_sin, 0, sizeof(local_sin));
	local_sin.sin_family = AF_INET;
	local_sin.sin_port = htons(KISSPort);
	if (KISSAddr[0] == 0x00)
		local_sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	else
	{
		local_sin.sin_addr.s_addr = inet_addr(KISSAddr);
		if (local_sin.sin_addr.s_addr == INADDR_NONE)
		{
			ZF_LOGE("KISS: invalid listen address \"%s\".  KISS server not"
				" started.", KISSAddr);
			KISSPort = 0;
			return false;
		}
	}

	KISSListenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (KISSListenSock == INVALID_SOCKET)
	{
		ZF_LOGE("KISS: socket() failed error %d", WSAGetLastError());
		KISSPort = 0;
		return false;
	}

	setsockopt(KISSListenSock, SOL_SOCKET, SO_REUSEADDR, (char *)&param, sizeof(param));

	if (bind(KISSListenSock, (struct sockaddr *)&local_sin, sizeof(local_sin)) == SOCKET_ERROR)
	{
		ZF_LOGE("KISS: bind() failed for %s:%d error %d.  KISS server not"
			" started.",
			KISSAddr[0] ? KISSAddr : "127.0.0.1", KISSPort, WSAGetLastError());
		closesocket(KISSListenSock);
		KISSListenSock = INVALID_SOCKET;
		KISSPort = 0;
		return false;
	}

	if (listen(KISSListenSock, 4) == SOCKET_ERROR)
	{
		ZF_LOGE("KISS: listen() failed error %d.  KISS server not started.",
			WSAGetLastError());
		closesocket(KISSListenSock);
		KISSListenSock = INVALID_SOCKET;
		KISSPort = 0;
		return false;
	}

	ioctl(KISSListenSock, FIONBIO, &param);

	ZF_LOGI("KISS: listening for TCP KISS connections on %s:%d (FEC mode %s,"
		" %d bytes/FEC frame; AX.25 frames larger than %d bytes are fragmented)",
		KISSAddr[0] ? KISSAddr : "127.0.0.1", KISSPort, strFECMode, cap,
		cap - KISS_FRAG_HDR);

	return true;
}

// Queue one decapsulated AX.25 frame for transmission.  A private copy is made;
// the frame is fragmented and modulated later by KISSPumpTx() once the modem is
// idle, so that we never pipeline a second frame into the FEC byte-stream queue
// while the first is still being carved into FEC frames.
static void KISSEnqueueTx(const UCHAR *axdata, int len)
{
	if (len <= 0)
		return;

	if (KISSTxQCount >= KISS_TXQ_MAX)
	{
		ZF_LOGW("KISS: transmit queue full (%d frames); dropping %d byte AX.25"
			" frame.", KISS_TXQ_MAX, len);
		return;
	}

	UCHAR *copy = malloc(len);
	if (copy == NULL)
	{
		ZF_LOGE("KISS: out of memory queueing %d byte AX.25 frame.", len);
		return;
	}
	memcpy(copy, axdata, len);

	int slot = (KISSTxQHead + KISSTxQCount) % KISS_TXQ_MAX;
	KISSTxQ[slot].data = copy;
	KISSTxQ[slot].len = len;
	KISSTxQCount++;
}

// If an AX.25 frame is queued and the modem is idle, fragment it across one or
// more FEC frames and start transmitting.  Called regularly from KISSPoll().
static void KISSPumpTx()
{
	if (KISSListenSock == INVALID_SOCKET || KISSTxQCount == 0)
		return;

	// Only start a transmission while the modem is idle (not already sending and
	// not receiving a frame).  Feeding the FEC queue while a previous frame is
	// still being carved would misalign the fragmentation headers.
	if (ProtocolState != DISC)
		return;

	KISSTxEntry *e = &KISSTxQ[KISSTxQHead];

	int cap = KISSModeCapacity(strFECMode);
	// Worst-case fragmented size: the frame plus a header per fragment.
	UCHAR frags[KISS_FRAME_MAX + KISS_FRAG_MAXFRAGS * KISS_FRAG_HDR];
	int flen = KISSFragmentBuild(e->data, e->len, cap, KISSTxMsgId,
		frags, sizeof(frags));
	if (flen < 0)
	{
		ZF_LOGE("KISS: cannot fragment %d byte AX.25 frame for FEC mode %s (%d"
			" bytes/frame); dropping.", e->len, strFECMode, cap);
	}
	else
	{
		int chunk = cap - KISS_FRAG_HDR;
		int nfrags = (e->len + chunk - 1) / chunk;
		ZF_LOGI("KISS: transmitting %d byte AX.25 frame as %d FEC fragment(s)"
			" (%s, msgid %u)", e->len, nfrags, strFECMode, KISSTxMsgId);

		// Send without an ARDOP IDFrame.  The source callsign is carried within
		// the AX.25 frame, and an ARDOP IDFrame would be meaningless to clients.
		if (!StartFEC(frags, flen, strFECMode, FECRepeats, false))
			ZF_LOGW("KISS: StartFEC() failed for queued KISS frame.");
		KISSTxMsgId++;
	}

	// Dequeue whether the frame was sent or dropped.
	free(e->data);
	e->data = NULL;
	KISSTxQHead = (KISSTxQHead + 1) % KISS_TXQ_MAX;
	KISSTxQCount--;
}

// Process one complete (already de-escaped) KISS frame.
static void KISSProcessFrame(UCHAR *frame, int len)
{
	if (len < 1)
		return;  // Need at least the type/command byte

	UCHAR cmd = frame[0] & 0x0F;  // low nibble is the command, high nibble port

	if (cmd == 0x00)
	{
		// Data frame.  The remainder is the raw AX.25 frame; queue it for
		// transmission by KISSPumpTx() when the modem is next idle.
		KISSEnqueueTx(frame + 1, len - 1);
	}
	else
	{
		// TXDELAY, PERSIST, SLOTTIME, TXTAIL, FULLDUPLEX, SETHARDWARE, RETURN.
		// These TNC parameters do not apply to ardopcf, so they are ignored.
		ZF_LOGD("KISS: ignoring non-data KISS command 0x%02X", cmd);
	}
}

// Feed received bytes from a client through its KISS decoder, transmitting any
// completed frames.
static void KISSDecode(int idx, UCHAR *data, int len)
{
	for (int i = 0; i < len; i++)
	{
		int flen = KISSDecoderByte(&KISSDecoders[idx], data[i]);
		if (flen > 0)
			KISSProcessFrame(KISSDecoders[idx].frame, flen);
	}
}

static void KISSCloseClient(int idx)
{
	if (KISSClients[idx] != INVALID_SOCKET)
	{
		closesocket(KISSClients[idx]);
		KISSClients[idx] = INVALID_SOCKET;
	}
	KISSDecoderReset(&KISSDecoders[idx]);
}

// Accept new connections and read data from connected clients.  Non-blocking.
void KISSPoll()
{
	u_long param = 1;
	UCHAR buf[KISS_FRAME_MAX];

	if (KISSListenSock == INVALID_SOCKET)
		return;

	// Start transmitting a queued AX.25 frame if the modem is idle.
	KISSPumpTx();

	// Accept any pending connections.
	while (true)
	{
		struct sockaddr_in sin;
		int addrlen = sizeof(sin);
		SOCKET newsock = accept(KISSListenSock, (struct sockaddr *)&sin, &addrlen);

		if (newsock == INVALID_SOCKET)
			break;  // EWOULDBLOCK or no pending connection

		int slot = -1;
		for (int i = 0; i < KISS_MAX_CLIENTS; i++)
		{
			if (KISSClients[i] == INVALID_SOCKET)
			{
				slot = i;
				break;
			}
		}
		if (slot == -1)
		{
			ZF_LOGW("KISS: too many clients, rejecting new connection.");
			closesocket(newsock);
			continue;
		}

		ioctl(newsock, FIONBIO, &param);
		KISSClients[slot] = newsock;
		KISSDecoderReset(&KISSDecoders[slot]);
		ZF_LOGI("KISS: client connected from %s:%d (slot %d)",
			inet_ntoa(sin.sin_addr), ntohs(sin.sin_port), slot);
	}

	// Read from connected clients.
	for (int i = 0; i < KISS_MAX_CLIENTS; i++)
	{
		if (KISSClients[i] == INVALID_SOCKET)
			continue;

		int len = recv(KISSClients[i], (char *)buf, sizeof(buf), 0);
		if (len > 0)
		{
			KISSDecode(i, buf, len);
		}
		else if (len == 0)
		{
			ZF_LOGI("KISS: client (slot %d) disconnected.", i);
			KISSCloseClient(i);
		}
		else
		{
			// len < 0: error or no data available
#ifdef WIN32
			if (WSAGetLastError() != WSAEWOULDBLOCK)
#else
			if (errno != EWOULDBLOCK && errno != EAGAIN)
#endif
			{
				ZF_LOGI("KISS: client (slot %d) read error, closing.", i);
				KISSCloseClient(i);
			}
		}
	}
}

// KISS encapsulate a fully reassembled AX.25 frame and send it to all connected
// clients.
static void KISSDeliverFrame(const UCHAR *axdata, int len)
{
	// Build the KISS frame.  Worst case each payload byte expands to two bytes,
	// plus the leading FEND, type byte, and trailing FEND.
	UCHAR out[2 * KISS_FRAME_MAX + 3];
	int o = KISSEncode(axdata, len, out, sizeof(out));
	if (o < 0)
	{
		ZF_LOGW("KISS: received frame too large to encode (%d bytes), dropping.",
			len);
		return;
	}

	for (int i = 0; i < KISS_MAX_CLIENTS; i++)
	{
		if (KISSClients[i] == INVALID_SOCKET)
			continue;
		if (send(KISSClients[i], (char *)out, o, MSG_NOSIGNAL) == SOCKET_ERROR)
		{
#ifdef WIN32
			if (WSAGetLastError() != WSAEWOULDBLOCK)
#else
			if (errno != EWOULDBLOCK && errno != EAGAIN)
#endif
			{
				ZF_LOGI("KISS: send to client (slot %d) failed, closing.", i);
				KISSCloseClient(i);
			}
		}
	}
	ZF_LOGI("KISS: delivered %d byte AX.25 frame to clients.", len);
}

// Called from the FEC receive path with a successfully decoded frame.  The frame
// carries a fragmentation header; feed it to the reassembler and deliver to KISS
// clients only once a complete AX.25 frame has been reassembled.
void KISSSendToClients(UCHAR *axdata, int len)
{
	if (KISSListenSock == INVALID_SOCKET || len <= 0)
		return;

	UCHAR frame[KISS_FRAME_MAX];
	int flen = KISSReassembleFrame(&KISSRxReasm, axdata, len, frame, sizeof(frame));
	if (flen <= 0)
		return;  // fragment consumed, or out-of-sequence; nothing complete yet

	KISSDeliverFrame(frame, flen);
}
