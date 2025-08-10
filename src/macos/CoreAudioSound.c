// macOS CoreAudio partial implementation.
// Incremental milestones add functionality while preserving buildability
// and avoiding impact to Linux/Windows code paths.
//
// Current scope:
//  - Directional enablement (input-only / output-only / both / none)
//  - Runtime reconfiguration (device/string changes)
//  - Real device enumeration via CoreAudio (GetDevices)
//  - Channel selection logging (mirrors Linux/Windows semantics)
// Audio streaming logic (render/capture callbacks, channel routing) remains
// stubbed and will be added in later milestones.

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#ifdef __APPLE__
#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#endif

#include "common/audio.h"
#include "common/log.h"
#include "common/ardopcommon.h"
#include "common/Webgui.h"
#include "common/ptt.h"

// Globals required by audio.h (mirroring ALSA.c declarations)
short txbuffer[2][SendSize];
int TxIndex = 0;

// Track enabled state
bool AudioInit = false;  // One-time overall audio enumeration done

// Stub CoreAudio state flags (would map to actual AudioUnit configuration)
static bool coreAudioInitialized = false;      // We "created" (stub) an AudioUnit
static bool coreAudioInputActive = false;      // Input bus logically enabled
static bool coreAudioOutputActive = false;     // Output bus logically enabled
// Track last configured device names to detect change
static char lastCaptureDev[DEVSTRSZ] = "";
static char lastPlaybackDev[DEVSTRSZ] = "";
// Track last successfully opened (good) device names for RESTORE feature
static char last_rx_dev[DEVSTRSZ] = "";  // Capture side
static char last_tx_dev[DEVSTRSZ] = "";  // Playback side

// Mutex to guard reconfiguration (minimal thread-safety). If future code
// invokes OpenSound* from different threads this prevents partial teardown.
static pthread_mutex_t coreAudioMutex = PTHREAD_MUTEX_INITIALIZER;

// Helper to decide if a device string represents an enabled direction.
// Enabled if non-null, non-empty, not NOSOUND, not -1.
static bool dev_enabled(const char *dev) {
    return dev != NULL && dev[0] != '\0' && strcmp(dev, "NOSOUND") != 0 && strcmp(dev, "-1") != 0;
}

// Initialize (or reconfigure) the CoreAudio path for the current devices.
// captureDev / playbackDev are the canonical strings (may be "NOSOUND").
// This function is idempotent and safe to call after each open/close.
// For now it only computes direction booleans and logs them; real AudioUnit
// setup will later replace the stub block guarded by comments.
static void InitCoreAudio(const char *captureDev, const char *playbackDev) {
    bool wantInput = dev_enabled(captureDev);
    bool wantOutput = dev_enabled(playbackDev);

    if (!wantInput && !wantOutput) {
        if (coreAudioInitialized) {
            // Future: dispose AudioUnit instance
            coreAudioInitialized = false;
        }
        coreAudioInputActive = false;
        coreAudioOutputActive = false;
        ZF_LOGD("CoreAudio init: inputEnabled=%d outputEnabled=%d (no AudioUnit)", (int)coreAudioInputActive, (int)coreAudioOutputActive);
        return;
    }

    // Stub: mark desired directions active.
    coreAudioInputActive = wantInput;
    coreAudioOutputActive = wantOutput;
    coreAudioInitialized = true;
    ZF_LOGD("CoreAudio init: inputEnabled=%d outputEnabled=%d", (int)coreAudioInputActive, (int)coreAudioOutputActive);
}

