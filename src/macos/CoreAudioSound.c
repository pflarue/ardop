#include <stdio.h>
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
#include <unistd.h>
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

// Ring buffer for audio output (large enough for several seconds of audio)
#define RINGBUF_SIZE (SendSize * 256) // 256*1200 = 307200 samples ~7s at 44.1kHz
static short ringbuf[RINGBUF_SIZE];
static volatile int ringbuf_write = 0; // Next write position
static volatile int ringbuf_read = 0;  // Next read position
static volatile int ringbuf_count = 0; // Number of samples in buffer

// Track enabled state
bool AudioInit = false;  // One-time overall audio enumeration done

// CoreAudio state - real AudioUnit instances and configuration
static bool coreAudioInitialized = false;      // AudioUnit created and configured
static bool coreAudioInputActive = false;      // Input bus enabled and running
static bool coreAudioOutputActive = false;     // Output bus enabled and running
static AudioUnit audioUnit = NULL;             // The actual AudioUnit instance
// Track last configured device names to detect change
static char lastCaptureDev[DEVSTRSZ] = "";
static char lastPlaybackDev[DEVSTRSZ] = "";
// Track last successfully opened (good) device names for RESTORE feature
static char last_rx_dev[DEVSTRSZ] = "";  // Capture side
static char last_tx_dev[DEVSTRSZ] = "";  // Playback side

// Mutex to guard reconfiguration (minimal thread-safety). If future code
// invokes OpenSound* from different threads this prevents partial teardown.
static pthread_mutex_t coreAudioMutex = PTHREAD_MUTEX_INITIALIZER;

// Audio buffer state for transmission
static volatile int txReadIndex = 0;           // Read position in txbuffer
static volatile bool audioPlaying = false;    // AudioUnit is actively playing
static volatile bool audioFinished = false;   // All audio data has been consumed
static volatile float srcPosition = 0.0f;     // Sample rate conversion position

// Helper to decide if a device string represents an enabled direction.
// Enabled if non-null, non-empty, not NOSOUND, not -1.
static bool dev_enabled(const char *dev) {
    return dev != NULL && dev[0] != '\0' && strcmp(dev, "NOSOUND") != 0 && strcmp(dev, "-1") != 0;
}

