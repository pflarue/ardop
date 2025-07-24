#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <cmocka.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <errno.h>

#include "setup.h"

// Simple type definitions without pulling in full ARDOP headers
#define HANDLE int
#define BOOL int
#define TRUE 1
#define FALSE 0

// Function prototypes for testing specific macOS functionality
// These are simplified versions that focus on testing the logic

// Serial port speed conversion test
struct speed_struct
{
    int user_speed;
    int termios_speed; // Using int instead of speed_t for simplicity
};

// Test data - common serial speeds
static struct speed_struct speed_table[] = {
    {300, 300},
    {600, 600},
    {1200, 1200},
    {2400, 2400},
    {4800, 4800},
    {9600, 9600},
    {19200, 19200},
    {38400, 38400},
    {57600, 57600},
    {115200, 115200},
    {-1, 0}};

// Simple function to test speed validation logic
int validate_serial_speed(int speed)
{
    struct speed_struct *s;
    for (s = speed_table; s->user_speed != -1; s++)
    {
        if (s->user_speed == speed)
        {
            return 1; // Valid speed
        }
    }
    return 0; // Invalid speed
}

// Test device name validation for macOS
int is_macos_device_name(const char *device)
{
    if (!device)
    {
        return 0;
    }

    // Check for typical macOS device prefixes
    if (strstr(device, "/dev/cu.") == device)
    {
        return 1;
    }

    return 0;
}

// Test CM108 device detection
int is_cm108_device(const char *device_id)
{
    if (!device_id)
    {
        return 0;
    }

    // Convert to uppercase for case-insensitive comparison
    char upper_id[256];
    strncpy(upper_id, device_id, sizeof(upper_id) - 1);
    upper_id[sizeof(upper_id) - 1] = '\0';

    for (int i = 0; upper_id[i]; i++)
    {
        if (upper_id[i] >= 'a' && upper_id[i] <= 'z')
        {
            upper_id[i] = upper_id[i] - 'a' + 'A';
        }
    }

    // Check for CM108 VID:PID patterns (case insensitive)
    if (strstr(upper_id, "0D8C:0008") ||
        strstr(upper_id, "0D8C:000C") ||
        strstr(upper_id, "CM108") ||
        (strstr(upper_id, "VID_0D8C") && strstr(upper_id, "PID_0008")) ||
        (strstr(upper_id, "VID_0D8C") && strstr(upper_id, "PID_000C")))
    {
        return 1;
    }

    return 0;
}

// Audio buffer management logic test
#define BUFFER_SIZE 1200
#define NUM_BUFFERS 4

typedef struct
{
    short buffers[NUM_BUFFERS][BUFFER_SIZE];
    int write_index;
    int read_index;
    int samples_available;
} audio_buffer_t;

void init_audio_buffer(audio_buffer_t *buf)
{
    if (!buf)
        return;

    memset(buf->buffers, 0, sizeof(buf->buffers));
    buf->write_index = 0;
    buf->read_index = 0;
    buf->samples_available = 0;
}

int write_audio_samples(audio_buffer_t *buf, short *samples, int count)
{
    if (!buf || !samples || count <= 0 || count > BUFFER_SIZE)
    {
        return 0;
    }

    if (buf->samples_available >= NUM_BUFFERS * BUFFER_SIZE)
    {
        return 0; // Buffer full
    }

    memcpy(buf->buffers[buf->write_index], samples, count * sizeof(short));
    buf->write_index = (buf->write_index + 1) % NUM_BUFFERS;
    buf->samples_available += count;

    return count;
}

int read_audio_samples(audio_buffer_t *buf, short *samples, int max_count)
{
    if (!buf || !samples || max_count <= 0)
    {
        return 0;
    }

    if (buf->samples_available == 0)
    {
        return 0; // No data available
    }

    int count = (max_count > BUFFER_SIZE) ? BUFFER_SIZE : max_count;
    memcpy(samples, buf->buffers[buf->read_index], count * sizeof(short));
    buf->read_index = (buf->read_index + 1) % NUM_BUFFERS;
    buf->samples_available -= count;

    return count;
}

