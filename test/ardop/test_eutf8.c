#include <stdarg.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "setup.h"

#include "common/eutf8.h"

// to_eutf8() is used by eutf8() but not included in eutf8.h
size_t to_eutf8(char* dstdata, int dstsize, char* srcdata, int srclen,
	bool escape_tab, bool escape_lf, bool escape_cr);

/* test to_eutf8() */
// Copied fromeutf8_test.c at GitHub.com/pflarue/eutf8 with minimal
// modifications to adapt to cmocka.  Tests of from_eutf8() are omitted since
// this function is not implemented in ardopcf.  Ber tests are omiited to avoid
// needing to include the required additional data files.
static void test_to_eutf8(void** state) {
	(void)state; /* unused */

	// Notice that none of the elements of testdata[] include a
	// NULL (\x00 -> \\00).  This allows strlen() to be used to determine the
	// lengths of the elements.  An element containing a NULL (\x00) would then
	// ignore any data after the NULL.
	// Later tests do include NULL characters to verify that to_eutf8() handles
	// these correctly.
	//
	// In the values for *testdata[], writing something linke "\x8BFF" produces
	// a "hex escape sequence out of range" error.  To avoid this, such strings
	// can be broken into to automatrically concatenated string like "\x8B""FF".
	char * testdata[] = {
		// ASCII text with tab, lf, and cr [0, 1]
		"This\tis a\r\ntest", "This\tis a\r\ntest",

		// Various backslashes in source data with no change required [2. 3]
		"\\AX \\XA \\ab \\aB \\Ab \\\xE2\x80\x8BX",
			"\\AX \\XA \\ab \\aB \\Ab \\\xE2\x80\x8BX",

		// Backslash + HH.  ZWS required [4, 5]
		"\\FF", "\\\xE2\x80\x8B""FF",
		// Backslash + ZWS + hex.  additional ZWS required [6, 7]
		"\\\xE2\x80\x8B""AB",  "\\\xE2\x80\x8B\xE2\x80\x8B""AB",

		// All 32 valid UTF-8 C1 control codes [8, 9]
		"\xC2\x80\xC2\x81\xC2\x82\xC2\x83\xC2\x84\xC2\x85\xC2\x86\xC2\x87"
		"\xC2\x88\xC2\x89\xC2\x8A\xC2\x8B\xC2\x8C\xC2\x8D\xC2\x8E\xC2\x8F"
		"\xC2\x90\xC2\x91\xC2\x92\xC2\x93\xC2\x94\xC2\x95\xC2\x96\xC2\x97"
		"\xC2\x98\xC2\x99\xC2\x9A\xC2\x9B\xC2\x9C\xC2\x9D\xC2\x9E\xC2\x9F",
		"\\C2\\80\\C2\\81\\C2\\82\\C2\\83\\C2\\84\\C2\\85\\C2\\86\\C2\\87"
		"\\C2\\88\\C2\\89\\C2\\8A\\C2\\8B\\C2\\8C\\C2\\8D\\C2\\8E\\C2\\8F"
		"\\C2\\90\\C2\\91\\C2\\92\\C2\\93\\C2\\94\\C2\\95\\C2\\96\\C2\\97"
		"\\C2\\98\\C2\\99\\C2\\9A\\C2\\9B\\C2\\9C\\C2\\9D\\C2\\9E\\C2\\9F",


		// All 66 valid UTF-8 noncharacters [10, 11]
		"\xEF\xB7\x90 \xEF\xB7\x91 \xEF\xB7\x92 \xEF\xB7\x93"
		"\xEF\xB7\x94 \xEF\xB7\x95 \xEF\xB7\x96 \xEF\xB7\x97"
		"\xEF\xB7\x98 \xEF\xB7\x99 \xEF\xB7\x9A \xEF\xB7\x9B"
		"\xEF\xB7\x9C \xEF\xB7\x9D \xEF\xB7\x9E \xEF\xB7\x9F"
		"\xEF\xB7\xA0 \xEF\xB7\xA1 \xEF\xB7\xA2 \xEF\xB7\xA3"
		"\xEF\xB7\xA4 \xEF\xB7\xA5 \xEF\xB7\xA6 \xEF\xB7\xA7"
		"\xEF\xB7\xA8 \xEF\xB7\xA9 \xEF\xB7\xAA \xEF\xB7\xAB"
		"\xEF\xB7\xAC \xEF\xB7\xAD \xEF\xB7\xAE \xEF\xB7\xAF"
		"\xEF\xBF\xBE \xEF\xBF\xBF"
		"\xF0\x9F\xBF\xBE \xF0\x9F\xBF\xBF"
		"\xF0\xAF\xBF\xBE \xF0\xAF\xBF\xBF"
		"\xF0\xBF\xBF\xBE \xF0\xBF\xBF\xBF"
		"\xF1\x8F\xBF\xBE \xF1\x8F\xBF\xBF"
		"\xF1\x9F\xBF\xBE \xF1\x9F\xBF\xBF"
		"\xF1\xAF\xBF\xBE \xF1\xAF\xBF\xBF"
		"\xF1\xBF\xBF\xBE \xF1\xBF\xBF\xBF"
		"\xF2\x8F\xBF\xBE \xF2\x8F\xBF\xBF"
		"\xF2\x9F\xBF\xBE \xF2\x9F\xBF\xBF"
		"\xF2\xAF\xBF\xBE \xF2\xAF\xBF\xBF"
		"\xF2\xBF\xBF\xBE \xF2\xBF\xBF\xBF"
		"\xF3\x8F\xBF\xBE \xF3\x8F\xBF\xBF"
		"\xF3\x9F\xBF\xBE \xF3\x9F\xBF\xBF"
		"\xF3\xAF\xBF\xBE \xF3\xAF\xBF\xBF"
		"\xF3\xBF\xBF\xBE \xF3\xBF\xBF\xBF"
		"\xF4\x8F\xBF\xBE \xF4\x8F\xBF\xBF",
		"\\EF\\B7\\90 \\EF\\B7\\91 \\EF\\B7\\92 \\EF\\B7\\93"
		"\\EF\\B7\\94 \\EF\\B7\\95 \\EF\\B7\\96 \\EF\\B7\\97"
		"\\EF\\B7\\98 \\EF\\B7\\99 \\EF\\B7\\9A \\EF\\B7\\9B"
		"\\EF\\B7\\9C \\EF\\B7\\9D \\EF\\B7\\9E \\EF\\B7\\9F"
		"\\EF\\B7\\A0 \\EF\\B7\\A1 \\EF\\B7\\A2 \\EF\\B7\\A3"
		"\\EF\\B7\\A4 \\EF\\B7\\A5 \\EF\\B7\\A6 \\EF\\B7\\A7"
		"\\EF\\B7\\A8 \\EF\\B7\\A9 \\EF\\B7\\AA \\EF\\B7\\AB"
		"\\EF\\B7\\AC \\EF\\B7\\AD \\EF\\B7\\AE \\EF\\B7\\AF"
		"\\EF\\BF\\BE \\EF\\BF\\BF"
		"\\F0\\9F\\BF\\BE \\F0\\9F\\BF\\BF"
		"\\F0\\AF\\BF\\BE \\F0\\AF\\BF\\BF"
		"\\F0\\BF\\BF\\BE \\F0\\BF\\BF\\BF"
		"\\F1\\8F\\BF\\BE \\F1\\8F\\BF\\BF"
		"\\F1\\9F\\BF\\BE \\F1\\9F\\BF\\BF"
		"\\F1\\AF\\BF\\BE \\F1\\AF\\BF\\BF"
		"\\F1\\BF\\BF\\BE \\F1\\BF\\BF\\BF"
		"\\F2\\8F\\BF\\BE \\F2\\8F\\BF\\BF"
		"\\F2\\9F\\BF\\BE \\F2\\9F\\BF\\BF"
		"\\F2\\AF\\BF\\BE \\F2\\AF\\BF\\BF"
		"\\F2\\BF\\BF\\BE \\F2\\BF\\BF\\BF"
		"\\F3\\8F\\BF\\BE \\F3\\8F\\BF\\BF"
		"\\F3\\9F\\BF\\BE \\F3\\9F\\BF\\BF"
		"\\F3\\AF\\BF\\BE \\F3\\AF\\BF\\BF"
		"\\F3\\BF\\BF\\BE \\F3\\BF\\BF\\BF"
		"\\F4\\8F\\BF\\BE \\F4\\8F\\BF\\BF",

		// The start and end of each block of valid UTF-8 private use characters
		// [12, 13]
		"\xEE\x80\x80\xEE\x80\x81 \xEF\xA3\xBE\xEF\xA3\xBF"
		"\xF3\xB0\x80\x80\xF3\xB0\x80\x81 \xF3\xBF\xBF\xBC\xF3\xBF\xBF\xBD"
		"\xF4\x80\x80\x80\xF4\x80\x80\x81 \xF4\x8F\xBF\xBC\xF4\x8F\xBF\xBD",
		"\\EE\\80\\80\\EE\\80\\81 \\EF\\A3\\BE\\EF\\A3\\BF"
		"\\F3\\B0\\80\\80\\F3\\B0\\80\\81 \\F3\\BF\\BF\\BC\\F3\\BF\\BF\\BD"
		"\\F4\\80\\80\\80\\F4\\80\\80\\81 \\F4\\8F\\BF\\BC\\F4\\8F\\BF\\BD",

		// Some byte sequences just outside blocks of Well Formed UTF-8 [14, 15]
		"\x80\x81"
		"\xC2\x7F\xC2\xC0 \xC3\x7F\xC3\xC0 \xDE\x7F\xDE\xC0 \xC5\xDF\xDF\xC0"
		"\xE0\xA0\x7F\xE0\xBF\xC0 \xE0\x9F\x80\xE0\xC0\xBF"
		"\xE1\x80\x7F\xE1\xBF\xC0 \xE1\x7F\x80\xE1\xC0\xBF"
		"\xEC\x80\x7F\xEC\xBF\xC0 \xEC\x7F\x80\xEC\xC0\xBF"
		"\xED\x80\x7F\xED\x9F\xC0 \xED\x7F\x80\xED\xA0\xBF"
		"\xEE\x80\x7F\xEE\xBF\xC0 \xEE\x7F\x80\xEE\xC0\xBF"
		"\xEF\x80\x7F\xEF\xBF\xC0 \xEF\x7F\x80\xEF\xC0\xBF"
		"\xF0\x90\x80\x7F\xF0\xBF\xBF\xC0 \xF0\x90\x7F\x80\xF0\xBF\xC0\xBF"
		"\xF1\x80\x80\x7F\xF1\xBF\xBF\xC0 \xF1\x7F\x80\x80\xF1\xC0\xBF\xBF"
		"\xF3\x80\x80\x7F\xF3\xBF\xBF\xC0 \xF3\x80\x7F\x80\xF3\xBF\xC0\xBF"
		"\xF4\x80\x80\x7F\xF4\x8F\xBF\xC0 \xF4\x80\x7F\x80\xF4\x8F\xC0\xBF",
		"\\80\\81"
		"\\C2\\7F\\C2\\C0 \\C3\\7F\\C3\\C0 \\DE\\7F\\DE\\C0 \\C5\\DF\\DF\\C0"
		"\\E0\\A0\\7F\\E0\\BF\\C0 \\E0\\9F\\80\\E0\\C0\\BF"
		"\\E1\\80\\7F\\E1\\BF\\C0 \\E1\\7F\\80\\E1\\C0\\BF"
		"\\EC\\80\\7F\\EC\\BF\\C0 \\EC\\7F\\80\\EC\\C0\\BF"
		"\\ED\\80\\7F\\ED\\9F\\C0 \\ED\\7F\\80\\ED\\A0\\BF"
		"\\EE\\80\\7F\\EE\\BF\\C0 \\EE\\7F\\80\\EE\\C0\\BF"
		"\\EF\\80\\7F\\EF\\BF\\C0 \\EF\\7F\\80\\EF\\C0\\BF"
		"\\F0\\90\\80\\7F\\F0\\BF\\BF\\C0 \\F0\\90\\7F\\80\\F0\\BF\\C0\\BF"
		"\\F1\\80\\80\\7F\\F1\\BF\\BF\\C0 \\F1\\7F\\80\\80\\F1\\C0\\BF\\BF"
		"\\F3\\80\\80\\7F\\F3\\BF\\BF\\C0 \\F3\\80\\7F\\80\\F3\\BF\\C0\\BF"
		"\\F4\\80\\80\\7F\\F4\\8F\\BF\\C0 \\F4\\80\\7F\\80\\F4\\8F\\C0\\BF",

		// Some valid UTF-8 with first byte between 0xC2 and 0xDF (not C1 control
		// [16, 17])
		"\xC2\xAB\xCF\x80 \xDD\xBF \xDF\x86",
			"\xC2\xAB\xCF\x80 \xDD\xBF \xDF\x86",

		// Some valid UTF-8 with first byte 0xE0 [18, 19]
		"\xE0\xA0\x80 \xE0\xBF\xBF \xE0\xB5\x90 \xE0\xA5\xBB",
			"\xE0\xA0\x80 \xE0\xBF\xBF \xE0\xB5\x90 \xE0\xA5\xBB",

		// Some valid UTF-8 with first byte 0xE1 to 0xEC [20, 21]
		"\xE1\x80\x80 \xEC\xBF\xBF \xE2\x95\x90 \xEA\xA5\xBB",
			"\xE1\x80\x80 \xEC\xBF\xBF \xE2\x95\x90 \xEA\xA5\xBB",

		// Some valid UTF-8 with first byte 0xED [22, 23]
		"\xED\x80\x80 \xED\x9F\xBF \xED\x95\x90 \xED\x8D\xBB",
			"\xED\x80\x80 \xED\x9F\xBF \xED\x95\x90 \xED\x8D\xBB",

		// Some valid (non-private) UTF-8 with first byte 0xEF [24, 25]
		"\xEF\xA4\x80 \xEF\xBF\xBD \xEF\xB7\x8F \xEF\xB8\xAF",
			"\xEF\xA4\x80 \xEF\xBF\xBD \xEF\xB7\x8F \xEF\xB8\xAF",

		// Some private use UTF-8 with first byte 0xEE and 0xEF (escaped) [26, 27]
		"\xEE\x80\x80 \xEF\xA3\xBF \xEE\x80\xBF \xEF\xA3\x80",
			"\\EE\\80\\80 \\EF\\A3\\BF \\EE\\80\\BF \\EF\\A3\\80",

		// Test proper handling of an othewise valid UTF-8 text which is
		// truncated in the middle of a multibyte character such that the
		// remaining initial bytes of that character must be escaped because
		// they are no longer valid.
		// A series of space separated 3-byte characters: OK [28, 29]
		"\xE3\x82\xB3 \xE3\x83\xB3 \xE3\x83\x8B \xE3\x83\x81 \xE3\x83\x8F",
			"\xE3\x82\xB3 \xE3\x83\xB3 \xE3\x83\x8B \xE3\x83\x81 \xE3\x83\x8F",
		// This sequence with the last byte omitted [30, 31]
		"\xE3\x82\xB3 \xE3\x83\xB3 \xE3\x83\x8B \xE3\x83\x81 \xE3\x83",
			"\xE3\x82\xB3 \xE3\x83\xB3 \xE3\x83\x8B \xE3\x83\x81 \\E3\\83",
		// This sequence with the last two bytes omitted [32, 33]
		"\xE3\x82\xB3 \xE3\x83\xB3 \xE3\x83\x8B \xE3\x83\x81 \xE3",
			"\xE3\x82\xB3 \xE3\x83\xB3 \xE3\x83\x8B \xE3\x83\x81 \\E3",

		// A series of space separated 4-byte characters: OK [34, 35]
		"\xF0\xA2\x87\xB0 \xF1\xA2\x87\xB0 \xF2\xA2\x87\xB0 \xF3\xA2\x87\xB0",
			"\xF0\xA2\x87\xB0 \xF1\xA2\x87\xB0 \xF2\xA2\x87\xB0 \xF3\xA2\x87\xB0",
		// This sequence with the last byte omitted [36, 37]
		"\xF0\xA2\x87\xB0 \xF1\xA2\x87\xB0 \xF2\xA2\x87\xB0 \xF3\xA2\x87",
			"\xF0\xA2\x87\xB0 \xF1\xA2\x87\xB0 \xF2\xA2\x87\xB0 \\F3\\A2\\87",
		// This sequence with the last two bytes omitted [38, 39]
		"\xF0\xA2\x87\xB0 \xF1\xA2\x87\xB0 \xF2\xA2\x87\xB0 \xF3\xA2",
			"\xF0\xA2\x87\xB0 \xF1\xA2\x87\xB0 \xF2\xA2\x87\xB0 \\F3\\A2",
		// This sequence with the last three bytes omitted [40, 41]
		"\xF0\xA2\x87\xB0 \xF1\xA2\x87\xB0 \xF2\xA2\x87\xB0 \xF3",
			"\xF0\xA2\x87\xB0 \xF1\xA2\x87\xB0 \xF2\xA2\x87\xB0 \\F3",

		// Multiple zero width spaces after a backslash and followed by two upper
		// case hex digits.  The eutf8 encoding adds additional zero width
		// spaces. [42, 43]
		"\\DD \\\xE2\x80\x8B""F0 \\\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B""CA",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B\xE2\x80\x8B""F0"
			" \\\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B""CA",
		// This sequence with the last byte omitted such that an additional zero
		// width space is not added [44, 45]
		"\\DD \\\xE2\x80\x8B""F0 \\\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B""C",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B\xE2\x80\x8B""F0"
			" \\\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B""C",
		// This sequence with the last two bytes omitted such that an additional
		// zero width space is not added [46, 47]
		"\\DD \\\xE2\x80\x8B""F0 \\\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B\xE2\x80\x8B""F0"
			" \\\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B",
		// This sequence with the last three bytes omitted such the last zero
		// width space is incomplete so that its remaining bytes must be escaped.
		// [48, 49]
		"\\DD \\\xE2\x80\x8B""F0 \\\xE2\x80\x8B\xE2\x80\x8B\xE2\x80",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B\xE2\x80\x8B""F0"
			" \\\xE2\x80\x8B\xE2\x80\x8B\\E2\\80",
		// This sequence with the last four bytes omitted such the last zero
		// width space is incomplete so that its remaining byte must be escaped.
		// [50, 51]
		"\\DD \\\xE2\x80\x8B""F0 \\\xE2\x80\x8B\xE2\x80\x8B\xE2",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B\xE2\x80\x8B""F0"
			" \\\xE2\x80\x8B\xE2\x80\x8B\\E2",

		// Ending with a single zero width space after a backslash and followed
		// by two upper case hex digits.  The eutf8 encoding adds additional zero
		// width spaces. [52, 53]
		"\\DD \\\xE2\x80\x8B""F0",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B\xE2\x80\x8B""F0",
		// This sequence with the last byte omitted such that an additional zero
		// width space is not added [54, 55]
		"\\DD \\\xE2\x80\x8B""F",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B""F",
		// This sequence with the last two bytes omitted such that an additional
		// zero width space is not added [56, 57]
		"\\DD \\\xE2\x80\x8B",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B",
		// This sequence with the last three bytes omitted such the zero width
		// space is incomplete so that its remaining bytes must be escaped.
		// [58, 59]
		"\\DD \\\xE2\x80",
			"\\\xE2\x80\x8B""DD \\\\E2\\80",
		// This sequence with the last four bytes omitted such the zero width
		// space is incomplete so that its remaining byte must be escaped.
		// [60, 61]
		"\\DD \\\xE2",
			"\\\xE2\x80\x8B""DD \\\\E2",
		// This sequence with the last five bytes omitted such that it ends with
		// a backslash [62, 63]
		"\\DD \\",
			"\\\xE2\x80\x8B""DD \\",

		// Ending with a backslash and followed by two upper case hex digits.
		// The eutf8 encoding adds a zero width spaces. [64, 65]
		"\\DD \\BC",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B""BC",
		// This sequence with the last byte omitted such that a zero width space
		// is not added [66, 67]
		"\\DD \\B",
			"\\\xE2\x80\x8B""DD \\B",
		// This sequence with the last two bytes omitted such that it ends with
		// a backslash [68, 69]
		"\\DD \\",
			"\\\xE2\x80\x8B""DD \\",

		// empty input [70], 71]
		"", "",

		// An example where the eutf8 version is three times as long as raw
		// [72, 73]
		"\xE2\x80\xF4\x9A", "\\E2\\80\\F4\\9A",  // 4 bytes -> 12 bytes
	};

	char tmpdata[1024];
	size_t tmplen;

	char raw[] =
		"\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"
		"\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F"
		"\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2A\x2B\x2C\x2D\x2E\x2F"
		"\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3A\x3B\x3C\x3D\x3E\x3F"
		"\x40\x41\x42\x43\x44\x45\x46\x47\x48\x49\x4A\x4B\x4C\x4D\x4E\x4F"
		"\x50\x51\x52\x53\x54\x55\x56\x57\x58\x59\x5A\x5B\x5C\x5D\x5E\x5F"
		"\x60\x61\x62\x63\x64\x65\x66\x67\x68\x69\x6A\x6B\x6C\x6D\x6E\x6F"
		"\x70\x71\x72\x73\x74\x75\x76\x77\x78\x79\x7A\x7B\x7C\x7D\x7E\x7F";


	// e8_options[3 * i], [3 * i + 1], and [3 * i + 2] contain sequences of
	// escape_tab, escape_lf, and escape_cr respectively, that should produce
	// e8_variants[i] from raw.
	//
	bool e8_options[] = {
		// escape_tab, escape_lf
		true, true, true,
		false, true, true,
		true, false, true,
		true, true, false,
		false, false, false,
	};

	char * e8_variants[] = {
		// This version of e8 escapes all of horizontal tabs (\x09),
		// line feed (\x0A), and carriage return (\x0D)
		"\\00\\01\\02\\03\\04\\05\\06\\07\\08\\09\\0A\\0B\\0C\\0D\\0E\\0F"
		"\\10\\11\\12\\13\\14\\15\\16\\17\\18\\19\\1A\\1B\\1C\\1D\\1E\\1F"
		" !\"#$%&'()*+,-./"
		"0123456789:;<=>?"
		"@ABCDEFGHIJKLMNO"
		"PQRSTUVWXYZ[\\]^_"
		"`abcdefghijklmno"
		"pqrstuvwxyz{|}~\\7F",

		// This version of e8 escapes all but horizontal tab (\x09)
		"\\00\\01\\02\\03\\04\\05\\06\\07\\08\x09\\0A\\0B\\0C\\0D\\0E\\0F"
		"\\10\\11\\12\\13\\14\\15\\16\\17\\18\\19\\1A\\1B\\1C\\1D\\1E\\1F"
		" !\"#$%&'()*+,-./"
		"0123456789:;<=>?"
		"@ABCDEFGHIJKLMNO"
		"PQRSTUVWXYZ[\\]^_"
		"`abcdefghijklmno"
		"pqrstuvwxyz{|}~\\7F",

		// This version of e8 escapes all but line feed (\x0A)
		"\\00\\01\\02\\03\\04\\05\\06\\07\\08\\09\x0A\\0B\\0C\\0D\\0E\\0F"
		"\\10\\11\\12\\13\\14\\15\\16\\17\\18\\19\\1A\\1B\\1C\\1D\\1E\\1F"
		" !\"#$%&'()*+,-./"
		"0123456789:;<=>?"
		"@ABCDEFGHIJKLMNO"
		"PQRSTUVWXYZ[\\]^_"
		"`abcdefghijklmno"
		"pqrstuvwxyz{|}~\\7F",

		// This version of e8 escapes all but carriage returns (\x0D)
		"\\00\\01\\02\\03\\04\\05\\06\\07\\08\\09\\0A\\0B\\0C\x0D\\0E\\0F"
		"\\10\\11\\12\\13\\14\\15\\16\\17\\18\\19\\1A\\1B\\1C\\1D\\1E\\1F"
		" !\"#$%&'()*+,-./"
		"0123456789:;<=>?"
		"@ABCDEFGHIJKLMNO"
		"PQRSTUVWXYZ[\\]^_"
		"`abcdefghijklmno"
		"pqrstuvwxyz{|}~\\7F",

		// This version of e8 escapes none of horizontal tab (\x09),
		// line feed (\x0A), nor carriage return (\x0D)
		"\\00\\01\\02\\03\\04\\05\\06\\07\\08\x09\x0A\\0B\\0C\x0D\\0E\\0F"
		"\\10\\11\\12\\13\\14\\15\\16\\17\\18\\19\\1A\\1B\\1C\\1D\\1E\\1F"
		" !\"#$%&'()*+,-./"
		"0123456789:;<=>?"
		"@ABCDEFGHIJKLMNO"
		"PQRSTUVWXYZ[\\]^_"
		"`abcdefghijklmno"
		"pqrstuvwxyz{|}~\\7F"
	};

	// Use sizeof() - 1 for lengths here and in later tests so as to ignore the
	// NULL terminator at the end of raw which would otherwise be converted to
	// \\00 which would then not match the unescaped NULL terminator at the end
	// of e8

	// This test ensures that \x00 isn't mistakenly interpreted as an upper case
	// hex digit, as was the case in an early development version of eutf8.c
	// Because of the NULL, strlen(shortraw) isn't valid, so this cannot be
	// included in testdata[].
	char shortraw[] = "\\\x00""CF";  // extra "" so \x00CF isn't a single byte
	char shorte8[] = "\\\\00CF";

	// Use assert_memory_equal() rather than assert_string_equal() tmpdata after
	// to_eutf8() is not a null terminated string.

	tmplen = to_eutf8(tmpdata, sizeof(tmpdata), shortraw, sizeof(shortraw) - 1,
		false, false, false);
	assert_int_equal(tmplen, strlen(shorte8));
	assert_memory_equal(tmpdata, shorte8, tmplen);


	for (size_t i = 0; i < sizeof(e8_variants) / sizeof(e8_variants[0]); ++i) {
		// e8_options[3 * i] is escape_tab,
		// e8_options[3 * i + 1] is escape_lf,
		// e8_options[3 * i + 2] is escape_cr,
		tmplen = to_eutf8(tmpdata, sizeof(tmpdata), raw, sizeof(raw) - 1,
			e8_options[3 * i], e8_options[3 * i + 1], e8_options[3 * i + 2]);
		assert_int_equal(tmplen, strlen(e8_variants[i]));
		assert_memory_equal(tmpdata, e8_variants[i], tmplen);
	}


	// testdata contains e8 values assuming escape_tab, escape_lf, and
	// escape_cr are all false.
	for (size_t i = 0; i < sizeof(testdata) / sizeof(testdata[0]); i += 2) {
		// testdata[i] is raw, testdata[i + 1] is e8
		tmplen = to_eutf8(tmpdata, sizeof(tmpdata), testdata[i], strlen(testdata[i]), false, false, false);
		assert_int_equal(tmplen, strlen(testdata[i + 1]));
		assert_memory_equal(tmpdata, testdata[i + 1], tmplen);
	}

	// The following tests what occurs when dstsize is insufficient to hold the
	// full output of to_eutf8() and from_eutf8().  Some implementations,
	// including the refernece implementations in python and js do not use
	// output buffers whose size is externally limited like this.  So, these
	// tests are not required for those implementations.

	// For to_eutf8():
	// Return the length of the data in dstdata.  If the converted contents of
	// srcdata would required dstsize or more bytes, then the return value will be
	// exactly equal to dstsize.  One or more question marks may be used to pad the
	// end of dstdata in this case if a multi-byte utf-8 sequence would have
	// required more than dstsize bytes to fully write.  dstdata will not be
	// terminated with a null unless srcdata[srclen - 1] is null.

	// All of the following test cases can be performed using testraw, a single
	// well-chosen input data sequence by simply varying the value of dstsize.
	// teste8_bysize[dstsize] is the expected output of to_eutf8(testraw).


	// to_eutf8() test case 1: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a valid UTF-8 single byte
	// character that does not require an escape.


	// to_eutf8() test case 2: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a single byte that requires
	// an escape.

	// to_eutf8() test case 3: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls within a single byte that requires
	// an escape.


	// to_eutf8() test case 4: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a byte that could be the
	// first byte of a multi-byte UTF-8 sequence, but which is not followed by
	// an appropriate second byte, so that a single byte escape is required.

	// to_eutf8() test case 5: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls within a byte that could be the
	// first byte of a multi-byte UTF-8 sequence, but which is not followed by
	// an appropriate second byte, so that a single byte escape is required.


	// to_eutf8() test case 6: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a valid UTF-8 multi-byte
	// character that does not require an escape.

	// to_eutf8() test case 7: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls within a valid UTF-8 multi-byte
	// character that does not require an escape.


	// to_eutf8() test case 8: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a valid UTF-8 multi-byte
	// character that requires multiple escapes (such as a C1 control
	// character)

	// to_eutf8() test case 9: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls within a valid UTF-8 multi-byte
	// character that requires multiple escapes (such as a C1 control
	// character)


	// to_eutf8() test case 10: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a backslash, an inserted
	// 3-byte Zero Width Space, and two uppercase hex digits.

	// to_eutf8() test case 11: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a backslash, an inserted
	// 3-byte Zero Width Space, and one uppercase hex digit, where a second
	// uppercase hex digit follows.

	// to_eutf8() test case 12: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a backslash and an inserted
	// 3-byte Zero Width Space, where two uppercase hex digits follow.

	// to_eutf8() test case 13: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls within an inserted 3-byte Zero
	// Width Space after a backslahs, where two uppercase hex digits follow.


	// to_eutf8() test case 14: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a backslash, an inserted
	// 3-byte Zero Width Space, an existing 3-byte Zero Width Space, and two
	// uppercase hex digits.

	// to_eutf8() test case 15: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a backslash, an inserted
	// 3-byte Zero Width Space, an existing 3-byte Zero Width Space, and two
	// one uppercase hex digit, where a second uppercase hex digit follows.

	// to_eutf8() test case 16: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a backslash, an inserted
	// 3-byte Zero Width Space, and an existing 3-byte Zero Width Space, where
	// two uppercase hex digits follow.

	// to_eutf8() test case 17: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls within an existing 3-byte Zero
	// width space after a backslash and an inserted 3-byte Zero Width Space,
	// where two uppercase hex digits follow.

	// to_eutf8() test case 18: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after an inserted 3-byte Zero
	// width space after a backslash, where an existing 3-byte Zero Width Space
	// and two uppercase hex digits follow.

	// to_eutf8() test case 19: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls within an inserted 3-byte Zero
	// width space after a backslash, where an existing 3-byte Zero Width Space
	// and two uppercase hex digits follow.

	// to_eutf8() test case 20: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a backslash, were an
	// inserted 3-byte Zero width space, an existing 3-byte Zero Width Space and
	// two uppercase hex digits follow.


	// The use of double-double quotes ("") in testraw and teste8_bysize[] are
	// required to avoid a "hex escape sequence out of range" error because
	// "\x8BDB" looks to the compiler like it contains a 4-digit hex sequence
	// and thus it must be written as "\x8B""DB".

	// \x15 is a single byte C0 control code "NAK" that requires an escape.
	// \xD0 is valid as a UTF-8 first byte, but is invalid if not followed by
	//   a second byte of 0x90-0xBF.
	// \xC2\xA9 is \u00A9, the copyright symbole (©).
	// \xC2\x94 is \x0094, a C1 control character, "CCH" Cancel Character "ESC T".
	// \\CF (where the \\ indicates a single literal backslash) is a sequence of
	//   ASCII characters (a backslash followed by two uppercase hex digits)
	//   that could be mistaken as a eutf8 escape sequence, so that to_eutf8()
	//   inserts a Zero Width Space after the backslash to ensure that
	//   data == from_eutf8(to_eutf8(data)).
	// \xE2\x80\x8B is a Zero Width Space, such that \\\xE2\x80\x8BDB is a
	//   backslash followed by a Zero Width Space followed by two uppercase hex
	//   characters.  to_eutf8() inserts an additional Zero Width Space after
	//   the backslash to ensure that  data == from_eutf8(to_eutf8(data)).

	char testraw[] = "\\\xE2\x80\x8B""DB\\CF\xC2\x94\xC2\xA9\xD0\x15""AB";
	char * teste8_bysize[] = {
		"", // dstsize = 0
		"?", // dstsize = 1
		"??",
		"???",
		"\\\xE2\x80\x8B",
		"\\\xE2\x80\x8B?",  // 5
		"\\\xE2\x80\x8B??",
		"\\\xE2\x80\x8B\xE2\x80\x8B",
		"\\\xE2\x80\x8B\xE2\x80\x8B""D",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB?",  // 10
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB??",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB???",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""C",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF",  // 15
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF?",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF??",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF???",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF????",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF?????",  // 20
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94?",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9?",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9??",  // 25
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9\\D0",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9\\D0?",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9\\D0??",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9\\D0\\15",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9\\D0\\15A",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9\\D0\\15AB",
	};

	for (size_t i = 0; i < sizeof(teste8_bysize) / sizeof(teste8_bysize[0]); ++i) {
		tmplen = to_eutf8(tmpdata, i, testraw, strlen(testraw), true, true, true);
		assert_int_equal(tmplen, i);
		assert_memory_equal(tmpdata, teste8_bysize[i], tmplen);
	}
}




