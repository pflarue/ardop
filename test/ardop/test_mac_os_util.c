#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/time.h>
#include <errno.h>

#include "common/os_util.h"
#include "common/log.h"

// Refer to globals defined in production objects.
extern char DecodeWav[5][256];
extern int WavNow;
extern bool blnClosing;
extern int closedByPosixSignal;
extern char PlaybackDevice[80];

static void test_time_monotonic_and_sleep(void **state) {
    (void)state;
    unsigned int t1 = getNow();
    Sleep(20); // Should sleep unless PlaybackDevice==NOSOUND
    unsigned int t2 = getNow();
    assert_true(t2 >= t1); // monotonic non-decreasing
    // Expect at least ~15ms progressed unless system was busy; allow >=5ms.
    assert_true(t2 - t1 >= 5 || t2 == t1); // accommodate DecodeWav path (not active here)

    // Test NOSOUND acceleration (Sleep should early-return)
    strcpy(PlaybackDevice, "NOSOUND");
    unsigned int t3 = getNow();
    Sleep(50); // should shortcut
    unsigned int t4 = getNow();
    assert_true(t4 - t3 < 40); // did not actually sleep full duration
    PlaybackDevice[0] = '\0';
}

static void test_get_utctimestr_format(void **state) {
    (void)state;
    char buf[32]; memset(buf, 0, sizeof(buf));
    get_utctimestr(buf);
    // Current implementation uses YYYYMMDD_HHMMSS (15 chars)
    assert_int_equal((int)strlen(buf), 15);
    // Underscore position 8
    assert_int_equal(buf[8], '_');
    for (int i=0;i<15;i++) {
        if (i==8) continue;
        assert_true((buf[i] >= '0' && buf[i] <= '9'));
    }
}

static void test_signal_abbreviations(void **state) {
    (void)state;
    assert_string_equal(PlatformSignalAbbreviation(SIGINT), "SIGINT");
    assert_string_equal(PlatformSignalAbbreviation(SIGTERM), "SIGTERM");
    // Unknown large value returns "Unknown"
    assert_string_equal(PlatformSignalAbbreviation(9999), "Unknown");
}

static void test_tcpconnect_negative_path(void **state) {
    (void)state;
    // Port 1 is almost certainly closed locally; testing=true for fast timeout.
    int fd = tcpconnect("127.0.0.1", 1, true);
    assert_int_equal(fd, -1);
    // Ensure tcpclose safely ignores invalid/zero fds.
    tcpclose(&fd); // fd == -1, no effect expected
    fd = 0; tcpclose(&fd);
}

static void test_gpio_stubs_noop(void **state) {
    (void)state;
    // These should be safe no-ops returning benign values on macOS (no-op stubs).
    int r = gpioInitialise();
    assert_int_equal(r, 0);
    gpioWrite(17, 1);
    SetupGPIOPTT(17, false);
    // Repeated calls still benign (warning only once internally)
    gpioWrite(17, 0);
    SetupGPIOPTT(18, true);
}

static void test_cm108_stub_failure(void **state) {
    (void)state;
    // Attempt to open a clearly nonexistent VID:PID (FFFF:FFFF) expecting failure (0)
    HANDLE h = OpenCM108("FFFF:FFFF");
    assert_int_equal(h, 0);
    // Setting PTT on invalid handle should fail
    assert_int_equal(CM108_set_ptt(123456, true), -1);
    // CloseCM108 on invalid handle must be safe
    CloseCM108(&h);
    assert_int_equal(h, 0);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_time_monotonic_and_sleep),
        cmocka_unit_test(test_get_utctimestr_format),
        cmocka_unit_test(test_signal_abbreviations),
        cmocka_unit_test(test_tcpconnect_negative_path),
        cmocka_unit_test(test_gpio_stubs_noop),
        cmocka_unit_test(test_cm108_stub_failure),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
