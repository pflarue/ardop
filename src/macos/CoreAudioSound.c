// macOS CoreAudio stub implementation for initial build scaffolding
// These stubs allow the project to compile/link on macOS. They provide
// minimal behavior and log that functionality is not yet implemented.
// All real functionality will be added in subsequent milestones.

#include <stdbool.h>
#include <string.h>

#include "common/audio.h"
#include "common/log.h"
#include "common/ardopcommon.h"
#include "common/Webgui.h"
#include "common/ptt.h"

// Globals required by audio.h (mirroring ALSA.c declarations)
short txbuffer[2][SendSize];
int TxIndex = 0;

// Track enabled state
bool AudioInit = false;

// Minimal one-time log helper
static void log_stub_once(const char *fn) {
    static bool noted = false;
    if (!noted) {
        ZF_LOGI("macOS audio stub in use – CoreAudio not implemented yet");
        noted = true;
    }
    ZF_LOGD("macOS stub: %s() invoked", fn);
}

void GetDevices() {
    log_stub_once("GetDevices");
    // Provide just NOSOUND so higher layers have a fallback
    FreeDevices(&AudioDevices);
    InitDevices(&AudioDevices);
    int idx = ExtendDevices(&AudioDevices);
    if (idx >= 0) {
        DeviceInfo *dev = AudioDevices[idx];
        dev->name = strdup("NOSOUND");
        dev->desc = strdup("macOS stub device");
        dev->capture = true;
        dev->playback = true;
    }
}

void InitAudio(bool quiet) {
    (void)quiet;
    log_stub_once("InitAudio");
    GetDevices();
    AudioInit = true;
}

bool OpenSoundPlayback(char *devstr, int ch) {
    (void)ch;
    log_stub_once("OpenSoundPlayback");
    if (devstr == NULL || devstr[0] == '\0') {
        CloseSoundPlayback(false);
        return false;
    }
    if (strcmp(devstr, "NOSOUND") == 0 || strcmp(devstr, "-1") == 0) {
        TXEnabled = true;
        strncpy(PlaybackDevice, "NOSOUND", DEVSTRSZ - 1);
        PlaybackDevice[DEVSTRSZ-1] = '\0';
        updateWebGuiAudioConfig(false);
        return true;
    }
    ZF_LOGW("macOS audio stub: requested playback device '%s' not available (using NOSOUND)", devstr);
    return OpenSoundPlayback("NOSOUND", ch);
}

bool OpenSoundCapture(char *devstr, int ch) {
    (void)ch;
    log_stub_once("OpenSoundCapture");
    if (devstr == NULL || devstr[0] == '\0') {
        CloseSoundCapture(false);
        return false;
    }
    if (strcmp(devstr, "NOSOUND") == 0 || strcmp(devstr, "-1") == 0) {
        RXEnabled = true;
        RXSilent = false;
        strncpy(CaptureDevice, "NOSOUND", DEVSTRSZ - 1);
        CaptureDevice[DEVSTRSZ-1] = '\0';
        updateWebGuiAudioConfig(false);
        return true;
    }
    ZF_LOGW("macOS audio stub: requested capture device '%s' not available (using NOSOUND)", devstr);
    return OpenSoundCapture("NOSOUND", ch);
}

void CloseSoundPlayback(bool do_getdevices) {
    log_stub_once("CloseSoundPlayback");
    PlaybackDevice[0] = '\0';
    SoundIsPlaying = false;
    TXEnabled = false;
    KeyPTT(false);
    updateWebGuiAudioConfig(do_getdevices);
}

void CloseSoundCapture(bool do_getdevices) {
    log_stub_once("CloseSoundCapture");
    CaptureDevice[0] = '\0';
    RXEnabled = false;
    updateWebGuiAudioConfig(do_getdevices);
}

bool SendtoCard(int n) {
    (void)n;
    log_stub_once("SendtoCard");
    if (!TXEnabled) return false;
    // Nothing to send in stub
    return true;
}

void PollReceivedSamples() {
    // No audio capture in stub. Intentionally silent.
}

void StopCapture() {
    // Nothing
}

bool SoundFlush() {
    log_stub_once("SoundFlush");
    KeyPTT(false);
    return TXEnabled;
}

bool crestorable() { return false; }
bool prestorable() { return false; }
