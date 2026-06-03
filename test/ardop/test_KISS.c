#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include <stdbool.h>

#include "setup.h"

#include "common/ARDOPC.h"
#include "common/ardopcommon.h"

// KISS special characters (kept private to KISS.c, redefined here for tests)
#define FEND  0xC0
#define FESC  0xDB
#define TFEND 0xDC
#define TFESC 0xDD

// --- KISSConfig() -----------------------------------------------------------

static void test_KISSConfig(void **state)
{
	(void)state;

	// Port only -> empty address (loopback)
	KISSPort = -1;
	strcpy(KISSAddr, "dirty");
	assert_true(KISSConfig("8001"));
	assert_int_equal(KISSPort, 8001);
	assert_string_equal(KISSAddr, "");

	// address:port
	assert_true(KISSConfig("192.168.1.5:1234"));
	assert_int_equal(KISSPort, 1234);
	assert_string_equal(KISSAddr, "192.168.1.5");

	// listen on all interfaces
	assert_true(KISSConfig("0.0.0.0:8765"));
	assert_int_equal(KISSPort, 8765);
	assert_string_equal(KISSAddr, "0.0.0.0");

	// Invalid: port 0, out of range, non-numeric, empty, NULL
	assert_false(KISSConfig("0"));
	assert_int_equal(KISSPort, 0);
	assert_false(KISSConfig("70000"));  // > 65535
	assert_false(KISSConfig("abc"));  // atoi -> 0
	assert_false(KISSConfig("10.0.0.1:0"));  // valid addr, bad port
	assert_false(KISSConfig(""));
	assert_false(KISSConfig(NULL));
}

// --- KISSModeCapacity() -----------------------------------------------------

static void test_KISSModeCapacity(void **state)
{
	(void)state;

	// Values come from the real FrameSize[] table in ARDOPC.c.
	assert_int_equal(KISSModeCapacity("4FSK.500.100"), 64);  // the default mode
	assert_int_equal(KISSModeCapacity("4PSK.500.100"), 128);
	assert_int_equal(KISSModeCapacity("16QAM.500.100"), 256);
	assert_int_equal(KISSModeCapacity("4FSK.2000.600"), 600);
	assert_int_equal(KISSModeCapacity("16QAM.2000.100"), 1024);

	// Unknown mode -> 0 (which the caller treats as a hard failure)
	assert_int_equal(KISSModeCapacity("BOGUS.MODE"), 0);
	assert_int_equal(KISSModeCapacity(""), 0);
}

// --- KISSEncode() -----------------------------------------------------------

static void test_KISSEncode(void **state)
{
	(void)state;
	UCHAR out[64];

	// Plain payload: FEND, type(0x00), payload, FEND
	UCHAR ax[] = {0x82, 0xA0, 0x01};
	int n = KISSEncode(ax, sizeof(ax), out, sizeof(out));
	UCHAR exp[] = {FEND, 0x00, 0x82, 0xA0, 0x01, FEND};
	assert_int_equal(n, (int)sizeof(exp));
	assert_memory_equal(out, exp, sizeof(exp));

	// Payload containing FEND and FESC must be escaped
	UCHAR ax2[] = {FEND, FESC, 0x10};
	int n2 = KISSEncode(ax2, sizeof(ax2), out, sizeof(out));
	UCHAR exp2[] = {FEND, 0x00, FESC, TFEND, FESC, TFESC, 0x10, FEND};
	assert_int_equal(n2, (int)sizeof(exp2));
	assert_memory_equal(out, exp2, sizeof(exp2));

	// Empty payload -> FEND, type, FEND
	int n3 = KISSEncode(NULL, 0, out, sizeof(out));
	assert_int_equal(n3, 3);
	assert_int_equal(out[0], FEND);
	assert_int_equal(out[1], 0x00);
	assert_int_equal(out[2], FEND);

	// Output buffer too small -> -1 (no overrun)
	UCHAR small[4];
	assert_int_equal(KISSEncode(ax, sizeof(ax), small, sizeof(small)), -1);
	assert_int_equal(KISSEncode(ax, sizeof(ax), out, 2), -1);
}

// --- KISSDecoderByte() ------------------------------------------------------

// Feed a buffer of bytes through the decoder.  Copies the most recently
// completed frame into outframe/outlen and counts completed frames.
static void feed(KISSDecoder *d, const UCHAR *buf, int len,
	UCHAR *outframe, int *outlen, int *nframes)
{
	*nframes = 0;
	*outlen = 0;
	for (int i = 0; i < len; i++)
	{
		int f = KISSDecoderByte(d, buf[i]);
		if (f > 0)
		{
			(*nframes)++;
			memcpy(outframe, d->frame, f);
			*outlen = f;
		}
	}
}

static void test_KISSDecode_simple(void **state)
{
	(void)state;
	KISSDecoder d;
	KISSDecoderReset(&d);
	UCHAR fr[64];
	int fl, nf;

	UCHAR in[] = {FEND, 0x00, 0x11, 0x22, FEND};
	feed(&d, in, sizeof(in), fr, &fl, &nf);
	assert_int_equal(nf, 1);
	assert_int_equal(fl, 3);
	UCHAR exp[] = {0x00, 0x11, 0x22};
	assert_memory_equal(fr, exp, sizeof(exp));
}

static void test_KISSDecode_extra_fends(void **state)
{
	(void)state;
	KISSDecoder d;
	KISSDecoderReset(&d);
	UCHAR fr[64];
	int fl, nf;

	// Leading and trailing/back-to-back FENDs are just delimiters.
	UCHAR in[] = {FEND, FEND, 0x00, 0x41, FEND, FEND};
	feed(&d, in, sizeof(in), fr, &fl, &nf);
	assert_int_equal(nf, 1);
	assert_int_equal(fl, 2);
	UCHAR exp[] = {0x00, 0x41};
	assert_memory_equal(fr, exp, sizeof(exp));
}

