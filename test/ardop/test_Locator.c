#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include <stdio.h>
#include <stdlib.h>

#include "setup.h"

#include "common/Locator.h"

static void test_default_locator_bytes(void **state) {
	(void)state; /* unused */

	// calculate byte sequence used for invalid grid square
	Packed6 nogs;
	assert_true(packed6_from_str("NO GS", &nogs));

	// ensure this is the default wireline representation
	const Packed6* b = locator_as_bytes(NULL);
	assert_memory_equal(&nogs, b, sizeof(Packed6));

	// and for unset grid squares, too
	Locator empty;
	locator_init(&empty);
	assert_false(locator_is_populated(&empty));
	b = locator_as_bytes(&empty);
	assert_memory_equal(&nogs, b, sizeof(Packed6));
}

/* test reading grid squares from strings */
static void test_locator_from_str(void **state)
{
	(void)state; /* unused */

	const static Packed6 PZERO = {{
		0, 0, 0, 0, 0, 0
	}};

	Locator loc;

	locator_init(&loc);
	assert_false(locator_is_populated(&loc));

	// the default locator has wireline memory of all zeros
	assert_memory_equal(&loc.wire, &PZERO, sizeof(loc.wire));

	// grid 0
	assert_int_equal(LOCATOR_OK, locator_from_str("", &loc));
	assert_string_equal(loc.grid, "");
	assert_false(locator_is_populated(&loc));
	assert_memory_equal(&loc.wire, &PZERO, sizeof(loc.wire));

	// grid 2
	assert_int_equal(LOCATOR_OK, locator_from_str("ar", &loc));
	assert_true(locator_is_populated(&loc));
	assert_string_equal(loc.grid, "AR");

	// grid 4
	assert_int_equal(LOCATOR_OK, locator_from_str("BL11", &loc));
	assert_true(locator_is_populated(&loc));
	assert_string_equal(loc.grid, "BL11");

	// grid 6
	assert_int_equal(LOCATOR_OK, locator_from_str("BL11bh", &loc));
	assert_true(locator_is_populated(&loc));
	assert_string_equal(loc.grid, "BL11bh");

	// grid 8
	assert_int_equal(LOCATOR_OK, locator_from_str("ar11ax16", &loc));
	assert_true(locator_is_populated(&loc));
	assert_string_equal(loc.grid, "AR11ax16");
}

static void test_locator_reject_toolong(void** state) {
	Locator loc;

	assert_int_equal(LOCATOR_ERR_TOOLONG, locator_from_str("BL11BH16KK", &loc));
	assert_false(locator_is_populated(&loc));
}

static void test_locator_reject_oddlength(void** state) {
	Locator loc;

	assert_int_equal(LOCATOR_ERR_FMT_LENGTH, locator_from_str("B", &loc));
	assert_false(locator_is_populated(&loc));

	assert_int_equal(LOCATOR_ERR_FMT_LENGTH, locator_from_str("BL1", &loc));
	assert_false(locator_is_populated(&loc));

	assert_int_equal(LOCATOR_ERR_FMT_LENGTH, locator_from_str("BL11B", &loc));
	assert_false(locator_is_populated(&loc));

	assert_int_equal(LOCATOR_ERR_FMT_LENGTH, locator_from_str("BL11BH1", &loc));
	assert_false(locator_is_populated(&loc));
}