// Determine if CoreAudio (stub) needs to be re-created due to changes in
// device strings or enabled directions. Performs teardown and InitCoreAudio
// in required order while holding a mutex. Logs one INFO line plus a DEBUG
// transition summary when a reinit occurs.
static void ReinitCoreAudioIfNeeded(void) {
    pthread_mutex_lock(&coreAudioMutex);
    bool wantInput = dev_enabled(CaptureDevice);
    bool wantOutput = dev_enabled(PlaybackDevice);
    bool deviceChanged = false;
    if (wantInput) {
        if (strncmp(CaptureDevice, lastCaptureDev, DEVSTRSZ - 1) != 0)
            deviceChanged = true;
    } else if (lastCaptureDev[0] != '\0') {
        // Was previously active, now disabled
        deviceChanged = true;
    }
    if (wantOutput) {
        if (strncmp(PlaybackDevice, lastPlaybackDev, DEVSTRSZ - 1) != 0)
            deviceChanged = true;
    } else if (lastPlaybackDev[0] != '\0') {
        deviceChanged = true;
    }
    bool flagsChanged = (wantInput != coreAudioInputActive) || (wantOutput != coreAudioOutputActive) || (wantInput == false && coreAudioInputActive) || (wantOutput == false && coreAudioOutputActive);

    if (!deviceChanged && !flagsChanged) {
        pthread_mutex_unlock(&coreAudioMutex);
        return;  // No change
    }

    // Log transition summary (old->new) at DEBUG
    ZF_LOGD("CoreAudio reinit detail: inEnabled %d->%d outEnabled %d->%d capDev '%s'->'%s' playDev '%s'->'%s'",
        (int)coreAudioInputActive, (int)wantInput,
        (int)coreAudioOutputActive, (int)wantOutput,
        lastCaptureDev, CaptureDevice,
        lastPlaybackDev, PlaybackDevice);

    // Teardown sequence (stop -> uninit -> dispose) stubbed
    if (coreAudioInitialized) {
        // Maintain documented order even while stubbed so future real
        // implementation plugs in here without risking order regressions.
        // 1) Stop
        // 2) Uninitialize
        // 3) Dispose
        // (All three are no-ops in current stub.)
        coreAudioInitialized = false;
        coreAudioInputActive = false;
        coreAudioOutputActive = false;
    }

    // Recreate with new configuration
    InitCoreAudio(CaptureDevice, PlaybackDevice);

    // Update last known device names (store empty if disabled)
    if (wantInput)
        snprintf(lastCaptureDev, DEVSTRSZ, "%s", CaptureDevice);
    else
        lastCaptureDev[0] = '\0';
    if (wantOutput)
        snprintf(lastPlaybackDev, DEVSTRSZ, "%s", PlaybackDevice);
    else
        lastPlaybackDev[0] = '\0';

    ZF_LOGI("CoreAudio reinit complete (in=%d out=%d)", (int)coreAudioInputActive, (int)coreAudioOutputActive);
    pthread_mutex_unlock(&coreAudioMutex);
}

// Minimal one-time log helper
static void log_stub_once(void) {
    static bool noted = false;
    if (!noted) {
        // One-time notice that full CoreAudio streaming is not yet implemented.
        ZF_LOGW("macOS CoreAudio streaming not implemented yet (using stub path)");
        noted = true;
    }
}