/* test eutf8() */
// Unlike to_eutf8(), eutf8() always escapes horizontal tab, line feed, and
// carriage return characters.  It also adds a terminating NULL to the output,
// making it a string.
static void test_eutf8(void** state) {
	(void)state; /* unused */

	// Notice that none of the elements of testdata[] include a
	// NULL (\x00 -> \\00).  This allows strlen() to be used to determine the
	// lengths of the elements.  An element containing a NULL (\x00) would then
	// ignore any data after the NULL.
	// Later tests do include NULL characters to verify that to_eutf8() handles
	// these correctly.
	//
	// In the values for *testdata[], writing something linke "\x8BFF" produces
	// a "hex escape sequence out of range" error.  To avoid this, such strings
	// can be broken into to automatrically concatenated string like "\x8B""FF".
	//
	// This version of testdata differs from that used in test_to_eutf8 in that
	// all horixontal tab, line feed, and carriage return characters are
	// escaped.
	char * testdata[] = {
		// ASCII text with tab and lf [0, 1]
		"This\tis a\r\ntest", "This\\09is a\\0D\\0Atest",

		// Various backslashes in source data with no change required [2. 3]
		"\\AX \\XA \\ab \\aB \\Ab \\\xE2\x80\x8BX",
			"\\AX \\XA \\ab \\aB \\Ab \\\xE2\x80\x8BX",

		// Backslash + HH.  ZWS required [4, 5]
		"\\FF", "\\\xE2\x80\x8B""FF",
		// Backslash + ZWS + hex.  additional ZWS required [6, 7]
		"\\\xE2\x80\x8B""AB",  "\\\xE2\x80\x8B\xE2\x80\x8B""AB",

		// All 32 valid UTF-8 C1 control codes [8, 9]
		"\xC2\x80\xC2\x81\xC2\x82\xC2\x83\xC2\x84\xC2\x85\xC2\x86\xC2\x87"
		"\xC2\x88\xC2\x89\xC2\x8A\xC2\x8B\xC2\x8C\xC2\x8D\xC2\x8E\xC2\x8F"
		"\xC2\x90\xC2\x91\xC2\x92\xC2\x93\xC2\x94\xC2\x95\xC2\x96\xC2\x97"
		"\xC2\x98\xC2\x99\xC2\x9A\xC2\x9B\xC2\x9C\xC2\x9D\xC2\x9E\xC2\x9F",
		"\\C2\\80\\C2\\81\\C2\\82\\C2\\83\\C2\\84\\C2\\85\\C2\\86\\C2\\87"
		"\\C2\\88\\C2\\89\\C2\\8A\\C2\\8B\\C2\\8C\\C2\\8D\\C2\\8E\\C2\\8F"
		"\\C2\\90\\C2\\91\\C2\\92\\C2\\93\\C2\\94\\C2\\95\\C2\\96\\C2\\97"
		"\\C2\\98\\C2\\99\\C2\\9A\\C2\\9B\\C2\\9C\\C2\\9D\\C2\\9E\\C2\\9F",


		// All 66 valid UTF-8 noncharacters [10, 11]
		"\xEF\xB7\x90 \xEF\xB7\x91 \xEF\xB7\x92 \xEF\xB7\x93"
		"\xEF\xB7\x94 \xEF\xB7\x95 \xEF\xB7\x96 \xEF\xB7\x97"
		"\xEF\xB7\x98 \xEF\xB7\x99 \xEF\xB7\x9A \xEF\xB7\x9B"
		"\xEF\xB7\x9C \xEF\xB7\x9D \xEF\xB7\x9E \xEF\xB7\x9F"
		"\xEF\xB7\xA0 \xEF\xB7\xA1 \xEF\xB7\xA2 \xEF\xB7\xA3"
		"\xEF\xB7\xA4 \xEF\xB7\xA5 \xEF\xB7\xA6 \xEF\xB7\xA7"
		"\xEF\xB7\xA8 \xEF\xB7\xA9 \xEF\xB7\xAA \xEF\xB7\xAB"
		"\xEF\xB7\xAC \xEF\xB7\xAD \xEF\xB7\xAE \xEF\xB7\xAF"
		"\xEF\xBF\xBE \xEF\xBF\xBF"
		"\xF0\x9F\xBF\xBE \xF0\x9F\xBF\xBF"
		"\xF0\xAF\xBF\xBE \xF0\xAF\xBF\xBF"
		"\xF0\xBF\xBF\xBE \xF0\xBF\xBF\xBF"
		"\xF1\x8F\xBF\xBE \xF1\x8F\xBF\xBF"
		"\xF1\x9F\xBF\xBE \xF1\x9F\xBF\xBF"
		"\xF1\xAF\xBF\xBE \xF1\xAF\xBF\xBF"
		"\xF1\xBF\xBF\xBE \xF1\xBF\xBF\xBF"
		"\xF2\x8F\xBF\xBE \xF2\x8F\xBF\xBF"
		"\xF2\x9F\xBF\xBE \xF2\x9F\xBF\xBF"
		"\xF2\xAF\xBF\xBE \xF2\xAF\xBF\xBF"
		"\xF2\xBF\xBF\xBE \xF2\xBF\xBF\xBF"
		"\xF3\x8F\xBF\xBE \xF3\x8F\xBF\xBF"
		"\xF3\x9F\xBF\xBE \xF3\x9F\xBF\xBF"
		"\xF3\xAF\xBF\xBE \xF3\xAF\xBF\xBF"
		"\xF3\xBF\xBF\xBE \xF3\xBF\xBF\xBF"
		"\xF4\x8F\xBF\xBE \xF4\x8F\xBF\xBF",
		"\\EF\\B7\\90 \\EF\\B7\\91 \\EF\\B7\\92 \\EF\\B7\\93"
		"\\EF\\B7\\94 \\EF\\B7\\95 \\EF\\B7\\96 \\EF\\B7\\97"
		"\\EF\\B7\\98 \\EF\\B7\\99 \\EF\\B7\\9A \\EF\\B7\\9B"
		"\\EF\\B7\\9C \\EF\\B7\\9D \\EF\\B7\\9E \\EF\\B7\\9F"
		"\\EF\\B7\\A0 \\EF\\B7\\A1 \\EF\\B7\\A2 \\EF\\B7\\A3"
		"\\EF\\B7\\A4 \\EF\\B7\\A5 \\EF\\B7\\A6 \\EF\\B7\\A7"
		"\\EF\\B7\\A8 \\EF\\B7\\A9 \\EF\\B7\\AA \\EF\\B7\\AB"
		"\\EF\\B7\\AC \\EF\\B7\\AD \\EF\\B7\\AE \\EF\\B7\\AF"
		"\\EF\\BF\\BE \\EF\\BF\\BF"
		"\\F0\\9F\\BF\\BE \\F0\\9F\\BF\\BF"
		"\\F0\\AF\\BF\\BE \\F0\\AF\\BF\\BF"
		"\\F0\\BF\\BF\\BE \\F0\\BF\\BF\\BF"
		"\\F1\\8F\\BF\\BE \\F1\\8F\\BF\\BF"
		"\\F1\\9F\\BF\\BE \\F1\\9F\\BF\\BF"
		"\\F1\\AF\\BF\\BE \\F1\\AF\\BF\\BF"
		"\\F1\\BF\\BF\\BE \\F1\\BF\\BF\\BF"
		"\\F2\\8F\\BF\\BE \\F2\\8F\\BF\\BF"
		"\\F2\\9F\\BF\\BE \\F2\\9F\\BF\\BF"
		"\\F2\\AF\\BF\\BE \\F2\\AF\\BF\\BF"
		"\\F2\\BF\\BF\\BE \\F2\\BF\\BF\\BF"
		"\\F3\\8F\\BF\\BE \\F3\\8F\\BF\\BF"
		"\\F3\\9F\\BF\\BE \\F3\\9F\\BF\\BF"
		"\\F3\\AF\\BF\\BE \\F3\\AF\\BF\\BF"
		"\\F3\\BF\\BF\\BE \\F3\\BF\\BF\\BF"
		"\\F4\\8F\\BF\\BE \\F4\\8F\\BF\\BF",

		// The start and end of each block of valid UTF-8 private use characters
		// [12, 13]
		"\xEE\x80\x80\xEE\x80\x81 \xEF\xA3\xBE\xEF\xA3\xBF"
		"\xF3\xB0\x80\x80\xF3\xB0\x80\x81 \xF3\xBF\xBF\xBC\xF3\xBF\xBF\xBD"
		"\xF4\x80\x80\x80\xF4\x80\x80\x81 \xF4\x8F\xBF\xBC\xF4\x8F\xBF\xBD",
		"\\EE\\80\\80\\EE\\80\\81 \\EF\\A3\\BE\\EF\\A3\\BF"
		"\\F3\\B0\\80\\80\\F3\\B0\\80\\81 \\F3\\BF\\BF\\BC\\F3\\BF\\BF\\BD"
		"\\F4\\80\\80\\80\\F4\\80\\80\\81 \\F4\\8F\\BF\\BC\\F4\\8F\\BF\\BD",

		// Some byte sequences just outside blocks of Well Formed UTF-8 [14, 15]
		"\x80\x81"
		"\xC2\x7F\xC2\xC0 \xC3\x7F\xC3\xC0 \xDE\x7F\xDE\xC0 \xC5\xDF\xDF\xC0"
		"\xE0\xA0\x7F\xE0\xBF\xC0 \xE0\x9F\x80\xE0\xC0\xBF"
		"\xE1\x80\x7F\xE1\xBF\xC0 \xE1\x7F\x80\xE1\xC0\xBF"
		"\xEC\x80\x7F\xEC\xBF\xC0 \xEC\x7F\x80\xEC\xC0\xBF"
		"\xED\x80\x7F\xED\x9F\xC0 \xED\x7F\x80\xED\xA0\xBF"
		"\xEE\x80\x7F\xEE\xBF\xC0 \xEE\x7F\x80\xEE\xC0\xBF"
		"\xEF\x80\x7F\xEF\xBF\xC0 \xEF\x7F\x80\xEF\xC0\xBF"
		"\xF0\x90\x80\x7F\xF0\xBF\xBF\xC0 \xF0\x90\x7F\x80\xF0\xBF\xC0\xBF"
		"\xF1\x80\x80\x7F\xF1\xBF\xBF\xC0 \xF1\x7F\x80\x80\xF1\xC0\xBF\xBF"
		"\xF3\x80\x80\x7F\xF3\xBF\xBF\xC0 \xF3\x80\x7F\x80\xF3\xBF\xC0\xBF"
		"\xF4\x80\x80\x7F\xF4\x8F\xBF\xC0 \xF4\x80\x7F\x80\xF4\x8F\xC0\xBF",
		"\\80\\81"
		"\\C2\\7F\\C2\\C0 \\C3\\7F\\C3\\C0 \\DE\\7F\\DE\\C0 \\C5\\DF\\DF\\C0"
		"\\E0\\A0\\7F\\E0\\BF\\C0 \\E0\\9F\\80\\E0\\C0\\BF"
		"\\E1\\80\\7F\\E1\\BF\\C0 \\E1\\7F\\80\\E1\\C0\\BF"
		"\\EC\\80\\7F\\EC\\BF\\C0 \\EC\\7F\\80\\EC\\C0\\BF"
		"\\ED\\80\\7F\\ED\\9F\\C0 \\ED\\7F\\80\\ED\\A0\\BF"
		"\\EE\\80\\7F\\EE\\BF\\C0 \\EE\\7F\\80\\EE\\C0\\BF"
		"\\EF\\80\\7F\\EF\\BF\\C0 \\EF\\7F\\80\\EF\\C0\\BF"
		"\\F0\\90\\80\\7F\\F0\\BF\\BF\\C0 \\F0\\90\\7F\\80\\F0\\BF\\C0\\BF"
		"\\F1\\80\\80\\7F\\F1\\BF\\BF\\C0 \\F1\\7F\\80\\80\\F1\\C0\\BF\\BF"
		"\\F3\\80\\80\\7F\\F3\\BF\\BF\\C0 \\F3\\80\\7F\\80\\F3\\BF\\C0\\BF"
		"\\F4\\80\\80\\7F\\F4\\8F\\BF\\C0 \\F4\\80\\7F\\80\\F4\\8F\\C0\\BF",

		// Some valid UTF-8 with first byte between 0xC2 and 0xDF (not C1 control
		// [16, 17])
		"\xC2\xAB\xCF\x80 \xDD\xBF \xDF\x86",
			"\xC2\xAB\xCF\x80 \xDD\xBF \xDF\x86",

		// Some valid UTF-8 with first byte 0xE0 [18, 19]
		"\xE0\xA0\x80 \xE0\xBF\xBF \xE0\xB5\x90 \xE0\xA5\xBB",
			"\xE0\xA0\x80 \xE0\xBF\xBF \xE0\xB5\x90 \xE0\xA5\xBB",

		// Some valid UTF-8 with first byte 0xE1 to 0xEC [20, 21]
		"\xE1\x80\x80 \xEC\xBF\xBF \xE2\x95\x90 \xEA\xA5\xBB",
			"\xE1\x80\x80 \xEC\xBF\xBF \xE2\x95\x90 \xEA\xA5\xBB",

		// Some valid UTF-8 with first byte 0xED [22, 23]
		"\xED\x80\x80 \xED\x9F\xBF \xED\x95\x90 \xED\x8D\xBB",
			"\xED\x80\x80 \xED\x9F\xBF \xED\x95\x90 \xED\x8D\xBB",

		// Some valid (non-private) UTF-8 with first byte 0xEF [24, 25]
		"\xEF\xA4\x80 \xEF\xBF\xBD \xEF\xB7\x8F \xEF\xB8\xAF",
			"\xEF\xA4\x80 \xEF\xBF\xBD \xEF\xB7\x8F \xEF\xB8\xAF",

		// Some private use UTF-8 with first byte 0xEE and 0xEF (escaped) [26, 27]
		"\xEE\x80\x80 \xEF\xA3\xBF \xEE\x80\xBF \xEF\xA3\x80",
			"\\EE\\80\\80 \\EF\\A3\\BF \\EE\\80\\BF \\EF\\A3\\80",

		// Test proper handling of an othewise valid UTF-8 text which is
		// truncated in the middle of a multibyte character such that the
		// remaining initial bytes of that character must be escaped because
		// they are no longer valid.
		// A series of space separated 3-byte characters: OK [28, 29]
		"\xE3\x82\xB3 \xE3\x83\xB3 \xE3\x83\x8B \xE3\x83\x81 \xE3\x83\x8F",
			"\xE3\x82\xB3 \xE3\x83\xB3 \xE3\x83\x8B \xE3\x83\x81 \xE3\x83\x8F",
		// This sequence with the last byte omitted [30, 31]
		"\xE3\x82\xB3 \xE3\x83\xB3 \xE3\x83\x8B \xE3\x83\x81 \xE3\x83",
			"\xE3\x82\xB3 \xE3\x83\xB3 \xE3\x83\x8B \xE3\x83\x81 \\E3\\83",
		// This sequence with the last two bytes omitted [32, 33]
		"\xE3\x82\xB3 \xE3\x83\xB3 \xE3\x83\x8B \xE3\x83\x81 \xE3",
			"\xE3\x82\xB3 \xE3\x83\xB3 \xE3\x83\x8B \xE3\x83\x81 \\E3",

		// A series of space separated 4-byte characters: OK [34, 35]
		"\xF0\xA2\x87\xB0 \xF1\xA2\x87\xB0 \xF2\xA2\x87\xB0 \xF3\xA2\x87\xB0",
			"\xF0\xA2\x87\xB0 \xF1\xA2\x87\xB0 \xF2\xA2\x87\xB0 \xF3\xA2\x87\xB0",
		// This sequence with the last byte omitted [36, 37]
		"\xF0\xA2\x87\xB0 \xF1\xA2\x87\xB0 \xF2\xA2\x87\xB0 \xF3\xA2\x87",
			"\xF0\xA2\x87\xB0 \xF1\xA2\x87\xB0 \xF2\xA2\x87\xB0 \\F3\\A2\\87",
		// This sequence with the last two bytes omitted [38, 39]
		"\xF0\xA2\x87\xB0 \xF1\xA2\x87\xB0 \xF2\xA2\x87\xB0 \xF3\xA2",
			"\xF0\xA2\x87\xB0 \xF1\xA2\x87\xB0 \xF2\xA2\x87\xB0 \\F3\\A2",
		// This sequence with the last three bytes omitted [40, 41]
		"\xF0\xA2\x87\xB0 \xF1\xA2\x87\xB0 \xF2\xA2\x87\xB0 \xF3",
			"\xF0\xA2\x87\xB0 \xF1\xA2\x87\xB0 \xF2\xA2\x87\xB0 \\F3",

		// Multiple zero width spaces after a backslash and followed by two upper
		// case hex digits.  The eutf8 encoding adds additional zero width
		// spaces. [42, 43]
		"\\DD \\\xE2\x80\x8B""F0 \\\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B""CA",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B\xE2\x80\x8B""F0"
			" \\\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B""CA",
		// This sequence with the last byte omitted such that an additional zero
		// width space is not added [44, 45]
		"\\DD \\\xE2\x80\x8B""F0 \\\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B""C",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B\xE2\x80\x8B""F0"
			" \\\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B""C",
		// This sequence with the last two bytes omitted such that an additional
		// zero width space is not added [46, 47]
		"\\DD \\\xE2\x80\x8B""F0 \\\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B\xE2\x80\x8B""F0"
			" \\\xE2\x80\x8B\xE2\x80\x8B\xE2\x80\x8B",
		// This sequence with the last three bytes omitted such the last zero
		// width space is incomplete so that its remaining bytes must be escaped.
		// [48, 49]
		"\\DD \\\xE2\x80\x8B""F0 \\\xE2\x80\x8B\xE2\x80\x8B\xE2\x80",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B\xE2\x80\x8B""F0"
			" \\\xE2\x80\x8B\xE2\x80\x8B\\E2\\80",
		// This sequence with the last four bytes omitted such the last zero
		// width space is incomplete so that its remaining byte must be escaped.
		// [50, 51]
		"\\DD \\\xE2\x80\x8B""F0 \\\xE2\x80\x8B\xE2\x80\x8B\xE2",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B\xE2\x80\x8B""F0"
			" \\\xE2\x80\x8B\xE2\x80\x8B\\E2",

		// Ending with a single zero width space after a backslash and followed
		// by two upper case hex digits.  The eutf8 encoding adds additional zero
		// width spaces. [52, 53]
		"\\DD \\\xE2\x80\x8B""F0",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B\xE2\x80\x8B""F0",
		// This sequence with the last byte omitted such that an additional zero
		// width space is not added [54, 55]
		"\\DD \\\xE2\x80\x8B""F",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B""F",
		// This sequence with the last two bytes omitted such that an additional
		// zero width space is not added [56, 57]
		"\\DD \\\xE2\x80\x8B",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B",
		// This sequence with the last three bytes omitted such the zero width
		// space is incomplete so that its remaining bytes must be escaped.
		// [58, 59]
		"\\DD \\\xE2\x80",
			"\\\xE2\x80\x8B""DD \\\\E2\\80",
		// This sequence with the last four bytes omitted such the zero width
		// space is incomplete so that its remaining byte must be escaped.
		// [60, 61]
		"\\DD \\\xE2",
			"\\\xE2\x80\x8B""DD \\\\E2",
		// This sequence with the last five bytes omitted such that it ends with
		// a backslash [62, 63]
		"\\DD \\",
			"\\\xE2\x80\x8B""DD \\",

		// Ending with a backslash and followed by two upper case hex digits.
		// The eutf8 encoding adds a zero width spaces. [64, 65]
		"\\DD \\BC",
			"\\\xE2\x80\x8B""DD \\\xE2\x80\x8B""BC",
		// This sequence with the last byte omitted such that a zero width space
		// is not added [66, 67]
		"\\DD \\B",
			"\\\xE2\x80\x8B""DD \\B",
		// This sequence with the last two bytes omitted such that it ends with
		// a backslash [68, 69]
		"\\DD \\",
			"\\\xE2\x80\x8B""DD \\",

		// empty input [70], 71]
		"", "",

		// An example where the eutf8 version is three times as long as raw
		// [72, 73]
		"\xE2\x80\xF4\x9A", "\\E2\\80\\F4\\9A",  // 4 bytes -> 12 bytes
	};

	char tmpdata[1024];
	size_t tmplen;

	char raw[] =
		"\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"
		"\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F"
		"\x20\x21\x22\x23\x24\x25\x26\x27\x28\x29\x2A\x2B\x2C\x2D\x2E\x2F"
		"\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3A\x3B\x3C\x3D\x3E\x3F"
		"\x40\x41\x42\x43\x44\x45\x46\x47\x48\x49\x4A\x4B\x4C\x4D\x4E\x4F"
		"\x50\x51\x52\x53\x54\x55\x56\x57\x58\x59\x5A\x5B\x5C\x5D\x5E\x5F"
		"\x60\x61\x62\x63\x64\x65\x66\x67\x68\x69\x6A\x6B\x6C\x6D\x6E\x6F"
		"\x70\x71\x72\x73\x74\x75\x76\x77\x78\x79\x7A\x7B\x7C\x7D\x7E\x7F";


	// Only the first element (true, true, true) of e8_variats used in
	// test_to_eutf8()
	char * e8_variants[] = {
		// This version of e8 escapes all of horizontal tabs (\x09),
		// line feed (\x0A), and carriage return (\x0D)
		"\\00\\01\\02\\03\\04\\05\\06\\07\\08\\09\\0A\\0B\\0C\\0D\\0E\\0F"
		"\\10\\11\\12\\13\\14\\15\\16\\17\\18\\19\\1A\\1B\\1C\\1D\\1E\\1F"
		" !\"#$%&'()*+,-./"
		"0123456789:;<=>?"
		"@ABCDEFGHIJKLMNO"
		"PQRSTUVWXYZ[\\]^_"
		"`abcdefghijklmno"
		"pqrstuvwxyz{|}~\\7F",
	};

	// This test ensures that \x00 isn't mistakenly interpreted as an upper case
	// hex digit, as was the case in an early development version of eutf8.c
	// Because of the NULL, strlen(shortraw) isn't valid, so this cannot be
	// included in testdata[].
	char shortraw[] = "\\\x00""CF";  // extra "" so \x00CF isn't a single byte
	char shorte8[] = "\\\\00CF";

	// Use sizeof() - 1 for lengths here and in later tests so as to ignore the
	// NULL terminator at the end of raw which would otherwise be converted to
	// \\00 which would then not match the unescaped NULL terminator at the end
	// of e8

	// Unlike in test_to_eutf8(), here tmpdata after eutf8() is a null
	// terminated string.  So, assert_string_equal() can be used

	tmplen = eutf8(tmpdata, sizeof(tmpdata), shortraw, sizeof(shortraw) - 1);
	assert_int_equal(tmplen, strlen(shorte8) + 1);  // tmplen includes the NULL
	assert_string_equal(tmpdata, shorte8);


	for (size_t i = 0; i < sizeof(e8_variants) / sizeof(e8_variants[0]); ++i) {
		tmplen = eutf8(tmpdata, sizeof(tmpdata), raw, sizeof(raw) - 1);
		assert_int_equal(tmplen, strlen(e8_variants[i]) + 1);
		assert_string_equal(tmpdata, e8_variants[i]);
	}

	// testdata contains e8 values assuming escape_tab and escape_lf are both false.
	for (size_t i = 0; i < sizeof(testdata) / sizeof(testdata[0]); i += 2) {
		// testdata[i] is raw, testdata[i + 1] is e8
		tmplen = eutf8(tmpdata, sizeof(tmpdata), testdata[i], strlen(testdata[i]));
		assert_int_equal(tmplen, strlen(testdata[i + 1]) + 1);
		assert_string_equal(tmpdata, testdata[i + 1]);
	}

	// The following tests what occurs when dstsize is insufficient to hold the
	// full output of to_eutf8() and from_eutf8().  Some implementations,
	// including the refernece implementations in python and js do not use
	// output buffers whose size is externally limited like this.  So, these
	// tests are not required for those implementations.

	// For to_eutf8():
	// Return the length of the data in dstdata.  If the converted contents of
	// srcdata would required dstsize or more bytes, then the return value will be
	// exactly equal to dstsize.  One or more question marks may be used to pad the
	// end of dstdata in this case if a multi-byte utf-8 sequence would have
	// required more than dstsize bytes to fully write.  dstdata will not be
	// terminated with a null unless srcdata[srclen - 1] is null.

	// All of the following test cases can be performed using testraw, a single
	// well-chosen input data sequence by simply varying the value of dstsize.
	// teste8_bysize[dstsize] is the expected output of to_eutf8(testraw).


	// to_eutf8() test case 1: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a valid UTF-8 single byte
	// character that does not require an escape.


	// to_eutf8() test case 2: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a single byte that requires
	// an escape.

	// to_eutf8() test case 3: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls within a single byte that requires
	// an escape.


	// to_eutf8() test case 4: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a byte that could be the
	// first byte of a multi-byte UTF-8 sequence, but which is not followed by
	// an appropriate second byte, so that a single byte escape is required.

	// to_eutf8() test case 5: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls within a byte that could be the
	// first byte of a multi-byte UTF-8 sequence, but which is not followed by
	// an appropriate second byte, so that a single byte escape is required.


	// to_eutf8() test case 6: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a valid UTF-8 multi-byte
	// character that does not require an escape.

	// to_eutf8() test case 7: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls within a valid UTF-8 multi-byte
	// character that does not require an escape.


	// to_eutf8() test case 8: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a valid UTF-8 multi-byte
	// character that requires multiple escapes (such as a C1 control
	// character)

	// to_eutf8() test case 9: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls within a valid UTF-8 multi-byte
	// character that requires multiple escapes (such as a C1 control
	// character)


	// to_eutf8() test case 10: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a backslash, an inserted
	// 3-byte Zero Width Space, and two uppercase hex digits.

	// to_eutf8() test case 11: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a backslash, an inserted
	// 3-byte Zero Width Space, and one uppercase hex digit, where a second
	// uppercase hex digit follows.

	// to_eutf8() test case 12: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a backslash and an inserted
	// 3-byte Zero Width Space, where two uppercase hex digits follow.

	// to_eutf8() test case 13: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls within an inserted 3-byte Zero
	// Width Space after a backslahs, where two uppercase hex digits follow.


	// to_eutf8() test case 14: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a backslash, an inserted
	// 3-byte Zero Width Space, an existing 3-byte Zero Width Space, and two
	// uppercase hex digits.

	// to_eutf8() test case 15: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a backslash, an inserted
	// 3-byte Zero Width Space, an existing 3-byte Zero Width Space, and two
	// one uppercase hex digit, where a second uppercase hex digit follows.

	// to_eutf8() test case 16: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a backslash, an inserted
	// 3-byte Zero Width Space, and an existing 3-byte Zero Width Space, where
	// two uppercase hex digits follow.

	// to_eutf8() test case 17: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls within an existing 3-byte Zero
	// width space after a backslash and an inserted 3-byte Zero Width Space,
	// where two uppercase hex digits follow.

	// to_eutf8() test case 18: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after an inserted 3-byte Zero
	// width space after a backslash, where an existing 3-byte Zero Width Space
	// and two uppercase hex digits follow.

	// to_eutf8() test case 19: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls within an inserted 3-byte Zero
	// width space after a backslash, where an existing 3-byte Zero Width Space
	// and two uppercase hex digits follow.

	// to_eutf8() test case 20: dstsize is too small to hold the eutf8 encoded
	// result, but the dstsize boundary falls after a backslash, were an
	// inserted 3-byte Zero width space, an existing 3-byte Zero Width Space and
	// two uppercase hex digits follow.


	// The use of double-double quotes ("") in testraw and teste8_bysize[] are
	// required to avoid a "hex escape sequence out of range" error because
	// "\x8BDB" looks to the compiler like it contains a 4-digit hex sequence
	// and thus it must be written as "\x8B""DB".

	// \x15 is a single byte C0 control code "NAK" that requires an escape.
	// \xD0 is valid as a UTF-8 first byte, but is invalid if not followed by
	//   a second byte of 0x90-0xBF.
	// \xC2\xA9 is \u00A9, the copyright symbole (©).
	// \xC2\x94 is \x0094, a C1 control character, "CCH" Cancel Character "ESC T".
	// \\CF (where the \\ indicates a single literal backslash) is a sequence of
	//   ASCII characters (a backslash followed by two uppercase hex digits)
	//   that could be mistaken as a eutf8 escape sequence, so that to_eutf8()
	//   inserts a Zero Width Space after the backslash to ensure that
	//   data == from_eutf8(to_eutf8(data)).
	// \xE2\x80\x8B is a Zero Width Space, such that \\\xE2\x80\x8BDB is a
	//   backslash followed by a Zero Width Space followed by two uppercase hex
	//   characters.  to_eutf8() inserts an additional Zero Width Space after
	//   the backslash to ensure that  data == from_eutf8(to_eutf8(data)).

	char testraw[] = "\\\xE2\x80\x8B""DB\\CF\xC2\x94\xC2\xA9\xD0\x15""AB";
	char * teste8_bysize[] = {
		"", // dstsize = 0 + 1
		"?", // dstsize = 1 + 1
		"??",
		"???",
		"\\\xE2\x80\x8B",
		"\\\xE2\x80\x8B?",  // 5 + 1
		"\\\xE2\x80\x8B??",
		"\\\xE2\x80\x8B\xE2\x80\x8B",
		"\\\xE2\x80\x8B\xE2\x80\x8B""D",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB?",  // 10 + 1
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB??",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB???",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""C",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF",  // 15 + 1
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF?",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF??",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF???",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF????",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF?????",  // 20 + 1
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94?",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9?",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9??",  // 25 + 1
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9\\D0",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9\\D0?",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9\\D0??",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9\\D0\\15",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9\\D0\\15A",
		"\\\xE2\x80\x8B\xE2\x80\x8B""DB\\\xE2\x80\x8B""CF\\C2\\94\xC2\xA9\\D0\\15AB",
	};

	for (size_t i = 0; i < sizeof(teste8_bysize) / sizeof(teste8_bysize[0]); ++i) {
		tmplen = eutf8(tmpdata, i + 1, testraw, strlen(testraw));
		assert_int_equal(tmplen, i + 1);
		assert_string_equal(tmpdata, teste8_bysize[i]);
	}
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_to_eutf8),
		cmocka_unit_test(test_eutf8),
	};

	ardop_test_setup();
	return cmocka_run_group_tests(tests, NULL, NULL);
}
