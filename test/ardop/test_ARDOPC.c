#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "setup.h"

#include "common/ARDOPC.h"

void ASCIIto6Bit(const char *Padded, UCHAR *Compressed);
void Bit6ToASCII(const UCHAR *Padded, UCHAR *UnCompressed);
void DeCompressCallsign(const char *bytCallsign, char *returned, size_t returnlen);
void CompressCallsign(const char *Callsign, UCHAR *Compressed);

/*
 * Check that the 8-character ASCII string `out` compresses and
 * decompresses back into `expect`. The input must be an 8-character
 * string. Unused characters should be padded with spaces (U+0020).
 */
static void assert_6bit_inout(const char* out, const char* expect) {
	UCHAR compressed[6];
	char decompressed[9];

	ASCIIto6Bit(out, compressed);
	Bit6ToASCII(compressed, decompressed);
	decompressed[8] = '\0';
	assert_string_equal(decompressed, expect);
}

/*
 * Check that the given `call`sign (with optional SSID) has the given
 * compressed `hexstr` representation in the ARDOP protocol. The given
 * `hexstr` must be 12 characters of hexadecimal like "b908e1b2c010"
 */
static void assert_callsign_wireline_is(const char *call, const char *hexstr)
{
	UCHAR compressed[6];
	char compressed_hex[2*sizeof(compressed) + 1];
	compressed_hex[2 * sizeof(compressed)] = '\0';

	// check string must be exactly 12 characters long
	assert_int_equal(strlen(hexstr), 2 * sizeof(compressed));

	CompressCallsign(call, compressed);

	for (size_t i = 0; i < sizeof(compressed); ++i) {
		snprintf(&compressed_hex[2*i], 3, "%02x", (unsigned int)compressed[i]);
	}
	assert_string_equal(hexstr, compressed_hex);
}

/*
 * Verify that the given `call_in` callsign (with optional SSID) becomes
 * `call_out` after a round-trip through the callsign compress/decompress
 * algorithm. This verifies that the callsign can be sent and received.
 */
static void assert_callsign_inout(const char *call_in, const char *call_out)
{
	char out[11];
	UCHAR compressed[6];

	CompressCallsign(call_in, compressed);
	DeCompressCallsign(compressed, out, sizeof(out));
	assert_string_equal(call_out, out);
}

/* test string-packing compression and decompression */
static void test_ascii_6bit(void **state)
{
	(void)state; /* unused */

	assert_6bit_inout("        ", "        ");
	assert_6bit_inout("AZ09-_  ", "AZ09-_  ");
	assert_6bit_inout("A      Z", "A      Z");
	assert_6bit_inout("HI\0\0\0\0\0U", "HI      ");
	assert_6bit_inout("abcxyz  ", "ABCXYZ  ");
}

/*
 * test callsign wireline representation
 *
 * All callsigns were manually tested by sending an IDFrame whose contents
 * were encoded using CompressCallsign() to ARDOP_WIN v 1.0.2.6 where the
 * received callsign was read from the Rcv Frame field of the associated
 * ardop gui.
 *
 * All values matched the expected result except for "LONGCAL-15" which
 * was displayed as "LONGCAL-1". It is unknown at this time whether that
 * exception actually indicates that ARDOP_WIN actually decoded it
 * incorrectly, or whether the ardop gui simply failed to display it
 * properly.
 */
static void test_compress_callsign(void **state)
{
	(void)state; /* unused */

	// six character + SSID values are taken from direct
	// invocation of CompressCallsign() in ardopcf eab1f3165a30.
	assert_callsign_wireline_is("A1A", "851840000010");
	assert_callsign_wireline_is("A1A-1", "851840000011");
	assert_callsign_wireline_is("N0CALL", "b908e1b2c010");
	assert_callsign_wireline_is("N0CALL-A", "b908e1b2c021");
	assert_callsign_wireline_is("N0CALL-Z", "b908e1b2c03a");

	// non-canonical representation
	assert_callsign_wireline_is("N0CALL-0", "b908e1b2c010");
}

/* test callsign round-trip through the protocol */
static void test_decompress_callsign(void **state)
{
	(void)state; /* unused */

	assert_callsign_inout("A1A", "A1A");
	assert_callsign_inout("A1A-1", "A1A-1");
	assert_callsign_inout("N0CALL", "N0CALL");
	assert_callsign_inout("N0CALL-A", "N0CALL-A");
	assert_callsign_inout("N0CALL-Z", "N0CALL-Z");
	assert_callsign_inout("LONGCAL-15", "LONGCAL-15");

	// non-canonical representation
	assert_callsign_inout("N0CALL-0", "N0CALL");

	// too long callsign
	assert_callsign_inout("CAFECAFE", "CAFECAF");

	// overflow
	assert_callsign_inout("MUCHTOOLON-G", "MUCHTOO");

	// ssid out of numeric range is truncated
	assert_callsign_inout("N0CALL-16", "N0CALL-1");

	// empty
	assert_callsign_inout("", "");
	assert_callsign_inout(NULL, "");
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_ascii_6bit),
		cmocka_unit_test(test_compress_callsign),
		cmocka_unit_test(test_decompress_callsign),
	};

	ardop_test_setup();
	return cmocka_run_group_tests(tests, NULL, NULL);
}
