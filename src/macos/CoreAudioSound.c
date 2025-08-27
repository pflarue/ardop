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

// CoreAudio state - real AudioUnit instances and configuration
static bool coreAudioInitialized = false;      // AudioUnits created and configured
static bool coreAudioInputActive = false;      // Input bus enabled and running
static bool coreAudioOutputActive = false;     // Output bus enabled and running
static AudioUnit inputAudioUnit = NULL;        // AudioUnit for input capture
static AudioUnit outputAudioUnit = NULL;       // AudioUnit for output playback


// Ring buffer for audio output (large enough for several seconds of audio)
#define RINGBUF_SIZE (SendSize * 256) // 256*1200 = 307200 samples ~7s at 44.1kHz
static short ringbuf[RINGBUF_SIZE];
static volatile int ringbuf_write = 0; // Next write position
static volatile int ringbuf_read = 0;  // Next read position
static volatile int ringbuf_count = 0; // Number of samples in buffer

// Ring buffer for audio input (capture)
#define INBUF_SIZE (ReceiveSize * 256) // 256*240 = 61440 samples ~5s at 12kHz
static float inbuf[INBUF_SIZE];
static volatile int inbuf_write = 0;
static volatile int inbuf_read = 0;
static volatile int inbuf_count = 0;

// Buffer for delivering 240-sample blocks to ProcessNewSamples
static short rxblock[ReceiveSize];
static int rxblock_fill = 0;
// CoreAudio input callback for audio capture
static OSStatus inputCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
                             const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
                             UInt32 inNumberFrames, AudioBufferList *ioData) {
    (void)ioData; // ioData is not used for input callbacks
    
    if (!coreAudioInputActive || !RXEnabled || !inputAudioUnit)
        return noErr;

#ifdef __APPLE__
    // Allocate buffer for captured audio (mono float32)
    const UInt32 channelsPerFrame = 1; // We want mono input
    const UInt32 bytesPerFrame = sizeof(Float32) * channelsPerFrame;
    const UInt32 bufferSize = inNumberFrames * bytesPerFrame;
    
    Float32 *capturedBuffer = (Float32 *)malloc(bufferSize);
    if (!capturedBuffer) {
        return noErr; // Out of memory
    }
    
    // Set up AudioBufferList for captured data
    AudioBufferList capturedData = {0};
    capturedData.mNumberBuffers = 1;
    capturedData.mBuffers[0].mNumberChannels = channelsPerFrame;
    capturedData.mBuffers[0].mDataByteSize = bufferSize;
    capturedData.mBuffers[0].mData = capturedBuffer;
    
    // Actually capture the audio from the input device
    OSStatus status = AudioUnitRender(inputAudioUnit, ioActionFlags, inTimeStamp, inBusNumber, 
                                     inNumberFrames, &capturedData);
    
    if (status == noErr) {
        // Process captured audio samples
        Float32 *inputBuffer = (Float32 *)capturedData.mBuffers[0].mData;
        static int debugCount = 0;
        int nonZeroSamples = 0;
        
        for (UInt32 frame = 0; frame < inNumberFrames; frame++) {
            float sample = inputBuffer[frame];
            if (fabs(sample) > 0.001f) nonZeroSamples++;
            if (inbuf_count < INBUF_SIZE) {
                inbuf[inbuf_write] = sample;
                inbuf_write = (inbuf_write + 1) % INBUF_SIZE;
                inbuf_count++;
            }
        }
        
    } else {
        static int errorCount = 0;
        if (++errorCount % 10 == 0) {
            ZF_LOGE("INPUT ERROR: AudioUnitRender failed, status=%d (count=%d)", (int)status, errorCount);
        }
    }
    
    free(capturedBuffer);
    return status;
#else
    (void)inRefCon; (void)ioActionFlags; (void)inTimeStamp; (void)inBusNumber; (void)inNumberFrames;
    return noErr;
#endif
}

// Track enabled state
bool AudioInit = false;  // One-time overall audio enumeration done
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
static volatile bool audioUnitStarted = false; // Whether AudioUnit is started (for input or output)

