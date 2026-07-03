// host_shim.c - embedded replacement for the TCP host interface
//
// The modem core (src/common) talks to its controlling host through a small
// set of TCP* functions and two socket globals.  For the embedded library
// builds (iOS/Android) we provide those same symbols here but route them to
// in-process queues and callbacks instead of TCP sockets.  This file is shared
// by every embedded platform; nothing in it is platform specific.
//
// Replaces src/common/TCPHostInterface.c (which is excluded from embedded
// builds).  It must provide every symbol from that file that the rest of the
// core references:
//   - TCPControlSock, TCPDataSock      (closed by ardopmain() on shutdown)
//   - TCPHostInit(), TCPHostPoll()
//   - TCPSendCommandToHost[Quiet](), TCPQueueCommandToHost(),
//     TCPSendReplyToHost(), TCPAddTagToDataAndSendToHost()
//   - SendtoGUI()

#include <stdbool.h>

#include "common/log.h"
#include "embed_internal.h"

// Referenced by ardopmain() which unconditionally calls closesocket() on these
// at shutdown.  -1 makes that close() a harmless no-op (EBADF) rather than
// closing a real descriptor such as stdin.
int TCPControlSock = -1;
int TCPDataSock = -1;

// No listening sockets in the embedded build.
bool TCPHostInit(void)
{
	ZF_LOGI("ardop embedded host interface active (no TCP sockets)");
	return true;
}

// Called every iteration of the modem main loop.  In the TCP build this polled
// the control/data sockets; here it drains the in-process inbound queue,
// applying queued commands and TX data to the core on the modem thread.
void TCPHostPoll(void)
{
	ardop_embed_drain_inbound();
}

// ---- Outbound: command channel -------------------------------------------

void TCPSendCommandToHost(char *strText)
{
	ardop_embed_emit_event(strText);
}

void TCPSendCommandToHostQuiet(char *strText)
{
	ardop_embed_emit_event(strText);
}

void TCPQueueCommandToHost(char *strText)
{
	ardop_embed_emit_event(strText);
}

void TCPSendReplyToHost(char *strText)
{
	ardop_embed_emit_event(strText);
}

// ---- Outbound: received data ---------------------------------------------

void TCPAddTagToDataAndSendToHost(unsigned char *bytData, char *strTag, int Len)
{
	ardop_embed_emit_data(strTag, bytData, Len);
}

// ---- Legacy UDP GUI: unsupported -----------------------------------------

int SendtoGUI(char Type, unsigned char *Msg, int Len)
{
	(void)Type;
	(void)Msg;
	(void)Len;
	return 0;
}
