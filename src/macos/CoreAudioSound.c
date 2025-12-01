#include <stdio.h>
// macOS CoreAudio implementation for ARDOP audio I/O.
// Provides complete audio streaming functionality with separate AudioUnits
// for input capture and output playback, matching Linux/Windows capabilities.
//
// Features:
//  - Full-duplex audio streaming with separate input/output AudioUnits
//  - Real device enumeration via CoreAudio APIs
//  - Sample rate conversion (48kHz device ↔ 12kHz ARDOP)
//  - Runtime device reconfiguration and directional enablement
//  - Ring buffer management for audio capture and playback

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#ifdef __APPLE__
#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include <AudioToolbox/AudioConverter.h>
#include <AudioUnit/AudioUnit.h>

#define ARDOP_AUDIOCONVERTER_NO_DATA 'NoDa'
#endif

#include "common/audio.h"
#include "common/log.h"
#include "common/ardopcommon.h"
#include "common/Webgui.h"
#include "common/ptt.h"
#include "common/os_util.h"

// Globals required by audio.h (mirroring ALSA.c declarations)
short txbuffer[2][SendSize];
int TxIndex = 0;

// CoreAudio state - real AudioUnit instances and configuration
static bool coreAudioInitialized = false;  // AudioUnits created and configured
static bool coreAudioInputActive = false;  // Input bus enabled and running
static bool coreAudioOutputActive = false; // Output bus enabled and running
static AudioUnit inputAudioUnit = NULL;    // AudioUnit for input capture
static AudioUnit outputAudioUnit = NULL;   // AudioUnit for output playback

// Ring buffer for audio output (large enough for several seconds of audio)
#define RINGBUF_SIZE (SendSize * 256) // 256*1200 = 307200 samples ~7s at 44.1kHz
static short ringbuf[RINGBUF_SIZE];
static volatile int ringbuf_write = 0; // Next write position
static volatile int ringbuf_read = 0;  // Next read position
static volatile int ringbuf_count = 0; // Number of samples in buffer
static volatile uint64_t txSamplesQueued = 0; // Total TX samples queued (12kHz domain)
static volatile uint64_t txSamplesPlayed = 0; // Total TX samples consumed by renderer

static double coreAudioOutputSampleRate = 48000.0;
static double coreAudioInputSampleRate = 48000.0;

#ifdef __APPLE__
static AudioConverterRef txConverter = NULL;
static AudioConverterRef rxConverter = NULL;

#define TX_CONVERTER_MAX_OUTPUT_FRAMES 4096
#define TX_CONVERTER_INPUT_CHUNK 4096
#define RX_CONVERTER_MAX_OUTPUT_FRAMES 4096
#define RX_CONVERTER_INPUT_CHUNK 4096

static SInt16 txConverterInputChunk[TX_CONVERTER_INPUT_CHUNK];
static Float32 txConverterOutputChunk[TX_CONVERTER_MAX_OUTPUT_FRAMES];
static Float32 rxConverterInputChunk[RX_CONVERTER_INPUT_CHUNK];
#endif

static inline uint64_t tx_samples_pending(void)
{
    uint64_t queued = txSamplesQueued;
    uint64_t played = txSamplesPlayed;
    return (queued > played) ? (queued - played) : 0;
}

static inline void tx_reset_counters(void)
{
    txSamplesQueued = 0;
    txSamplesPlayed = 0;
}

static bool testBypassAudioStart = false;
static bool testBypassOutputHandleCheck = false;
static bool testBypassAudioStop = false;
static bool testForceLegacySRC = false;
static bool testBypassRecoveryRestart = false;
static bool testBypassRecoveryReopen = false;

static pthread_mutex_t coreAudioMutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int txReadIndex = 0;           // Read position in txbuffer
static volatile bool audioPlaying = false;     // AudioUnit is actively playing
static volatile bool audioFinished = false;    // All audio data has been consumed
static volatile float srcPosition = 0.0f;      // Sample rate conversion position
static volatile bool audioUnitStarted = false; // Whether AudioUnit is started (for input or output)

// Ring buffer for audio input (capture)
#define INBUF_SIZE (ReceiveSize * 512) // 512*240 = 122880 samples ~10s at 12kHz
static float inbuf[INBUF_SIZE];
static volatile int inbuf_write = 0;
static volatile int inbuf_read = 0;
static volatile int inbuf_count = 0;

// Buffer for delivering 240-sample blocks to ProcessNewSamples
static short rxblock[ReceiveSize];
static int rxblock_fill = 0;

// CoreAudio diagnostics (minimal, non-verbose)
static struct
{
    // Callback frame size sampling (startup only)
    bool sampling_active;
    int sample_count;
    UInt32 frame_size_min, frame_size_max;
    UInt32 frame_size_sum;
    bool diagnostics_reported;

    // Ring buffer usage monitoring
    int max_output_usage_percent;
    int max_input_usage_percent;
    bool buffer_warning_shown;
} coreAudioDiag = {false, 0, UINT32_MAX, 0, 0, false, 0, 0, false};

#define CORE_AUDIO_RX_SILENCE_TIMEOUT_MS 2000U
#define CORE_AUDIO_TX_STALL_TIMEOUT_MS 1000U
#define CORE_AUDIO_RECOVERY_BACKOFF_MS 1000U
#define CORE_AUDIO_MAX_CONVERTER_ERRORS 3
#define CORE_AUDIO_FLUSH_TIMEOUT_MS 500U
#define CORE_AUDIO_FLUSH_MARGIN_MS 100U
#define CORE_AUDIO_FLUSH_MAX_TIMEOUT_MS 5000U

static struct
{
    unsigned int lastRxMs;
    unsigned int lastTxMs;
    unsigned int lastRxRecoverAttemptMs;
    unsigned int lastTxRecoverAttemptMs;
    unsigned int rxRecoveries;
    unsigned int txRecoveries;
    int consecutiveRxErrors;
    int consecutiveTxErrors;
} coreAudioRecovery = {0, 0, 0, 0, 0, 0, 0, 0};

static bool coreAudioPendingRxRecovery = false;
static bool coreAudioPendingTxRecovery = false;
static const char *coreAudioPendingRxReason = NULL;
static const char *coreAudioPendingTxReason = NULL;

static bool testNowOverride = false;
static unsigned int testNowValue = 0;

static bool dev_enabled(const char *dev);
#ifdef __APPLE__
static void RebuildTxConverter(void);
static void RebuildRxConverter(void);
static void coreaudio_disable_tx_converter(const char *reason);
#endif
static bool EnsureAudioUnitsStarted(void);

static inline unsigned int coreaudio_now_ms(void)
{
    if (testNowOverride)
        return testNowValue;
    return Now;
}

static void coreaudio_mark_rx_activity(void)
{
    coreAudioRecovery.lastRxMs = coreaudio_now_ms();
    coreAudioRecovery.consecutiveRxErrors = 0;
}

static void coreaudio_mark_tx_activity(void)
{
    coreAudioRecovery.lastTxMs = coreaudio_now_ms();
    coreAudioRecovery.consecutiveTxErrors = 0;
}

static bool coreaudio_should_monitor_rx(void)
{
    return RXEnabled && dev_enabled(CaptureDevice);
}

static bool coreaudio_should_monitor_tx(void)
{
    if (!TXEnabled || !audioPlaying || !dev_enabled(PlaybackDevice))
        return false;
    return (ringbuf_count > 0) || (tx_samples_pending() > 0);
}

static bool coreaudio_direction_enabled(bool capture)
{
    if (capture)
        return RXEnabled && dev_enabled(CaptureDevice);
    return TXEnabled && dev_enabled(PlaybackDevice);
}

static void coreaudio_reset_rx_buffers(void)
{
    inbuf_read = 0;
    inbuf_write = 0;
    inbuf_count = 0;
    rxblock_fill = 0;
}

static bool coreaudio_soft_restart(bool capture);
static bool coreaudio_full_reopen(bool capture);
static void coreaudio_request_recovery(bool capture, const char *reason);
static void coreaudio_attempt_recovery(bool capture, const char *reason);
static void CheckAndRecoverAudioHealth(void);

static void coreaudio_note_callback_error(bool capture, const char *source, OSStatus status)
{
#ifdef __APPLE__
    int *counter = capture ? &coreAudioRecovery.consecutiveRxErrors : &coreAudioRecovery.consecutiveTxErrors;
    (*counter)++;
    ZF_LOGW("CoreAudio %s error in %s (status=%d, count=%d)", capture ? "RX" : "TX", source, (int)status, *counter);
    if (*counter >= CORE_AUDIO_MAX_CONVERTER_ERRORS)
    {
        coreaudio_request_recovery(capture, source);
        *counter = 0;
    }
#else
    (void)capture;
    (void)source;
    (void)status;
#endif
}

static bool coreaudio_soft_restart(bool capture)
{
#ifdef __APPLE__
    pthread_mutex_lock(&coreAudioMutex);
    AudioUnit unit = capture ? inputAudioUnit : outputAudioUnit;
    if (!unit)
    {
        pthread_mutex_unlock(&coreAudioMutex);
        return false;
    }

    if (!testBypassAudioStop)
    {
        AudioOutputUnitStop(unit);
    }
    AudioUnitUninitialize(unit);
    OSStatus status = AudioUnitInitialize(unit);
    if (status != noErr)
    {
        pthread_mutex_unlock(&coreAudioMutex);
        ZF_LOGW("CoreAudio: Failed to reinitialize %s AudioUnit (status=%d)", capture ? "capture" : "playback", (int)status);
        return false;
    }

    if (capture)
    {
        coreaudio_reset_rx_buffers();
        RebuildRxConverter();
    }
    else
    {
        RebuildTxConverter();
    }

    audioUnitStarted = false;
    pthread_mutex_unlock(&coreAudioMutex);
    return EnsureAudioUnitsStarted();
#else
    (void)capture;
    return false;
#endif
}

static bool coreaudio_full_reopen(bool capture)
{
    if (capture)
    {
        if (!crestorable())
            return false;
        RXEnabled = false;
        return OpenSoundCapture("RESTORE", Cch);
    }
    else
    {
        if (!prestorable())
            return false;
        TXEnabled = false;
        SoundIsPlaying = false;
        return OpenSoundPlayback("RESTORE", Pch);
    }
}

static void coreaudio_request_recovery(bool capture, const char *reason)
{
    if (capture)
    {
        if (!coreAudioPendingRxRecovery)
        {
            coreAudioPendingRxReason = reason;
        }
        coreAudioPendingRxRecovery = true;
    }
    else
    {
        if (!coreAudioPendingTxRecovery)
        {
            coreAudioPendingTxReason = reason;
        }
        coreAudioPendingTxRecovery = true;
    }
}

