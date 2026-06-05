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

// --- KISSFragmentBuild() ----------------------------------------------------

// Fill buf with a recognizable byte pattern.
static void fill_pattern(UCHAR *buf, int len)
{
	for (int i = 0; i < len; i++)
		buf[i] = (UCHAR)(i & 0xFF);
}

static void test_KISSFragmentBuild_single(void **state)
{
	(void)state;
	UCHAR ax[256], out[512];
	fill_pattern(ax, sizeof(ax));

	// A frame that fits in one fragment (50 <= chunk = 126).
	int n = KISSFragmentBuild(ax, 50, 128, 0x2A, out, sizeof(out));
	assert_int_equal(n, KISS_FRAG_HDR + 50);
	assert_int_equal(out[0], 0x2A);  // msgid
	assert_int_equal(out[1], 0x80 | 0);  // last flag, index 0
	assert_memory_equal(out + KISS_FRAG_HDR, ax, 50);

	// len exactly == chunk (126) is still a single fragment of framecap bytes.
	n = KISSFragmentBuild(ax, 126, 128, 0x01, out, sizeof(out));
	assert_int_equal(n, 128);
	assert_int_equal(out[1], 0x80 | 0);
	assert_memory_equal(out + KISS_FRAG_HDR, ax, 126);
}

static void test_KISSFragmentBuild_two(void **state)
{
	(void)state;
	UCHAR ax[256], out[512];
	fill_pattern(ax, sizeof(ax));

	// 200 bytes, chunk = 126 -> 2 fragments (126 + 74).
	int n = KISSFragmentBuild(ax, 200, 128, 0x07, out, sizeof(out));
	assert_int_equal(n, 128 + (KISS_FRAG_HDR + 74));

	// Fragment 0: full framecap, index 0, not last.
	assert_int_equal(out[0], 0x07);
	assert_int_equal(out[1], 0x00);
	assert_memory_equal(out + KISS_FRAG_HDR, ax, 126);

	// Fragment 1 begins at offset framecap (128): index 1, last.
	assert_int_equal(out[128], 0x07);
	assert_int_equal(out[129], 0x80 | 1);
	assert_memory_equal(out + 128 + KISS_FRAG_HDR, ax + 126, 74);
}

static void test_KISSFragmentBuild_exact_multiple(void **state)
{
	(void)state;
	UCHAR ax[512], out[512];
	fill_pattern(ax, sizeof(ax));

	// len = 3 * chunk -> 3 full fragments, the last one full size but flagged.
	int len = 3 * 126;
	int n = KISSFragmentBuild(ax, len, 128, 0x00, out, sizeof(out));
	assert_int_equal(n, 3 * 128);

	assert_int_equal(out[1], 0x00);  // frag 0: index 0, not last
	assert_int_equal(out[128 + 1], 0x01);  // frag 1: index 1, not last
	assert_int_equal(out[256 + 1], 0x80 | 2);  // frag 2: index 2, last
}

static void test_KISSFragmentBuild_errors(void **state)
{
	(void)state;
	UCHAR ax[256], out[1024];
	fill_pattern(ax, sizeof(ax));

	// len <= 0
	assert_int_equal(KISSFragmentBuild(ax, 0, 128, 0, out, sizeof(out)), -1);
	// framecap <= header (no room for payload)
	assert_int_equal(KISSFragmentBuild(ax, 10, KISS_FRAG_HDR, 0, out, sizeof(out)), -1);
	// out too small
	assert_int_equal(KISSFragmentBuild(ax, 50, 128, 0, out, 10), -1);

	// More than KISS_FRAG_MAXFRAGS fragments: framecap = HDR + 1 -> chunk 1.
	UCHAR big[KISS_FRAG_MAXFRAGS + 8];
	fill_pattern(big, sizeof(big));
	assert_int_equal(
		KISSFragmentBuild(big, KISS_FRAG_MAXFRAGS + 1, KISS_FRAG_HDR + 1, 0,
			out, sizeof(out)), -1);
	// Exactly KISS_FRAG_MAXFRAGS fragments is allowed.
	int n = KISSFragmentBuild(big, KISS_FRAG_MAXFRAGS, KISS_FRAG_HDR + 1, 0,
		out, sizeof(out));
	assert_int_equal(n, KISS_FRAG_MAXFRAGS * (KISS_FRAG_HDR + 1));
}

// --- KISSReassembleFrame() --------------------------------------------------

// Carve a KISSFragmentBuild() output buffer into framecap-sized FEC frames (the
// last frame is the remainder) and feed each through the reassembler.  Stores
// the final return value in *finalret and returns the number of pieces fed.
static int carve_and_feed(KISSReassembler *r, const UCHAR *buf, int buflen,
	int framecap, UCHAR *out, int outsize, int *finalret)
{
	int npieces = 0, ret = 0;
	for (int off = 0; off < buflen; off += framecap)
	{
		int plen = buflen - off;
		if (plen > framecap)
			plen = framecap;
		ret = KISSReassembleFrame(r, buf + off, plen, out, outsize);
		npieces++;
	}
	*finalret = ret;
	return npieces;
}

static void test_KISSReassemble_roundtrip_single(void **state)
{
	(void)state;
	UCHAR ax[256], frags[512], out[KISS_FRAME_MAX];
	fill_pattern(ax, sizeof(ax));

	int n = KISSFragmentBuild(ax, 50, 128, 0x11, frags, sizeof(frags));
	assert_true(n > 0);

	KISSReassembler r;
	KISSReassemblerReset(&r);
	int ret = 0;
	int pieces = carve_and_feed(&r, frags, n, 128, out, sizeof(out), &ret);
	assert_int_equal(pieces, 1);
	assert_int_equal(ret, 50);
	assert_memory_equal(out, ax, 50);
}

