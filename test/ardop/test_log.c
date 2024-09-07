#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include <limits.h>
#include <stdio.h>
#include <time.h>

#include "setup.h"

#include "common/log.h"
#include "common/log_file.h"

// These are unused, but ardop_test_setup() needs them
int FileLogLevel;
int ConsoleLogLevel;

int ardop_log_level_filter(const int level);

/* Mocks */

FILE* __wrap_fopen(
	const char* restrict pathname,
	const char* restrict mode
) {
	check_expected_ptr(pathname);
	return mock_ptr_type(FILE*);
};

FILE* __wrap_freopen(
	const char* restrict pathname,
	const char* restrict mode,
	FILE* restrict handle
) {
	check_expected_ptr(pathname);
	return mock_ptr_type(FILE*);
};

int __wrap_fclose(FILE* handle) {
	return (int)mock();
};

size_t __wrap_fwrite(
	const void* data,
	size_t size,
	size_t nmemb,
	FILE* stream) {

	check_expected_ptr(data);
	check_expected(size);
	assert_int_equal(nmemb, 1);

	return size;
}

int __wrap_fflush(FILE* stream) {
	return 0;
}

/* test filename calculation */
static void test_ardop_logfile_calc_filename(void** state)
{
	// Sat Aug  3 10:37:16 PM UTC 2024
	const time_t UNIX_TIME = 1722724636LL;

	(void)state; /* unused */

	char filename[64] = "";

	ArdopLogFile uut;
	ardop_logfile_init(&uut, "", "mylog", ".txt", 6663);

	bool ok = ardop_logfile_calc_filename(&uut, UNIX_TIME, filename, sizeof(filename));
	assert_true(ok);
	assert_string_equal(filename, "mylog6663_20240803.txt");

	// with overflow
	ok = ardop_logfile_calc_filename(&uut, UNIX_TIME, filename, 4);
	assert_false(ok);

	// with directory
	ardop_logfile_init(&uut, "out", "mylog", ".txt", 6663);
	ok = ardop_logfile_calc_filename(&uut, UNIX_TIME, filename, sizeof(filename));
	assert_true(ok);
	assert_string_equal(filename, "out" ARDOP_PLATFORM_PATHSEP "mylog6663_20240803.txt");

	// with directory, already have trailing pathsep
	ardop_logfile_init(&uut, "out" ARDOP_PLATFORM_PATHSEP, "mylog", ".txt", 6663);
	ok = ardop_logfile_calc_filename(&uut, UNIX_TIME, filename, sizeof(filename));
	assert_true(ok);
	assert_string_equal(filename, "out" ARDOP_PLATFORM_PATHSEP "mylog6663_20240803.txt");
}

static void test_ardop_logfile_need_rollover(void** state) {
	(void)state; /* unused */

	// same time → no new file
	assert_false(ardop_logfile_need_rollover(0, 0));

	// no rollover
	assert_false(ardop_logfile_need_rollover(0, 86399));
	assert_false(ardop_logfile_need_rollover(-86400, -86399));

	// day rollover
	assert_true(ardop_logfile_need_rollover(-1, 0));
	assert_true(ardop_logfile_need_rollover(86399, 86400));
	assert_true(ardop_logfile_need_rollover(-86401, -86400));

	// day rollover the wrong way, but we don't care
	assert_true(ardop_logfile_need_rollover(0, -1));
}

