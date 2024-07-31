#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include <stdio.h>
#include <stdlib.h>

#include "setup.h"

#include "common/StationId.h"

char stationid_ssid_pack(const StationId* station);
ARDOP_MUSTUSE station_id_err
stationid_ssid_unpack(const char ssid_byte, StationId* station);

/*
 * Check that the given `station` ID (with optional SSID) has the given
 * compressed `hexstr` representation in the ARDOP protocol. The given
 * `hexstr` must be 12 characters of hexadecimal like "b908e1b2c010"
 */
static void assert_stationid_to_wireline_is(const char* station, const char* hexstr)
{
	char compressed_hex[17] = "";

	StationId id;
	assert_int_equal(STATIONID_OK, stationid_from_str(station, &id));

	for (size_t i = 0; i < sizeof(id.wire); ++i) {
		snprintf(&compressed_hex[2 * i], 3, "%02hhx", id.wire.b[i]);
	}
	assert_string_equal(hexstr, compressed_hex);
}

/*
 * Check that the given `station` ID (with optional SSID) has the given
 * compressed `hexstr` representation in the ARDOP protocol. The given
 * `hexstr` must be 12 characters of hexadecimal like "b908e1b2c010"
 */
static void assert_stationid_from_wireline_is(
	const char* hexstr, const char* callsign, const char* ssid)
{
	// hex to bytes
	Packed6 wireline;
	for (size_t i = 0; i < sizeof(wireline.b); ++i) {
		sscanf(&hexstr[2 * i], "%2hhx", &wireline.b[i]);
	}

	StationId id;
	assert_int_equal(
		STATIONID_OK,
		stationid_from_bytes(&wireline.b[0], sizeof(wireline.b), &id)
	);

	assert_string_equal(id.call, callsign);
	assert_string_equal(id.ssid, ssid);
}

/* test SSID conversion */
static void test_stationid_ssid_pack(void** state) {
	(void)state; /* unused */

	StationId id;
	stationid_init(&id);

	// default SSID is 0
	assert_int_equal('0', stationid_ssid_pack(&id));

	// empty SSID not valid
	memset(&id, 0, sizeof(id));
	assert_int_equal(0, stationid_ssid_pack(&id));

	// valid SSIDs

	snprintf(id.ssid, sizeof(id.ssid), "%s", "A");
	assert_int_equal('A', stationid_ssid_pack(&id));

	snprintf(id.ssid, sizeof(id.ssid), "%s", "Z");
	assert_int_equal('Z', stationid_ssid_pack(&id));

	snprintf(id.ssid, sizeof(id.ssid), "%s", "0");
	assert_int_equal('0', stationid_ssid_pack(&id));

	snprintf(id.ssid, sizeof(id.ssid), "%s", "9");
	assert_int_equal('9', stationid_ssid_pack(&id));

	snprintf(id.ssid, sizeof(id.ssid), "%s", "10");
	assert_int_equal(':', stationid_ssid_pack(&id));

	snprintf(id.ssid, sizeof(id.ssid), "%s", "15");
	assert_int_equal('?', stationid_ssid_pack(&id));

	// numbers out of range (remember this is base 36)
	snprintf(id.ssid, sizeof(id.ssid), "%s", "-1");
	assert_int_equal(0, stationid_ssid_pack(&id));

	snprintf(id.ssid, sizeof(id.ssid), "%s", "16");
	assert_int_equal(0, stationid_ssid_pack(&id));

	snprintf(id.ssid, sizeof(id.ssid), "%s", "1A");
	assert_int_equal(0, stationid_ssid_pack(&id));

	snprintf(id.ssid, sizeof(id.ssid), "%s", "A0");
	assert_int_equal(0, stationid_ssid_pack(&id));

	// non-number rejected
	snprintf(id.ssid, sizeof(id.ssid), "%s", "0@");
	assert_int_equal(0, stationid_ssid_pack(&id));
}

