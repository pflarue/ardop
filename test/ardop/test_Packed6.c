#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "setup.h"

#include "common/Packed6.h"

/*
 * Check that C string `inp` compresses and decompresses as
 * `expect`. The `expect` output will always be exactly 8 characters,
 * with any unused characters right-padded with spaces (ASCII 32).
 *
 * For cases where the compression should not succeed, set
 * `succeed` to false.
 */
static void assert_6bit_inout(const char *inp, const char *expect, bool succeed)
{
	Packed6 compressed;
	char out[9];

	if (succeed)
	{
		assert_true(packed6_from_str(inp, &compressed));
		assert_true(packed6_to_str(&compressed, out, sizeof(out)));
		assert_string_equal(out, expect);
	}
	else
	{
		assert_false(packed6_from_str(inp, &compressed));
		assert_true(packed6_to_str(&compressed, out, sizeof(out)));
		assert_string_equal(out, expect);
	}
}

/* test string-packing compression and decompression */
static void test_ascii_6bit(void **state)
{
	(void)state; /* unused */

	assert_6bit_inout("        ", "        ", true);
	assert_6bit_inout("AZ09-_  ", "AZ09-_  ", true);
	assert_6bit_inout("A      Z", "A      Z", true);
	assert_6bit_inout("HI\0\0\0\0\0U", "HI      ", true);
	assert_6bit_inout("abcxyz  ", "ABCXYZ  ", true);

	// empty input → spaces
	assert_6bit_inout("", "        ", true);
	assert_6bit_inout(NULL, "        ", true);

	// strings which are too long are truncated
	assert_6bit_inout("TOOLONGSTRING", "TOOLONGS", false);

	// invalid characters truncate
	assert_6bit_inout("FAIL~D", "FAIL D  ", false);
	assert_6bit_inout("{NO}", " NO     ", false);
}

/* test Packed6 construction from bytes */
static void test_packed6_from_bytes(void** state)
{
	(void)state; /* unused */

	Packed6 pkout;
	Packed6 pkin;

	assert_true(packed6_from_str("ABCXYZ", &pkout));
	packed6_from_bytes(pkout.b, &pkin);
	assert_memory_equal(&pkout, &pkin, sizeof(Packed6));

	char out[9] = {0};
	assert_true(packed6_to_str(&pkin, out, sizeof(out)));
	assert_string_equal("ABCXYZ  ", out);

	memset(out, 0, sizeof(out));
	packed6_to_fixed_str(&pkin, out);
	assert_string_equal("ABCXYZ  ", out);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_ascii_6bit),
		cmocka_unit_test(test_packed6_from_bytes)
	};

	ardop_test_setup();
	return cmocka_run_group_tests(tests, NULL, NULL);
}
