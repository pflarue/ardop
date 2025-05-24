#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include "setup.h"
#include "common/txframe.h"

// TODO: Add tests for txframe()

static void test_escapeeol(void** state) {
	(void) state;  // This line avoids an unused parameter warning
	char src[64] = "";
	char dst[65] = "";

	strcpy(src, "test");
	assert_int_equal(escapeeol(dst, sizeof(dst), src), strlen(src));
	assert_string_equal(dst, src);

	strcpy(src, "test\nline2");
	assert_int_equal(escapeeol(dst, sizeof(dst), src), strlen(src) + 1);
	assert_string_equal(dst, "test\\nline2");

	strcpy(src, "test\rline2");
	assert_int_equal(escapeeol(dst, sizeof(dst), src), strlen(src) + 1);
	assert_string_equal(dst, "test\\rline2");

	strcpy(src, "\nline2");
	assert_int_equal(escapeeol(dst, sizeof(dst), src), strlen(src) + 1);
	assert_string_equal(dst, "\\nline2");

	strcpy(src, "\rline2");
	assert_int_equal(escapeeol(dst, sizeof(dst), src), strlen(src) + 1);
	assert_string_equal(dst, "\\rline2");

	// If src contains characters outside of 0x20 - 0x7E other than \n (0x0A)
	// and \r (0x0D), it returns false and contents of dst on return is
	// undefined.
	strcpy(src, "test\t");
	assert_int_equal(escapeeol(dst, sizeof(dst), src), -1);
	strcpy(src, "test\x1F");
	assert_int_equal(escapeeol(dst, sizeof(dst), src), -1);
	strcpy(src, "test\x7F");
	assert_int_equal(escapeeol(dst, sizeof(dst), src), -1);
	strcpy(src, "test\xFF");
	assert_int_equal(escapeeol(dst, sizeof(dst), src), -1);

	// empty str
	strcpy(src, "");
	assert_int_equal(escapeeol(dst, sizeof(dst), src), strlen(src));
	assert_string_equal(dst, "");


	strcpy(src, "test");
	assert_int_equal(escapeeol(dst, strlen(src) + 1, src), strlen(src));
	// insuffient size (must be 1 greater than strlen(dst) for NULL)
	assert_int_equal(escapeeol(dst, strlen(src), src), -1);

	strcpy(src, "test\n");
	assert_int_equal(escapeeol(dst, strlen(src) + 2, src), strlen(src) + 1);
	// insuffient size (must be 1 greater than strlen(dst) for NULL)
	// Here strlen(dst) = strlen(src) + 1 because the single character
	// \n is replaced with two characters
	assert_int_equal(escapeeol(dst, strlen(src) + 1, src), -1);
}

