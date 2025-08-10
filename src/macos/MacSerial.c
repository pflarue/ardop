// macOS serial (COM) stub implementation
// Provides minimal placeholders so higher layers can link. Real
// implementations will be added in a later milestone.

#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "common/os_util.h"
#include "common/log.h"

HANDLE OpenCOMPort(void *Port, int speed) {
    (void)Port; (void)speed;
    ZF_LOGW("macOS serial stub: OpenCOMPort not implemented");
    return 0;
}

void CloseCOMPort(HANDLE *fd) {
    if (fd && *fd) {
        close(*fd);
        *fd = 0;
    }
}

bool COMSetRTS(HANDLE fd) { (void)fd; ZF_LOGW("macOS serial stub: COMSetRTS"); return false; }
bool COMClearRTS(HANDLE fd) { (void)fd; ZF_LOGW("macOS serial stub: COMClearRTS"); return false; }
bool COMSetDTR(HANDLE fd) { (void)fd; ZF_LOGW("macOS serial stub: COMSetDTR"); return false; }
bool COMClearDTR(HANDLE fd) { (void)fd; ZF_LOGW("macOS serial stub: COMClearDTR"); return false; }

bool WriteCOMBlock(HANDLE fd, unsigned char * Block, int BytesToWrite) {
    (void)fd; (void)Block; (void)BytesToWrite;
    ZF_LOGW("macOS serial stub: WriteCOMBlock");
    return false;
}

int ReadCOMBlock(HANDLE fd, unsigned char * Block, int MaxLength) {
    (void)fd; (void)Block; (void)MaxLength;
    ZF_LOGW("macOS serial stub: ReadCOMBlock");
    return -1;
}

char ** GetSerialStrlist() { return NULL; }
