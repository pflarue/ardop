// macOS CM108 (USB HID) PTT support.
// Implements OpenCM108/CM108_set_ptt/CloseCM108/GetCM108Strlist mirroring
// Linux/Windows semantics while confined to src/macos/.
// Uses IOKit HID Manager. We map IOHIDDeviceRef to small integer HANDLEs.
// HANDLE is typedef int (see os_util.h) so we maintain an internal table.

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "common/os_util.h"
#include "common/log.h"

// Known CM108 values (mirror Windows list)
static const int CM108VID = 0x0D8C;
static const int CM108PIDS[] = {0x0008,0x0009,0x000A,0x000B,0x000C,0x000D,0x000E,0x000F,0x0012,0x0013,0x0139,0x013A,0x013C};
static const int AIOCVID = 0x1209; // AIOC
static const int AIOCPID = 0x7388;

#define MAX_HID_HANDLES 16
typedef struct {
    int in_use;
    int h; // exported integer handle
    IOHIDDeviceRef dev;
    int vid;
    int pid;
} hid_slot_t;

// Slot table lifetime: static for process duration. Each successful OpenCM108
// assigns a slot; CloseCM108 releases (IOHIDDeviceClose + CFRelease) and resets.
// No global teardown needed beyond individual Close calls. Enumerations use
// open_first() which returns a retained IOHIDDeviceRef that is always
// CFRelease()'d in the same loop so no leak.
static hid_slot_t hid_slots[MAX_HID_HANDLES];
static int next_handle_base = 1000;

static hid_slot_t * alloc_slot(void) {
    for (int i=0;i<MAX_HID_HANDLES;i++) {
        if (!hid_slots[i].in_use) {
            hid_slots[i].in_use = 1;
            hid_slots[i].h = next_handle_base++;
            return &hid_slots[i];
        }
    }
    return NULL;
}
static hid_slot_t * find_slot(int h) {
    for (int i=0;i<MAX_HID_HANDLES;i++) if (hid_slots[i].in_use && hid_slots[i].h == h) return &hid_slots[i];
    return NULL;
}
static void free_slot(hid_slot_t *s) {
    if (!s) return; s->in_use = 0; s->dev = NULL; s->vid = s->pid = 0; }

static bool pid_in_known(int pid) {
    for (unsigned i=0;i<sizeof(CM108PIDS)/sizeof(CM108PIDS[0]);++i) if (CM108PIDS[i]==pid) return true; return false;
}

// Common report (5 bytes) used for PTT toggle; matches Linux/Windows logic.
static int send_ptt(IOHIDDeviceRef dev, bool State) {
    unsigned char buf[5] = {0,0, (unsigned char)(State ? (1 << (3-1)) : 0), (1 << (3-1)), 0};
    IOReturn r = IOHIDDeviceSetReport(dev, kIOHIDReportTypeOutput, 0, buf, sizeof(buf));
    if (r != kIOReturnSuccess) {
        ZF_LOGE("CM108 PTT report send failed (0x%08x)", r);
        return -1;
    }
    return 0;
}

// Build an IOHIDManager matching dict for a single VID/PID
static CFMutableDictionaryRef make_matching_dict(int vid, int pid) {
    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!dict) return NULL;
    CFNumberRef vidNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid);
    CFNumberRef pidNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid);
    if (vidNum && pidNum) {
        CFDictionarySetValue(dict, CFSTR(kIOHIDVendorIDKey), vidNum);
        CFDictionarySetValue(dict, CFSTR(kIOHIDProductIDKey), pidNum);
    }
    if (vidNum) CFRelease(vidNum);
    if (pidNum) CFRelease(pidNum);
    return dict;
}

static IOHIDDeviceRef open_first(int vid, int pid) {
    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!mgr) return NULL;
    CFMutableDictionaryRef match = make_matching_dict(vid, pid);
    if (!match) { CFRelease(mgr); return NULL; }
    IOHIDManagerSetDeviceMatching(mgr, match);
    CFRelease(match);
    if (IOHIDManagerOpen(mgr, kIOHIDOptionsTypeNone) != kIOReturnSuccess) { CFRelease(mgr); return NULL; }
    CFSetRef devs = IOHIDManagerCopyDevices(mgr);
    if (!devs) { CFRelease(mgr); return NULL; }
    IOHIDDeviceRef found = NULL;
    CFIndex n = CFSetGetCount(devs);
    if (n > 0) {
        IOHIDDeviceRef *arr = (IOHIDDeviceRef*)calloc((size_t)n, sizeof(IOHIDDeviceRef));
        if (arr) {
            CFSetGetValues(devs, (const void**)arr);
            // Just take first
            found = (IOHIDDeviceRef)CFRetain(arr[0]);
            free(arr);
        }
    }
    CFRelease(devs);
    CFRelease(mgr);
    if (found && IOHIDDeviceOpen(found, kIOHIDOptionsTypeNone) != kIOReturnSuccess) { CFRelease(found); return NULL; }
    return found;
}

