#include "common/StationId.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// minimum number of characters in a callsign
#define CALLSIGN_MIN 2

// maximum number of characters in a callsign
#define CALLSIGN_MAX 7

// broadcast message directed at all stations
#define CQ "CQ"

/**
 * Validate callsign
 *
 * Validate the `call`sign field of the given `station` ID.
 * Callsigns which are too short, too long, or contain invalid
 * characters are rejected. No attempt is made to enforce or
 * validate a particular format (i.e., U.S. FCC conventions).
 *
 * Returns STATIONID_OK if the callsign is valid or
 * STATIONID_ERR_INVALID_SSID if not.
 */
ARDOP_MUSTUSE station_id_err
stationid_validate_callsign(const StationId* station);

/**
 * Pack an SSID string into its one-byte representation
 *
 * Converts the `ssid` string field in `station` to its
 * one-byte representation for `Packed6`. Letters A–Z
 * are represented as `A`—`Z`, and numbers 0–9 are
 * represented as `0`–`9`. Numbers 10–15 are represented
 * as `:`–`?`.
 *
 * Returns 0 if the SSID portion of `station` is invalid
 * and cannot be represented.
 */
char stationid_ssid_pack(const StationId* station);

/**
 * Unpack an SSID from its one-byte representation
 *
 * Converts the given `Packed6` one-byte SSID representation
 * of an SSID to its `ssid` string representation in `station`.
 *
 * Letters A–Z are represented as `A`—`Z`, and numbers 0–9 are
 * represented as `0`–`9`. Numbers 10–15 are represented
 * as `:`–`?`.
 *
 * Returns STATIONID_OK if the unpacked SSID is valid or
 * STATIONID_ERR_INVALID_SSID if not.
 */
ARDOP_MUSTUSE station_id_err
stationid_ssid_unpack(const char ssid_byte, StationId* station);

// parse a string like `N0CALL-15` to station ID callsign and ssid
static station_id_err stationid_parse_str(const char* id_str, size_t id_str_len, StationId* station) {
	stationid_init(station); // zeroize

	bool copying_callsign = true;
	char* out = &station->call[0];
	size_t opos = 0;
	size_t osize = sizeof(station->call);

	for (size_t ipos = 0; ipos < id_str_len; ++ipos) {
		switch (id_str[ipos]) {
		case '\0':
			// end of string
			return STATIONID_OK;
		case '-':
			if (copying_callsign) {
				// found ssid separator
				// start copying ssid instead
				copying_callsign = false;
				out = &station->ssid[0];
				opos = 0;
				osize = sizeof(station->ssid);
				continue;
			}
		}

		// copy to the current destination
		if (!(opos + 1 < osize)) {
			// output capacity exceeded
			return STATIONID_ERR_TOOLONG;
		}
		out[opos] = id_str[ipos];
		opos += 1;
	}

	return STATIONID_OK;
}

// compress callsign and SSID portions into a Packed6
// the ASCII text fields must be populated
static station_id_err stationid_compress(const StationId* station, Packed6* out) {
	// check callsign
	station_id_err res = STATIONID_OK;
	res = stationid_validate_callsign(station);
	if (res) {
		return res;
	}

	// convert SSID to one-byte representation
	char ssid_byte = stationid_ssid_pack(station);
	if (ssid_byte == 0) {
		return STATIONID_ERR_INVALID_SSID;
	}

	// generate pre-packed string
	//
	// this string looks like `N0CALL 0`, with unused characters
	// padded with spaces. The last character is reserved for the
	// SSID and is zero if there is no SSID
	char work[PACKED6_MAX + 1];
	snprintf(work, sizeof(work), "%-7.7s%c", station->call, ssid_byte);

	// compress
	if (packed6_from_str_slice(work, sizeof(work) - 1, out)) {
		return STATIONID_OK;
	} else {
		return STATIONID_ERR_COMPRESS;
	}
}

// uncompress callsign and SSID portions from a Packed6
// the wire bytes must be populated
static station_id_err stationid_uncompress(const Packed6* in, StationId* station) {
	char work[PACKED6_MAX + 1] = "";
	if (! packed6_to_str(in, work, sizeof(work))) {
		return STATIONID_ERR_DECOMPRESS;
	}

	// translate space to NUL, which will truncate the callsign
	for (size_t i = 0; i < sizeof(work); ++i) {
		if (work[i] == ' ') {
			work[i] = 0;
		}
	}

	// copy callsign
	snprintf(station->call, sizeof(station->call), "%.7s", work);
	station_id_err res = stationid_validate_callsign(station);
	if (res) {
		return res;
	}

	// decode and validate the SSID
	return stationid_ssid_unpack(work[CALLSIGN_MAX], station);
}

