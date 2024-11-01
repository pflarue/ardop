#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include <stdbool.h>

#include "setup.h"

#include "common/ARDOPC.h"

// check that needle contains haystack
#define assert_string_contains(needle, haystack) \
	assert_ptr_not_equal( \
		NULL, \
		strstr(needle, haystack) \
	)

bool parse_station_and_nattempts(
	const char* cmd,
	char* params,
	char* fault,
	size_t fault_size,
	StationId* target,
	long* nattempts
);

/* test string-packing compression and decompression */
static void test_parse_station_and_nattempts(void **state)
{
	(void)state; /* unused */

	const char COMMAND[] = "ARQCALL";

	char params[32] = "";
	char fault[64] = "";
	StationId target;
	long nattempts;

	// null parameters → error
	fault[0] = '\0';
	assert_false(
		parse_station_and_nattempts(
			COMMAND,
			NULL,
			fault,
			sizeof(fault),
			&target,
			&nattempts
		)
	);
	assert_string_contains(fault, "ARQCALL");
	assert_string_contains(fault, "expected \"TARGET NATTEMPTS\"");

	// no parameters → error
	params[0] = '\0';
	fault[0] = '\0';
	assert_false(
		parse_station_and_nattempts(
			COMMAND,
			params,
			fault,
			sizeof(fault),
			&target,
			&nattempts
		)
	);
	assert_string_contains(fault, "expected \"TARGET NATTEMPTS\"");

	// no nattempts → error
	snprintf(params, sizeof(params), "N0CALL-A");
	fault[0] = '\0';
	assert_false(
		parse_station_and_nattempts(
			COMMAND,
			params,
			fault,
			sizeof(fault),
			&target,
			&nattempts
		)
	);
	assert_string_contains(fault, "expected \"TARGET NATTEMPTS\"");

	// bad station ID → error
	snprintf(params, sizeof(params), "A 5");
	fault[0] = '\0';
	assert_false(
		parse_station_and_nattempts(
			COMMAND,
			params,
			fault,
			sizeof(fault),
			&target,
			&nattempts
		)
	);
	assert_string_contains(fault, "invalid TARGET");

	// non-numeric nattempts → error
	snprintf(params, sizeof(params), "N0CALL-A 5X");
	fault[0] = '\0';
	assert_false(
		parse_station_and_nattempts(
			COMMAND,
			params,
			fault,
			sizeof(fault),
			&target,
			&nattempts
		)
	);
	assert_string_contains(fault, "NATTEMPTS not valid as number");

	// non-positive nattempts → error
	snprintf(params, sizeof(params), "N0CALL-A 0");
	fault[0] = '\0';
	assert_false(
		parse_station_and_nattempts(
			COMMAND,
			params,
			fault,
			sizeof(fault),
			&target,
			&nattempts
		)
	);
	assert_string_contains(fault, "NATTEMPTS must be positive");

	// OK
	snprintf(params, sizeof(params), "N0CALL-A 1");
	fault[0] = '\0';
	assert_true(
		parse_station_and_nattempts(
			COMMAND,
			params,
			fault,
			sizeof(fault),
			&target,
			&nattempts
		)
	);
	assert_int_equal('\0', fault[0]);
	assert_int_equal(1, nattempts);
	assert_string_equal(target.str, "N0CALL-A");
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_parse_station_and_nattempts)
	};

	ardop_test_setup();
	return cmocka_run_group_tests(tests, NULL, NULL);
}