// CoreAudio render callback for audio output
// This function is called by CoreAudio when it needs audio samples to play
static OSStatus renderCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
                              const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
                              UInt32 inNumberFrames, AudioBufferList *ioData) {
    (void)inRefCon; (void)ioActionFlags; (void)inTimeStamp; (void)inBusNumber;
    
    // (Debug logging removed for normal operation)
    if (!audioPlaying || !TXEnabled || ioData->mNumberBuffers == 0) {
        // Fill with silence
        for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
            memset(ioData->mBuffers[i].mData, 0, ioData->mBuffers[i].mDataByteSize);
        }
        return noErr;
    }
    // Get output buffer (assume first buffer, mono or stereo)
    float *outputBuffer = (float*)ioData->mBuffers[0].mData;
    UInt32 channelsPerFrame = ioData->mBuffers[0].mNumberChannels;
    
    // Convert samples from txbuffer (16-bit signed) to float and copy to output
    // Handle sample rate conversion from 44.1kHz (ARDOP) to 48kHz (device)
    int samplesRead = 0;
    int nonzero = 0;
    // Sample rate conversion state (static to persist across callbacks)
    static double srcPos = 0.0; // Position in source (ring buffer) in 12kHz samples
    const double srcRate = 12000.0;
    const double dstRate = 48000.0;
    const double rateRatio = srcRate / dstRate; // 0.25
    static float srcRatio = 12000.0f / 48000.0f;  // 0.25 - need to read slower
    
    // Track total samples played across all SendtoCard() calls
    static int totalSamplesQueued = 0;
    static int totalSamplesPlayed = 0;
    // On first callback after playback starts, compute totalSamplesQueued
    // (callbackCount logic removed with debug logging)

    // Calculate how many samples are actually queued (sum of both buffers)
    // This is a workaround: in a real implementation, track this in SendtoCard()
    int buffer0_nonzero = 0, buffer1_nonzero = 0;
    for (int i = 0; i < SendSize; i++) {
        if (txbuffer[0][i] != 0) buffer0_nonzero++;
        if (txbuffer[1][i] != 0) buffer1_nonzero++;
    }
    totalSamplesQueued = buffer0_nonzero + buffer1_nonzero;

    for (UInt32 frame = 0; frame < inNumberFrames; frame++) {
        // Linear interpolation SRC: for each output frame, compute position in input (ring buffer)
        int srcIndex0 = (int)srcPos;
        int srcIndex1 = srcIndex0 + 1;
        double frac = srcPos - srcIndex0;
        short s0 = 0, s1 = 0;
        if (ringbuf_count > srcIndex1) {
            int idx0 = (ringbuf_read + srcIndex0) % RINGBUF_SIZE;
            int idx1 = (ringbuf_read + srcIndex1) % RINGBUF_SIZE;
            s0 = ringbuf[idx0];
            s1 = ringbuf[idx1];
            samplesRead++;
        } else if (ringbuf_count > srcIndex0) {
            int idx0 = (ringbuf_read + srcIndex0) % RINGBUF_SIZE;
            s0 = ringbuf[idx0];
            s1 = s0;
            samplesRead++;
        } else {
            // Buffer underrun: output silence
            if (audioPlaying && !audioFinished) {
                audioFinished = true;
                audioPlaying = false;
                ZF_LOGI("RenderCallback: All audio played (ring buffer empty), signaling audioFinished");
            }
            s0 = 0;
            s1 = 0;
        }
        short sample = (short)((1.0 - frac) * s0 + frac * s1);
        if (sample != 0) nonzero++;
        float floatSample = sample / 32768.0f;
        for (UInt32 ch = 0; ch < channelsPerFrame; ch++) {
            outputBuffer[frame * channelsPerFrame + ch] = floatSample;
        }
        srcPos += rateRatio;
        // When enough output frames have been produced to consume a source sample, advance ringbuf_read
        while (srcPos >= 1.0 && ringbuf_count > 0) {
            ringbuf_read = (ringbuf_read + 1) % RINGBUF_SIZE;
            ringbuf_count--;
            srcPos -= 1.0;
        }
    }
    // (Debug logging removed for normal operation)
    return noErr;
}