ARDOP_MUSTUSE station_id_err
stationid_validate_callsign(const StationId* station) {
	size_t len = strnlen(station->call, sizeof(station->call));
	bool ok = len >= CALLSIGN_MIN && len <= CALLSIGN_MAX;
	for (size_t i = 0; i < len; ++i) {
		// whitespace not allowed
		// other whitespace is filtered by the compressor
		ok &= station->call[i] != ' ';
	}

	if (ok)
		return STATIONID_OK;

	return STATIONID_ERR_INVALID_CALLSIGN;
}

char stationid_ssid_pack(const StationId* station) {
	// convert to number from base 36 (A → 10, Z → 35)
	char* ssid_numeric_end = 0;
	long ssid_numeric = strtol(station->ssid, &ssid_numeric_end, 36);
	if (ssid_numeric_end == station->ssid ||
		(*ssid_numeric_end) != '\0') {
		// empty string or  entire string is not a number → error
		return 0;
	}
	else if (ssid_numeric >= 0 && ssid_numeric <= 9) {
		// map `0` – `9` to '0' – '9'
		return '0' + (char)ssid_numeric;
	}
	else if (ssid_numeric >= 10 && ssid_numeric <= 35) {
		// map `A` ­– `Z` to 'A' – 'Z'
		return 'A' + (char)(ssid_numeric - 10);
	}
	else if (ssid_numeric >= 36 && ssid_numeric <= 41) {
		// map `10` – `15` to : ; < = > ?
		return ':' + (char)(ssid_numeric - 36);
	}
	else {
		// not valid
		return 0;
	}
}

station_id_err
stationid_ssid_unpack(const char ssid_byte, StationId* station) {
	if (ssid_byte >= '0' && ssid_byte <= '?') {
		snprintf(station->ssid, sizeof(station->ssid),
			"%hhd", (uint8_t)(ssid_byte - '0'));
		return STATIONID_OK;
	} else if (ssid_byte >= 'A' && ssid_byte <= 'Z') {
		snprintf(station->ssid, sizeof(station->ssid),
			"%c", ssid_byte);
		return STATIONID_OK;
	} else {
		return STATIONID_ERR_INVALID_SSID;
	}
}

void stationid_init(StationId* station) {
	memset(station, 0, sizeof(*station));
	station->ssid[0] = '0';
}

void stationid_make_cq(StationId* station) {
	station_id_err ignore = stationid_from_str_slice(CQ, sizeof(CQ), station);
	(void)ignore; /* unused */
}

station_id_err stationid_from_str(const char* str, StationId* station) {
	const char* inp = str ? str : "";
	size_t len = strlen(inp);
	return stationid_from_str_slice(inp, len, station);
}

station_id_err stationid_from_str_slice(const char* str, size_t len, StationId* station) {
	station_id_err out = STATIONID_OK;

	// parse CALLSIGN-SSID
	out = stationid_parse_str(str, len, station);
	if (out) {
		stationid_init(station);
		return out;
	}

	// validate and compress
	out = stationid_compress(station, &station->wire);
	if (out) {
		stationid_init(station);
		return out;
	}

	// canonicalize by uncompressing
	out = stationid_uncompress(&station->wire, station);
	if (out) {
		stationid_init(station);
		return out;
	}

	return out;
}

station_id_err stationid_from_bytes(const void* bytes, size_t nbytes, StationId* station) {
	stationid_init(station);

	// read Packed6
	if (! packed6_from_bytes(bytes, nbytes, &station->wire)) {
		stationid_init(station);
		return STATIONID_ERR_DECOMPRESS;
	}

	// decompress and validate
	station_id_err out = stationid_uncompress(&station->wire, station);
	if (out) {
		stationid_init(station);
		return out;
	}

	return out;
}

bool stationid_ok(const StationId* station) {
	// a station ID is OK if its wireline representation
	// is non-zero
	bool ok = false;

	for (size_t i = 0; i < sizeof(station->wire); ++i) {
		ok |= station->wire.b[i];
	}
	return ok;
}

bool stationid_is_cq(const StationId* station) {
	return 0 == strncmp(CQ, station->call, sizeof(station->call));
}

bool stationid_eq(const StationId* a, const StationId* b) {
	// two stations are equal if their wireline representations
	// are equal
	return 0 == memcmp(a->wire.b, b->wire.b, sizeof(a->wire.b));
}
