#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <stdbool.h>

#include <math.h>
#include "setup.h"

#include "common/audio.h"
#include "common/ardopcommon.h"

void coreaudio_test_reset_tx_state(void);
void coreaudio_test_set_initialized(bool init);
void coreaudio_test_bypass_audio_start(bool enable);
void coreaudio_test_bypass_output_handle_check(bool enable);
void coreaudio_test_bypass_audio_stop(bool enable);
uint64_t coreaudio_test_tx_pending(void);
uint64_t coreaudio_test_samples_played(void);
int coreaudio_test_ringbuf_count(void);
bool coreaudio_test_audio_finished(void);
void coreaudio_test_override_output_samplerate(double rate);
double coreaudio_test_get_output_samplerate(void);
void coreaudio_test_render(float *buffer, uint32_t frames, uint32_t channels);
#include "common/ARDOPC.h"

// Forward declarations (from CoreAudioSound.c) for RESTORE helpers
bool crestorable();
bool prestorable();

static void reset_audio_state(void) {
    // Minimal reset: avoid invoking Close* prior to any initialization since
    // those paths may call into WebGUI update logic not required for these
    // unit tests. We just clear device names and disable flags.
    CaptureDevice[0] = '\0';
    PlaybackDevice[0] = '\0';
    RXEnabled = false;
    TXEnabled = false;
}

static void prepare_tx_test(void)
{
    reset_audio_state();
    InitAudio(true);
    coreaudio_test_reset_tx_state();
    coreaudio_test_set_initialized(true);
    coreaudio_test_bypass_audio_start(true);
    coreaudio_test_bypass_output_handle_check(true);
    coreaudio_test_bypass_audio_stop(true);
    TXEnabled = true;
}

static void cleanup_tx_test(void)
{
    SoundFlush();
    TXEnabled = false;
    coreaudio_test_bypass_audio_start(false);
    coreaudio_test_bypass_output_handle_check(false);
    coreaudio_test_bypass_audio_stop(false);
    coreaudio_test_set_initialized(false);
    coreaudio_test_override_output_samplerate(48000.0);
    coreaudio_test_reset_tx_state();
}

static int count_devices(bool *has_nosound) {
    int count = 0; *has_nosound = false;
    if (AudioDevices) {
        for (int i = 0; AudioDevices[i] != NULL; ++i) {
            ++count;
            if (AudioDevices[i]->name && strcmp(AudioDevices[i]->name, "NOSOUND") == 0)
                *has_nosound = true;
        }
    }
    return count;
}

static void test_device_list_contains_nosound(void **state) {
    (void)state;
    reset_audio_state();
    InitAudio(true); // quiet
    bool has_nosound = false;
    int n = count_devices(&has_nosound);
    assert_true(n > 0); // Non-empty list
    assert_true(has_nosound); // NOSOUND sentinel present
}

static void test_open_close_nosound_idempotent(void **state) {
    (void)state;
    reset_audio_state();
    InitAudio(true);
    assert_true(OpenSoundCapture("NOSOUND", 1));
    assert_false(RXEnabled); // NOSOUND should disable RX
    assert_string_equal(CaptureDevice, "NOSOUND");
    // Open again (idempotent expectation: still disabled, no crash)
    assert_true(OpenSoundCapture("NOSOUND", 1));
    assert_false(RXEnabled);
    assert_true(OpenSoundPlayback("NOSOUND", 1));
    assert_false(TXEnabled); // NOSOUND should disable TX
    assert_string_equal(PlaybackDevice, "NOSOUND");
    assert_true(OpenSoundPlayback("NOSOUND", 1));
    assert_false(TXEnabled);
    // Close twice (idempotent)
    CloseSoundCapture(false);
    CloseSoundCapture(false);
    CloseSoundPlayback(false);
    CloseSoundPlayback(false);
}

static void test_exclusive_modes_rx_only_then_tx_only(void **state) {
    (void)state;
    reset_audio_state();
    InitAudio(true);
    // RX only (use dummy device name distinct from NOSOUND to enable RX)
    assert_true(OpenSoundCapture("DummyMic", 1));
    // In stub path, presence of device string implies logical selection.
    assert_string_equal(CaptureDevice, "DummyMic");
    assert_false(TXEnabled); // TX still unopened
    // Switch to TX only
    CloseSoundCapture(false);
    assert_false(RXEnabled);
    assert_true(OpenSoundPlayback("DummySpk", 1));
    assert_string_equal(PlaybackDevice, "DummySpk");
}