static void test_ardop_logfile_handle(void** state) {
	// Sat Aug  3 10:37:16 PM UTC 2024
	const time_t UNIX_TIME = 1722724636LL;

	// Sun Aug  4 10:37:16 AM UTC 2024 (twelve hours later)
	const time_t UNIX_TIME_LATER = 1722767836LL;

	(void)state; /* unused */

	ArdopLogFile uut;
	ardop_logfile_init(&uut, "", "ARDOPDebug", ".log", 0);

	FILE* out;

	// no port → no file
	out = ardop_logfile_handle(&uut, UNIX_TIME);
	assert_false(!!out);

	// port → open file
	uut.port = 2048;
	expect_string(__wrap_fopen, pathname, "ARDOPDebug2048_20240803.log");
	will_return(__wrap_fopen, 1);

	out = ardop_logfile_handle(&uut, UNIX_TIME);
	assert_int_equal((uintptr_t)out, 1);

	// file is unchanged since the day has not yet cycled
	out = ardop_logfile_handle(&uut, UNIX_TIME + 800);
	assert_int_equal((uintptr_t)out, 1);

	// open fails at the rollover
	expect_string(__wrap_freopen, pathname, "ARDOPDebug2048_20240804.log");
	will_return(__wrap_freopen, 0);

	out = ardop_logfile_handle(&uut, UNIX_TIME_LATER);
	assert_false(!!out);

	// we do not make any further attempts to open logs
	out = ardop_logfile_handle(&uut, UNIX_TIME_LATER + 86402);
	assert_false(!!out);

	// until we reset
	ardop_logfile_close(&uut);

	expect_string(__wrap_fopen, pathname, "ARDOPDebug2048_20240804.log");
	will_return(__wrap_fopen, 2);

	out = ardop_logfile_handle(&uut, UNIX_TIME_LATER);
	assert_int_equal((uintptr_t)out, 2);

	// time can flow backwards, too
	expect_string(__wrap_freopen, pathname, "ARDOPDebug2048_20240803.log");
	will_return(__wrap_freopen, 3);

	out = ardop_logfile_handle(&uut, UNIX_TIME);
	assert_int_equal((uintptr_t)out, 3);

	// cleanup
	will_return(__wrap_fclose, 0);
	ardop_logfile_close(&uut);
	assert_int_equal((uintptr_t)uut.handle, 0);
}

static void test_ardop_logfile_write(void** state) {
	(void)state; /* unused */

	const static char MSG[] = "x";

	ArdopLogFile uut;
	ardop_logfile_init(&uut, "", "ARDOPDebug", ".log", 8515);

	// no file is open, so this fails
	bool rv = ardop_logfile_write_norotate(&uut, MSG, sizeof(MSG));
	assert_false(rv);

	// next file open will succeed
	expect_any(__wrap_fopen, pathname);
	will_return(__wrap_fopen, 1);

	// this causes a file open and write
	expect_value(__wrap_fwrite, data, (uintptr_t)MSG);
	expect_value(__wrap_fwrite, size, sizeof(MSG));
	rv = ardop_logfile_write(&uut, MSG, sizeof(MSG));
	assert_true(rv);

	// this causes a write but not open
	expect_value(__wrap_fwrite, data, (uintptr_t)MSG);
	expect_value(__wrap_fwrite, size, sizeof(MSG));
	rv = ardop_logfile_write_norotate(&uut, MSG, sizeof(MSG));
	assert_true(rv);
}

static void test_ardop_log_verbosity(void** state) {
	const static int LEVELS[] = {
		ZF_LOG_VERBOSE,
		ZF_LOG_DEBUG,
		ZF_LOG_INFO,
		ZF_LOG_WARN,
		ZF_LOG_ERROR,
		ZF_LOG_FATAL
	};
	const size_t NUM_LEVELS = sizeof(LEVELS)/sizeof(LEVELS[0]);

	(void)state; /* unused */

	// ensure that levels can be assigned
	for (size_t lvl = 0; lvl < NUM_LEVELS; ++lvl) {
		assert_int_equal(LEVELS[lvl], ardop_log_level_filter(LEVELS[lvl]));
	}

	assert_int_equal(ZF_LOG_VERBOSE, ardop_log_level_filter(0));
	assert_int_equal(ZF_LOG_NONE, ardop_log_level_filter(ZF_LOG_FATAL + 1));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_ardop_logfile_calc_filename),
		cmocka_unit_test(test_ardop_logfile_need_rollover),
		cmocka_unit_test(test_ardop_logfile_handle),
		cmocka_unit_test(test_ardop_logfile_write),
		cmocka_unit_test(test_ardop_log_verbosity)
	};

	ardop_test_setup();
	return cmocka_run_group_tests(tests, NULL, NULL);
}