// Initialize (or reconfigure) the CoreAudio path for the current devices.
// captureDev / playbackDev are the canonical strings (may be "NOSOUND").
// This function is idempotent and safe to call after each open/close.
// Creates real AudioUnit instances and configures them for audio I/O.
static void InitCoreAudio(const char *captureDev, const char *playbackDev) {
    bool wantInput = dev_enabled(captureDev);
    bool wantOutput = dev_enabled(playbackDev);
    
    ZF_LOGI("InitCoreAudio called: captureDev='%s', playbackDev='%s', wantInput=%d, wantOutput=%d", 
            captureDev ? captureDev : "NULL", playbackDev ? playbackDev : "NULL", wantInput, wantOutput);

    if (!wantInput && !wantOutput) {
        if (coreAudioInitialized && audioUnit) {
            AudioUnitUninitialize(audioUnit);
            AudioComponentInstanceDispose(audioUnit);
            audioUnit = NULL;
            coreAudioInitialized = false;
            ZF_LOGI("CoreAudio: Disposed AudioUnit due to no enabled devices");
        }
        coreAudioInputActive = false;
        coreAudioOutputActive = false;
        ZF_LOGD("CoreAudio init: inputEnabled=%d outputEnabled=%d (no AudioUnit)", (int)coreAudioInputActive, (int)coreAudioOutputActive);
        return;
    }

#ifdef __APPLE__
    OSStatus status = noErr;
    
    // Dispose existing AudioUnit if we have one
    if (coreAudioInitialized && audioUnit) {
        AudioUnitUninitialize(audioUnit);
        AudioComponentInstanceDispose(audioUnit);
        audioUnit = NULL;
        coreAudioInitialized = false;
    }

    // Create AudioUnit (using HAL output unit to support specific devices)
    AudioComponentDescription desc = {0};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_HALOutput;  // Use HAL instead of DefaultOutput
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    
    AudioComponent component = AudioComponentFindNext(NULL, &desc);
    if (!component) {
        ZF_LOGE("CoreAudio: Failed to find HAL output component");
        return;
    }
    
    status = AudioComponentInstanceNew(component, &audioUnit);
    if (status != noErr || !audioUnit) {
        ZF_LOGE("CoreAudio: Failed to create AudioUnit instance (status=%d)", (int)status);
        return;
    }

    // Enable output on the HAL unit (required for HAL units)
    UInt32 enableOutput = 1;
    status = AudioUnitSetProperty(audioUnit, kAudioOutputUnitProperty_EnableIO,
                                kAudioUnitScope_Output, 0, &enableOutput, sizeof(enableOutput));
    if (status != noErr) {
        ZF_LOGE("CoreAudio: Failed to enable output (status=%d)", (int)status);
        AudioComponentInstanceDispose(audioUnit);
        audioUnit = NULL;
        return;
    } else {
        ZF_LOGI("CoreAudio: Output enabled on HAL unit");
    }

    // Disable input on the HAL unit for now (we're only doing output)
    UInt32 enableInput = 0;
    status = AudioUnitSetProperty(audioUnit, kAudioOutputUnitProperty_EnableIO,
                                kAudioUnitScope_Input, 1, &enableInput, sizeof(enableInput));
    if (status != noErr) {
        ZF_LOGW("CoreAudio: Failed to disable input (status=%d)", (int)status);
    } else {
        ZF_LOGI("CoreAudio: Input disabled on HAL unit");
    }

    // Configure specific output device if we have one
    AudioDeviceID targetDeviceID = kAudioObjectUnknown;
    if (wantOutput && playbackDev && playbackDev[0] != '\0') {
        
        // Find the device ID for our playback device name
        UInt32 propSize = 0;
        AudioObjectPropertyAddress listAddr = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &listAddr, 0, NULL, &propSize) == noErr && propSize > 0) {
            UInt32 deviceCount = propSize / sizeof(AudioDeviceID);
            AudioDeviceID *deviceList = (AudioDeviceID *)malloc(propSize);
            if (deviceList && AudioObjectGetPropertyData(kAudioObjectSystemObject, &listAddr, 0, NULL, &propSize, deviceList) == noErr) {
                for (UInt32 i = 0; i < deviceCount; i++) {
                    CFStringRef deviceName = NULL;
                    UInt32 size = sizeof(CFStringRef);
                    AudioObjectPropertyAddress nameAddr = { kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
                    if (AudioObjectGetPropertyData(deviceList[i], &nameAddr, 0, NULL, &size, &deviceName) == noErr && deviceName) {
                        char nameBuffer[256];
                        if (CFStringGetCString(deviceName, nameBuffer, sizeof(nameBuffer), kCFStringEncodingUTF8)) {
                            if (strcmp(nameBuffer, playbackDev) == 0) {
                                targetDeviceID = deviceList[i];
                                ZF_LOGI("CoreAudio: Found device ID %u for '%s'", (unsigned)targetDeviceID, playbackDev);
                                break;
                            }
                        }
                        CFRelease(deviceName);
                    }
                }
            }
            free(deviceList);
        }
        
        if (targetDeviceID != kAudioObjectUnknown) {
            status = AudioUnitSetProperty(audioUnit, kAudioOutputUnitProperty_CurrentDevice,
                                        kAudioUnitScope_Global, 0, &targetDeviceID, sizeof(targetDeviceID));
            if (status != noErr) {
                ZF_LOGW("CoreAudio: Failed to set specific output device %u (status=%d)", (unsigned)targetDeviceID, (int)status);
            } else {
                ZF_LOGI("CoreAudio: Set output device to %u ('%s')", (unsigned)targetDeviceID, playbackDev);
            }
        } else {
            ZF_LOGW("CoreAudio: Could not find device ID for playback device '%s'", playbackDev);
        }
    }

    // Configure output if needed
    if (wantOutput) {
        // Get the device's current format first
        AudioStreamBasicDescription deviceFormat = {0};
        if (targetDeviceID != kAudioObjectUnknown) {
            UInt32 formatSize = sizeof(deviceFormat);
            AudioObjectPropertyAddress formatAddr = { kAudioDevicePropertyStreamFormat, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMain };
            if (AudioObjectGetPropertyData(targetDeviceID, &formatAddr, 0, NULL, &formatSize, &deviceFormat) == noErr) {
                ZF_LOGI("CoreAudio: Device format: %.1fHz, %u channels, %u bits", 
                        deviceFormat.mSampleRate, (unsigned)deviceFormat.mChannelsPerFrame, (unsigned)deviceFormat.mBitsPerChannel);
            }
        }
        
        // Set up audio format - use device's native sample rate and stereo if needed
        AudioStreamBasicDescription format = {0};
        // Use device's sample rate if available, otherwise default to 48kHz
        format.mSampleRate = (deviceFormat.mSampleRate > 0) ? deviceFormat.mSampleRate : 48000.0;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kLinearPCMFormatFlagIsPacked;
        // Use device's channel count if available, otherwise mono
        format.mChannelsPerFrame = (deviceFormat.mChannelsPerFrame > 0) ? deviceFormat.mChannelsPerFrame : 1;
        format.mFramesPerPacket = 1;
        format.mBitsPerChannel = 32;
        format.mBytesPerFrame = format.mChannelsPerFrame * sizeof(Float32);
        format.mBytesPerPacket = format.mBytesPerFrame;
        
        status = AudioUnitSetProperty(audioUnit, kAudioUnitProperty_StreamFormat,
                                    kAudioUnitScope_Input, 0, &format, sizeof(format));
        if (status != noErr) {
            ZF_LOGE("CoreAudio: Failed to set output format (status=%d)", (int)status);
        } else {
            ZF_LOGI("CoreAudio: Set format: %.1fHz, %u channels, 32-bit float", 
                    format.mSampleRate, (unsigned)format.mChannelsPerFrame);
        }
        
        // Set render callback
        AURenderCallbackStruct callbackStruct = {0};
        callbackStruct.inputProc = renderCallback;
        callbackStruct.inputProcRefCon = NULL;
        
        status = AudioUnitSetProperty(audioUnit, kAudioUnitProperty_SetRenderCallback,
                                    kAudioUnitScope_Input, 0, &callbackStruct, sizeof(callbackStruct));
        if (status != noErr) {
            ZF_LOGE("CoreAudio: Failed to set render callback (status=%d)", (int)status);
            AudioComponentInstanceDispose(audioUnit);
            audioUnit = NULL;
            return;
        } else {
            ZF_LOGI("CoreAudio: Render callback set successfully");
        }
    }
    
    // Initialize the AudioUnit
    status = AudioUnitInitialize(audioUnit);
    if (status != noErr) {
        ZF_LOGE("CoreAudio: Failed to initialize AudioUnit (status=%d)", (int)status);
        AudioComponentInstanceDispose(audioUnit);
        audioUnit = NULL;
        return;
    }

    coreAudioInputActive = wantInput;
    coreAudioOutputActive = wantOutput;
    coreAudioInitialized = true;
    ZF_LOGI("CoreAudio: AudioUnit initialized successfully - inputEnabled=%d outputEnabled=%d", 
            (int)coreAudioInputActive, (int)coreAudioOutputActive);
    
    // Verify callback was set (only if we have output)
    if (wantOutput) {
        AURenderCallbackStruct verifyCallback;
        UInt32 verifySize = sizeof(verifyCallback);
        status = AudioUnitGetProperty(audioUnit, kAudioUnitProperty_SetRenderCallback,
                                     kAudioUnitScope_Input, 0, &verifyCallback, &verifySize);
        if (status == noErr) {
            ZF_LOGI("CoreAudio: Render callback verified: proc=%p", (void*)verifyCallback.inputProc);
        } else {
            ZF_LOGE("CoreAudio: Failed to verify render callback (status=%d)", (int)status);
        }
    }