static void coreaudio_attempt_recovery(bool capture, const char *reason)
{
#ifdef __APPLE__
    if (!coreaudio_direction_enabled(capture))
        return;

    bool verboseTestRecovery = false;
    const char *verboseEnv = getenv("ARDOP_TEST_VERBOSE_RECOVERY");
    if (verboseEnv && verboseEnv[0] != '\0')
        verboseTestRecovery = true;

    if (verboseTestRecovery)
    {
        fprintf(stderr, "CoreAudio recovery start capture=%d restartBypass=%d reopenBypass=%d\n",
                capture ? 1 : 0,
                testBypassRecoveryRestart ? 1 : 0,
                testBypassRecoveryReopen ? 1 : 0);
    }

    unsigned int now = coreaudio_now_ms();
    if (capture)
    {
        coreAudioRecovery.lastRxRecoverAttemptMs = now;
        coreAudioRecovery.rxRecoveries++;
    }
    else
    {
        coreAudioRecovery.lastTxRecoverAttemptMs = now;
        coreAudioRecovery.txRecoveries++;
    }

    ZF_LOGW("CoreAudio %s recovery triggered (%s)", capture ? "RX" : "TX", reason);
    bool recovered = false;
    if (testBypassRecoveryRestart)
    {
        ZF_LOGD("CoreAudio %s recovery restart bypassed for tests", capture ? "RX" : "TX");
        recovered = true;
    }
    else
    {
        recovered = coreaudio_soft_restart(capture);
    }
    if (!recovered)
    {
        if (testBypassRecoveryReopen)
        {
            ZF_LOGD("CoreAudio %s soft restart failed but RESTORE bypassed for tests", capture ? "RX" : "TX");
            recovered = true;
        }
        else
        {
            ZF_LOGW("CoreAudio %s soft restart failed, attempting RESTORE", capture ? "RX" : "TX");
            recovered = coreaudio_full_reopen(capture);
        }
    }

    if (recovered)
    {
        if (capture)
            coreaudio_mark_rx_activity();
        else
            coreaudio_mark_tx_activity();
    }
    else
    {
        ZF_LOGW("CoreAudio %s recovery failed", capture ? "RX" : "TX");
    }
#else
    (void)capture;
    (void)reason;
#endif
}

static void coreaudio_service_pending_recovery(bool capture, unsigned int now)
{
    bool *pendingFlag = capture ? &coreAudioPendingRxRecovery : &coreAudioPendingTxRecovery;
    const char **reasonSlot = capture ? &coreAudioPendingRxReason : &coreAudioPendingTxReason;
    if (!*pendingFlag)
        return;

    if (!coreaudio_direction_enabled(capture))
    {
        *pendingFlag = false;
        *reasonSlot = NULL;
        return;
    }

    unsigned int lastAttempt = capture ? coreAudioRecovery.lastRxRecoverAttemptMs : coreAudioRecovery.lastTxRecoverAttemptMs;
    if (now - lastAttempt < CORE_AUDIO_RECOVERY_BACKOFF_MS)
    {
        return;
    }

    const char *reason = *reasonSlot ? *reasonSlot : (capture ? "Pending RX recovery" : "Pending TX recovery");
    *pendingFlag = false;
    *reasonSlot = NULL;
    coreaudio_attempt_recovery(capture, reason);
}

static void CheckAndRecoverAudioHealth(void)
{
    unsigned int now = coreaudio_now_ms();

    coreaudio_service_pending_recovery(true, now);
    coreaudio_service_pending_recovery(false, now);

    if (!coreaudio_should_monitor_rx())
    {
        coreAudioRecovery.lastRxMs = now;
        coreAudioRecovery.consecutiveRxErrors = 0;
    }
    else
    {
        if (coreAudioRecovery.lastRxMs == 0)
            coreAudioRecovery.lastRxMs = now;
        unsigned int delta = now - coreAudioRecovery.lastRxMs;
        if (delta > CORE_AUDIO_RX_SILENCE_TIMEOUT_MS &&
            now - coreAudioRecovery.lastRxRecoverAttemptMs > CORE_AUDIO_RECOVERY_BACKOFF_MS)
        {
            coreaudio_attempt_recovery(true, "RX watchdog timeout");
        }
    }

    if (!coreaudio_should_monitor_tx())
    {
        coreAudioRecovery.lastTxMs = now;
        coreAudioRecovery.consecutiveTxErrors = 0;
    }
    else
    {
        if (coreAudioRecovery.lastTxMs == 0)
            coreAudioRecovery.lastTxMs = now;
        unsigned int delta = now - coreAudioRecovery.lastTxMs;
        if (delta > CORE_AUDIO_TX_STALL_TIMEOUT_MS &&
            now - coreAudioRecovery.lastTxRecoverAttemptMs > CORE_AUDIO_RECOVERY_BACKOFF_MS)
        {
            coreaudio_attempt_recovery(false, "TX watchdog timeout");
        }
    }
}

#ifdef __APPLE__
#define INPUT_CAPTURE_MAX_FRAMES 8192
static Float32 capturedFrameBuffer[INPUT_CAPTURE_MAX_FRAMES];

static AudioDeviceID coreaudio_get_default_device(bool input)
{
    AudioDeviceID device = kAudioObjectUnknown;
    AudioObjectPropertyAddress addr = {
        input ? kAudioHardwarePropertyDefaultInputDevice : kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain};
    UInt32 size = sizeof(device);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &size, &device) != noErr)
    {
        device = kAudioObjectUnknown;
    }
    return device;
}

static double coreaudio_query_nominal_rate(AudioDeviceID deviceID, AudioObjectPropertyScope scope)
{
    if (deviceID == kAudioObjectUnknown)
        return 0.0;

    double rate = 0.0;
    UInt32 size = sizeof(rate);
    AudioObjectPropertyAddress addr = {kAudioDevicePropertyNominalSampleRate, scope, kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyData(deviceID, &addr, 0, NULL, &size, &rate) != noErr || rate <= 0.0)
    {
        return 0.0;
    }
    return (double)rate;
}
#endif

#ifdef __APPLE__
static void DisposeTxConverter(void)
{
    if (txConverter)
    {
        AudioConverterDispose(txConverter);
        txConverter = NULL;
    }
}

static void DisposeRxConverter(void)
{
    if (rxConverter)
    {
        AudioConverterDispose(rxConverter);
        rxConverter = NULL;
    }
}

static bool coreAudioTxConverterAllowed = true;

static void coreaudio_disable_tx_converter(const char *reason)
{
    if (!coreAudioTxConverterAllowed)
        return;
    coreAudioTxConverterAllowed = false;
    if (reason && reason[0] != '\0')
        ZF_LOGW("CoreAudio: Falling back to legacy TX path (%s)", reason);
    else
        ZF_LOGW("CoreAudio: Falling back to legacy TX path");
    DisposeTxConverter();
}

static void RebuildTxConverter(void)
{
    DisposeTxConverter();
    if (!coreAudioTxConverterAllowed)
        return;
    if (!coreAudioOutputActive)
        return;

    AudioStreamBasicDescription inFormat = {0};
    inFormat.mSampleRate = 12000.0;
    inFormat.mFormatID = kAudioFormatLinearPCM;
    inFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    inFormat.mChannelsPerFrame = 1;
    inFormat.mFramesPerPacket = 1;
    inFormat.mBitsPerChannel = 16;
    inFormat.mBytesPerFrame = sizeof(SInt16);
    inFormat.mBytesPerPacket = sizeof(SInt16);

    AudioStreamBasicDescription outFormat = {0};
    outFormat.mSampleRate = (coreAudioOutputSampleRate > 0.0) ? coreAudioOutputSampleRate : 48000.0;
    outFormat.mFormatID = kAudioFormatLinearPCM;
    outFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kLinearPCMFormatFlagIsPacked;
    outFormat.mChannelsPerFrame = 1;
    outFormat.mFramesPerPacket = 1;
    outFormat.mBitsPerChannel = 32;
    outFormat.mBytesPerFrame = sizeof(Float32);
    outFormat.mBytesPerPacket = sizeof(Float32);

    OSStatus status = AudioConverterNew(&inFormat, &outFormat, &txConverter);
    if (status != noErr)
    {
        txConverter = NULL;
        ZF_LOGW("CoreAudio: Failed to create TX AudioConverter (status=%d)", (int)status);
        coreaudio_disable_tx_converter("converter init failed");
        return;
    }

    UInt32 quality = kAudioConverterQuality_Max;
    AudioConverterSetProperty(txConverter, kAudioConverterSampleRateConverterQuality, sizeof(quality), &quality);
    UInt32 complexity = kAudioConverterSampleRateConverterComplexity_Mastering;
    AudioConverterSetProperty(txConverter, kAudioConverterSampleRateConverterComplexity, sizeof(complexity), &complexity);
}

static void RebuildRxConverter(void)
{
    DisposeRxConverter();
    if (!coreAudioInputActive)
        return;

    AudioStreamBasicDescription inFormat = {0};
    inFormat.mSampleRate = (coreAudioInputSampleRate > 0.0) ? coreAudioInputSampleRate : 48000.0;
    inFormat.mFormatID = kAudioFormatLinearPCM;
    inFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kLinearPCMFormatFlagIsPacked;
    inFormat.mChannelsPerFrame = 1;
    inFormat.mFramesPerPacket = 1;
    inFormat.mBitsPerChannel = 32;
    inFormat.mBytesPerFrame = sizeof(Float32);
    inFormat.mBytesPerPacket = sizeof(Float32);

    AudioStreamBasicDescription outFormat = {0};
    outFormat.mSampleRate = 12000.0;
    outFormat.mFormatID = kAudioFormatLinearPCM;
    outFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    outFormat.mChannelsPerFrame = 1;
    outFormat.mFramesPerPacket = 1;
    outFormat.mBitsPerChannel = 16;
    outFormat.mBytesPerFrame = sizeof(SInt16);
    outFormat.mBytesPerPacket = sizeof(SInt16);

    OSStatus status = AudioConverterNew(&inFormat, &outFormat, &rxConverter);
    if (status != noErr)
    {
        rxConverter = NULL;
        ZF_LOGW("CoreAudio: Failed to create RX AudioConverter (status=%d)", (int)status);
        return;
    }

    UInt32 quality = kAudioConverterQuality_Max;
    AudioConverterSetProperty(rxConverter, kAudioConverterSampleRateConverterQuality, sizeof(quality), &quality);
    UInt32 complexity = kAudioConverterSampleRateConverterComplexity_Mastering;
    AudioConverterSetProperty(rxConverter, kAudioConverterSampleRateConverterComplexity, sizeof(complexity), &complexity);
}

static void RebuildAudioConverters(void)
{
    if (!coreAudioTxConverterAllowed)
        DisposeTxConverter();
    RebuildTxConverter();
    RebuildRxConverter();
}