static void test_parseeol(void** state) {
	(void) state;  // This line avoids an unused parameter warning
	char src[64] = "";
	char dst[65] = "";

	strcpy(src, "test");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src));
	assert_string_equal(dst, src);

	strcpy(src, "\\test");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src));
	assert_string_equal(dst, src);

	strcpy(src, "test\\");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src));
	assert_string_equal(dst, src);


	strcpy(src, "tes\\t");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src));
	assert_string_equal(dst, src);

	strcpy(src, "tes\\\\t");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src));
	assert_string_equal(dst, src);

	strcpy(src, "test\\r");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 1);
	assert_string_equal(dst, "test\r");

	strcpy(src, "test\\n");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 1);
	assert_string_equal(dst, "test\n");

	strcpy(src, "test\\r\\n");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 2);
	assert_string_equal(dst, "test\r\n");

	strcpy(src, "test\\n\\r");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 2);
	assert_string_equal(dst, "test\n\r");

	strcpy(src, "test\\\\r");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 1);
	assert_string_equal(dst, "test\\r");

	strcpy(src, "test\\\\n");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 1);
	assert_string_equal(dst, "test\\n");


	strcpy(src, "test\\rline2");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 1);
	assert_string_equal(dst, "test\rline2");

	strcpy(src, "test\\nline2");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 1);
	assert_string_equal(dst, "test\nline2");

	strcpy(src, "test\\r\\nline2");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 2);
	assert_string_equal(dst, "test\r\nline2");

	strcpy(src, "test\\n\\rline2");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 2);
	assert_string_equal(dst, "test\n\rline2");

	strcpy(src, "test\\\\rline2");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 1);
	assert_string_equal(dst, "test\\rline2");

	strcpy(src, "test\\\\nline2");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 1);
	assert_string_equal(dst, "test\\nline2");


	strcpy(src, "test\\rline2\\rline3");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 2);
	assert_string_equal(dst, "test\rline2\rline3");

	strcpy(src, "test\\nline2\\nline3");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 2);
	assert_string_equal(dst, "test\nline2\nline3");

	strcpy(src, "test\\r\\nline2\\r\\nline3");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 4);
	assert_string_equal(dst, "test\r\nline2\r\nline3");

	strcpy(src, "test\\n\\rline2\\n\\rline3");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 4);
	assert_string_equal(dst, "test\n\rline2\n\rline3");

	strcpy(src, "test\\\\rline2\\\\rline3");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 2);
	assert_string_equal(dst, "test\\rline2\\rline3");

	strcpy(src, "test\\\\nline2\\\\nline3");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 2);
	assert_string_equal(dst, "test\\nline2\\nline3");


	strcpy(src, "\\rline2");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 1);
	assert_string_equal(dst, "\rline2");

	strcpy(src, "\\nline2");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 1);
	assert_string_equal(dst, "\nline2");

	strcpy(src, "\\r\\nline2");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 2);
	assert_string_equal(dst, "\r\nline2");

	strcpy(src, "\\n\\rline2");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 2);
	assert_string_equal(dst, "\n\rline2");

	strcpy(src, "\\\\rline2");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 1);
	assert_string_equal(dst, "\\rline2");

	strcpy(src, "\\\\nline2");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src) - 1);
	assert_string_equal(dst, "\\nline2");

	// If src contains characters outside of 0x20 - 0x7E, it returns false
	// and contents of dst on return is undefined.
	strcpy(src, "test\n");
	assert_int_equal(parseeol(dst, sizeof(dst), src), -1);
	strcpy(src, "test\r");
	assert_int_equal(parseeol(dst, sizeof(dst), src), -1);
	strcpy(src, "test\t");
	assert_int_equal(parseeol(dst, sizeof(dst), src), -1);
	strcpy(src, "test\x1F");
	assert_int_equal(parseeol(dst, sizeof(dst), src), -1);
	strcpy(src, "test\x7F");
	assert_int_equal(parseeol(dst, sizeof(dst), src), -1);
	strcpy(src, "test\xFF");
	assert_int_equal(parseeol(dst, sizeof(dst), src), -1);

	// empty str
	strcpy(src, "");
	assert_int_equal(parseeol(dst, sizeof(dst), src), strlen(src));
	assert_string_equal(dst, "");


	strcpy(src, "test");
	assert_int_equal(parseeol(dst, strlen(src) + 1, src), strlen(src));
	// insuffient size (must be 1 greater than strlen(dst) for NULL)
	assert_int_equal(parseeol(dst, strlen(src), src), -1);

	strcpy(src, "test\\n");
	// Even through strlen(dst) = strlen(src) - 1 because the two characters \\n
	// are replaced by the single character \n, size must be at least
	// strlen(src) + 1.
	assert_int_equal(parseeol(dst, strlen(src) + 1, src), strlen(src) - 1);
	// insuffient size
	assert_int_equal(parseeol(dst, strlen(src), src), -1);
}