/* test SSID conversion */
static void test_stationid_ssid_unpack(void** state) {
	(void)state; /* unused */

	StationId id;
	stationid_init(&id);

	assert_int_equal(STATIONID_OK, stationid_ssid_unpack('A', &id));
	assert_string_equal(id.ssid, "A");

	assert_int_equal(STATIONID_OK, stationid_ssid_unpack('Z', &id));
	assert_string_equal(id.ssid, "Z");

	assert_int_equal(STATIONID_OK, stationid_ssid_unpack('0', &id));
	assert_string_equal(id.ssid, "0");

	assert_int_equal(STATIONID_OK, stationid_ssid_unpack('9', &id));
	assert_string_equal(id.ssid, "9");

	assert_int_equal(STATIONID_OK, stationid_ssid_unpack(':', &id));
	assert_string_equal(id.ssid, "10");

	assert_int_equal(STATIONID_OK, stationid_ssid_unpack('?', &id));
	assert_string_equal(id.ssid, "15");

	// not valid SSIDs
	assert_int_equal(STATIONID_ERR_INVALID_SSID, stationid_ssid_unpack(0, &id));
	assert_int_equal(STATIONID_ERR_INVALID_SSID, stationid_ssid_unpack(' ', &id));
	assert_int_equal(STATIONID_ERR_INVALID_SSID, stationid_ssid_unpack('@', &id));
}