#else
    // Non-Apple platform stub
    coreAudioInputActive = wantInput;
    coreAudioOutputActive = wantOutput;
    coreAudioInitialized = true;
    ZF_LOGD("CoreAudio init: inputEnabled=%d outputEnabled=%d (stub)", (int)coreAudioInputActive, (int)coreAudioOutputActive);
#endif
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
    ;  // Empty statement to satisfy C syntax
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
    ZF_LOGI("SoundFlush: called, will KeyPTT(false) and wait for audio to finish");
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
    if (!TXEnabled || !coreAudioInitialized || !audioUnit) {
    ZF_LOGW("SendtoCard: Cannot send - TXEnabled=%d, initialized=%d, audioUnit=%p", 
        TXEnabled, coreAudioInitialized, (void*)audioUnit);
    return false;
    }
    // Copy n samples from txbuffer[TxIndex] into ring buffer
    int written = 0;
    int nonzero = 0;
    for (int i = 0; i < n; i++) {
        if (ringbuf_count >= RINGBUF_SIZE) {
            ZF_LOGE("SendtoCard: ring buffer overrun! Dropping audio sample.");
            break;
        }
        short sample = txbuffer[TxIndex][i];
        ringbuf[ringbuf_write] = sample;
        if (sample != 0) nonzero++;
        ringbuf_write = (ringbuf_write + 1) % RINGBUF_SIZE;
        ringbuf_count++;
        written++;
    }
    // (Debug logging removed for normal operation)
    // Start AudioUnit playback if not already playing
    if (!audioPlaying) {
#ifdef __APPLE__
        OSStatus status = AudioOutputUnitStart(audioUnit);
        if (status != noErr) {
            ZF_LOGE("CoreAudio: Failed to start AudioUnit (status=%d)", (int)status);
            return false;
        }
        ZF_LOGI("CoreAudio: AudioUnit started successfully");
#endif
        audioPlaying = true;
        audioFinished = false;  // Reset finished flag
        srcPosition = 0.0f;     // Reset sample rate conversion position
        SoundIsPlaying = true;
        ZF_LOGI("CoreAudio: Started audio playback (ring buffer mode)");
    }
    return true;
}

void PollReceivedSamples() {
    // No audio capture in stub. Intentionally silent.
}

void StopCapture() {
    // Nothing
}

bool SoundFlush() {
    KeyPTT(false);

    // Wait for all staged audio to finish playing
    if (audioPlaying && audioUnit) {
        // Wait for the render callback to consume all the audio data
        int flushWaitCount = 0;
        while (audioPlaying && !audioFinished) {
            usleep(1000); // Sleep for 1ms
            flushWaitCount++;
        }
#ifdef __APPLE__
        OSStatus status = AudioOutputUnitStop(audioUnit);
        if (status != noErr) {
            ZF_LOGW("CoreAudio: Failed to stop AudioUnit (status=%d)", (int)status);
        }
#endif
        audioPlaying = false;
        SoundIsPlaying = false;
        // Reset ring buffer for next transmission
        ringbuf_read = 0;
        ringbuf_write = 0;
        ringbuf_count = 0;
        txReadIndex = 0;
        audioFinished = false;
        srcPosition = 0.0f;
    }

    return TXEnabled;
}

bool crestorable() { return last_rx_dev[0] != '\0'; }
bool prestorable() { return last_tx_dev[0] != '\0'; }