// Platform utility function tests
const char *test_platform_signal_abbreviation(int sig)
{
    // Test implementation of PlatformSignalAbbreviation logic
    switch (sig)
    {
    case 1: return "HUP";    // SIGHUP
    case 2: return "INT";    // SIGINT
    case 3: return "QUIT";   // SIGQUIT
    case 4: return "ILL";    // SIGILL
    case 5: return "TRAP";   // SIGTRAP
    case 6: return "ABRT";   // SIGABRT
    case 7: return "EMT";    // SIGEMT
    case 8: return "FPE";    // SIGFPE
    case 9: return "KILL";   // SIGKILL
    case 10: return "BUS";   // SIGBUS
    case 11: return "SEGV";  // SIGSEGV
    case 12: return "SYS";   // SIGSYS
    case 13: return "PIPE";  // SIGPIPE
    case 14: return "ALRM";  // SIGALRM
    case 15: return "TERM";  // SIGTERM
    case 16: return "URG";   // SIGURG
    case 17: return "STOP";  // SIGSTOP
    case 18: return "TSTP";  // SIGTSTP
    case 19: return "CONT";  // SIGCONT
    case 20: return "CHLD";  // SIGCHLD
    case 21: return "TTIN";  // SIGTTIN
    case 22: return "TTOU";  // SIGTTOU
    case 23: return "IO";    // SIGIO
    case 24: return "XCPU";  // SIGXCPU
    case 25: return "XFSZ";  // SIGXFSZ
    case 26: return "VTALRM"; // SIGVTALRM
    case 27: return "PROF";  // SIGPROF
    case 28: return "WINCH"; // SIGWINCH
    case 29: return "INFO";  // SIGINFO
    case 30: return "USR1";  // SIGUSR1
    case 31: return "USR2";  // SIGUSR2
    default: return "UNKNOWN";
    }
}

// Mock timing functions for testing
unsigned int mock_get_ticks()
{
    static unsigned int mock_time = 1000;
    mock_time += 100; // Increment by 100ms each call
    return mock_time;
}

void mock_platform_sleep(int ms)
{
    // For testing, we just validate the parameter
    assert_true(ms >= 0);
    assert_true(ms <= 10000); // Reasonable upper bound
}

// Serial port control bit manipulation tests
int test_set_dtr_bit(int current_status)
{
    // Simulate DTR bit setting (TIOCM_DTR = 0x002)
    return current_status | 0x002;
}

int test_clear_dtr_bit(int current_status)
{
    // Simulate DTR bit clearing
    return current_status & ~0x002;
}

int test_set_rts_bit(int current_status)
{
    // Simulate RTS bit setting (TIOCM_RTS = 0x004)
    return current_status | 0x004;
}

int test_clear_rts_bit(int current_status)
{
    // Simulate RTS bit clearing
    return current_status & ~0x004;
}

// Test serial write retry logic
typedef struct {
    int total_bytes;
    int bytes_per_write;
    int error_on_attempt;
    int errno_value;
} write_scenario_t;

int simulate_write_com_block(write_scenario_t *scenario, int attempt)
{
    if (scenario->error_on_attempt == attempt) {
        errno = scenario->errno_value;
        return -1;
    }
    
    int remaining = scenario->total_bytes - (attempt * scenario->bytes_per_write);
    if (remaining <= 0) return 0;
    
    return (remaining >= scenario->bytes_per_write) ? scenario->bytes_per_write : remaining;
}

// Test cases