static OSStatus txConverterInputProc(AudioConverterRef inAudioConverter,
                                     UInt32 *ioNumberDataPackets,
                                     AudioBufferList *ioData,
                                     AudioStreamPacketDescription **outDataPacketDescription,
                                     void *inUserData)
{
    (void)inAudioConverter;
    (void)outDataPacketDescription;
    (void)inUserData;

    if (*ioNumberDataPackets == 0)
        return noErr;

    UInt32 capacity = (*ioNumberDataPackets > TX_CONVERTER_INPUT_CHUNK) ? TX_CONVERTER_INPUT_CHUNK : *ioNumberDataPackets;
    UInt32 available = (ringbuf_count < (int)capacity) ? (UInt32)ringbuf_count : capacity;

    if (available == 0)
    {
        *ioNumberDataPackets = 0;
        ioData->mNumberBuffers = 0;
        return ARDOP_AUDIOCONVERTER_NO_DATA;
    }

    ioData->mNumberBuffers = 1;
    ioData->mBuffers[0].mNumberChannels = 1;
    ioData->mBuffers[0].mData = txConverterInputChunk;
    ioData->mBuffers[0].mDataByteSize = available * sizeof(SInt16);

    for (UInt32 i = 0; i < available; ++i)
    {
        txConverterInputChunk[i] = ringbuf[ringbuf_read];
        ringbuf_read = (ringbuf_read + 1) % RINGBUF_SIZE;
        ringbuf_count--;
        txSamplesPlayed++;
    }

    *ioNumberDataPackets = available;
    if (available > 0)
    {
        coreaudio_mark_tx_activity();
        coreAudioRecovery.consecutiveTxErrors = 0;
    }
    return noErr;
}

static OSStatus rxConverterInputProc(AudioConverterRef inAudioConverter,
                                     UInt32 *ioNumberDataPackets,
                                     AudioBufferList *ioData,
                                     AudioStreamPacketDescription **outDataPacketDescription,
                                     void *inUserData)
{
    (void)inAudioConverter;
    (void)outDataPacketDescription;
    (void)inUserData;

    if (*ioNumberDataPackets == 0)
        return noErr;

    UInt32 capacity = (*ioNumberDataPackets > RX_CONVERTER_INPUT_CHUNK) ? RX_CONVERTER_INPUT_CHUNK : *ioNumberDataPackets;
    UInt32 available = (inbuf_count < (int)capacity) ? (UInt32)inbuf_count : capacity;

    if (available == 0)
    {
        *ioNumberDataPackets = 0;
        ioData->mNumberBuffers = 0;
        return ARDOP_AUDIOCONVERTER_NO_DATA;
    }

    ioData->mNumberBuffers = 1;
    ioData->mBuffers[0].mNumberChannels = 1;
    ioData->mBuffers[0].mData = rxConverterInputChunk;
    ioData->mBuffers[0].mDataByteSize = available * sizeof(Float32);

    for (UInt32 i = 0; i < available; ++i)
    {
        rxConverterInputChunk[i] = inbuf[inbuf_read];
        inbuf_read = (inbuf_read + 1) % INBUF_SIZE;
        inbuf_count--;
    }

    *ioNumberDataPackets = available;
    return noErr;
}
#endif
// CoreAudio input callback for audio capture
static OSStatus inputCallback(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
                              const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
                              UInt32 inNumberFrames, AudioBufferList *ioData)
{
    (void)ioData; // ioData is not used for input callbacks

    if (!coreAudioInputActive || !RXEnabled || !inputAudioUnit)
        return noErr;

    // Sample callback frame sizes during startup (first 50 callbacks)
    if (coreAudioDiag.sampling_active && coreAudioDiag.sample_count < 50)
    {
        coreAudioDiag.sample_count++;
        if (inNumberFrames < coreAudioDiag.frame_size_min)
            coreAudioDiag.frame_size_min = inNumberFrames;
        if (inNumberFrames > coreAudioDiag.frame_size_max)
            coreAudioDiag.frame_size_max = inNumberFrames;
        coreAudioDiag.frame_size_sum += inNumberFrames;
    }

#ifdef __APPLE__
    // Allocate buffer for captured audio (mono float32)
    const UInt32 channelsPerFrame = 1; // We want mono input
    const UInt32 bytesPerFrame = sizeof(Float32) * channelsPerFrame;
    const UInt32 bufferSize = inNumberFrames * bytesPerFrame;

    if (inNumberFrames > INPUT_CAPTURE_MAX_FRAMES)
    {
        static int warningCount = 0;
        if (warningCount < 5 || warningCount % 100 == 0)
        {
            ZF_LOGW("CoreAudio input callback requested %u frames; max supported is %u. Dropping frame block.",
                    (unsigned int)inNumberFrames, (unsigned int)INPUT_CAPTURE_MAX_FRAMES);
        }
        warningCount++;
        return noErr;
    }

    Float32 *capturedBuffer = capturedFrameBuffer;

    // Set up AudioBufferList for captured data
    AudioBufferList capturedData = {0};
    capturedData.mNumberBuffers = 1;
    capturedData.mBuffers[0].mNumberChannels = channelsPerFrame;
    capturedData.mBuffers[0].mDataByteSize = bufferSize;
    capturedData.mBuffers[0].mData = capturedBuffer;

    // Actually capture the audio from the input device
    OSStatus status = AudioUnitRender(inputAudioUnit, ioActionFlags, inTimeStamp, inBusNumber,
                                      inNumberFrames, &capturedData);

    if (status == noErr)
    {
        // Process captured audio samples
        Float32 *inputBuffer = (Float32 *)capturedData.mBuffers[0].mData;

        for (UInt32 frame = 0; frame < inNumberFrames; frame++)
        {
            float sample = inputBuffer[frame];
            if (inbuf_count < INBUF_SIZE)
            {
                inbuf[inbuf_write] = sample;
                inbuf_write = (inbuf_write + 1) % INBUF_SIZE;
                inbuf_count++;
            }
        }

        // Monitor input buffer usage (non-verbose)
        int input_usage_percent = (inbuf_count * 100) / INBUF_SIZE;
        if (input_usage_percent > coreAudioDiag.max_input_usage_percent)
        {
            coreAudioDiag.max_input_usage_percent = input_usage_percent;
        }

        if (inNumberFrames > 0)
        {
            coreaudio_mark_rx_activity();
            coreAudioRecovery.consecutiveRxErrors = 0;
        }
    }
    else
    {
        static int errorCount = 0;
        if (++errorCount % 10 == 0)
        {
            ZF_LOGE("INPUT ERROR: AudioUnitRender failed, status=%d (count=%d)", (int)status, errorCount);
        }
        coreaudio_note_callback_error(true, "AudioUnitRender", status);
    }

    return status;
#else
    (void)inRefCon;
    (void)ioActionFlags;
    (void)inTimeStamp;
    (void)inBusNumber;
    (void)inNumberFrames;
    return noErr;
#endif
}

// Track enabled state
bool AudioInit = false; // One-time overall audio enumeration done
// Track last configured device names to detect change
static char lastCaptureDev[DEVSTRSZ] = "";
static char lastPlaybackDev[DEVSTRSZ] = "";
// Track last successfully opened (good) device names for RESTORE feature
static char last_rx_dev[DEVSTRSZ] = ""; // Capture side
static char last_tx_dev[DEVSTRSZ] = ""; // Playback side

// Helper to decide if a device string represents an enabled direction.
// Enabled if non-null, non-empty, not NOSOUND, not -1.
static bool dev_enabled(const char *dev)
{
    return dev != NULL && dev[0] != '\0' && strcmp(dev, "NOSOUND") != 0 && strcmp(dev, "-1") != 0;
}