static void test_KISSReassemble_roundtrip_three(void **state)
{
	(void)state;
	UCHAR ax[512], frags[1024], out[KISS_FRAME_MAX];
	fill_pattern(ax, sizeof(ax));

	// 300 bytes, chunk 126 -> 3 fragments.
	int n = KISSFragmentBuild(ax, 300, 128, 0x55, frags, sizeof(frags));
	assert_true(n > 0);

	KISSReassembler r;
	KISSReassemblerReset(&r);
	int ret = 0;
	int pieces = carve_and_feed(&r, frags, n, 128, out, sizeof(out), &ret);
	assert_int_equal(pieces, 3);
	assert_int_equal(ret, 300);
	assert_memory_equal(out, ax, 300);
}

static void test_KISSReassemble_gap(void **state)
{
	(void)state;
	UCHAR out[KISS_FRAME_MAX];
	KISSReassembler r;
	KISSReassemblerReset(&r);

	UCHAR f0[] = {5, 0x00, 'a', 'b'};  // index 0, not last
	assert_int_equal(KISSReassembleFrame(&r, f0, sizeof(f0), out, sizeof(out)), 0);
	UCHAR f2[] = {5, 0x80 | 2, 'x', 'y'};  // index 2, last -> gap (expected 1)
	assert_int_equal(KISSReassembleFrame(&r, f2, sizeof(f2), out, sizeof(out)), -1);
}

static void test_KISSReassemble_wrong_msgid(void **state)
{
	(void)state;
	UCHAR out[KISS_FRAME_MAX];
	KISSReassembler r;
	KISSReassemblerReset(&r);

	UCHAR f0[] = {5, 0x00, 'a'};  // index 0, msgid 5
	assert_int_equal(KISSReassembleFrame(&r, f0, sizeof(f0), out, sizeof(out)), 0);
	UCHAR f1[] = {6, 0x80 | 1, 'b'};  // index 1, last, but msgid 6
	assert_int_equal(KISSReassembleFrame(&r, f1, sizeof(f1), out, sizeof(out)), -1);
}

static void test_KISSReassemble_start_midstream(void **state)
{
	(void)state;
	UCHAR out[KISS_FRAME_MAX];
	KISSReassembler r;
	KISSReassemblerReset(&r);

	// First fragment seen has a non-zero index: cannot start, ignored.
	UCHAR f1[] = {5, 0x80 | 1, 'b'};
	assert_int_equal(KISSReassembleFrame(&r, f1, sizeof(f1), out, sizeof(out)), -1);
}

static void test_KISSReassemble_restart(void **state)
{
	(void)state;
	UCHAR out[KISS_FRAME_MAX];
	KISSReassembler r;
	KISSReassemblerReset(&r);

	// A partial in progress...
	UCHAR a[] = {5, 0x00, 'a', 'b'};  // index 0, not last
	assert_int_equal(KISSReassembleFrame(&r, a, sizeof(a), out, sizeof(out)), 0);
	// ...then a fresh index-0 fragment restarts cleanly and completes.
	UCHAR b[] = {7, 0x80 | 0, 'z'};  // index 0, last, msgid 7
	assert_int_equal(KISSReassembleFrame(&r, b, sizeof(b), out, sizeof(out)), 1);
	assert_int_equal(out[0], 'z');
}

static void test_KISSReassemble_runt(void **state)
{
	(void)state;
	UCHAR out[KISS_FRAME_MAX];
	KISSReassembler r;
	KISSReassemblerReset(&r);

	UCHAR f0[] = {5, 0x00, 'a'};  // index 0, not last -> partial in progress
	assert_int_equal(KISSReassembleFrame(&r, f0, sizeof(f0), out, sizeof(out)), 0);
	UCHAR runt[] = {9};  // shorter than the header: ignored
	assert_int_equal(KISSReassembleFrame(&r, runt, sizeof(runt), out, sizeof(out)), -1);
	// The runt must not have disturbed the partial: the next fragment completes.
	UCHAR f1[] = {5, 0x80 | 1, 'b'};  // index 1, last
	assert_int_equal(KISSReassembleFrame(&r, f1, sizeof(f1), out, sizeof(out)), 2);
	UCHAR exp[] = {'a', 'b'};
	assert_memory_equal(out, exp, sizeof(exp));
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
		cmocka_unit_test(test_KISSFragmentBuild_single),
		cmocka_unit_test(test_KISSFragmentBuild_two),
		cmocka_unit_test(test_KISSFragmentBuild_exact_multiple),
		cmocka_unit_test(test_KISSFragmentBuild_errors),
		cmocka_unit_test(test_KISSReassemble_roundtrip_single),
		cmocka_unit_test(test_KISSReassemble_roundtrip_three),
		cmocka_unit_test(test_KISSReassemble_gap),
		cmocka_unit_test(test_KISSReassemble_wrong_msgid),
		cmocka_unit_test(test_KISSReassemble_start_midstream),
		cmocka_unit_test(test_KISSReassemble_restart),
		cmocka_unit_test(test_KISSReassemble_runt),
	};

	ardop_test_setup();
	return cmocka_run_group_tests(tests, NULL, NULL);
}