/* test reading callsigns from strings */
static void test_stationid_from_str(void **state)
{
	(void)state; /* unused */

	StationId id;

	stationid_init(&id);
	assert_false(stationid_ok(&id));

	// short callsign, no SSID
	assert_int_equal(STATIONID_OK, stationid_from_str("A1A", &id));
	assert_true(stationid_ok(&id));
	assert_string_equal("A1A", id.call);
	assert_string_equal("0", id.ssid);

	// silly, but OK
	assert_int_equal(STATIONID_OK, stationid_from_str("A1A-", &id));
	assert_true(stationid_ok(&id));
	assert_string_equal("A1A", id.call);
	assert_string_equal("0", id.ssid);

	// extra nulls
	assert_int_equal(STATIONID_OK, stationid_from_str_slice("A1A-15\0\0\0", sizeof("A1A-15\0\0\0"), &id));
	assert_true(stationid_ok(&id));
	assert_string_equal("A1A", id.call);
	assert_string_equal("15", id.ssid);

	// numeric SSID
	assert_int_equal(STATIONID_OK, stationid_from_str("A1A-1", &id));
	assert_true(stationid_ok(&id));
	assert_string_equal("A1A", id.call);
	assert_string_equal("1", id.ssid);

	assert_int_equal(STATIONID_OK, stationid_from_str("A1A-15", &id));
	assert_true(stationid_ok(&id));
	assert_string_equal("A1A", id.call);
	assert_string_equal("15", id.ssid);

	// alphabetical SSID
	assert_int_equal(STATIONID_OK, stationid_from_str("N0CALL-A", &id));
	assert_true(stationid_ok(&id));
	assert_string_equal("N0CALL", id.call);
	assert_string_equal("A", id.ssid);

	assert_int_equal(STATIONID_OK, stationid_from_str("N0CALL-Z", &id));
	assert_true(stationid_ok(&id));
	assert_string_equal("N0CALL", id.call);
	assert_string_equal("Z", id.ssid);

	// 7-character callsign, uppercased
	assert_int_equal(STATIONID_OK, stationid_from_str("longcal-15", &id));
	assert_true(stationid_ok(&id));
	assert_string_equal("LONGCAL", id.call);
	assert_string_equal("15", id.ssid);

	// callsign may use non-dash ASCII as long as it will compress
	assert_int_equal(STATIONID_OK, stationid_from_str("CAFE/P-2", &id));
	assert_true(stationid_ok(&id));
	assert_string_equal("CAFE/P", id.call);
	assert_string_equal("2", id.ssid);

	// too long callsign
	assert_int_equal(STATIONID_ERR_TOOLONG, stationid_from_str("CAFECAFE", &id));
	assert_false(stationid_ok(&id));

	// too short callsign
	assert_int_equal(STATIONID_ERR_INVALID_CALLSIGN, stationid_from_str("X", &id));

	// spaces not allowed
	assert_int_equal(STATIONID_ERR_INVALID_CALLSIGN, stationid_from_str("CQ M", &id));

	// invalid characters don't compress
	assert_int_equal(STATIONID_ERR_COMPRESS, stationid_from_str("CQ\t", &id));

	// too long SSID
	assert_int_equal(STATIONID_ERR_TOOLONG, stationid_from_str("CAF-234", &id));
	assert_false(stationid_ok(&id));

	// too many dashes
	assert_int_equal(STATIONID_ERR_TOOLONG, stationid_from_str("A-1-A", &id));
	assert_false(stationid_ok(&id));

	// SSID out of allowed numeric ranges
	assert_int_equal(STATIONID_ERR_INVALID_SSID, stationid_from_str("N0CALL-16", &id));
	assert_false(stationid_ok(&id));
	assert_int_equal(STATIONID_ERR_INVALID_SSID, stationid_from_str("N0CALL--1", &id));
	assert_false(stationid_ok(&id));
	assert_int_equal(STATIONID_ERR_INVALID_SSID, stationid_from_str("N0CALL-1.", &id));
	assert_false(stationid_ok(&id));

	// empty
	assert_int_equal(STATIONID_ERR_INVALID_CALLSIGN, stationid_from_str("", &id));
	assert_false(stationid_ok(&id));
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
static void test_stationid_wireline(void** state)
{
	(void)state; /* unused */

	// six character + SSID values are taken from direct
	// invocation of CompressCallsign() in ardopcf eab1f3165a30.
	assert_stationid_to_wireline_is("A1A", "851840000010");
	assert_stationid_from_wireline_is("851840000010", "A1A", "0");

	assert_stationid_to_wireline_is("A1A-1", "851840000011");
	assert_stationid_from_wireline_is("851840000011", "A1A", "1");

	assert_stationid_to_wireline_is("N0CALL", "b908e1b2c010");
	assert_stationid_from_wireline_is("b908e1b2c010", "N0CALL", "0");

	assert_stationid_to_wireline_is("N0CALL-A", "b908e1b2c021");
	assert_stationid_from_wireline_is("b908e1b2c021", "N0CALL", "A");

	assert_stationid_to_wireline_is("N0CALL-Z", "b908e1b2c03a");
	assert_stationid_from_wireline_is("b908e1b2c03a", "N0CALL", "Z");

	// non-canonical representation
	assert_stationid_to_wireline_is("N0CALL-0", "b908e1b2c010");
}

static void test_stationid_eq(void** state)
{
	(void)state; /* unused */

	StationId a;
	StationId b;

	stationid_init(&a);
	stationid_init(&b);
	assert_true(stationid_eq(&a, &b));

	assert_int_equal(STATIONID_OK, stationid_from_str("n0call", &a));
	assert_int_equal(STATIONID_OK, stationid_from_str("N0CALL", &b));
	assert_true(stationid_eq(&a, &b));

	assert_int_equal(STATIONID_OK, stationid_from_str("N0CALL-0", &a));
	assert_int_equal(STATIONID_OK, stationid_from_str("N0CALL", &b));
	assert_true(stationid_eq(&a, &b));

	assert_int_equal(STATIONID_OK, stationid_from_str("N0CALL-A", &a));
	assert_int_equal(STATIONID_OK, stationid_from_str("N0CALL", &b));
	assert_false(stationid_eq(&a, &b));
}

static void test_stationid_cq(void** state) {
	(void)state; /* unused */

	StationId cq;
	stationid_make_cq(&cq);

	assert_true(stationid_ok(&cq));
	assert_true(stationid_is_cq(&cq));
	assert_string_equal(cq.call, "CQ");
	assert_string_equal(cq.ssid, "0");

	// a regular callsign is not CQ
	StationId regular;
	assert_int_equal(STATIONID_OK, stationid_from_str("CQDX", &regular));
	assert_true(stationid_ok(&regular));
	assert_false(stationid_is_cq(&regular));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_stationid_ssid_pack),
		cmocka_unit_test(test_stationid_ssid_unpack),
		cmocka_unit_test(test_stationid_from_str),
		cmocka_unit_test(test_stationid_wireline),
		cmocka_unit_test(test_stationid_eq),
		cmocka_unit_test(test_stationid_cq)
	};

	ardop_test_setup();
	return cmocka_run_group_tests(tests, NULL, NULL);
}