// Start the AudioUnits if they're initialized and not already started
// This is needed for both TX and RX operation
static bool EnsureAudioUnitsStarted(void)
{
    if (!coreAudioInitialized || audioUnitStarted)
    {
        return audioUnitStarted; // Already started or not initialized
    }

    if (testBypassAudioStart)
    {
        audioUnitStarted = true;
        if (!coreAudioDiag.sampling_active && !coreAudioDiag.diagnostics_reported)
        {
            coreAudioDiag.sampling_active = true;
            coreAudioDiag.sample_count = 0;
            coreAudioDiag.frame_size_min = UINT32_MAX;
            coreAudioDiag.frame_size_max = 0;
            coreAudioDiag.frame_size_sum = 0;
        }
        return true;
    }

#ifdef __APPLE__
    bool success = true;

    // Start input AudioUnit if we have one
    if (inputAudioUnit)
    {
        OSStatus status = AudioOutputUnitStart(inputAudioUnit);
        if (status != noErr)
        {
            ZF_LOGE("CoreAudio: Failed to start input AudioUnit (status=%d)", (int)status);
            success = false;
        }
    }

    // Start output AudioUnit if we have one
    if (outputAudioUnit)
    {
        OSStatus status = AudioOutputUnitStart(outputAudioUnit);
        if (status != noErr)
        {
            ZF_LOGE("CoreAudio: Failed to start output AudioUnit (status=%d)", (int)status);
            success = false;
        }
    }

    if (success)
    {
        audioUnitStarted = true;

        // Start diagnostic sampling on first successful start
        if (!coreAudioDiag.sampling_active && !coreAudioDiag.diagnostics_reported)
        {
            coreAudioDiag.sampling_active = true;
            coreAudioDiag.sample_count = 0;
            coreAudioDiag.frame_size_min = UINT32_MAX;
            coreAudioDiag.frame_size_max = 0;
            coreAudioDiag.frame_size_sum = 0;
        }

        if (ZF_LOG_ON_DEBUG)
        {
            ZF_LOGD("CoreAudio: AudioUnits started successfully");
        }
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
                               UInt32 inNumberFrames, AudioBufferList *ioData)
{
    (void)inRefCon;
    (void)ioActionFlags;
    (void)inTimeStamp;
    (void)inBusNumber;

    // (Debug logging removed for normal operation)
    if (!audioPlaying || !TXEnabled || ioData->mNumberBuffers == 0)
    {
        // Fill with silence
        for (UInt32 i = 0; i < ioData->mNumberBuffers; i++)
        {
            memset(ioData->mBuffers[i].mData, 0, ioData->mBuffers[i].mDataByteSize);
        }
        return noErr;
    }
    // Get output buffer (assume first buffer, mono or stereo)
    float *outputBuffer = (float *)ioData->mBuffers[0].mData;
    UInt32 channelsPerFrame = ioData->mBuffers[0].mNumberChannels;

    bool usedConverter = false;
    bool producedAudio = false;
#ifdef __APPLE__
    if (txConverter && coreAudioTxConverterAllowed)
    {
        usedConverter = true;
        UInt32 framesRemaining = inNumberFrames;
        UInt32 frameOffset = 0;
        while (framesRemaining > 0)
        {
            UInt32 chunkFrames = framesRemaining;
            if (chunkFrames > TX_CONVERTER_MAX_OUTPUT_FRAMES)
                chunkFrames = TX_CONVERTER_MAX_OUTPUT_FRAMES;

            AudioBufferList outList = {0};
            outList.mNumberBuffers = 1;
            outList.mBuffers[0].mNumberChannels = 1;
            outList.mBuffers[0].mDataByteSize = chunkFrames * sizeof(Float32);
            outList.mBuffers[0].mData = txConverterOutputChunk;

            UInt32 ioFrames = chunkFrames;
            OSStatus status = AudioConverterFillComplexBuffer(txConverter, txConverterInputProc, NULL, &ioFrames, &outList, NULL);
            if (status == ARDOP_AUDIOCONVERTER_NO_DATA || ioFrames == 0)
            {
                for (UInt32 frame = 0; frame < chunkFrames; ++frame)
                {
                    for (UInt32 ch = 0; ch < channelsPerFrame; ++ch)
                        outputBuffer[(frameOffset + frame) * channelsPerFrame + ch] = 0.0f;
                }
                if (ringbuf_count == 0 && tx_samples_pending() == 0)
                {
                    audioFinished = true;
                    audioPlaying = false;
                }
            }
            else
            {
                if (status != noErr)
                {
                    coreaudio_note_callback_error(false, "AudioConverterFill", status);
                    coreaudio_disable_tx_converter("converter runtime error");
                    usedConverter = false;
                    break;
                }
                for (UInt32 frame = 0; frame < ioFrames; ++frame)
                {
                    float sample = txConverterOutputChunk[frame];
                    for (UInt32 ch = 0; ch < channelsPerFrame; ++ch)
                        outputBuffer[(frameOffset + frame) * channelsPerFrame + ch] = sample;
                }
                for (UInt32 frame = ioFrames; frame < chunkFrames; ++frame)
                {
                    for (UInt32 ch = 0; ch < channelsPerFrame; ++ch)
                        outputBuffer[(frameOffset + frame) * channelsPerFrame + ch] = 0.0f;
                }
                if (ioFrames > 0)
                {
                    producedAudio = true;
                }
            }

            framesRemaining -= chunkFrames;
            frameOffset += chunkFrames;
        }
    }
#endif

    if (!usedConverter)
    {
        // Convert samples from txbuffer (16-bit signed) to float and copy to output
        // Handle sample rate conversion from 12kHz (ARDOP) to the device sample rate
        double srcPos = srcPosition; // Persisted across callbacks for deterministic SRC
        const double srcRate = 12000.0;
        const double dstRate = (coreAudioOutputSampleRate > 0.0) ? coreAudioOutputSampleRate : 48000.0;
        const double rateRatio = srcRate / dstRate;

        for (UInt32 frame = 0; frame < inNumberFrames; frame++)
        {
            // Linear interpolation SRC: for each output frame, compute position in input (ring buffer)
            int srcIndex0 = (int)srcPos;
            int srcIndex1 = srcIndex0 + 1;
            double frac = srcPos - srcIndex0;
            short s0 = 0, s1 = 0;
            if (ringbuf_count > srcIndex1)
            {
                int idx0 = (ringbuf_read + srcIndex0) % RINGBUF_SIZE;
                int idx1 = (ringbuf_read + srcIndex1) % RINGBUF_SIZE;
                s0 = ringbuf[idx0];
                s1 = ringbuf[idx1];
            }
            else if (ringbuf_count > srcIndex0)
            {
                int idx0 = (ringbuf_read + srcIndex0) % RINGBUF_SIZE;
                s0 = ringbuf[idx0];
                s1 = s0;
            }
            else
            {
                // Buffer underrun: output silence
                if (audioPlaying && !audioFinished)
                {
                    audioFinished = true;
                    audioPlaying = false;
                    if (ZF_LOG_ON_DEBUG)
                    {
                        ZF_LOGD("RenderCallback: All audio played (ring buffer empty), signaling audioFinished");
                    }
                }
                s0 = 0;
                s1 = 0;
            }
            short sample = (short)((1.0 - frac) * s0 + frac * s1);
            float floatSample = sample / 32768.0f;
            for (UInt32 ch = 0; ch < channelsPerFrame; ch++)
            {
                outputBuffer[frame * channelsPerFrame + ch] = floatSample;
            }
            srcPos += rateRatio;
            // When enough output frames have been produced to consume a source sample, advance ringbuf_read
            while (srcPos >= 1.0 && ringbuf_count > 0)
            {
                ringbuf_read = (ringbuf_read + 1) % RINGBUF_SIZE;
                ringbuf_count--;
                txSamplesPlayed++;
                srcPos -= 1.0;
                producedAudio = true;
            }
        }
        srcPosition = (float)srcPos;
    }

    if (producedAudio)
    {
        coreaudio_mark_tx_activity();
        coreAudioRecovery.consecutiveTxErrors = 0;
    }

    if (ringbuf_count == 0 && audioPlaying && !audioFinished && tx_samples_pending() == 0)
    {
        audioFinished = true;
        audioPlaying = false;
        if (ZF_LOG_ON_DEBUG)
        {
            ZF_LOGD("RenderCallback: TX ring buffer drained (%llu/%llu samples played)",
                    (unsigned long long)txSamplesPlayed, (unsigned long long)txSamplesQueued);
        }
    }

    // Monitor output buffer usage (non-verbose)
    int output_usage_percent = (ringbuf_count * 100) / RINGBUF_SIZE;
    if (output_usage_percent > coreAudioDiag.max_output_usage_percent)
    {
        coreAudioDiag.max_output_usage_percent = output_usage_percent;
    }

    return noErr;
}

void coreaudio_test_reset_tx_state(void)
{
    ringbuf_write = 0;
    ringbuf_read = 0;
    ringbuf_count = 0;
    tx_reset_counters();
    audioPlaying = false;
    audioFinished = false;
    srcPosition = 0.0f;
    SoundIsPlaying = false;
    audioUnitStarted = false;
    txReadIndex = 0;
}

void coreaudio_test_force_flush_idle(void)
{
    ringbuf_write = 0;
    ringbuf_read = 0;
    ringbuf_count = 0;
    tx_reset_counters();
    audioFinished = true;
    audioPlaying = false;
}

void coreaudio_test_reset_recovery_state(void)
{
    coreAudioRecovery.lastRxMs = 0;
    coreAudioRecovery.lastTxMs = 0;
    coreAudioRecovery.lastRxRecoverAttemptMs = 0;
    coreAudioRecovery.lastTxRecoverAttemptMs = 0;
    coreAudioRecovery.rxRecoveries = 0;
    coreAudioRecovery.txRecoveries = 0;
    coreAudioRecovery.consecutiveRxErrors = 0;
    coreAudioRecovery.consecutiveTxErrors = 0;
    coreAudioPendingRxRecovery = false;
    coreAudioPendingTxRecovery = false;
    coreAudioPendingRxReason = NULL;
    coreAudioPendingTxReason = NULL;
}

void coreaudio_test_set_recovery_activity(unsigned int rxMs, unsigned int txMs)
{
    coreAudioRecovery.lastRxMs = rxMs;
    coreAudioRecovery.lastTxMs = txMs;
}

void coreaudio_test_set_recovery_attempts(unsigned int rxAttemptMs, unsigned int txAttemptMs)
{
    coreAudioRecovery.lastRxRecoverAttemptMs = rxAttemptMs;
    coreAudioRecovery.lastTxRecoverAttemptMs = txAttemptMs;
}

unsigned int coreaudio_test_get_rx_recoveries(void)
{
    return coreAudioRecovery.rxRecoveries;
}

unsigned int coreaudio_test_get_tx_recoveries(void)
{
    return coreAudioRecovery.txRecoveries;
}

void coreaudio_test_set_consecutive_errors(int rxErrors, int txErrors)
{
    coreAudioRecovery.consecutiveRxErrors = rxErrors;
    coreAudioRecovery.consecutiveTxErrors = txErrors;
}

void coreaudio_test_set_now(unsigned int nowMs)
{
    testNowOverride = true;
    testNowValue = nowMs;
}

void coreaudio_test_clear_now_override(void)
{
    testNowOverride = false;
}

void coreaudio_test_run_watchdog(void)
{
    CheckAndRecoverAudioHealth();
}

unsigned int coreaudio_test_max_converter_errors(void)
{
    return CORE_AUDIO_MAX_CONVERTER_ERRORS;
}

unsigned int coreaudio_test_rx_timeout_ms(void)
{
    return CORE_AUDIO_RX_SILENCE_TIMEOUT_MS;
}

unsigned int coreaudio_test_tx_timeout_ms(void)
{
    return CORE_AUDIO_TX_STALL_TIMEOUT_MS;
}

unsigned int coreaudio_test_recovery_backoff_ms(void)
{
    return CORE_AUDIO_RECOVERY_BACKOFF_MS;
}

void coreaudio_test_set_last_devices(const char *capture, const char *playback)
{
    if (capture && capture[0] != '\0')
        snprintf(last_rx_dev, DEVSTRSZ, "%s", capture);
    else
        last_rx_dev[0] = '\0';

    if (playback && playback[0] != '\0')
        snprintf(last_tx_dev, DEVSTRSZ, "%s", playback);
    else
        last_tx_dev[0] = '\0';
}

void coreaudio_test_inject_converter_error(bool capture, int status)
{
#ifdef __APPLE__
    coreaudio_note_callback_error(capture, "test", (OSStatus)status);
#else
    (void)capture;
    (void)status;
#endif
}

void coreaudio_test_set_initialized(bool init)
{
    coreAudioInitialized = init;
    coreAudioOutputActive = init;
}

void coreaudio_test_bypass_audio_start(bool enable)
{
    testBypassAudioStart = enable;
}

void coreaudio_test_bypass_output_handle_check(bool enable)
{
    testBypassOutputHandleCheck = enable;
}

void coreaudio_test_bypass_audio_stop(bool enable)
{
    testBypassAudioStop = enable;
}

void coreaudio_test_bypass_recovery_restart(bool enable)
{
    testBypassRecoveryRestart = enable;
}

bool coreaudio_test_is_bypass_recovery_restart(void)
{
    return testBypassRecoveryRestart;
}

void coreaudio_test_bypass_recovery_reopen(bool enable)
{
    testBypassRecoveryReopen = enable;
}

uint64_t coreaudio_test_tx_pending(void)
{
    return tx_samples_pending();
}

uint64_t coreaudio_test_samples_played(void)
{
    return txSamplesPlayed;
}

int coreaudio_test_ringbuf_count(void)
{
    return ringbuf_count;
}

bool coreaudio_test_audio_finished(void)
{
    return audioFinished;
}

void coreaudio_test_override_output_samplerate(double rate)
{
    coreAudioOutputSampleRate = (rate > 0.0) ? rate : 48000.0;
}

double coreaudio_test_get_output_samplerate(void)
{
    return coreAudioOutputSampleRate;
}

void coreaudio_test_force_legacy_src(bool enable)
{
    testForceLegacySRC = enable;
}

void coreaudio_test_render(float *buffer, uint32_t frames, uint32_t channels)
{
#ifdef __APPLE__
    AudioBufferList list;
    AudioUnitRenderActionFlags flags = 0;
    list.mNumberBuffers = 1;
    list.mBuffers[0].mNumberChannels = channels;
    list.mBuffers[0].mDataByteSize = (UInt32)(frames * channels * sizeof(Float32));
    list.mBuffers[0].mData = buffer;
    renderCallback(NULL, &flags, NULL, 0, frames, &list);
#else
    (void)buffer;
    (void)frames;
    (void)channels;
#endif
}

// Check if we should report CoreAudio diagnostics (called periodically, reports once)
static void CheckAndReportCoreDiagnostics(void)
{
    // Report startup diagnostics once after sampling is complete
    if (coreAudioDiag.sampling_active && coreAudioDiag.sample_count >= 50 && !coreAudioDiag.diagnostics_reported)
    {
        coreAudioDiag.sampling_active = false; // Stop sampling
        coreAudioDiag.diagnostics_reported = true;

        if (coreAudioDiag.sample_count > 0)
        {
            UInt32 avg_frames = coreAudioDiag.frame_size_sum / coreAudioDiag.sample_count;

            // Report basic callback info (similar to Linux buffer info)
            double reportRate = coreAudioOutputActive ? coreAudioOutputSampleRate : coreAudioInputSampleRate;
            if (reportRate <= 0.0)
                reportRate = 48000.0;
            ZF_LOGI("CoreAudio: Audio callbacks using %u frame chunks at %.1fHz", avg_frames, reportRate);

            // Warn about significant frame size variation (like Linux period_size warnings)
            if (coreAudioDiag.frame_size_max > coreAudioDiag.frame_size_min * 2)
            {
                ZF_LOGW("##############################");
                ZF_LOGW("WARNING: CoreAudio callback frame size varies significantly (%u-%u frames)",
                        coreAudioDiag.frame_size_min, coreAudioDiag.frame_size_max);
                ZF_LOGW("##############################");
            }
        }
    }

    // Check for buffer usage warnings (threshold-based, like Linux buffer_size warnings)
    if (!coreAudioDiag.buffer_warning_shown &&
        (coreAudioDiag.max_output_usage_percent > 80 || coreAudioDiag.max_input_usage_percent > 80))
    {
        coreAudioDiag.buffer_warning_shown = true;
        ZF_LOGW("##############################");
        ZF_LOGW("WARNING: CoreAudio ring buffer usage exceeded 80%% (Output: %d%%, Input: %d%%)",
                coreAudioDiag.max_output_usage_percent, coreAudioDiag.max_input_usage_percent);
        ZF_LOGW("##############################");
    }
}

// Initialize (or reconfigure) the CoreAudio path for the current devices.
// captureDev / playbackDev are the canonical strings (may be "NOSOUND").
// This function is idempotent and safe to call after each open/close.
// Creates real AudioUnit instances and configures them for audio I/O.
static void InitCoreAudio(const char *captureDev, const char *playbackDev)
{
    bool wantInput = dev_enabled(captureDev);
    bool wantOutput = dev_enabled(playbackDev);

    ZF_LOGI("InitCoreAudio called: captureDev='%s', playbackDev='%s', wantInput=%d, wantOutput=%d",
            captureDev ? captureDev : "NULL", playbackDev ? playbackDev : "NULL", wantInput, wantOutput);

    if (!wantInput && !wantOutput)
    {
#ifdef __APPLE__
        DisposeTxConverter();
        DisposeRxConverter();
#endif
        if (coreAudioInitialized)
        {
            if (inputAudioUnit)
            {
                AudioUnitUninitialize(inputAudioUnit);
                AudioComponentInstanceDispose(inputAudioUnit);
                inputAudioUnit = NULL;
            }
            if (outputAudioUnit)
            {
                AudioUnitUninitialize(outputAudioUnit);
                AudioComponentInstanceDispose(outputAudioUnit);
                outputAudioUnit = NULL;
            }
            coreAudioInitialized = false;
            audioUnitStarted = false; // AudioUnits are disposed
            ZF_LOGI("CoreAudio: Disposed AudioUnits due to no enabled devices");
        }
        coreAudioInputActive = false;
        coreAudioOutputActive = false;
        ZF_LOGD("CoreAudio init: inputEnabled=%d outputEnabled=%d (no AudioUnits)", (int)coreAudioInputActive, (int)coreAudioOutputActive);
        return;
    }

#ifdef __APPLE__
    OSStatus status = noErr;
    AudioDeviceID resolvedInputDevice = kAudioObjectUnknown;
    AudioDeviceID resolvedOutputDevice = kAudioObjectUnknown;
    UInt32 outputDeviceChannels = 2;

    // Dispose existing AudioUnits if we have them
    if (coreAudioInitialized)
    {
        if (inputAudioUnit)
        {
            AudioUnitUninitialize(inputAudioUnit);
            AudioComponentInstanceDispose(inputAudioUnit);
            inputAudioUnit = NULL;
        }
        if (outputAudioUnit)
        {
            AudioUnitUninitialize(outputAudioUnit);
            AudioComponentInstanceDispose(outputAudioUnit);
            outputAudioUnit = NULL;
        }
        coreAudioInitialized = false;
        audioUnitStarted = false; // AudioUnits are disposed
    }

    // Create INPUT AudioUnit if needed
    if (wantInput)
    {
        AudioComponentDescription inputDesc = {0};
        inputDesc.componentType = kAudioUnitType_Output;
        inputDesc.componentSubType = kAudioUnitSubType_HALOutput; // HAL for input
        inputDesc.componentManufacturer = kAudioUnitManufacturer_Apple;

        AudioComponent inputComponent = AudioComponentFindNext(NULL, &inputDesc);
        if (!inputComponent)
        {
            ZF_LOGE("CoreAudio: Failed to find HAL input component");
            return;
        }

        status = AudioComponentInstanceNew(inputComponent, &inputAudioUnit);
        if (status != noErr || !inputAudioUnit)
        {
            ZF_LOGE("CoreAudio: Failed to create input AudioUnit instance (status=%d)", (int)status);
            return;
        }
        ZF_LOGD("CoreAudio: Created input AudioUnit");
    }

    // Create OUTPUT AudioUnit if needed
    if (wantOutput)
    {
        AudioComponentDescription outputDesc = {0};
        outputDesc.componentType = kAudioUnitType_Output;
        outputDesc.componentSubType = kAudioUnitSubType_HALOutput; // HAL for output
        outputDesc.componentManufacturer = kAudioUnitManufacturer_Apple;

        AudioComponent outputComponent = AudioComponentFindNext(NULL, &outputDesc);
        if (!outputComponent)
        {
            ZF_LOGE("CoreAudio: Failed to find HAL output component");
            if (inputAudioUnit)
            {
                AudioComponentInstanceDispose(inputAudioUnit);
                inputAudioUnit = NULL;
            }
            return;
        }

        status = AudioComponentInstanceNew(outputComponent, &outputAudioUnit);
        if (status != noErr || !outputAudioUnit)
        {
            ZF_LOGE("CoreAudio: Failed to create output AudioUnit instance (status=%d)", (int)status);
            if (inputAudioUnit)
            {
                AudioComponentInstanceDispose(inputAudioUnit);
                inputAudioUnit = NULL;
            }
            return;
        }
        ZF_LOGD("CoreAudio: Created output AudioUnit");
    }

    // Configure INPUT AudioUnit if we created one
    if (inputAudioUnit)
    {
        // Enable input on the HAL unit
        UInt32 enableInput = 1;
        status = AudioUnitSetProperty(inputAudioUnit, kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Input, 1, &enableInput, sizeof(enableInput));
        if (status != noErr)
        {
            ZF_LOGE("CoreAudio: Failed to enable input on input AudioUnit (status=%d)", (int)status);
            AudioComponentInstanceDispose(inputAudioUnit);
            inputAudioUnit = NULL;
            if (outputAudioUnit)
            {
                AudioComponentInstanceDispose(outputAudioUnit);
                outputAudioUnit = NULL;
            }
            return;
        }

        // Disable output on input AudioUnit (input-only)
        UInt32 disableOutput = 0;
        status = AudioUnitSetProperty(inputAudioUnit, kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Output, 0, &disableOutput, sizeof(disableOutput));
        if (status != noErr)
        {
            ZF_LOGW("CoreAudio: Failed to disable output on input AudioUnit (status=%d)", (int)status);
        }

        // Configure specific input device if we have one
        if (captureDev && captureDev[0] != '\0')
        {
            AudioDeviceID inputDeviceID = kAudioObjectUnknown;

            // Find the device ID for our capture device name
            UInt32 propSize = 0;
            AudioObjectPropertyAddress listAddr = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
            if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &listAddr, 0, NULL, &propSize) == noErr && propSize > 0)
            {
                UInt32 deviceCount = propSize / sizeof(AudioDeviceID);
                AudioDeviceID *deviceList = (AudioDeviceID *)malloc(propSize);
                if (deviceList && AudioObjectGetPropertyData(kAudioObjectSystemObject, &listAddr, 0, NULL, &propSize, deviceList) == noErr)
                {
                    for (UInt32 i = 0; i < deviceCount; i++)
                    {
                        CFStringRef deviceName = NULL;
                        UInt32 size = sizeof(CFStringRef);
                        AudioObjectPropertyAddress nameAddr = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
                        if (AudioObjectGetPropertyData(deviceList[i], &nameAddr, 0, NULL, &size, &deviceName) == noErr && deviceName)
                        {
                            char nameBuffer[256];
                            if (CFStringGetCString(deviceName, nameBuffer, sizeof(nameBuffer), kCFStringEncodingUTF8))
                            {
                                if (strcmp(nameBuffer, captureDev) == 0)
                                {
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

            if (inputDeviceID != kAudioObjectUnknown)
            {
                status = AudioUnitSetProperty(inputAudioUnit, kAudioOutputUnitProperty_CurrentDevice,
                                              kAudioUnitScope_Global, 0, &inputDeviceID, sizeof(inputDeviceID));
                if (status != noErr)
                {
                    ZF_LOGW("CoreAudio: Failed to set specific input device %u (status=%d)", (unsigned)inputDeviceID, (int)status);
                }
                else
                {
                    ZF_LOGD("CoreAudio: Set input device to %u ('%s')", (unsigned)inputDeviceID, captureDev);
                    resolvedInputDevice = inputDeviceID;
                }
            }
            else
            {
                ZF_LOGW("CoreAudio: Could not find device ID for capture device '%s'", captureDev);
            }
        }
    }

    // Configure OUTPUT AudioUnit if we created one
    if (outputAudioUnit)
    {
        // Enable output on the HAL unit
        UInt32 enableOutput = 1;
        status = AudioUnitSetProperty(outputAudioUnit, kAudioOutputUnitProperty_EnableIO,
                                      kAudioUnitScope_Output, 0, &enableOutput, sizeof(enableOutput));
        if (status != noErr)
        {
            ZF_LOGE("CoreAudio: Failed to enable output on output AudioUnit (status=%d)", (int)status);
            if (inputAudioUnit)
            {
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
        if (status != noErr)
        {
            ZF_LOGW("CoreAudio: Failed to disable input on output AudioUnit (status=%d)", (int)status);
        }
    }

    // Configure specific output device if we have one
    AudioDeviceID targetDeviceID = kAudioObjectUnknown;
    if (wantOutput && playbackDev && playbackDev[0] != '\0')
    {

        // Find the device ID for our playback device name
        UInt32 propSize = 0;
        AudioObjectPropertyAddress listAddr = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &listAddr, 0, NULL, &propSize) == noErr && propSize > 0)
        {
            UInt32 deviceCount = propSize / sizeof(AudioDeviceID);
            AudioDeviceID *deviceList = (AudioDeviceID *)malloc(propSize);
            if (deviceList && AudioObjectGetPropertyData(kAudioObjectSystemObject, &listAddr, 0, NULL, &propSize, deviceList) == noErr)
            {
                for (UInt32 i = 0; i < deviceCount; i++)
                {
                    CFStringRef deviceName = NULL;
                    UInt32 size = sizeof(CFStringRef);
                    AudioObjectPropertyAddress nameAddr = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
                    if (AudioObjectGetPropertyData(deviceList[i], &nameAddr, 0, NULL, &size, &deviceName) == noErr && deviceName)
                    {
                        char nameBuffer[256];
                        if (CFStringGetCString(deviceName, nameBuffer, sizeof(nameBuffer), kCFStringEncodingUTF8))
                        {
                            if (strcmp(nameBuffer, playbackDev) == 0)
                            {
                                targetDeviceID = deviceList[i];
                                ZF_LOGD("CoreAudio: Found device ID %u for '%s'", (unsigned)targetDeviceID, playbackDev);
                                break;
                            }
                        }
                        CFRelease(deviceName);
                    }
                }
            }
            free(deviceList);
        }

        if (targetDeviceID != kAudioObjectUnknown && outputAudioUnit)
        {
            status = AudioUnitSetProperty(outputAudioUnit, kAudioOutputUnitProperty_CurrentDevice,
                                          kAudioUnitScope_Global, 0, &targetDeviceID, sizeof(targetDeviceID));
            if (status != noErr)
            {
                ZF_LOGW("CoreAudio: Failed to set specific output device %u (status=%d)", (unsigned)targetDeviceID, (int)status);
            }
            else
            {
                ZF_LOGD("CoreAudio: Set output device to %u ('%s')", (unsigned)targetDeviceID, playbackDev);
                resolvedOutputDevice = targetDeviceID;
            }
        }
        else if (wantOutput)
        {
            ZF_LOGW("CoreAudio: Could not find device ID for playback device '%s'", playbackDev);
        }
    }

    // Determine active device IDs if CoreAudio chose defaults
    if (inputAudioUnit)
    {
        UInt32 size = sizeof(resolvedInputDevice);
        if (resolvedInputDevice == kAudioObjectUnknown &&
            AudioUnitGetProperty(inputAudioUnit, kAudioOutputUnitProperty_CurrentDevice,
                                 kAudioUnitScope_Global, 0, &resolvedInputDevice, &size) != noErr)
        {
            resolvedInputDevice = coreaudio_get_default_device(true);
        }

        AudioStreamBasicDescription deviceFormat = {0};
        size = sizeof(deviceFormat);
        coreAudioInputSampleRate = 48000.0;
        if (AudioUnitGetProperty(inputAudioUnit, kAudioUnitProperty_StreamFormat,
                                 kAudioUnitScope_Input, 1, &deviceFormat, &size) == noErr &&
            deviceFormat.mSampleRate > 0.0)
        {
            coreAudioInputSampleRate = deviceFormat.mSampleRate;
        }
        else
        {
            double rate = coreaudio_query_nominal_rate(resolvedInputDevice, kAudioDevicePropertyScopeInput);
            if (rate > 0.0)
                coreAudioInputSampleRate = rate;
        }

        if (coreAudioInputSampleRate <= 0.0)
            coreAudioInputSampleRate = 48000.0;

        ZF_LOGI("CoreAudio: Input device sample rate negotiated to %.1f Hz", coreAudioInputSampleRate);
    }
    else
    {
        coreAudioInputSampleRate = 48000.0;
    }

    if (outputAudioUnit)
    {
        UInt32 size = sizeof(resolvedOutputDevice);
        if (resolvedOutputDevice == kAudioObjectUnknown &&
            AudioUnitGetProperty(outputAudioUnit, kAudioOutputUnitProperty_CurrentDevice,
                                 kAudioUnitScope_Global, 0, &resolvedOutputDevice, &size) != noErr)
        {
            resolvedOutputDevice = coreaudio_get_default_device(false);
        }

        AudioStreamBasicDescription deviceFormat = {0};
        size = sizeof(deviceFormat);
        coreAudioOutputSampleRate = 48000.0;
        if (AudioUnitGetProperty(outputAudioUnit, kAudioUnitProperty_StreamFormat,
                                 kAudioUnitScope_Output, 0, &deviceFormat, &size) == noErr)
        {
            if (deviceFormat.mSampleRate > 0.0)
                coreAudioOutputSampleRate = deviceFormat.mSampleRate;
            if (deviceFormat.mChannelsPerFrame > 0)
                outputDeviceChannels = deviceFormat.mChannelsPerFrame;
        }
        else
        {
            double rate = coreaudio_query_nominal_rate(resolvedOutputDevice, kAudioDevicePropertyScopeOutput);
            if (rate > 0.0)
                coreAudioOutputSampleRate = rate;
        }

        if (coreAudioOutputSampleRate <= 0.0)
            coreAudioOutputSampleRate = 48000.0;
        if (outputDeviceChannels == 0)
            outputDeviceChannels = 2;

        ZF_LOGI("CoreAudio: Output device sample rate negotiated to %.1f Hz (%u channels)",
                coreAudioOutputSampleRate, (unsigned)outputDeviceChannels);
    }
    else
    {
        coreAudioOutputSampleRate = 48000.0;
        outputDeviceChannels = 2;
    }

    // Configure output if needed
    if (wantOutput)
    {
        coreAudioTxConverterAllowed = true;
        // Set up output format (float32, negotiated device rate/channel count)
        AudioStreamBasicDescription outputFormat = {0};
        outputFormat.mSampleRate = coreAudioOutputSampleRate;
        outputFormat.mFormatID = kAudioFormatLinearPCM;
        outputFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kLinearPCMFormatFlagIsPacked;
        outputFormat.mChannelsPerFrame = outputDeviceChannels;
        outputFormat.mFramesPerPacket = 1;
        outputFormat.mBitsPerChannel = 32;
        outputFormat.mBytesPerFrame = sizeof(Float32) * outputFormat.mChannelsPerFrame;
        outputFormat.mBytesPerPacket = outputFormat.mBytesPerFrame;

        status = AudioUnitSetProperty(outputAudioUnit, kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Input, 0, &outputFormat, sizeof(outputFormat));
        if (status != noErr)
        {
            ZF_LOGE("CoreAudio: Failed to set output format (status=%d)", (int)status);
        }
        else
        {
            ZF_LOGD("CoreAudio: Set output format: %.1fHz, %u channels, 32-bit float", outputFormat.mSampleRate, (unsigned)outputFormat.mChannelsPerFrame);
        }

        // Set render callback
        AURenderCallbackStruct renderCallbackStruct = {0};
        renderCallbackStruct.inputProc = renderCallback;
        renderCallbackStruct.inputProcRefCon = NULL;
        status = AudioUnitSetProperty(outputAudioUnit, kAudioUnitProperty_SetRenderCallback,
                                      kAudioUnitScope_Input, 0, &renderCallbackStruct, sizeof(renderCallbackStruct));
        if (status != noErr)
        {
            ZF_LOGE("CoreAudio: Failed to set render callback (status=%d)", (int)status);
            coreaudio_disable_tx_converter("render callback setup failed");
        }
    }
    // Configure input if needed
    if (wantInput)
    {
        // Set up input format (float32, mono, negotiated device rate)
        AudioStreamBasicDescription inputFormat = {0};
        inputFormat.mSampleRate = coreAudioInputSampleRate;
        inputFormat.mFormatID = kAudioFormatLinearPCM;
        inputFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian | kLinearPCMFormatFlagIsPacked;
        inputFormat.mChannelsPerFrame = 1;
        inputFormat.mFramesPerPacket = 1;
        inputFormat.mBitsPerChannel = 32;
        inputFormat.mBytesPerFrame = sizeof(Float32);
        inputFormat.mBytesPerPacket = inputFormat.mBytesPerFrame;
        status = AudioUnitSetProperty(inputAudioUnit, kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Output, 1, &inputFormat, sizeof(inputFormat));
        if (status != noErr)
        {
            ZF_LOGE("CoreAudio: Failed to set input format (status=%d)", (int)status);
        }
        else
        {
            ZF_LOGD("CoreAudio: Set input format: %.1fHz, %u channels, 32-bit float", inputFormat.mSampleRate, (unsigned)inputFormat.mChannelsPerFrame);
        }
        // Set input callback
        AURenderCallbackStruct inputCallbackStruct = {0};
        inputCallbackStruct.inputProc = inputCallback;
        inputCallbackStruct.inputProcRefCon = NULL;
        status = AudioUnitSetProperty(inputAudioUnit, kAudioOutputUnitProperty_SetInputCallback,
                                      kAudioUnitScope_Global, 0, &inputCallbackStruct, sizeof(inputCallbackStruct));
        if (status != noErr)
        {
            ZF_LOGE("CoreAudio: Failed to set input callback (status=%d)", (int)status);
        }
    }

    // Initialize the AudioUnits
    if (inputAudioUnit)
    {
        status = AudioUnitInitialize(inputAudioUnit);
        if (status != noErr)
        {
            ZF_LOGE("CoreAudio: Failed to initialize input AudioUnit (status=%d)", (int)status);
            AudioComponentInstanceDispose(inputAudioUnit);
            inputAudioUnit = NULL;
            if (outputAudioUnit)
            {
                AudioComponentInstanceDispose(outputAudioUnit);
                outputAudioUnit = NULL;
            }
            return;
        }
    }

    if (outputAudioUnit)
    {
        status = AudioUnitInitialize(outputAudioUnit);
        if (status != noErr)
        {
            ZF_LOGE("CoreAudio: Failed to initialize output AudioUnit (status=%d)", (int)status);
            if (inputAudioUnit)
            {
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
#ifdef __APPLE__
    RebuildAudioConverters();
#endif
    ZF_LOGD("CoreAudio: AudioUnit initialized successfully - inputEnabled=%d outputEnabled=%d",
            (int)coreAudioInputActive, (int)coreAudioOutputActive);

    // Verify callback was set (only if we have output)
    if (wantOutput && outputAudioUnit)
    {
        AURenderCallbackStruct verifyCallback;
        UInt32 verifySize = sizeof(verifyCallback);
        status = AudioUnitGetProperty(outputAudioUnit, kAudioUnitProperty_SetRenderCallback,
                                      kAudioUnitScope_Input, 0, &verifyCallback, &verifySize);
        if (status != noErr)
        {
            ZF_LOGE("CoreAudio: Failed to verify render callback (status=%d)", (int)status);
            coreaudio_disable_tx_converter("render callback verify failed");
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
static void ReinitCoreAudioIfNeeded(void)
{
    pthread_mutex_lock(&coreAudioMutex);
    bool wantInput = dev_enabled(CaptureDevice);
    bool wantOutput = dev_enabled(PlaybackDevice);
    bool deviceChanged = false;
    if (wantInput)
    {
        if (strncmp(CaptureDevice, lastCaptureDev, DEVSTRSZ - 1) != 0)
            deviceChanged = true;
    }
    else if (lastCaptureDev[0] != '\0')
    {
        // Was previously active, now disabled
        deviceChanged = true;
    }
    if (wantOutput)
    {
        if (strncmp(PlaybackDevice, lastPlaybackDev, DEVSTRSZ - 1) != 0)
            deviceChanged = true;
    }
    else if (lastPlaybackDev[0] != '\0')
    {
        deviceChanged = true;
    }
    bool flagsChanged = (wantInput != coreAudioInputActive) || (wantOutput != coreAudioOutputActive) || (wantInput == false && coreAudioInputActive) || (wantOutput == false && coreAudioOutputActive);

    if (!deviceChanged && !flagsChanged)
    {
        pthread_mutex_unlock(&coreAudioMutex);
        return; // No change
    }

    // Log transition summary (old->new) at DEBUG
    ZF_LOGD("CoreAudio reinit detail: inEnabled %d->%d outEnabled %d->%d capDev '%s'->'%s' playDev '%s'->'%s'",
            (int)coreAudioInputActive, (int)wantInput,
            (int)coreAudioOutputActive, (int)wantOutput,
            lastCaptureDev, CaptureDevice,
            lastPlaybackDev, PlaybackDevice);

    // Teardown sequence (stop -> uninit -> dispose)
    if (coreAudioInitialized)
    {
#ifdef __APPLE__
        DisposeTxConverter();
        DisposeRxConverter();
#endif
        // 1) Stop
        if (audioUnitStarted)
        {
            if (inputAudioUnit)
                AudioOutputUnitStop(inputAudioUnit);
            if (outputAudioUnit)
                AudioOutputUnitStop(outputAudioUnit);
            audioUnitStarted = false;
        }

        // 2) Uninitialize
        if (inputAudioUnit)
            AudioUnitUninitialize(inputAudioUnit);
        if (outputAudioUnit)
            AudioUnitUninitialize(outputAudioUnit);

        // 3) Dispose
        if (inputAudioUnit)
        {
            AudioComponentInstanceDispose(inputAudioUnit);
            inputAudioUnit = NULL;
        }
        if (outputAudioUnit)
        {
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

void GetDevices()
{
    FreeDevices(&AudioDevices);
    InitDevices(&AudioDevices);
    // Test hook: when ARDOP_TEST_SKIP_CA_ENUM is set (used by unit tests),
    // skip real CoreAudio enumeration to avoid dependency on host devices
    // and potential sandbox / CI issues. Only NOSOUND sentinel will be added.
    if (getenv("ARDOP_TEST_SKIP_CA_ENUM") != NULL)
    {
        ZF_LOGD("Skipping CoreAudio enumeration due to ARDOP_TEST_SKIP_CA_ENUM");
        goto add_nosound_only;
    }
#ifdef __APPLE__
    AudioDeviceID defIn = kAudioObjectUnknown;
    AudioDeviceID defOut = kAudioObjectUnknown;
    UInt32 size = sizeof(AudioDeviceID);
    AudioObjectPropertyAddress addrDefIn = {kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectPropertyAddress addrDefOut = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addrDefIn, 0, NULL, &size, &defIn) != noErr)
        defIn = kAudioObjectUnknown;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addrDefOut, 0, NULL, &size, &defOut) != noErr)
        defOut = kAudioObjectUnknown;
    AudioObjectPropertyAddress listAddr = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    size = 0;
    // First query the size of the device list. The previous call used
    // AudioObjectGetPropertyData with only 5 arguments which is invalid;
    // AudioObjectGetPropertyDataSize is the correct API to get the buffer size.
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &listAddr, 0, NULL, &size) != noErr || size == 0)
    {
        ZF_LOGW("CoreAudio: no devices returned; adding NOSOUND only");
    }
    else
    {
        UInt32 count = (UInt32)(size / sizeof(AudioDeviceID));
        AudioDeviceID *ids = (AudioDeviceID *)malloc(size);
        if (ids && AudioObjectGetPropertyData(kAudioObjectSystemObject, &listAddr, 0, NULL, &size, ids) == noErr)
        {
            // First pass: collect raw info so we can resolve duplicate names.
            typedef struct TmpDevInfo
            {
                AudioDeviceID did;
                bool hasIn;
                bool hasOut;
                char name[DEVSTRSZ];
                char uid[DEVSTRSZ];
                bool defIn;
                bool defOut;
            } TmpDevInfo;
            TmpDevInfo *tmp = calloc(count, sizeof(TmpDevInfo));
            UInt32 actual = 0;
            for (UInt32 i = 0; i < count; ++i)
            {
                AudioDeviceID did = ids[i];
                bool hasInput = false, hasOutput = false;
                AudioObjectPropertyAddress scAddrIn = {kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeInput, kAudioObjectPropertyElementMain};
                AudioObjectPropertyAddress scAddrOut = {kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMain};
                UInt32 scSize = 0;
                if (AudioObjectGetPropertyDataSize(did, &scAddrIn, 0, NULL, &scSize) == noErr && scSize > 0)
                {
                    AudioBufferList *abl = (AudioBufferList *)malloc(scSize);
                    if (abl && AudioObjectGetPropertyData(did, &scAddrIn, 0, NULL, &scSize, abl) == noErr)
                    {
                        for (UInt32 b = 0; b < abl->mNumberBuffers; ++b)
                            if (abl->mBuffers[b].mNumberChannels > 0)
                            {
                                hasInput = true;
                                break;
                            }
                    }
                    if (abl)
                        free(abl);
                }
                if (AudioObjectGetPropertyDataSize(did, &scAddrOut, 0, NULL, &scSize) == noErr && scSize > 0)
                {
                    AudioBufferList *abl = (AudioBufferList *)malloc(scSize);
                    if (abl && AudioObjectGetPropertyData(did, &scAddrOut, 0, NULL, &scSize, abl) == noErr)
                    {
                        for (UInt32 b = 0; b < abl->mNumberBuffers; ++b)
                            if (abl->mBuffers[b].mNumberChannels > 0)
                            {
                                hasOutput = true;
                                break;
                            }
                    }
                    if (abl)
                        free(abl);
                }
                if (!(hasInput || hasOutput))
                    continue;
                CFStringRef cfName = NULL;
                size = sizeof(CFStringRef);
                AudioObjectPropertyAddress nameAddr = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
                if (AudioObjectGetPropertyData(did, &nameAddr, 0, NULL, &size, &cfName) != noErr)
                    cfName = NULL;
                char nameBuf[DEVSTRSZ];
                nameBuf[0] = '\0';
                if (cfName)
                {
                    CFStringGetCString(cfName, nameBuf, sizeof(nameBuf), kCFStringEncodingUTF8);
                    CFRelease(cfName);
                }
                else
                {
                    snprintf(nameBuf, sizeof(nameBuf), "Device%u", (unsigned)i);
                }
                CFStringRef cfUID = NULL;
                size = sizeof(CFStringRef);
                AudioObjectPropertyAddress uidAddr = {kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
                if (AudioObjectGetPropertyData(did, &uidAddr, 0, NULL, &size, &cfUID) != noErr)
                    cfUID = NULL;
                char uidBuf[DEVSTRSZ];
                uidBuf[0] = '\0';
                if (cfUID)
                {
                    CFStringGetCString(cfUID, uidBuf, sizeof(uidBuf), kCFStringEncodingUTF8);
                    CFRelease(cfUID);
                }
                else
                {
                    snprintf(uidBuf, sizeof(uidBuf), "%s", nameBuf);
                }
                // Store into temp array for second pass duplicate resolution.
                strncpy(tmp[actual].name, nameBuf, DEVSTRSZ - 1);
                strncpy(tmp[actual].uid, uidBuf, DEVSTRSZ - 1);
                tmp[actual].hasIn = hasInput;
                tmp[actual].hasOut = hasOutput;
                tmp[actual].did = did;
                tmp[actual].defIn = (did == defIn);
                tmp[actual].defOut = (did == defOut);
                actual++;
            }
            // Second pass: detect duplicates; build final list.
            for (UInt32 i = 0; i < actual; ++i)
            {
                const char *baseName = tmp[i].name[0] ? tmp[i].name : tmp[i].uid;
                bool duplicate = false;
                for (UInt32 j = 0; j < actual; ++j)
                {
                    if (j == i)
                        continue;
                    if (strcmp(baseName, tmp[j].name[0] ? tmp[j].name : tmp[j].uid) == 0)
                    {
                        duplicate = true;
                        break;
                    }
                }
                char finalName[DEVSTRSZ];
                finalName[0] = '\0';
                if (!duplicate)
                {
                    snprintf(finalName, sizeof(finalName), "%s", baseName);
                }
                else
                {
                    // Use last 6 chars of UID (or full if shorter) to disambiguate.
                    size_t ulen = strlen(tmp[i].uid);
                    const char *suffix = tmp[i].uid;
                    if (ulen > 6)
                        suffix = tmp[i].uid + (ulen - 6);
                    snprintf(finalName, sizeof(finalName), "%s-%s", baseName, suffix);
                }
                char descBuf[DEVSTRSZ * 2];
                char defStr[32] = "";
                if (tmp[i].defIn && tmp[i].defOut)
                    snprintf(defStr, sizeof(defStr), " (default in/out)");
                else if (tmp[i].defIn)
                    snprintf(defStr, sizeof(defStr), " (default in)");
                else if (tmp[i].defOut)
                    snprintf(defStr, sizeof(defStr), " (default out)");
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
            if (tmp)
            {
                free(tmp);
                tmp = NULL;
            }
        }
        if (ids)
        {
            free(ids);
            ids = NULL;
        }
    }
#endif
add_nosound_only:; // Empty statement to satisfy C syntax
    int idx = ExtendDevices(&AudioDevices);
    if (idx >= 0)
    {
        DeviceInfo *dev = AudioDevices[idx];
        dev->name = strdup("NOSOUND");
        dev->desc = strdup("A dummy audio device for diagnostic use.");
        dev->capture = true;
        dev->playback = true;
    }
}

void InitAudio(bool quiet)
{
    (void)quiet;
    GetDevices();
    AudioInit = true;
    if (ZF_LOG_ON_DEBUG)
    {
        LogDevices(AudioDevices, "macOS audio devices", false, false);
    }
}

bool OpenSoundPlayback(char *devstr, int ch)
{
    if (devstr == NULL || devstr[0] == '\0')
    {
        CloseSoundPlayback(false);
        return false;
    }
    if (strcmp(devstr, "RESTORE") == 0)
    {
        if (last_tx_dev[0] == '\0')
        {
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
    if (strcmp(devstr, "NOSOUND") == 0)
    {
        TXEnabled = false;
        strncpy(PlaybackDevice, devstr, DEVSTRSZ - 1);
        PlaybackDevice[DEVSTRSZ - 1] = '\0';
        Pch = ch;
        ZF_LOGI("Playback NOSOUND sentinel selected (TX disabled)");
        ReinitCoreAudioIfNeeded();
        updateWebGuiAudioConfig(false);
        return true; // Success (no audio active by design)
    }
    // Accept any non-empty string (stub). Real implementation will validate.
    TXEnabled = true;
    strncpy(PlaybackDevice, devstr, DEVSTRSZ - 1);
    PlaybackDevice[DEVSTRSZ - 1] = '\0';
    Pch = ch;
    ZF_LOGI("Playback device '%s' opening (channels=%d)", PlaybackDevice, Pch);
    if (ZF_LOG_ON_DEBUG)
    {
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

bool OpenSoundCapture(char *devstr, int ch)
{
    if (devstr == NULL || devstr[0] == '\0')
    {
        CloseSoundCapture(false);
        return false;
    }
    if (strcmp(devstr, "RESTORE") == 0)
    {
        if (last_rx_dev[0] == '\0')
        {
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
    if (strcmp(devstr, "NOSOUND") == 0)
    {
        RXEnabled = false;
        RXSilent = true;
        strncpy(CaptureDevice, devstr, DEVSTRSZ - 1);
        CaptureDevice[DEVSTRSZ - 1] = '\0';
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
    CaptureDevice[DEVSTRSZ - 1] = '\0';
    Cch = ch;
    ZF_LOGI("Capture device '%s' opening (channels=%d)", CaptureDevice, Cch);
    if (ZF_LOG_ON_DEBUG)
    {
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

void CloseSoundPlayback(bool do_getdevices)
{
    char prev[DEVSTRSZ];
    snprintf(prev, sizeof(prev), "%s", PlaybackDevice);
    PlaybackDevice[0] = '\0';
    SoundIsPlaying = false;
    TXEnabled = false;
    ZF_LOGI("SoundFlush: called, will KeyPTT(false) and wait for audio to finish");
    KeyPTT(false);
    ReinitCoreAudioIfNeeded(); // Re-evaluate (may tear down AudioUnit if last direction)
    updateWebGuiAudioConfig(do_getdevices);
    if (prev[0])
        ZF_LOGI("Playback device '%s' closed", prev);
}

void CloseSoundCapture(bool do_getdevices)
{
    char prev[DEVSTRSZ];
    snprintf(prev, sizeof(prev), "%s", CaptureDevice);
    CaptureDevice[0] = '\0';
    RXEnabled = false;
    ReinitCoreAudioIfNeeded(); // Re-evaluate
    updateWebGuiAudioConfig(do_getdevices);
    if (prev[0])
        ZF_LOGI("Capture device '%s' closed", prev);
}

bool SendtoCard(int n)
{
    CheckAndRecoverAudioHealth();

    if (!TXEnabled || !coreAudioInitialized || (!outputAudioUnit && !testBypassOutputHandleCheck))
    {
        ZF_LOGW("SendtoCard: Cannot send - TXEnabled=%d, initialized=%d, outputAudioUnit=%p",
                TXEnabled, coreAudioInitialized, (void *)outputAudioUnit);
        return false;
    }
    // Copy n samples from txbuffer[TxIndex] into ring buffer
    int written = 0;
    bool resetCounters = (tx_samples_pending() == 0);
    if (resetCounters)
    {
        tx_reset_counters();
    }
    for (int i = 0; i < n; i++)
    {
        if (ringbuf_count >= RINGBUF_SIZE)
        {
            ZF_LOGE("SendtoCard: ring buffer overrun! Dropping audio sample.");
            break;
        }
        short sample = txbuffer[TxIndex][i];
        ringbuf[ringbuf_write] = sample;
        ringbuf_write = (ringbuf_write + 1) % RINGBUF_SIZE;
        ringbuf_count++;
        written++;
    }
    txSamplesQueued += (uint64_t)written;
    // (Debug logging removed for normal operation)
    // Start AudioUnit playback if not already playing
    if (!audioPlaying && written > 0)
    {
        if (!EnsureAudioUnitsStarted())
        {
            return false;
        }

        // Check for diagnostic reporting during TX as well
        CheckAndReportCoreDiagnostics();

        audioPlaying = true;
        audioFinished = false; // Reset finished flag
        srcPosition = 0.0f;    // Reset sample rate conversion position
        SoundIsPlaying = true;
        if (ZF_LOG_ON_DEBUG)
        {
            ZF_LOGD("CoreAudio: Started audio playback (ring buffer mode)");
        }
    }
    return true;
}

// Poll for received samples, perform SRC, and deliver 240-sample blocks to ProcessNewSamples
void PollReceivedSamples()
{
    CheckAndRecoverAudioHealth();

    // Only run if RX is enabled and input is active
    if (!RXEnabled || !coreAudioInputActive)
        return;

    // Ensure AudioUnits are started for input capture
    EnsureAudioUnitsStarted();

    // Check for diagnostic reporting (minimal, non-verbose)
    CheckAndReportCoreDiagnostics();
#ifdef __APPLE__
    if (txConverter && !testForceLegacySRC)
    {
        bool madeProgress = true;
        while (madeProgress)
        {
            madeProgress = false;
            UInt32 framesNeeded = ReceiveSize - rxblock_fill;
            if (framesNeeded == 0)
                framesNeeded = ReceiveSize;
            if (framesNeeded > RX_CONVERTER_MAX_OUTPUT_FRAMES)
                framesNeeded = RX_CONVERTER_MAX_OUTPUT_FRAMES;

            AudioBufferList outList = {0};
            outList.mNumberBuffers = 1;
            outList.mBuffers[0].mNumberChannels = 1;
            outList.mBuffers[0].mDataByteSize = framesNeeded * sizeof(SInt16);
            outList.mBuffers[0].mData = rxblock + rxblock_fill;

            UInt32 ioFrames = framesNeeded;
            OSStatus status = AudioConverterFillComplexBuffer(rxConverter, rxConverterInputProc, NULL, &ioFrames, &outList, NULL);
            if (status == ARDOP_AUDIOCONVERTER_NO_DATA || ioFrames == 0)
            {
                break;
            }

            rxblock_fill += ioFrames;
            madeProgress = true;
            while (rxblock_fill >= ReceiveSize)
            {
                ProcessNewSamples(rxblock, ReceiveSize);
                rxblock_fill -= ReceiveSize;
                if (rxblock_fill > 0)
                {
                    memmove(rxblock, rxblock + ReceiveSize, rxblock_fill * sizeof(short));
                }
            }
        }
        return;
    }
#endif

    // Legacy linear interpolation fallback when converters are unavailable
    static double srcPos = 0.0;
    const double srcRate = (coreAudioInputSampleRate > 0.0) ? coreAudioInputSampleRate : 48000.0;
    const double dstRate = 12000.0;
    const double rateRatio = srcRate / dstRate;
    while (inbuf_count > 4)
    {
        int srcIndex0 = (int)srcPos;
        int srcIndex1 = srcIndex0 + 1;
        float s0 = 0, s1 = 0;
        if (inbuf_count > srcIndex1)
        {
            int idx0 = (inbuf_read + srcIndex0) % INBUF_SIZE;
            int idx1 = (inbuf_read + srcIndex1) % INBUF_SIZE;
            s0 = inbuf[idx0];
            s1 = inbuf[idx1];
        }
        else if (inbuf_count > srcIndex0)
        {
            int idx0 = (inbuf_read + srcIndex0) % INBUF_SIZE;
            s0 = inbuf[idx0];
            s1 = s0;
        }
        else
        {
            break;
        }
        float fract = srcPos - srcIndex0;
        short sample = (short)(32767.0f * ((1.0 - fract) * s0 + fract * s1));
        rxblock[rxblock_fill++] = sample;
        if (rxblock_fill >= ReceiveSize)
        {
            ProcessNewSamples(rxblock, ReceiveSize);
            rxblock_fill = 0;
        }
        srcPos += rateRatio;
        while (srcPos >= 1.0 && inbuf_count > 0)
        {
            inbuf_read = (inbuf_read + 1) % INBUF_SIZE;
            inbuf_count--;
            srcPos -= 1.0;
        }
    }
}

void StopCapture()
{
    // Nothing
}

bool SoundFlush()
{
    // Wait for all staged audio to finish playing
    if (audioPlaying && (outputAudioUnit || testBypassOutputHandleCheck))
    {
        // Wait for the render callback to consume all the audio data
        uint64_t initialPending = tx_samples_pending();
        if ((uint64_t)ringbuf_count > initialPending)
            initialPending = (uint64_t)ringbuf_count;
        unsigned int waitStart = coreaudio_now_ms();
        unsigned int allowedWaitMs = CORE_AUDIO_FLUSH_TIMEOUT_MS;
        if (initialPending > 0)
        {
            unsigned int pendingMs = (unsigned int)((initialPending * 1000ULL) / 12000ULL);
            unsigned int dynamic = CORE_AUDIO_FLUSH_TIMEOUT_MS + pendingMs + CORE_AUDIO_FLUSH_MARGIN_MS;
            if (dynamic > CORE_AUDIO_FLUSH_MAX_TIMEOUT_MS)
                dynamic = CORE_AUDIO_FLUSH_MAX_TIMEOUT_MS;
            allowedWaitMs = dynamic;
        }

        while ((tx_samples_pending() > 0 || ringbuf_count > 0) && !audioFinished)
        {
            usleep(1000); // Sleep for 1ms
            unsigned int now = coreaudio_now_ms();
            if (now - waitStart > allowedWaitMs)
            {
                ZF_LOGW("SoundFlush: timeout waiting for TX buffer to drain (%llu pending samples)",
                        (unsigned long long)tx_samples_pending());
                coreaudio_request_recovery(false, "SoundFlush timeout");
                break;
            }
        }
#ifdef __APPLE__
        // Stop both AudioUnits
        if (inputAudioUnit && !testBypassAudioStop)
        {
            OSStatus status = AudioOutputUnitStop(inputAudioUnit);
            if (status != noErr)
            {
                ZF_LOGW("CoreAudio: Failed to stop input AudioUnit (status=%d)", (int)status);
            }
        }
        if (outputAudioUnit && !testBypassAudioStop)
        {
            OSStatus status = AudioOutputUnitStop(outputAudioUnit);
            if (status != noErr)
            {
                ZF_LOGW("CoreAudio: Failed to stop output AudioUnit (status=%d)", (int)status);
            }
        }
#endif
        audioPlaying = false;
        audioUnitStarted = false; // AudioUnit is now stopped
        SoundIsPlaying = false;
        // Reset ring buffer for next transmission
        ringbuf_read = 0;
        ringbuf_write = 0;
        ringbuf_count = 0;
        txReadIndex = 0;
        audioFinished = false;
        srcPosition = 0.0f;
        tx_reset_counters();
    }

    KeyPTT(false);

    return TXEnabled;
}

bool crestorable() { return last_rx_dev[0] != '\0'; }
bool prestorable() { return last_tx_dev[0] != '\0'; }