// test hex2bytes() and bytes2hex()
static void test_hex2bytes(void** state) {
	char str[64] = "";
	int len;
	char restored[64] = "";
	unsigned char bytes[32];
	(void) state;  // This line avoids an unused parameter warning

	strcpy(str, "000102A0A1A2F0F1FF");
	len = strlen(str) / 2;
	assert_int_equal(hex2bytes(str, len, bytes), 0);
	assert_memory_equal(bytes, "\x00\x01\x02\xA0\xA1\xA2\xF0\xF1\xFF", len);
	bytes2hex(restored, sizeof(restored), bytes, len, false);
	assert_string_equal(str, restored);

	assert_int_equal(bytes2hex(str, 19, bytes, 9, false), 18);
	// output is truncated if count is too small
	assert_int_equal(bytes2hex(str, 18, bytes, 9, false), 16);
	// bytes2hex() only fails producing -1 if count is 0
	assert_int_equal(bytes2hex(str, 0, bytes, 9, false), -1);

	assert_int_equal(bytes2hex(str, 27, bytes, 9, true), 26);
	// output is truncated if count is too small
	assert_int_equal(bytes2hex(str, 26, bytes, 9, true), 23);
	// bytes2hex() only fails producing -1 if count is 0
	assert_int_equal(bytes2hex(str, 0, bytes, 9, true), -1);


	// lowercase hex. OK
	strcpy(str, "000102A0a1a2f0f1ff");
	len = strlen(str) / 2;
	assert_int_equal(hex2bytes(str, len, bytes), 0);
	assert_memory_equal(bytes, "\x00\x01\x02\xA0\xA1\xA2\xF0\xF1\xFF", len);
	bytes2hex(restored, sizeof(restored), bytes, len, false);
	assert_string_equal("000102A0A1A2F0F1FF", restored);

	// len < strlen(str) / 2.  OK
	strcpy(str, "000102A0A1A2F0F1FF");
	len = strlen(str) / 2 - 2;
	assert_int_equal(hex2bytes(str, len, bytes), 0);
	assert_memory_equal(bytes, "\x00\x01\x02\xA0\xA1\xA2\xF0", len);
	bytes2hex(restored, sizeof(restored), bytes, len, false);
	str[2 * len] = 0x00;  // truncate
	assert_string_equal(str, restored);

	// len > strlen(str) / 2.  Fail
	strcpy(str, "000102A0A1A2F0F1FF");
	len = strlen(str) / 2 + 1;
	assert_int_equal(hex2bytes(str, len, bytes), 1);

	// contains invalid hex.  Fail
	strcpy(str, "000102A0A1A2F0F1FFFX");
	len = strlen(str) / 2;
	assert_int_equal(hex2bytes(str, len, bytes), 1);

	// contains invalid hex (including whitespace).  Fail
	strcpy(str, "000102A0A1A2F0F1FFFX  ");
	len = strlen(str) / 2;
	assert_int_equal(hex2bytes(str, len, bytes), 1);

	// Odd number of characters
	strcpy(str, "000102A0A1A2F0F1F");
	len = strlen(str) / 2;  // ignore last character
	assert_int_equal(hex2bytes(str, len, bytes), 0);
	assert_memory_equal(bytes, "\x00\x01\x02\xA0\xA1\xA2\xF0\xF1", len);
	bytes2hex(restored, sizeof(restored), bytes, len, false);
	str[2 * len] = 0x00;  // truncate
	assert_string_equal(str, restored);

	// empty str
	strcpy(str, "");
	len = 0;
	assert_int_equal(hex2bytes(str, len, bytes), 0);

	// empty str
	assert_int_equal(bytes2hex(str, 1, bytes, 0, false), 0);
	assert_string_equal(str, "");
	assert_int_equal(bytes2hex(str, 0, bytes, 0, false), -1);
}

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_escapeeol),
		cmocka_unit_test(test_parseeol),
		cmocka_unit_test(test_hex2bytes),
	};

	ardop_test_setup();
	return cmocka_run_group_tests(tests, NULL, NULL);
}