// Helper to decide if a device string represents an enabled direction.
// Enabled if non-null, non-empty, not NOSOUND, not -1.
static bool dev_enabled(const char *dev) {
    return dev != NULL && dev[0] != '\0' && strcmp(dev, "NOSOUND") != 0 && strcmp(dev, "-1") != 0;
}

// Start the AudioUnits if they're initialized and not already started
// This is needed for both TX and RX operation
static bool EnsureAudioUnitsStarted(void) {
    if (!coreAudioInitialized || audioUnitStarted) {
        return audioUnitStarted; // Already started or not initialized
    }
    
#ifdef __APPLE__
    bool success = true;
    
    // Start input AudioUnit if we have one
    if (inputAudioUnit) {
        OSStatus status = AudioOutputUnitStart(inputAudioUnit);
        if (status != noErr) {
            ZF_LOGE("CoreAudio: Failed to start input AudioUnit (status=%d)", (int)status);
            success = false;
        } else {
            }
    }
    
    // Start output AudioUnit if we have one
    if (outputAudioUnit) {
        OSStatus status = AudioOutputUnitStart(outputAudioUnit);
        if (status != noErr) {
            ZF_LOGE("CoreAudio: Failed to start output AudioUnit (status=%d)", (int)status);
            success = false;
        } else {
        }
    }
    
    if (success) {
        audioUnitStarted = true;
        ZF_LOGI("CoreAudio: AudioUnits started successfully");
    }
    return success;
#else
    return false;
#endif
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
        if (coreAudioInitialized) {
            if (inputAudioUnit) {
                AudioUnitUninitialize(inputAudioUnit);
                AudioComponentInstanceDispose(inputAudioUnit);
                inputAudioUnit = NULL;
            }
            if (outputAudioUnit) {
                AudioUnitUninitialize(outputAudioUnit);
                AudioComponentInstanceDispose(outputAudioUnit);
                outputAudioUnit = NULL;
            }
            coreAudioInitialized = false;
            audioUnitStarted = false;  // AudioUnits are disposed
            ZF_LOGI("CoreAudio: Disposed AudioUnits due to no enabled devices");
        }
        coreAudioInputActive = false;
        coreAudioOutputActive = false;
        ZF_LOGD("CoreAudio init: inputEnabled=%d outputEnabled=%d (no AudioUnits)", (int)coreAudioInputActive, (int)coreAudioOutputActive);
        return;
    }