static void test_KISSDecode_escaping(void **state)
{
	(void)state;
	KISSDecoder d;
	KISSDecoderReset(&d);
	UCHAR fr[64];
	int fl, nf;

	// FESC TFEND -> FEND, FESC TFESC -> FESC
	UCHAR in[] = {FEND, 0x00, FESC, TFEND, FESC, TFESC, 0x10, FEND};
	feed(&d, in, sizeof(in), fr, &fl, &nf);
	assert_int_equal(nf, 1);
	assert_int_equal(fl, 4);
	UCHAR exp[] = {0x00, FEND, FESC, 0x10};
	assert_memory_equal(fr, exp, sizeof(exp));
}

static void test_KISSDecode_bad_escape(void **state)
{
	(void)state;
	KISSDecoder d;
	KISSDecoderReset(&d);
	UCHAR fr[64];
	int fl, nf;

	// FESC followed by neither TFEND nor TFESC: store the byte as-is.
	UCHAR in[] = {FEND, 0x00, FESC, 0x99, FEND};
	feed(&d, in, sizeof(in), fr, &fl, &nf);
	assert_int_equal(nf, 1);
	assert_int_equal(fl, 2);
	UCHAR exp[] = {0x00, 0x99};
	assert_memory_equal(fr, exp, sizeof(exp));
}

static void test_KISSDecode_two_frames(void **state)
{
	(void)state;
	KISSDecoder d;
	KISSDecoderReset(&d);
	UCHAR fr[64];
	int fl, nf;

	UCHAR in[] = {FEND, 0x00, 0x41, FEND, 0x00, 0x42, FEND};
	feed(&d, in, sizeof(in), fr, &fl, &nf);
	assert_int_equal(nf, 2);
	// fr holds the last completed frame
	assert_int_equal(fl, 2);
	UCHAR exp[] = {0x00, 0x42};
	assert_memory_equal(fr, exp, sizeof(exp));
}

static void test_KISSDecode_split(void **state)
{
	(void)state;
	KISSDecoder d;
	KISSDecoderReset(&d);
	UCHAR fr[64];
	int fl, nf;

	// A frame split across two reads should reassemble.
	UCHAR in1[] = {FEND, 0x00, 0x41};
	feed(&d, in1, sizeof(in1), fr, &fl, &nf);
	assert_int_equal(nf, 0);

	UCHAR in2[] = {0x42, 0x43, FEND};
	feed(&d, in2, sizeof(in2), fr, &fl, &nf);
	assert_int_equal(nf, 1);
	assert_int_equal(fl, 4);
	UCHAR exp[] = {0x00, 0x41, 0x42, 0x43};
	assert_memory_equal(fr, exp, sizeof(exp));
}

static void test_KISSDecode_overflow(void **state)
{
	(void)state;
	KISSDecoder d;
	KISSDecoderReset(&d);
	UCHAR fr[KISS_FRAME_MAX + 16];
	int fl, nf;

	// Build a frame larger than KISS_FRAME_MAX; it must be discarded, not
	// overrun the buffer.
	UCHAR big[KISS_FRAME_MAX + 64];
	int p = 0;
	big[p++] = FEND;
	for (int i = 0; i < KISS_FRAME_MAX + 32; i++)
		big[p++] = 0x41;
	big[p++] = FEND;
	feed(&d, big, p, fr, &fl, &nf);
	assert_int_equal(nf, 0);  // oversize frame dropped

	// The decoder must recover and decode the next, valid frame.
	UCHAR ok[] = {FEND, 0x00, 0x55, FEND};
	feed(&d, ok, sizeof(ok), fr, &fl, &nf);
	assert_int_equal(nf, 1);
	assert_int_equal(fl, 2);
	UCHAR exp[] = {0x00, 0x55};
	assert_memory_equal(fr, exp, sizeof(exp));
}

// --- round trip -------------------------------------------------------------

static void test_KISS_roundtrip(void **state)
{
	(void)state;

	// Payload deliberately includes bytes that require escaping.
	UCHAR ax[] = {FEND, FESC, 0x01, 0x02, TFEND, 0x00, FESC};
	UCHAR enc[64];
	int n = KISSEncode(ax, sizeof(ax), enc, sizeof(enc));
	assert_true(n > 0);

	KISSDecoder d;
	KISSDecoderReset(&d);
	UCHAR fr[64];
	int fl, nf;
	feed(&d, enc, n, fr, &fl, &nf);

	assert_int_equal(nf, 1);
	// Decoded frame is the data type byte (0x00) followed by the original AX.25
	// frame.
	assert_int_equal(fl, (int)sizeof(ax) + 1);
	assert_int_equal(fr[0], 0x00);
	assert_memory_equal(fr + 1, ax, sizeof(ax));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_KISSConfig),
		cmocka_unit_test(test_KISSModeCapacity),
		cmocka_unit_test(test_KISSEncode),
		cmocka_unit_test(test_KISSDecode_simple),
		cmocka_unit_test(test_KISSDecode_extra_fends),
		cmocka_unit_test(test_KISSDecode_escaping),
		cmocka_unit_test(test_KISSDecode_bad_escape),
		cmocka_unit_test(test_KISSDecode_two_frames),
		cmocka_unit_test(test_KISSDecode_split),
		cmocka_unit_test(test_KISSDecode_overflow),
		cmocka_unit_test(test_KISS_roundtrip),
	};

	ardop_test_setup();
	return cmocka_run_group_tests(tests, NULL, NULL);
}
