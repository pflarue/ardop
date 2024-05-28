/*
The following dummy functions are provided as temporary
subsittutes for capabilities previously provided by
bpq32/pktSession.c, which has been removed.

The provided functionality is no longer supported.  However,
it will take additional effort to cleanly remove all references
to this code.
*/

#include <stddef.h>

int initMode = -1;
int PORTN2 = 0;
int PORTT1 = 0;

#define LOGERROR 3
void WriteDebugLog(int LogLevel, const char * format, ...);

char RequestStr[] =
	"Please notify the developers of ardopcf of the conditions under which"
	" this error message was produced via ardop.groups.io or by creating"
	" an Issue at GitHub.com/pflarue/ardop.\n";

int CheckForPktData(int c) {
	WriteDebugLog(LOGERROR,
		"ERROR: Unexpected call to unsupported function: CheckForPktData()\n%s",
		RequestStr
	);
	return (0);
}

int CheckForPktMon() {
	WriteDebugLog(LOGERROR,
		"ERROR: Unexpected call to unsupported function: CheckForPktMon()\n%s",
		RequestStr
	);
	return (0);
}

void ClosePacketSessions() {
	WriteDebugLog(LOGERROR,
		"ERROR: Unexpected call to unsupported function: ClosePacketSessions()\n%s",
		RequestStr
	);
	return;
}

void ConvertCallstoAX25() {
	WriteDebugLog(LOGERROR,
		"ERROR: Unexpected call to unsupported function: ConvertCallstoAX25()\n%s",
		RequestStr
	);
	return;
}

void L2Routine(unsigned char p, int l, int f, int t, int n, int m) {
	WriteDebugLog(LOGERROR,
		"ERROR: Unexpected call to unsupported function: L2Routine()\n%s",
		RequestStr
	);
	return;
}

unsigned char * PacketSessionPoll(unsigned char * n) {
	WriteDebugLog(LOGERROR,
		"ERROR: Unexpected call to unsupported function: PacketSessionPoll()\n%s",
		RequestStr
	);
	return NULL;
}

void ProcessDEDModeFrame(unsigned char * r, unsigned int l) {
	WriteDebugLog(LOGERROR,
		"ERROR: Unexpected call to unsupported function: ProcessDEDModeFrame()\n%s",
		RequestStr
	);
	return;
}

void ProcessPacketHostBytes(unsigned char *b, int l) {
	WriteDebugLog(LOGERROR,
		"ERROR: Unexpected call to unsupported function: ProcessPacketHostBytes()\n%s",
		RequestStr
	);
	return;
}

int ProcessPktCommand(int c, char * b, int l) {
	WriteDebugLog(LOGERROR,
		"ERROR: Unexpected call to unsupported function: ProcessPktCommand()\n%s",
		RequestStr
	);
	return (0);
}

void ProcessPktData(int c, unsigned char * b, int l) {
	WriteDebugLog(LOGERROR,
		"ERROR: Unexpected call to unsupported function: ProcessPktData()\n%s",
		RequestStr
	);
	return;
}

void ptkSessionBG() {
	WriteDebugLog(LOGERROR,
		"ERROR: Unexpected call to unsupported function: ptkSessionBG()\n%s",
		RequestStr
	);
	return;
}