#ifdef __APPLE__
    OSStatus status = noErr;
    
    // Dispose existing AudioUnits if we have them
    if (coreAudioInitialized) {
        if (inputAudioUnit) {
            AudioUnitUninitialize(inputAudioUnit);
            AudioComponentInstanceDispose(inputAudioUnit);
            inputAudioUnit = NULL;
        }
        if (outputAudioUnit) {
            AudioUnitUninitialize(outputAudioUnit);
            AudioComponentInstanceDispose(outputAudioUnit);
            outputAudioUnit = NULL;
        }
        coreAudioInitialized = false;
        audioUnitStarted = false;  // AudioUnits are disposed
    }

    // Create INPUT AudioUnit if needed
    if (wantInput) {
        AudioComponentDescription inputDesc = {0};
        inputDesc.componentType = kAudioUnitType_Output;
        inputDesc.componentSubType = kAudioUnitSubType_HALOutput;  // HAL for input
        inputDesc.componentManufacturer = kAudioUnitManufacturer_Apple;
        
        AudioComponent inputComponent = AudioComponentFindNext(NULL, &inputDesc);
        if (!inputComponent) {
            ZF_LOGE("CoreAudio: Failed to find HAL input component");
            return;
        }
        
        status = AudioComponentInstanceNew(inputComponent, &inputAudioUnit);
        if (status != noErr || !inputAudioUnit) {
            ZF_LOGE("CoreAudio: Failed to create input AudioUnit instance (status=%d)", (int)status);
            return;
        }
        ZF_LOGD("CoreAudio: Created input AudioUnit");
    }

    // Create OUTPUT AudioUnit if needed
    if (wantOutput) {
        AudioComponentDescription outputDesc = {0};
        outputDesc.componentType = kAudioUnitType_Output;
        outputDesc.componentSubType = kAudioUnitSubType_HALOutput;  // HAL for output
        outputDesc.componentManufacturer = kAudioUnitManufacturer_Apple;
        
        AudioComponent outputComponent = AudioComponentFindNext(NULL, &outputDesc);
        if (!outputComponent) {
            ZF_LOGE("CoreAudio: Failed to find HAL output component");
            if (inputAudioUnit) {
                AudioComponentInstanceDispose(inputAudioUnit);
                inputAudioUnit = NULL;
            }
            return;
        }
        
        status = AudioComponentInstanceNew(outputComponent, &outputAudioUnit);
        if (status != noErr || !outputAudioUnit) {
            ZF_LOGE("CoreAudio: Failed to create output AudioUnit instance (status=%d)", (int)status);
            if (inputAudioUnit) {
                AudioComponentInstanceDispose(inputAudioUnit);
                inputAudioUnit = NULL;
            }
            return;
        }
        ZF_LOGD("CoreAudio: Created output AudioUnit");
    }


    // Configure INPUT AudioUnit if we created one
    if (inputAudioUnit) {
        // Enable input on the HAL unit
        UInt32 enableInput = 1;
        status = AudioUnitSetProperty(inputAudioUnit, kAudioOutputUnitProperty_EnableIO,
                                    kAudioUnitScope_Input, 1, &enableInput, sizeof(enableInput));
        if (status != noErr) {
            ZF_LOGE("CoreAudio: Failed to enable input on input AudioUnit (status=%d)", (int)status);
            AudioComponentInstanceDispose(inputAudioUnit);
            inputAudioUnit = NULL;
            if (outputAudioUnit) {
                AudioComponentInstanceDispose(outputAudioUnit);
                outputAudioUnit = NULL;
            }
            return;
        }
        
        // Disable output on input AudioUnit (input-only)
        UInt32 disableOutput = 0;
        status = AudioUnitSetProperty(inputAudioUnit, kAudioOutputUnitProperty_EnableIO,
                                    kAudioUnitScope_Output, 0, &disableOutput, sizeof(disableOutput));
        if (status != noErr) {
            ZF_LOGW("CoreAudio: Failed to disable output on input AudioUnit (status=%d)", (int)status);
        }
        
        
        // Configure specific input device if we have one
        if (captureDev && captureDev[0] != '\0') {
            AudioDeviceID inputDeviceID = kAudioObjectUnknown;
            
            // Find the device ID for our capture device name
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
                                if (strcmp(nameBuffer, captureDev) == 0) {
                                    inputDeviceID = deviceList[i];
                                    ZF_LOGD("CoreAudio: Found device ID %u for input '%s'", (unsigned)inputDeviceID, captureDev);
                                    break;
                                }
                            }
                            CFRelease(deviceName);
                        }
                    }
                }
                free(deviceList);
            }
            
            if (inputDeviceID != kAudioObjectUnknown) {
                status = AudioUnitSetProperty(inputAudioUnit, kAudioOutputUnitProperty_CurrentDevice,
                                            kAudioUnitScope_Global, 0, &inputDeviceID, sizeof(inputDeviceID));
                if (status != noErr) {
                    ZF_LOGW("CoreAudio: Failed to set specific input device %u (status=%d)", (unsigned)inputDeviceID, (int)status);
                } else {
                    ZF_LOGD("CoreAudio: Set input device to %u ('%s')", (unsigned)inputDeviceID, captureDev);
                }
            } else {
                ZF_LOGW("CoreAudio: Could not find device ID for capture device '%s'", captureDev);
            }
        }
    }

    // Configure OUTPUT AudioUnit if we created one
    if (outputAudioUnit) {
        // Enable output on the HAL unit
        UInt32 enableOutput = 1;
        status = AudioUnitSetProperty(outputAudioUnit, kAudioOutputUnitProperty_EnableIO,
                                    kAudioUnitScope_Output, 0, &enableOutput, sizeof(enableOutput));
        if (status != noErr) {
            ZF_LOGE("CoreAudio: Failed to enable output on output AudioUnit (status=%d)", (int)status);
            if (inputAudioUnit) {
                AudioComponentInstanceDispose(inputAudioUnit);
                inputAudioUnit = NULL;
            }
            AudioComponentInstanceDispose(outputAudioUnit);
            outputAudioUnit = NULL;
            return;
        }
        
        // Disable input on output AudioUnit (output-only)
        UInt32 disableInput = 0;
        status = AudioUnitSetProperty(outputAudioUnit, kAudioOutputUnitProperty_EnableIO,
                                    kAudioUnitScope_Input, 1, &disableInput, sizeof(disableInput));
        if (status != noErr) {
            ZF_LOGW("CoreAudio: Failed to disable input on output AudioUnit (status=%d)", (int)status);
        }
        
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
        
        if (targetDeviceID != kAudioObjectUnknown && outputAudioUnit) {
            status = AudioUnitSetProperty(outputAudioUnit, kAudioOutputUnitProperty_CurrentDevice,
                                        kAudioUnitScope_Global, 0, &targetDeviceID, sizeof(targetDeviceID));
            if (status != noErr) {
                ZF_LOGW("CoreAudio: Failed to set specific output device %u (status=%d)", (unsigned)targetDeviceID, (int)status);
            } else {
                ZF_LOGD("CoreAudio: Set output device to %u ('%s')", (unsigned)targetDeviceID, playbackDev);
            }
        } else if (wantOutput) {
            ZF_LOGW("CoreAudio: Could not find device ID for playback device '%s'", playbackDev);
        }
    }

    // Configure output if needed
    if (wantOutput) {
        // Set up output format (float32, stereo, 48kHz)
        AudioStreamBasicDescription outputFormat = {0};
        outputFormat.mSampleRate = 48000.0;
        outputFormat.mFormatID = kAudioFormatLinearPCM;
        outputFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kLinearPCMFormatFlagIsPacked;
        outputFormat.mChannelsPerFrame = 2;  // Stereo
        outputFormat.mFramesPerPacket = 1;
        outputFormat.mBitsPerChannel = 32;
        outputFormat.mBytesPerFrame = sizeof(Float32) * outputFormat.mChannelsPerFrame;
        outputFormat.mBytesPerPacket = outputFormat.mBytesPerFrame;
        
        status = AudioUnitSetProperty(outputAudioUnit, kAudioUnitProperty_StreamFormat,
                                     kAudioUnitScope_Input, 0, &outputFormat, sizeof(outputFormat));
        if (status != noErr) {
            ZF_LOGE("CoreAudio: Failed to set output format (status=%d)", (int)status);
        } else {
            ZF_LOGD("CoreAudio: Set output format: %.1fHz, %u channels, 32-bit float", outputFormat.mSampleRate, (unsigned)outputFormat.mChannelsPerFrame);
        }
        
        // Set render callback
        AURenderCallbackStruct renderCallbackStruct = {0};
        renderCallbackStruct.inputProc = renderCallback;
        renderCallbackStruct.inputProcRefCon = NULL;
        status = AudioUnitSetProperty(outputAudioUnit, kAudioUnitProperty_SetRenderCallback,
                                     kAudioUnitScope_Input, 0, &renderCallbackStruct, sizeof(renderCallbackStruct));
        if (status != noErr) {
            ZF_LOGE("CoreAudio: Failed to set render callback (status=%d)", (int)status);
        } else {
            }
    }
    // Configure input if needed
    if (wantInput) {
        // Set up input format (always float32, mono, 48kHz or device rate)
        AudioStreamBasicDescription inputFormat = {0};
        inputFormat.mSampleRate = 48000.0; // TODO: query device for actual rate if needed
        inputFormat.mFormatID = kAudioFormatLinearPCM;
        inputFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kLinearPCMFormatFlagIsPacked;
        inputFormat.mChannelsPerFrame = 1;
        inputFormat.mFramesPerPacket = 1;
        inputFormat.mBitsPerChannel = 32;
        inputFormat.mBytesPerFrame = sizeof(Float32);
        inputFormat.mBytesPerPacket = inputFormat.mBytesPerFrame;
        status = AudioUnitSetProperty(inputAudioUnit, kAudioUnitProperty_StreamFormat,
                                    kAudioUnitScope_Output, 1, &inputFormat, sizeof(inputFormat));
        if (status != noErr) {
            ZF_LOGE("CoreAudio: Failed to set input format (status=%d)", (int)status);
        } else {
            ZF_LOGD("CoreAudio: Set input format: %.1fHz, %u channels, 32-bit float", inputFormat.mSampleRate, (unsigned)inputFormat.mChannelsPerFrame);
        }
        // Set input callback
        AURenderCallbackStruct inputCallbackStruct = {0};
        inputCallbackStruct.inputProc = inputCallback;
        inputCallbackStruct.inputProcRefCon = NULL;
        status = AudioUnitSetProperty(inputAudioUnit, kAudioOutputUnitProperty_SetInputCallback,
                                    kAudioUnitScope_Global, 0, &inputCallbackStruct, sizeof(inputCallbackStruct));
        if (status != noErr) {
            ZF_LOGE("CoreAudio: Failed to set input callback (status=%d)", (int)status);
        } else {
            }
    }
    
    // Initialize the AudioUnits
    if (inputAudioUnit) {
        status = AudioUnitInitialize(inputAudioUnit);
        if (status != noErr) {
            ZF_LOGE("CoreAudio: Failed to initialize input AudioUnit (status=%d)", (int)status);
            AudioComponentInstanceDispose(inputAudioUnit);
            inputAudioUnit = NULL;
            if (outputAudioUnit) {
                AudioComponentInstanceDispose(outputAudioUnit);
                outputAudioUnit = NULL;
            }
            return;
        }
    }
    
    if (outputAudioUnit) {
        status = AudioUnitInitialize(outputAudioUnit);
        if (status != noErr) {
            ZF_LOGE("CoreAudio: Failed to initialize output AudioUnit (status=%d)", (int)status);
            if (inputAudioUnit) {
                AudioUnitUninitialize(inputAudioUnit);
                AudioComponentInstanceDispose(inputAudioUnit);
                inputAudioUnit = NULL;
            }
            AudioComponentInstanceDispose(outputAudioUnit);
            outputAudioUnit = NULL;
            return;
        }
    }

    coreAudioInputActive = wantInput;
    coreAudioOutputActive = wantOutput;
    coreAudioInitialized = true;
    ZF_LOGD("CoreAudio: AudioUnit initialized successfully - inputEnabled=%d outputEnabled=%d", 
            (int)coreAudioInputActive, (int)coreAudioOutputActive);
    
    // Verify callback was set (only if we have output)
    if (wantOutput && outputAudioUnit) {
        AURenderCallbackStruct verifyCallback;
        UInt32 verifySize = sizeof(verifyCallback);
        status = AudioUnitGetProperty(outputAudioUnit, kAudioUnitProperty_SetRenderCallback,
                                     kAudioUnitScope_Input, 0, &verifyCallback, &verifySize);
        if (status == noErr) {
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

    // Teardown sequence (stop -> uninit -> dispose)
    if (coreAudioInitialized) {
        // 1) Stop
        if (audioUnitStarted) {
            if (inputAudioUnit) AudioOutputUnitStop(inputAudioUnit);
            if (outputAudioUnit) AudioOutputUnitStop(outputAudioUnit);
            audioUnitStarted = false;
        }
        
        // 2) Uninitialize
        if (inputAudioUnit) AudioUnitUninitialize(inputAudioUnit);
        if (outputAudioUnit) AudioUnitUninitialize(outputAudioUnit);
        
        // 3) Dispose
        if (inputAudioUnit) {
            AudioComponentInstanceDispose(inputAudioUnit);
            inputAudioUnit = NULL;
        }
        if (outputAudioUnit) {
            AudioComponentInstanceDispose(outputAudioUnit);
            outputAudioUnit = NULL;
        }
        
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

    ZF_LOGD("CoreAudio reinit complete (in=%d out=%d)", (int)coreAudioInputActive, (int)coreAudioOutputActive);
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
    if (!TXEnabled || !coreAudioInitialized || !outputAudioUnit) {
    ZF_LOGW("SendtoCard: Cannot send - TXEnabled=%d, initialized=%d, outputAudioUnit=%p", 
        TXEnabled, coreAudioInitialized, (void*)outputAudioUnit);
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
        if (!EnsureAudioUnitsStarted()) {
            return false;
        }
        audioPlaying = true;
        audioFinished = false;  // Reset finished flag
        srcPosition = 0.0f;     // Reset sample rate conversion position
        SoundIsPlaying = true;
        ZF_LOGI("CoreAudio: Started audio playback (ring buffer mode)");
    }
    return true;
}

// Poll for received samples, perform SRC, and deliver 240-sample blocks to ProcessNewSamples
void PollReceivedSamples() {
    
    // Only run if RX is enabled and input is active
    if (!RXEnabled || !coreAudioInputActive)
        return;
    
    // Ensure AudioUnits are started for input capture
    EnsureAudioUnitsStarted();
    // Sample rate conversion: input is float32 at 48kHz, output must be 16-bit at 12kHz
    static double srcPos = 0.0;
    const double srcRate = 48000.0;
    const double dstRate = 12000.0;
    const double rateRatio = srcRate / dstRate; // 4.0
    while (inbuf_count > 4) { // Need at least 2 samples for interpolation
        // Linear interpolation SRC
        int srcIndex0 = (int)srcPos;
        int srcIndex1 = srcIndex0 + 1;
        float s0 = 0, s1 = 0;
        if (inbuf_count > srcIndex1) {
            int idx0 = (inbuf_read + srcIndex0) % INBUF_SIZE;
            int idx1 = (inbuf_read + srcIndex1) % INBUF_SIZE;
            s0 = inbuf[idx0];
            s1 = inbuf[idx1];
        } else if (inbuf_count > srcIndex0) {
            int idx0 = (inbuf_read + srcIndex0) % INBUF_SIZE;
            s0 = inbuf[idx0];
            s1 = s0;
        } else {
            break;
        }
        float fract = srcPos - srcIndex0;
        short sample = (short)(32767.0f * ((1.0 - fract) * s0 + fract * s1));
        rxblock[rxblock_fill++] = sample;
        if (rxblock_fill >= ReceiveSize) {
            ProcessNewSamples(rxblock, ReceiveSize);
            rxblock_fill = 0;
        }
        srcPos += rateRatio;
        // When enough output frames have been produced to consume a source sample, advance inbuf_read
        while (srcPos >= 1.0 && inbuf_count > 0) {
            inbuf_read = (inbuf_read + 1) % INBUF_SIZE;
            inbuf_count--;
            srcPos -= 1.0;
        }
    }
}

void StopCapture() {
    // Nothing
}

bool SoundFlush() {
    KeyPTT(false);

    // Wait for all staged audio to finish playing
    if (audioPlaying && outputAudioUnit) {
        // Wait for the render callback to consume all the audio data
        int flushWaitCount = 0;
        while (audioPlaying && !audioFinished) {
            usleep(1000); // Sleep for 1ms
            flushWaitCount++;
        }
#ifdef __APPLE__
        // Stop both AudioUnits
        if (inputAudioUnit) {
            OSStatus status = AudioOutputUnitStop(inputAudioUnit);
            if (status != noErr) {
                ZF_LOGW("CoreAudio: Failed to stop input AudioUnit (status=%d)", (int)status);
            }
        }
        if (outputAudioUnit) {
            OSStatus status = AudioOutputUnitStop(outputAudioUnit);
            if (status != noErr) {
                ZF_LOGW("CoreAudio: Failed to stop output AudioUnit (status=%d)", (int)status);
            }
        }
#endif
        audioPlaying = false;
        audioUnitStarted = false;  // AudioUnit is now stopped
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