// Parse VID:PID string (hex). Returns true on success.
static bool parse_vidpid(char *devstr, int *vid, int *pid) {
    char *next; *vid = (int)strtol(devstr, &next, 16); if (next==devstr || *next!=':') return false; char *p = next+1; *pid = (int)strtol(p, &next, 16); if (next==p) return false; return true;
}

HANDLE OpenCM108(char *devstr) {
    if (!devstr || !devstr[0]) { ZF_LOGE("CM108 open: empty devstr"); return 0; }
    // Accept forms: VID:PID (hex). Future: ? / ?? enumeration similar to Windows.
    int vid=0,pid=0;
    if (!parse_vidpid(devstr, &vid, &pid)) {
        ZF_LOGE("CM108 open: unable to parse VID:PID in '%s'", devstr);
        return 0;
    }
    IOHIDDeviceRef dev = open_first(vid, pid);
    if (!dev) { ZF_LOGE("CM108 open: no matching device for %04X:%04X", vid, pid); return 0; }
    hid_slot_t *s = alloc_slot();
    if (!s) { ZF_LOGE("CM108 open: no free handle slots"); CFRelease(dev); return 0; }
    s->dev = dev; s->vid = vid; s->pid = pid; HANDLE h = s->h;
    ZF_LOGI("CM108 device %04X:%04X opened (handle=%d)", vid, pid, h);
    return h;
}

int CM108_set_ptt(HANDLE fd, bool State) {
    hid_slot_t *s = find_slot(fd); if (!s) { ZF_LOGE("CM108_set_ptt: invalid handle %d", fd); return -1; }
    if (send_ptt(s->dev, State) != 0) return -1;
    ZF_LOGD("CM108 handle %d PTT %s", fd, State?"ON":"OFF");
    return 0;
}

void CloseCM108(HANDLE *fd) {
    if (!fd || !*fd) return; hid_slot_t *s = find_slot(*fd); if (!s) { *fd = 0; return; }
    ZF_LOGI("CM108 device handle %d closing", *fd);
    if (s->dev) { IOHIDDeviceClose(s->dev, kIOHIDOptionsTypeNone); CFRelease(s->dev); }
    free_slot(s); *fd = 0;
}

char** GetCM108Strlist() {
    // Enumerate known CM108 compatible (VID=0x0D8C + known PIDs) and AIOC.
    // Return CM108:VID:PID entries with empty description (placeholder).
    char **slist = NULL; int slistsize = 0;
    int vids[2] = {CM108VID, AIOCVID};
    for (int vi=0; vi<2; ++vi) {
        int vid = vids[vi];
        const int *pids; size_t pidcount;
        int singlePID = 0;
        if (vid == CM108VID) { pids = CM108PIDS; pidcount = sizeof(CM108PIDS)/sizeof(CM108PIDS[0]); }
        else { singlePID = AIOCPID; pids = &singlePID; pidcount = 1; }
        for (size_t pi=0; pi<pidcount; ++pi) {
            IOHIDDeviceRef dev = open_first(vid, pids[pi]);
            if (!dev) continue; // not present
            // Present: add to list
            if (slist == NULL) { slistsize = 1; slist = (char**)malloc(sizeof(char*)); if (!slist) { CFRelease(dev); return NULL; } slist[0] = NULL; }
            slistsize += 2; char **tmp = (char**)realloc(slist, slistsize * sizeof(char*)); if (!tmp) { CFRelease(dev); return slist; } slist = tmp;
            char namebuf[32]; snprintf(namebuf, sizeof(namebuf), "CM108:%04X:%04X", vid, pids[pi]);
            slist[slistsize-3] = strdup(namebuf);
            slist[slistsize-2] = strdup("");
            slist[slistsize-1] = NULL;
            CFRelease(dev); // Only enumerating; real open duplicates later
        }
    }
    if (slist) {
        int pairs=0; for (int i=0; slist[i]; i+=2) pairs++;
        ZF_LOGD("CM108 enumeration: %d device(s)", pairs);
        for (int i=0; slist[i]; i+=2) ZF_LOGD("  %s", slist[i]);
    } else {
        ZF_LOGD("CM108 enumeration: none found");
    }
    return slist;
}