static void test_restore_capture_and_playback(void **state) {
    (void)state;
    reset_audio_state();
    InitAudio(true);
    // Open real (dummy) devices
    assert_true(OpenSoundCapture("MicA", 1));
    assert_true(OpenSoundPlayback("SpkA", 1));
    assert_string_equal(CaptureDevice, "MicA");
    assert_string_equal(PlaybackDevice, "SpkA");
    // Switch both to NOSOUND (disabled)
    assert_true(OpenSoundCapture("NOSOUND", 1));
    // Disabled path indicated by NOSOUND sentinel
    assert_true(crestorable());
    assert_true(OpenSoundPlayback("NOSOUND", 1));
    // Disabled path indicated by NOSOUND sentinel
    assert_true(prestorable());
    // RESTORE should return to MicA / SpkA with flags enabled
    assert_true(OpenSoundCapture("RESTORE", 1));
    assert_string_equal(CaptureDevice, "MicA");
    assert_true(OpenSoundPlayback("RESTORE", 1));
    assert_string_equal(PlaybackDevice, "SpkA");
}

static void test_error_handling_invalid_device(void **state) {
    (void)state;
    reset_audio_state();
    InitAudio(true);
    // Empty string considered invalid by stub (returns false, leaves disabled)
    assert_false(OpenSoundCapture("", 1));
    assert_false(RXEnabled);
    assert_false(OpenSoundPlayback("", 1));
    assert_false(TXEnabled);
    // Closing unopened is safe
    CloseSoundCapture(false);
    CloseSoundPlayback(false);
}

static void test_tx_ringbuffer_render_drains(void **state)
{
    (void)state;
    prepare_tx_test();
    const short pattern[4] = {1000, 2000, 3000, 4000};
    memcpy(txbuffer[0], pattern, sizeof(pattern));
    assert_true(SendtoCard(4));
    assert_int_equal(coreaudio_test_ringbuf_count(), 4);
    assert_int_equal(coreaudio_test_tx_pending(), 4);
    float out[32] = {0};
    coreaudio_test_render(out, 32, 1);
    assert_int_equal(coreaudio_test_ringbuf_count(), 0);
    assert_int_equal(coreaudio_test_tx_pending(), 0);
    assert_int_equal(coreaudio_test_samples_played(), 4);
    assert_true(coreaudio_test_audio_finished());
    cleanup_tx_test();
}

static void test_tx_respects_output_rate(void **state)
{
    (void)state;
    prepare_tx_test();
    coreaudio_test_override_output_samplerate(24000.0);
    assert_float_equal((float)coreaudio_test_get_output_samplerate(), 24000.0f, 0.001f);
    const short pattern[2] = {12000, -12000};
    memcpy(txbuffer[0], pattern, sizeof(pattern));
    assert_true(SendtoCard(2));
    float out[8] = {0};
    coreaudio_test_render(out, 8, 1);
    assert_int_equal(coreaudio_test_samples_played(), 2);
    assert_int_equal(coreaudio_test_ringbuf_count(), 0);
    assert_int_equal(coreaudio_test_tx_pending(), 0);
    assert_true(fabsf(out[0] - (pattern[0] / 32768.0f)) < 1e-6f);
    cleanup_tx_test();
}

int main(void) {
    // Ensure CoreAudio enumeration is skipped for deterministic tests.
    setenv("ARDOP_TEST_SKIP_CA_ENUM", "1", 1);
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_device_list_contains_nosound),
        cmocka_unit_test(test_open_close_nosound_idempotent),
        cmocka_unit_test(test_exclusive_modes_rx_only_then_tx_only),
        cmocka_unit_test(test_restore_capture_and_playback),
        cmocka_unit_test(test_error_handling_invalid_device),
        cmocka_unit_test(test_tx_ringbuffer_render_drains),
        cmocka_unit_test(test_tx_respects_output_rate)
    };
    ardop_test_setup();
    return cmocka_run_group_tests(tests, NULL, NULL);
}
