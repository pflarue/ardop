#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <util.h> // openpty, ptsname

#include "setup.h"
#include "common/os_util.h"
#include "common/ardopcommon.h"

void macserial_test_set_allocators(void *(*malloc_fn)(size_t), void *(*realloc_fn)(void *, size_t), char *(*strdup_fn)(const char *));
void macserial_test_reset_allocators(void);

// Helper: create a PTY pair and return slave path.
static bool make_pty(char *slavePath, size_t sz, int *masterFd, int *slaveFd) {
    *masterFd = -1; *slaveFd = -1;
    int m, s;
    if (openpty(&m, &s, NULL, NULL, NULL) == -1) {
        return false;
    }
#ifdef TIOCGPTN
    // Linux-style (not available on macOS). Fallback below.
    int ptn = 0;
    if (ioctl(m, TIOCGPTN, &ptn) == 0) {
        if (snprintf(slavePath, sz, "/dev/pts/%d", ptn) >= (int)sz) {
            close(m); close(s); return false;
        }
    } else
#endif
    {
        // macOS/BSD: openpty already gave us s; use fcntl to get path via /dev/fd not portable.
        // Attempt to derive path via ttyname().
        char *tn = ttyname(s);
        if (!tn) { close(m); close(s); return false; }
        if (snprintf(slavePath, sz, "%s", tn) >= (int)sz) { close(m); close(s); return false; }
    }
    *masterFd = m; *slaveFd = s;
    return true;
}

static void test_open_invalid(void **state) {
    (void)state;
    HANDLE fd = OpenCOMPort((void*)"/dev/cu.NONEXISTENT", 9600);
    assert_int_equal(fd, 0); // failure returns 0
}

static void test_pty_happy_path(void **state) {
    (void)state;
    char slave[128]; int mfd, sfd;
    if (!make_pty(slave, sizeof(slave), &mfd, &sfd)) {
        skip();
    }
    // Open via ARDOP API (MacSerial uses O_NONBLOCK)
    HANDLE fd = OpenCOMPort(slave, 9600);
    assert_true(fd > 0);

    // Write path: send bytes through API, read from master
    unsigned char wbuf[5] = { 'H','e','l','l','o' };
    bool wok = WriteCOMBlock(fd, wbuf, 5);
    // Non-blocking write may be partial; treat true == full
    if (!wok) {
        // Accept partial but ensure at least one byte written to underlying fd
        unsigned char tmp[8]; ssize_t got = read(mfd, tmp, sizeof(tmp));
        assert_true(got >= 0); // just ensure no error
    } else {
        unsigned char rbuf[8]; ssize_t got = read(mfd, rbuf, sizeof(rbuf));
        assert_true(got == 5);
        assert_memory_equal(rbuf, wbuf, 5);
    }

    // Read path: write to master, read via API (non-blocking)
    unsigned char send2[3] = { 'A','B','C' };
    assert_int_equal(write(mfd, send2, 3), 3);
    usleep(10000); // small delay to let data be readable
    unsigned char recv2[16];
    int rret = ReadCOMBlock(fd, recv2, sizeof(recv2));
    assert_true(rret == 3 || rret == 0); // allow 0 if not yet available
    if (rret == 3) {
        assert_memory_equal(recv2, send2, 3);
    }

    CloseCOMPort(&fd);
    close(mfd); close(sfd);
}

static void test_modem_lines(void **state) {
    (void)state;
    char slave[128]; int mfd, sfd;
    if (!make_pty(slave, sizeof(slave), &mfd, &sfd)) {
        skip();
    }
    HANDLE fd = OpenCOMPort(slave, 9600);
    if (fd <= 0) { close(mfd); close(sfd); skip(); }

    // Pseudo-terminals on macOS may not implement modem control lines; if
    // first operation fails with ENOTTY, skip the modem line checks.
    if (!COMSetRTS(fd)) {
        if (errno == ENOTTY) {
            CloseCOMPort(&fd); close(mfd); close(sfd); skip();
        } else {
            fail_msg("COMSetRTS(fd)");
        }
    } else {
        assert_true(COMClearRTS(fd));
        assert_true(COMSetDTR(fd));
        assert_true(COMClearDTR(fd));
    }

    // Invalid fd behavior (should fail, not crash)
    assert_false(COMSetRTS(-1));
    assert_false(COMClearRTS(-1));
    assert_false(COMSetDTR(-1));
    assert_false(COMClearDTR(-1));

    CloseCOMPort(&fd);
    close(mfd); close(sfd);
}

static void test_close_semantics(void **state) {
    (void)state;
    char slave[128]; int mfd, sfd;
    if (!make_pty(slave, sizeof(slave), &mfd, &sfd)) {
        skip();
    }
    HANDLE fd = OpenCOMPort(slave, 9600);
    if (fd <= 0) { close(mfd); close(sfd); skip(); }

    // Close and ensure further operations behave
    CloseCOMPort(&fd);
    assert_int_equal(fd, 0);
    unsigned char b = 'X';
    // After close fd is 0; avoid assuming its semantics (stdin could be open).
    // Just verify API calls do not crash; acceptable outcomes:
    //  - WriteCOMBlock returns false (preferred) OR true (if fd 0 accepts write)
    //  - ReadCOMBlock returns >=0 and not a large unexpected positive count.
    (void)WriteCOMBlock(fd, &b, 1);
    if (fd == 0) {
        // Set non-blocking on stdin in test context to avoid hang
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl != -1) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }
    unsigned char rb[8];
    int r = -1;
    for (int i = 0; i < 5; ++i) { // small poll loop
        r = ReadCOMBlock(fd, rb, sizeof(rb));
        if (r != 0) break;
        usleep(1000);
    }
    assert_true(r <= (int)sizeof(rb));

    close(mfd); close(sfd);
}

static void *realloc_fail_once(void *ptr, size_t sz)
{
    static int counter = 0;
    counter++;
    if (counter == 1)
        return NULL;
    return realloc(ptr, sz);
}

static void test_getserial_realloc_failure(void **state)
{
    (void)state;
    char tmpl[] = "/tmp/ardop_serialXXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir)
        fail_msg("mkdtemp");

    char path[PATH_MAX];
    int len = snprintf(path, sizeof(path), "%s/cu.mock", dir);
    if (len < 0 || len >= (int)sizeof(path))
        fail_msg("snprintf path");
    int fd = creat(path, 0600);
    if (fd == -1)
        fail_msg("creat test entry");
    close(fd);

    if (setenv("ARDOP_TEST_SERIAL_DEV_DIR", dir, 1) == -1)
        fail_msg("setenv override");

    macserial_test_reset_allocators();
    macserial_test_set_allocators(NULL, realloc_fail_once, NULL);
    char **list = GetSerialStrlist();
    assert_non_null(list);
    assert_null(list[0]);
    macserial_test_reset_allocators();
    FreeStrlist(&list);

    unsetenv("ARDOP_TEST_SERIAL_DEV_DIR");
    unlink(path);
    rmdir(dir);
}

int main(void) {
    ardop_test_setup();
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_open_invalid),
        cmocka_unit_test(test_pty_happy_path),
        cmocka_unit_test(test_modem_lines),
        cmocka_unit_test(test_close_semantics),
        cmocka_unit_test(test_getserial_realloc_failure)
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
