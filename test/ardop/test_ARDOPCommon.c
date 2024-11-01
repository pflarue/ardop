#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "setup.h"

#include "common/ardopcommon.h"

/* test callsign round-trip through the protocol */
static void test_try_parse_long(void** state)
{
	(void)state; /* unused */

	long val = 1000;

	assert_false(try_parse_long("", &val));
	assert_int_equal(val, 0);

	val = 1000;
	assert_false(try_parse_long(NULL, &val));
	assert_int_equal(val, 0);

	val = 1000;
	assert_true(try_parse_long("0", &val));
	assert_int_equal(val, 0);

	assert_true(try_parse_long("-4", &val));
	assert_int_equal(val, -4);

	assert_false(try_parse_long("4.", &val));
	assert_int_equal(val, 4);

	assert_false(try_parse_long("a", &val));
	assert_int_equal(val, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_try_parse_long),
	};

	ardop_test_setup();
	return cmocka_run_group_tests(tests, NULL, NULL);
}