static void test_locator_reject_badrange(void** state) {
	Locator loc;

	// letter out of range
	assert_int_equal(LOCATOR_ERR_FMT_FIELD, locator_from_str("BS", &loc));
	assert_false(locator_is_populated(&loc));
	assert_int_equal(LOCATOR_ERR_FMT_FIELD, locator_from_str("SB", &loc));
	assert_false(locator_is_populated(&loc));
	assert_int_equal(LOCATOR_ERR_FMT_SUBSQUARE, locator_from_str("AA11AY", &loc));
	assert_false(locator_is_populated(&loc));
	assert_int_equal(LOCATOR_ERR_FMT_SUBSQUARE, locator_from_str("AA11YA", &loc));
	assert_false(locator_is_populated(&loc));

	// letter out of range (lowercase)
	assert_int_equal(LOCATOR_ERR_FMT_FIELD, locator_from_str("sb", &loc));
	assert_false(locator_is_populated(&loc));
	assert_int_equal(LOCATOR_ERR_FMT_FIELD, locator_from_str("bs", &loc));
	assert_false(locator_is_populated(&loc));
	assert_int_equal(LOCATOR_ERR_FMT_SUBSQUARE, locator_from_str("aa11ay", &loc));
	assert_false(locator_is_populated(&loc));
	assert_int_equal(LOCATOR_ERR_FMT_SUBSQUARE, locator_from_str("aa11ya", &loc));
	assert_false(locator_is_populated(&loc));

	// letter when want number
	assert_int_equal(LOCATOR_ERR_FMT_SQUARE, locator_from_str("BL1A", &loc));
	assert_false(locator_is_populated(&loc));

	// symbol when want number
	assert_int_equal(LOCATOR_ERR_FMT_SQUARE, locator_from_str("BL1/", &loc));
	assert_false(locator_is_populated(&loc));

	// number when want letter
	assert_int_equal(LOCATOR_ERR_FMT_FIELD, locator_from_str("A1", &loc));
	assert_false(locator_is_populated(&loc));

	// bad extended square
	assert_int_equal(LOCATOR_ERR_FMT_EXTSQUARE, locator_from_str("AA11AAzz", &loc));
	assert_false(locator_is_populated(&loc));
}

// there are several wire sequences for the "unpopulated" grid
// square, and they should all be silently accepted as "no grid."
//
// one of these includes legacy ARDOPC's packed-6 encoding
// of "`No GS`." The astute will note that this sequence used
// a lowercase `o`. The legacy encoder did not encode this
// correctly.
static void test_locator_accept_unpopulated(void** state) {
	const static Packed6 EMPTY = {{
		0, 0, 0, 0, 0, 0
	}};

	const static Packed6 NOGS_1 = {{
		0xbc, 0xf0, 0x27, 0xcc, 0x00, 0x00
	}};

	const static Packed6 NOGS_2 = {{
		0xba, 0xf0, 0x27, 0xcc, 0x00, 0x00
	}};

	Locator loc;

	assert_int_equal(LOCATOR_OK, locator_from_bytes(EMPTY.b, &loc));
	assert_false(locator_is_populated(&loc));
	assert_memory_equal(&loc.wire, &EMPTY, sizeof(loc.wire));

	assert_int_equal(LOCATOR_OK, locator_from_bytes(NOGS_1.b, &loc));
	assert_false(locator_is_populated(&loc));
	assert_memory_equal(&loc.wire, &EMPTY, sizeof(loc.wire));

	assert_int_equal(LOCATOR_OK, locator_from_bytes(NOGS_2.b, &loc));
	assert_false(locator_is_populated(&loc));
	assert_memory_equal(&loc.wire, &EMPTY, sizeof(loc.wire));
}

static void test_locator_from_bytes(void** state) {
	const static char* TESTGRID[] = {
		"AA",
		"BL11",
		"BH16kk",
		"BH16kk00",
	};

	Locator out;
	Locator inp;
	for (size_t gr = 0; gr < sizeof(TESTGRID)/sizeof(TESTGRID[0]); ++gr) {
		// test roundtrip out→in
		assert_int_equal(LOCATOR_OK, locator_from_str(TESTGRID[gr], &out));
		assert_int_equal(
			LOCATOR_OK,
			locator_from_bytes((const uint8_t*)locator_as_bytes(&out), &inp)
		);

		// data should be identical
		assert_memory_equal(&inp, &out, sizeof(inp));
	}
}

static void test_locator_strerror(void** state) {
	(void)state; /* unused */

	assert_string_equal("unknown error", locator_strerror(0));
	assert_string_equal("unknown error", locator_strerror(LOCATOR_ERR_MAX_));
	assert_string_equal("unknown error", locator_strerror(LOCATOR_ERR_MAX_ + 1));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_default_locator_bytes),
		cmocka_unit_test(test_locator_from_str),
		cmocka_unit_test(test_locator_reject_toolong),
		cmocka_unit_test(test_locator_reject_oddlength),
		cmocka_unit_test(test_locator_reject_badrange),
		cmocka_unit_test(test_locator_accept_unpopulated),
		cmocka_unit_test(test_locator_from_bytes),
		cmocka_unit_test(test_locator_strerror),
	};

	ardop_test_setup();
	return cmocka_run_group_tests(tests, NULL, NULL);
}