void GetDevices() {
    FreeDevices(&AudioDevices);
    InitDevices(&AudioDevices);
    // Test hook: when ARDOP_TEST_SKIP_CA_ENUM is set (used by unit tests),
    // skip real CoreAudio enumeration to avoid dependency on host devices
    // and potential sandbox / CI issues. Only NOSOUND sentinel will be added.
    if (getenv("ARDOP_TEST_SKIP_CA_ENUM") != NULL) {
        ZF_LOGD("Skipping CoreAudio enumeration due to ARDOP_TEST_SKIP_CA_ENUM");
        goto add_nosound_only;
    }
#ifdef __APPLE__
    AudioDeviceID defIn = kAudioObjectUnknown;
    AudioDeviceID defOut = kAudioObjectUnknown;
    UInt32 size = sizeof(AudioDeviceID);
    AudioObjectPropertyAddress addrDefIn = { kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioObjectPropertyAddress addrDefOut = { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addrDefIn, 0, NULL, &size, &defIn) != noErr)
        defIn = kAudioObjectUnknown;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addrDefOut, 0, NULL, &size, &defOut) != noErr)
        defOut = kAudioObjectUnknown;
    AudioObjectPropertyAddress listAddr = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    size = 0;
    // First query the size of the device list. The previous call used
    // AudioObjectGetPropertyData with only 5 arguments which is invalid;
    // AudioObjectGetPropertyDataSize is the correct API to get the buffer size.
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &listAddr, 0, NULL, &size) != noErr || size == 0) {
        ZF_LOGW("CoreAudio: no devices returned; adding NOSOUND only");
    } else {
        UInt32 count = (UInt32)(size / sizeof(AudioDeviceID));
        AudioDeviceID *ids = (AudioDeviceID *) malloc(size);
        if (ids && AudioObjectGetPropertyData(kAudioObjectSystemObject, &listAddr, 0, NULL, &size, ids) == noErr) {
            // First pass: collect raw info so we can resolve duplicate names.
            typedef struct TmpDevInfo { AudioDeviceID did; bool hasIn; bool hasOut; char name[DEVSTRSZ]; char uid[DEVSTRSZ]; bool defIn; bool defOut; } TmpDevInfo;
            TmpDevInfo *tmp = calloc(count, sizeof(TmpDevInfo));
            UInt32 actual = 0;
            for (UInt32 i = 0; i < count; ++i) {
                AudioDeviceID did = ids[i];
                bool hasInput = false, hasOutput = false;
                AudioObjectPropertyAddress scAddrIn = { kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeInput, kAudioObjectPropertyElementMain };
                AudioObjectPropertyAddress scAddrOut = { kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMain };
                UInt32 scSize = 0;
                if (AudioObjectGetPropertyDataSize(did, &scAddrIn, 0, NULL, &scSize) == noErr && scSize > 0) {
                    AudioBufferList *abl = (AudioBufferList *) malloc(scSize);
                    if (abl && AudioObjectGetPropertyData(did, &scAddrIn, 0, NULL, &scSize, abl) == noErr) {
                        for (UInt32 b = 0; b < abl->mNumberBuffers; ++b)
                            if (abl->mBuffers[b].mNumberChannels > 0) { hasInput = true; break; }
                    }
                    if (abl) free(abl);
                }
                if (AudioObjectGetPropertyDataSize(did, &scAddrOut, 0, NULL, &scSize) == noErr && scSize > 0) {
                    AudioBufferList *abl = (AudioBufferList *) malloc(scSize);
                    if (abl && AudioObjectGetPropertyData(did, &scAddrOut, 0, NULL, &scSize, abl) == noErr) {
                        for (UInt32 b = 0; b < abl->mNumberBuffers; ++b)
                            if (abl->mBuffers[b].mNumberChannels > 0) { hasOutput = true; break; }
                    }
                    if (abl) free(abl);
                }
                if (!(hasInput || hasOutput))
                    continue;
                CFStringRef cfName = NULL; size = sizeof(CFStringRef);
                AudioObjectPropertyAddress nameAddr = { kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
                if (AudioObjectGetPropertyData(did, &nameAddr, 0, NULL, &size, &cfName) != noErr)
                    cfName = NULL;
                char nameBuf[DEVSTRSZ]; nameBuf[0] = '\0';
                if (cfName) { CFStringGetCString(cfName, nameBuf, sizeof(nameBuf), kCFStringEncodingUTF8); CFRelease(cfName);} else { snprintf(nameBuf, sizeof(nameBuf), "Device%u", (unsigned)i); }
                CFStringRef cfUID = NULL; size = sizeof(CFStringRef);
                AudioObjectPropertyAddress uidAddr = { kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
                if (AudioObjectGetPropertyData(did, &uidAddr, 0, NULL, &size, &cfUID) != noErr)
                    cfUID = NULL;
                char uidBuf[DEVSTRSZ]; uidBuf[0] = '\0';
                if (cfUID) { CFStringGetCString(cfUID, uidBuf, sizeof(uidBuf), kCFStringEncodingUTF8); CFRelease(cfUID);} else { snprintf(uidBuf, sizeof(uidBuf), "%s", nameBuf); }
                // Store into temp array for second pass duplicate resolution.
                strncpy(tmp[actual].name, nameBuf, DEVSTRSZ-1);
                strncpy(tmp[actual].uid, uidBuf, DEVSTRSZ-1);
                tmp[actual].hasIn = hasInput; tmp[actual].hasOut = hasOutput;
                tmp[actual].did = did;
                tmp[actual].defIn = (did == defIn); tmp[actual].defOut = (did == defOut);
                actual++;
            }
            // Second pass: detect duplicates; build final list.
            for (UInt32 i = 0; i < actual; ++i) {
                const char *baseName = tmp[i].name[0] ? tmp[i].name : tmp[i].uid;
                bool duplicate = false;
                for (UInt32 j = 0; j < actual; ++j) {
                    if (j == i) continue;
                    if (strcmp(baseName, tmp[j].name[0]?tmp[j].name:tmp[j].uid) == 0) { duplicate = true; break; }
                }
                char finalName[DEVSTRSZ]; finalName[0] = '\0';
                if (!duplicate) {
                    snprintf(finalName, sizeof(finalName), "%s", baseName);
                } else {
                    // Use last 6 chars of UID (or full if shorter) to disambiguate.
                    size_t ulen = strlen(tmp[i].uid);
                    const char *suffix = tmp[i].uid;
                    if (ulen > 6) suffix = tmp[i].uid + (ulen - 6);
                    snprintf(finalName, sizeof(finalName), "%s-%s", baseName, suffix);
                }
                char descBuf[DEVSTRSZ * 2];
                char defStr[32] = "";
                if (tmp[i].defIn && tmp[i].defOut) snprintf(defStr, sizeof(defStr), " (default in/out)");
                else if (tmp[i].defIn) snprintf(defStr, sizeof(defStr), " (default in)");
                else if (tmp[i].defOut) snprintf(defStr, sizeof(defStr), " (default out)");
                snprintf(descBuf, sizeof(descBuf), "%s%s", finalName, defStr);
                int dindex = ExtendDevices(&AudioDevices);
                DeviceInfo *dev = AudioDevices[dindex];
                dev->name = strndup(finalName, DEVSTRSZ - 1);
                // Alias is the full stable UID; if name already equals UID we skip alias.
                if (strcmp(finalName, tmp[i].uid) != 0)
                    dev->alias = strndup(tmp[i].uid, DEVSTRSZ - 1);
                dev->desc = strndup(descBuf, DEVSTRSZ * 2 - 1);
                dev->capture = tmp[i].hasIn;
                dev->playback = tmp[i].hasOut;
            }
            if (tmp) { free(tmp); tmp = NULL; }
        }
        if (ids) { free(ids); ids = NULL; }
    }
#else
    log_stub_once();
#endif
add_nosound_only:
    int idx = ExtendDevices(&AudioDevices);
    if (idx >= 0) {
        DeviceInfo *dev = AudioDevices[idx];
        dev->name = strdup("NOSOUND");
        dev->desc = strdup("A dummy audio device for diagnostic use.");
        dev->capture = true;
        dev->playback = true;
    }
}

void InitAudio(bool quiet) {
    (void)quiet;
    log_stub_once();
    GetDevices();
    AudioInit = true;
    if (ZF_LOG_ON_DEBUG) {
        LogDevices(AudioDevices, "macOS audio devices", false, false);
    }
    // No CoreAudio initialization yet; occurs lazily in OpenSound* based on devices.
}

bool OpenSoundPlayback(char *devstr, int ch) {
    log_stub_once();
    if (devstr == NULL || devstr[0] == '\0') {
        CloseSoundPlayback(false);
        return false;
    }
    if (strcmp(devstr, "RESTORE") == 0) {
        if (last_tx_dev[0] == '\0') {
            ZF_LOGW("RESTORE requested for playback but no previous device to restore");
            return false;
        }
        ZF_LOGI("RESTORE playback -> '%s'", last_tx_dev);
        devstr = last_tx_dev;
    }
    // Special handling for NOSOUND: treat as a logical disable request for TX.
    // This differs from other arbitrary strings (real devices or placeholders)
    // which enable TX. We intentionally do NOT update last_tx_dev so that a
    // subsequent RESTORE returns to the prior real/open device. This mirrors
    // Linux/Windows behavior where selecting the sentinel NOSOUND disables
    // transmission without losing the previous selection.
    if (strcmp(devstr, "NOSOUND") == 0) {
        TXEnabled = false;
        strncpy(PlaybackDevice, devstr, DEVSTRSZ - 1);
        PlaybackDevice[DEVSTRSZ-1] = '\0';
        Pch = ch;
        ZF_LOGI("Playback NOSOUND sentinel selected (TX disabled)");
        ReinitCoreAudioIfNeeded();
        updateWebGuiAudioConfig(false);
        return true; // Success (no audio active by design)
    }
    // Accept any non-empty string (stub). Real implementation will validate.
    TXEnabled = true;
    strncpy(PlaybackDevice, devstr, DEVSTRSZ - 1);
    PlaybackDevice[DEVSTRSZ-1] = '\0';
    Pch = ch;
    ZF_LOGI("Playback device '%s' opening (channels=%d)", PlaybackDevice, Pch);
    if (ZF_LOG_ON_DEBUG) {
        if (Pch == 1)
            ZF_LOGD("Playback device '%s' opened mono (channel=%s)", PlaybackDevice, UseLeftTX ? "Left(default)" : (UseRightTX ? "Right" : "Left(default)"));
        else if (Pch == 2)
            ZF_LOGD("Playback device '%s' opened stereo (Left=%d Right=%d)", PlaybackDevice, UseLeftTX, UseRightTX);
        else
            ZF_LOGD("Playback device '%s' opened with unexpected channel count %d", PlaybackDevice, Pch);
    }
    // Recompute CoreAudio state with updated playback device.
    ReinitCoreAudioIfNeeded();
    updateWebGuiAudioConfig(false);
    // Record as restorable only after successful open/init
    snprintf(last_tx_dev, DEVSTRSZ, "%s", PlaybackDevice);
    return true;
}

bool OpenSoundCapture(char *devstr, int ch) {
    log_stub_once();
    if (devstr == NULL || devstr[0] == '\0') {
        CloseSoundCapture(false);
        return false;
    }
    if (strcmp(devstr, "RESTORE") == 0) {
        if (last_rx_dev[0] == '\0') {
            ZF_LOGW("RESTORE requested for capture but no previous device to restore");
            return false;
        }
        ZF_LOGI("RESTORE capture -> '%s'", last_rx_dev);
        devstr = last_rx_dev;
    }
    // NOSOUND sentinel disables RX path while keeping the prior restorable
    // device (last_rx_dev) intact for a future RESTORE. This matches the
    // cross-platform semantics of selecting NOSOUND as a diagnostic/null
    // device.
    if (strcmp(devstr, "NOSOUND") == 0) {
        RXEnabled = false;
        RXSilent = true;
        strncpy(CaptureDevice, devstr, DEVSTRSZ - 1);
        CaptureDevice[DEVSTRSZ-1] = '\0';
        Cch = ch;
        ZF_LOGI("Capture NOSOUND sentinel selected (RX disabled)");
        ReinitCoreAudioIfNeeded();
        updateWebGuiAudioConfig(false);
        return true;
    }
    // Accept any non-empty string (stub). Real implementation will validate.
    RXEnabled = true;
    RXSilent = false;
    strncpy(CaptureDevice, devstr, DEVSTRSZ - 1);
    CaptureDevice[DEVSTRSZ-1] = '\0';
    Cch = ch;
    ZF_LOGI("Capture device '%s' opening (channels=%d)", CaptureDevice, Cch);
    if (ZF_LOG_ON_DEBUG) {
        if (Cch == 1)
            ZF_LOGD("Capture device '%s' opened mono (channel=%s)", CaptureDevice, UseLeftRX ? "Left(default)" : (UseRightRX ? "Right" : "Left(default)"));
        else if (Cch == 2)
            ZF_LOGD("Capture device '%s' opened stereo (Left=%d Right=%d)", CaptureDevice, UseLeftRX, UseRightRX);
        else
            ZF_LOGD("Capture device '%s' opened with unexpected channel count %d", CaptureDevice, Cch);
    }
    ReinitCoreAudioIfNeeded();
    updateWebGuiAudioConfig(false);
    snprintf(last_rx_dev, DEVSTRSZ, "%s", CaptureDevice);
    return true;
}

void CloseSoundPlayback(bool do_getdevices) {
    log_stub_once();
    char prev[DEVSTRSZ];
    snprintf(prev, sizeof(prev), "%s", PlaybackDevice);
    PlaybackDevice[0] = '\0';
    SoundIsPlaying = false;
    TXEnabled = false;
    KeyPTT(false);
    ReinitCoreAudioIfNeeded();  // Re-evaluate (may tear down AudioUnit if last direction)
    updateWebGuiAudioConfig(do_getdevices);
    if (prev[0]) ZF_LOGI("Playback device '%s' closed", prev);
}

void CloseSoundCapture(bool do_getdevices) {
    log_stub_once();
    char prev[DEVSTRSZ];
    snprintf(prev, sizeof(prev), "%s", CaptureDevice);
    CaptureDevice[0] = '\0';
    RXEnabled = false;
    ReinitCoreAudioIfNeeded();  // Re-evaluate
    updateWebGuiAudioConfig(do_getdevices);
    if (prev[0]) ZF_LOGI("Capture device '%s' closed", prev);
}

bool SendtoCard(int n) {
    (void)n;
    log_stub_once();
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
    log_stub_once();
    KeyPTT(false);
    return TXEnabled;
}

bool crestorable() { return last_rx_dev[0] != '\0'; }
bool prestorable() { return last_tx_dev[0] != '\0'; }