static void test_validate_serial_speed_valid_speeds(void **state)
{
    (void)state;

    int valid_speeds[] = {300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
    int num_speeds = sizeof(valid_speeds) / sizeof(valid_speeds[0]);

    for (int i = 0; i < num_speeds; i++)
    {
        int result = validate_serial_speed(valid_speeds[i]);
        assert_int_equal(result, 1);
    }
}

static void test_validate_serial_speed_invalid_speeds(void **state)
{
    (void)state;

    int invalid_speeds[] = {100, 500, 1000, 7200, 14400, 500000};
    int num_speeds = sizeof(invalid_speeds) / sizeof(invalid_speeds[0]);

    for (int i = 0; i < num_speeds; i++)
    {
        int result = validate_serial_speed(invalid_speeds[i]);
        assert_int_equal(result, 0);
    }
}

static void test_is_macos_device_name_valid(void **state)
{
    (void)state;

    const char *valid_devices[] = {
        "/dev/cu.usbserial-A12345",
        "/dev/cu.usbmodem-12345",
        "/dev/cu.Bluetooth-Serial-1",
        "/dev/cu.wchusbserial1234",
        "/dev/cu.SLAB_USBtoUART"};

    int num_devices = sizeof(valid_devices) / sizeof(valid_devices[0]);

    for (int i = 0; i < num_devices; i++)
    {
        int result = is_macos_device_name(valid_devices[i]);
        assert_int_equal(result, 1);
    }
}

static void test_is_macos_device_name_invalid(void **state)
{
    (void)state;

    const char *invalid_devices[] = {
        "/dev/ttyUSB0",
        "/dev/ttyACM0",
        "/dev/ttyS0",
        "COM1",
        "/dev/tty.serial",
        NULL};

    int num_devices = sizeof(invalid_devices) / sizeof(invalid_devices[0]);

    for (int i = 0; i < num_devices; i++)
    {
        int result = is_macos_device_name(invalid_devices[i]);
        assert_int_equal(result, 0);
    }
}

static void test_is_cm108_device_valid(void **state)
{
    (void)state;

    const char *valid_cm108[] = {
        "0d8c:0008",
        "0d8c:000c",
        "CM108",
        "USB\\VID_0D8C&PID_0008",
        "device_with_0d8c:0008_in_path"};

    int num_devices = sizeof(valid_cm108) / sizeof(valid_cm108[0]);

    for (int i = 0; i < num_devices; i++)
    {
        int result = is_cm108_device(valid_cm108[i]);
        if (result != 1)
        {
            printf("Failed CM108 detection for: %s\n", valid_cm108[i]);
        }
        assert_int_equal(result, 1);
    }
}

static void test_is_cm108_device_invalid(void **state)
{
    (void)state;

    const char *invalid_cm108[] = {
        "1234:5678",
        "0d8c:1234",
        "1234:0008",
        "random_device",
        NULL};

    int num_devices = sizeof(invalid_cm108) / sizeof(invalid_cm108[0]);

    for (int i = 0; i < num_devices; i++)
    {
        int result = is_cm108_device(invalid_cm108[i]);
        assert_int_equal(result, 0);
    }
}

static void test_audio_buffer_init(void **state)
{
    (void)state;

    audio_buffer_t buf;
    init_audio_buffer(&buf);

    assert_int_equal(buf.write_index, 0);
    assert_int_equal(buf.read_index, 0);
    assert_int_equal(buf.samples_available, 0);
}

static void test_audio_buffer_write_read(void **state)
{
    (void)state;

    audio_buffer_t buf;
    init_audio_buffer(&buf);

    // Test write
    short test_samples[100];
    for (int i = 0; i < 100; i++)
    {
        test_samples[i] = (short)(i * 10);
    }

    int written = write_audio_samples(&buf, test_samples, 100);
    assert_int_equal(written, 100);
    assert_int_equal(buf.samples_available, 100);

    // Test read
    short read_samples[100];
    int read_count = read_audio_samples(&buf, read_samples, 100);
    assert_int_equal(read_count, 100);
    assert_int_equal(buf.samples_available, 0);

    // Verify data integrity
    for (int i = 0; i < 100; i++)
    {
        assert_int_equal(read_samples[i], i * 10);
    }
}

static void test_audio_buffer_overflow(void **state)
{
    (void)state;

    audio_buffer_t buf;
    init_audio_buffer(&buf);

    short test_samples[BUFFER_SIZE];
    memset(test_samples, 0, sizeof(test_samples));

    // Fill all buffers
    for (int i = 0; i < NUM_BUFFERS; i++)
    {
        int written = write_audio_samples(&buf, test_samples, BUFFER_SIZE);
        assert_int_equal(written, BUFFER_SIZE);
    }

    // Try to write one more buffer (should fail)
    int written = write_audio_samples(&buf, test_samples, BUFFER_SIZE);
    assert_int_equal(written, 0);
}

static void test_audio_buffer_underflow(void **state)
{
    (void)state;

    audio_buffer_t buf;
    init_audio_buffer(&buf);

    short read_samples[100];

    // Try to read from empty buffer
    int read_count = read_audio_samples(&buf, read_samples, 100);
    assert_int_equal(read_count, 0);
}

static void test_audio_buffer_invalid_params(void **state)
{
    (void)state;

    audio_buffer_t buf;
    init_audio_buffer(&buf);

    short samples[100];

    // Test null buffer
    int result = write_audio_samples(NULL, samples, 100);
    assert_int_equal(result, 0);

    result = read_audio_samples(NULL, samples, 100);
    assert_int_equal(result, 0);

    // Test null samples
    result = write_audio_samples(&buf, NULL, 100);
    assert_int_equal(result, 0);

    result = read_audio_samples(&buf, NULL, 100);
    assert_int_equal(result, 0);

    // Test invalid count
    result = write_audio_samples(&buf, samples, 0);
    assert_int_equal(result, 0);

    result = write_audio_samples(&buf, samples, -1);
    assert_int_equal(result, 0);

    result = read_audio_samples(&buf, samples, 0);
    assert_int_equal(result, 0);
}

// New platform utility tests
static void test_platform_signal_abbreviation_common_signals(void **state)
{
    (void)state;

    // Test common signals
    assert_string_equal(test_platform_signal_abbreviation(1), "HUP");
    assert_string_equal(test_platform_signal_abbreviation(2), "INT");
    assert_string_equal(test_platform_signal_abbreviation(3), "QUIT");
    assert_string_equal(test_platform_signal_abbreviation(9), "KILL");
    assert_string_equal(test_platform_signal_abbreviation(11), "SEGV");
    assert_string_equal(test_platform_signal_abbreviation(15), "TERM");
}

static void test_platform_signal_abbreviation_unknown(void **state)
{
    (void)state;

    // Test unknown signals
    assert_string_equal(test_platform_signal_abbreviation(99), "UNKNOWN");
    assert_string_equal(test_platform_signal_abbreviation(-1), "UNKNOWN");
    assert_string_equal(test_platform_signal_abbreviation(0), "UNKNOWN");
}

static void test_mock_get_ticks_monotonic(void **state)
{
    (void)state;

    // Test that ticks are monotonic increasing
    unsigned int tick1 = mock_get_ticks();
    unsigned int tick2 = mock_get_ticks();
    unsigned int tick3 = mock_get_ticks();

    assert_true(tick2 > tick1);
    assert_true(tick3 > tick2);
    assert_int_equal(tick2 - tick1, 100);
    assert_int_equal(tick3 - tick2, 100);
}

static void test_platform_sleep_valid_params(void **state)
{
    (void)state;

    // Test valid sleep parameters
    mock_platform_sleep(0);
    mock_platform_sleep(1);
    mock_platform_sleep(100);
    mock_platform_sleep(1000);
    mock_platform_sleep(10000);
}

static void test_dtr_bit_manipulation(void **state)
{
    (void)state;

    int status = 0x0000;

    // Test setting DTR bit
    int new_status = test_set_dtr_bit(status);
    assert_int_equal(new_status & 0x002, 0x002);

    // Test clearing DTR bit
    status = 0x00FF; // All bits set
    new_status = test_clear_dtr_bit(status);
    assert_int_equal(new_status & 0x002, 0x000);

    // Test DTR doesn't affect other bits
    status = 0x00F8; // Other bits set
    new_status = test_set_dtr_bit(status);
    assert_int_equal(new_status & 0x00F8, 0x00F8); // Other bits preserved
}

static void test_rts_bit_manipulation(void **state)
{
    (void)state;

    int status = 0x0000;

    // Test setting RTS bit
    int new_status = test_set_rts_bit(status);
    assert_int_equal(new_status & 0x004, 0x004);

    // Test clearing RTS bit
    status = 0x00FF; // All bits set
    new_status = test_clear_rts_bit(status);
    assert_int_equal(new_status & 0x004, 0x000);

    // Test RTS doesn't affect other bits
    status = 0x00F8; // Other bits set
    new_status = test_set_rts_bit(status);
    assert_int_equal(new_status & 0x00F8, 0x00F8); // Other bits preserved
}

static void test_write_com_block_success(void **state)
{
    (void)state;

    write_scenario_t scenario = {
        .total_bytes = 100,
        .bytes_per_write = 50,
        .error_on_attempt = -1, // No error
        .errno_value = 0
    };

    // Simulate successful writes
    int result1 = simulate_write_com_block(&scenario, 0);
    assert_int_equal(result1, 50);

    int result2 = simulate_write_com_block(&scenario, 1);
    assert_int_equal(result2, 50);

    int result3 = simulate_write_com_block(&scenario, 2);
    assert_int_equal(result3, 0); // All bytes written
}

static void test_write_com_block_retry_logic(void **state)
{
    (void)state;

    write_scenario_t scenario = {
        .total_bytes = 100,
        .bytes_per_write = 50,
        .error_on_attempt = 0, // Error on first attempt
        .errno_value = 11 // EAGAIN
    };

    // First attempt should fail with EAGAIN
    int result1 = simulate_write_com_block(&scenario, 0);
    assert_int_equal(result1, -1);
    assert_int_equal(errno, 11);

    // Second attempt should succeed
    scenario.error_on_attempt = -1; // No more errors
    int result2 = simulate_write_com_block(&scenario, 1);
    assert_int_equal(result2, 50);
}

// Audio system edge case tests
typedef struct {
    int component_available;
    int instance_creation_success;
    int format_setting_success;
    int callback_setting_success;
    int initialization_success;
    int start_success;
} audio_init_scenario_t;

int simulate_audio_init(audio_init_scenario_t *scenario)
{
    if (!scenario->component_available) return -1;
    if (!scenario->instance_creation_success) return -2;
    if (!scenario->format_setting_success) return -3;
    if (!scenario->callback_setting_success) return -4;
    if (!scenario->initialization_success) return -5;
    if (!scenario->start_success) return -6;
    return 0; // Success
}

static void test_audio_init_component_not_found(void **state)
{
    (void)state;

    audio_init_scenario_t scenario = {
        .component_available = 0, // Component not found
        .instance_creation_success = 1,
        .format_setting_success = 1,
        .callback_setting_success = 1,
        .initialization_success = 1,
        .start_success = 1
    };

    int result = simulate_audio_init(&scenario);
    assert_int_equal(result, -1);
}

static void test_audio_init_instance_creation_failure(void **state)
{
    (void)state;

    audio_init_scenario_t scenario = {
        .component_available = 1,
        .instance_creation_success = 0, // Instance creation fails
        .format_setting_success = 1,
        .callback_setting_success = 1,
        .initialization_success = 1,
        .start_success = 1
    };

    int result = simulate_audio_init(&scenario);
    assert_int_equal(result, -2);
}

static void test_audio_init_format_setting_failure(void **state)
{
    (void)state;

    audio_init_scenario_t scenario = {
        .component_available = 1,
        .instance_creation_success = 1,
        .format_setting_success = 0, // Format setting fails
        .callback_setting_success = 1,
        .initialization_success = 1,
        .start_success = 1
    };

    int result = simulate_audio_init(&scenario);
    assert_int_equal(result, -3);
}

static void test_audio_init_complete_success(void **state)
{
    (void)state;

    audio_init_scenario_t scenario = {
        .component_available = 1,
        .instance_creation_success = 1,
        .format_setting_success = 1,
        .callback_setting_success = 1,
        .initialization_success = 1,
        .start_success = 1
    };

    int result = simulate_audio_init(&scenario);
    assert_int_equal(result, 0);
}

// Audio callback error handling tests
typedef struct {
    void *buffer_data;
    int buffer_size;
    int number_frames;
    int return_status;
} audio_callback_scenario_t;

int simulate_audio_callback(audio_callback_scenario_t *scenario)
{
    if (!scenario->buffer_data) return -1; // Null buffer
    if (scenario->buffer_size <= 0) return -2; // Invalid size
    if (scenario->number_frames <= 0) return -3; // Invalid frame count
    if (scenario->number_frames > 2048) return -4; // Too many frames
    
    return scenario->return_status;
}

static void test_audio_callback_null_buffer(void **state)
{
    (void)state;

    audio_callback_scenario_t scenario = {
        .buffer_data = NULL, // Null buffer
        .buffer_size = 1024,
        .number_frames = 256,
        .return_status = 0
    };

    int result = simulate_audio_callback(&scenario);
    assert_int_equal(result, -1);
}

static void test_audio_callback_invalid_parameters(void **state)
{
    (void)state;

    short dummy_buffer[1024];

    // Test invalid buffer size
    audio_callback_scenario_t scenario1 = {
        .buffer_data = dummy_buffer,
        .buffer_size = 0, // Invalid size
        .number_frames = 256,
        .return_status = 0
    };

    int result1 = simulate_audio_callback(&scenario1);
    assert_int_equal(result1, -2);

    // Test invalid frame count
    audio_callback_scenario_t scenario2 = {
        .buffer_data = dummy_buffer,
        .buffer_size = 1024,
        .number_frames = 0, // Invalid frame count
        .return_status = 0
    };

    int result2 = simulate_audio_callback(&scenario2);
    assert_int_equal(result2, -3);

    // Test too many frames
    audio_callback_scenario_t scenario3 = {
        .buffer_data = dummy_buffer,
        .buffer_size = 1024,
        .number_frames = 4096, // Too many frames
        .return_status = 0
    };

    int result3 = simulate_audio_callback(&scenario3);
    assert_int_equal(result3, -4);
}

static void test_audio_callback_success(void **state)
{
    (void)state;

    short dummy_buffer[1024];

    audio_callback_scenario_t scenario = {
        .buffer_data = dummy_buffer,
        .buffer_size = 1024,
        .number_frames = 256,
        .return_status = 0
    };

    int result = simulate_audio_callback(&scenario);
    assert_int_equal(result, 0);
}

// Audio buffer recovery tests
static void test_audio_buffer_recovery_after_overflow(void **state)
{
    (void)state;

    audio_buffer_t buf;
    init_audio_buffer(&buf);

    short test_samples[BUFFER_SIZE];
    memset(test_samples, 0, sizeof(test_samples));

    // Fill all buffers to overflow
    for (int i = 0; i < NUM_BUFFERS; i++)
    {
        write_audio_samples(&buf, test_samples, BUFFER_SIZE);
    }

    // Try to write one more (should fail)
    int overflow_result = write_audio_samples(&buf, test_samples, BUFFER_SIZE);
    assert_int_equal(overflow_result, 0);

    // Read some data to make space
    short read_buffer[BUFFER_SIZE];
    int read_result = read_audio_samples(&buf, read_buffer, BUFFER_SIZE);
    assert_int_equal(read_result, BUFFER_SIZE);

    // Should be able to write again after recovery
    int recovery_result = write_audio_samples(&buf, test_samples, BUFFER_SIZE);
    assert_int_equal(recovery_result, BUFFER_SIZE);
}

static void test_audio_buffer_partial_write_recovery(void **state)
{
    (void)state;

    audio_buffer_t buf;
    init_audio_buffer(&buf);

    short test_samples[BUFFER_SIZE];
    memset(test_samples, 0, sizeof(test_samples));

    // Fill buffer to near capacity
    for (int i = 0; i < NUM_BUFFERS - 1; i++)
    {
        write_audio_samples(&buf, test_samples, BUFFER_SIZE);
    }

    // Try to write oversized data (should handle gracefully)
    int partial_result = write_audio_samples(&buf, test_samples, BUFFER_SIZE * 2);
    assert_int_equal(partial_result, 0); // Should reject oversized write

    // Normal sized write should still work
    int normal_result = write_audio_samples(&buf, test_samples, BUFFER_SIZE);
    assert_int_equal(normal_result, BUFFER_SIZE);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        // Original tests
        cmocka_unit_test(test_validate_serial_speed_valid_speeds),
        cmocka_unit_test(test_validate_serial_speed_invalid_speeds),
        cmocka_unit_test(test_is_macos_device_name_valid),
        cmocka_unit_test(test_is_macos_device_name_invalid),
        cmocka_unit_test(test_is_cm108_device_valid),
        cmocka_unit_test(test_is_cm108_device_invalid),
        cmocka_unit_test(test_audio_buffer_init),
        cmocka_unit_test(test_audio_buffer_write_read),
        cmocka_unit_test(test_audio_buffer_overflow),
        cmocka_unit_test(test_audio_buffer_underflow),
        cmocka_unit_test(test_audio_buffer_invalid_params),
        
        // New platform utility tests
        cmocka_unit_test(test_platform_signal_abbreviation_common_signals),
        cmocka_unit_test(test_platform_signal_abbreviation_unknown),
        cmocka_unit_test(test_mock_get_ticks_monotonic),
        cmocka_unit_test(test_platform_sleep_valid_params),
        
        // New serial logic tests
        cmocka_unit_test(test_dtr_bit_manipulation),
        cmocka_unit_test(test_rts_bit_manipulation),
        cmocka_unit_test(test_write_com_block_success),
        cmocka_unit_test(test_write_com_block_retry_logic),
        
        // New audio system edge case tests
        cmocka_unit_test(test_audio_init_component_not_found),
        cmocka_unit_test(test_audio_init_instance_creation_failure),
        cmocka_unit_test(test_audio_init_format_setting_failure),
        cmocka_unit_test(test_audio_init_complete_success),
        cmocka_unit_test(test_audio_callback_null_buffer),
        cmocka_unit_test(test_audio_callback_invalid_parameters),
        cmocka_unit_test(test_audio_callback_success),
        cmocka_unit_test(test_audio_buffer_recovery_after_overflow),
        cmocka_unit_test(test_audio_buffer_partial_write_recovery),
    };

    ardop_test_setup();
    return cmocka_run_group_tests(tests, NULL, NULL);
}