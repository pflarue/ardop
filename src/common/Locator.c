#include "common/Locator.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Legacy ARDOPC used to transmit "No GS" in IDFRAMEs to indicate
// that the grid square is unset. This is *not* valid as a
// Packed6 since it was actually encoded with a lowercase "o."
//
// To support this, we quietly accept any of the following
// byte sequences as an unset grid square.
static const Packed6 LOCATOR_NOGS[] = {
	{{0xbc, 0xf0, 0x27, 0xcc, 0x00, 0x00}},
	{{0xba, 0xf0, 0x27, 0xcc, 0x00, 0x00}},
};

/**
 * Validate locator
 *
 * Validate the `grid` field of the given `locator`.
 *
 * Returns LOCATOR_OK if the locator is valid or some
 * other value if it is not.
 */
ARDOP_MUSTUSE locator_err
locator_validate_grid(const Locator* locator);

// true if s is ASCII alphabetical with the given
// ending character `end`. For example, set `end = 'R'`
// to accept alphabet characters A through R. `end` must
// be uppercase.
//
// NOTE: ctype.h functions respect the current locale, and
// we do *not* want that since it might not be an English
// locale
inline static bool is_ascii_alpha_range(const char s, const char end) {
	const char LOWERCASE_ADD = 'a' - 'A';

	bool upper = s >= 'A' && s <= end;
	bool lower = s >= ('A' + LOWERCASE_ADD) && s <= (end + LOWERCASE_ADD);
	return (upper || lower);
}

// convert given character to ASCII lowercase
//
// NOTE: ctype.h tolower() respects the current locale, and
// we do *not* want that since it might not be an English
// locale
inline static char to_ascii_lowercase(const char s) {
	const char LOWERCASE_ADD = 'a' - 'A';

	if (s >= 'A' && s <= 'Z') {
		return (char)(s + LOWERCASE_ADD);
	} else {
		return s;
	}
}

// true if s is ASCII digit
inline static bool is_ascii_digit(const char s) {
	return s >= '0' && s <= '9';
}

// compress the grid square
// the `grid` field must be populated
static locator_err locator_compress(const Locator* locator, Packed6* out) {
	locator_err res = locator_validate_grid(locator);
	if (res) {
		return res;
	}

	if (!packed6_from_str_slice(locator->grid, sizeof(locator->grid) - 1, out)) {
		return LOCATOR_ERR_FMT_CHARSET;
	}

	return LOCATOR_OK;
}

// uncompress `grid` square from a Packed6
// the wire bytes must be populated
static locator_err locator_uncompress(const Packed6* in, Locator* locator) {
	// check for legacy "unset gridsquare" byte sequence
	for (size_t ind = 0; ind < sizeof(LOCATOR_NOGS)/sizeof(LOCATOR_NOGS[0]); ++ind) {
		if (0 == memcmp(in, &LOCATOR_NOGS[ind], sizeof(*in))) {
			locator_init(locator);
			return LOCATOR_OK;
		}
	}

	packed6_to_fixed_str(in, locator->grid);

	// translate second letter pair to lowercase
	for (size_t i = 4; i < 6; ++i) {
		locator->grid[i] = to_ascii_lowercase(locator->grid[i]);
	}

	// translate space to NUL, which will truncate the grid square
	for (size_t i = 0; i < sizeof(locator->grid); ++i) {
		if (locator->grid[i] == ' ') {
			locator->grid[i] = 0;
		}
	}

	return locator_validate_grid(locator);
}

ARDOP_MUSTUSE locator_err
locator_validate_grid(const Locator* locator) {
	size_t len = strnlen(locator->grid, sizeof(locator->grid));
	if (len % 2 != 0) {
		return LOCATOR_ERR_FMT_LENGTH;
	}

	for (size_t i = 0; i < len; ++i) {
		switch (i/2) {
			case 0:
				// field A – R
				if (! is_ascii_alpha_range(locator->grid[i], 'R'))
					return LOCATOR_ERR_FMT_FIELD;
				break;
			case 1:
				// square 0 – 9
				if (! is_ascii_digit(locator->grid[i]))
					return LOCATOR_ERR_FMT_SQUARE;
				break;
			case 2:
				// subsquare A – X
				if (! is_ascii_alpha_range(locator->grid[i], 'X'))
					return LOCATOR_ERR_FMT_SUBSQUARE;
				break;
			default:
				// extended square, 0 – 9
				if (! is_ascii_digit(locator->grid[i]))
					return LOCATOR_ERR_FMT_EXTSQUARE;
				break;
		}
	}

	return LOCATOR_OK;
}

void locator_init(Locator* locator) {
	memset(locator, 0, sizeof(*locator));
}

locator_err locator_from_str(const char* str, Locator* locator) {
	const char* inp = str ? str : "";
	size_t len = strlen(inp);
	return locator_from_str_slice(inp, len, locator);
}

locator_err locator_from_str_slice(const char* str, size_t len, Locator* locator) {
	locator_err out = LOCATOR_OK;

	// empty grid is OK
	if (len == 0)
		return out;

	// copy string, checking for truncation
	int ck = snprintf(locator->grid, sizeof(locator->grid), "%.*s", (int)len, str);
	if (ck <= 0 || ck >= sizeof(locator->grid)) {
		locator_init(locator);
		return LOCATOR_ERR_TOOLONG;
	}

	// validate and compress
	out = locator_compress(locator, &locator->wire);
	if (out) {
		locator_init(locator);
		return out;
	}

	// canonicalize by uncompressing
	out = locator_uncompress(&locator->wire, locator);
	if (out) {
		locator_init(locator);
		return out;
	}

	return out;
}

const Packed6* locator_as_bytes(const Locator* locator) {
	if (!! locator && locator_is_populated(locator)) {
		return &(locator->wire);
	} else {
		return &LOCATOR_NOGS[1];
	}
}

locator_err locator_from_bytes(const uint8_t bytes[PACKED6_SIZE], Locator* locator) {
	locator_init(locator);

	// read Packed6
	packed6_from_bytes(bytes, &locator->wire);

	// decompress and validate
	locator_err out = locator_uncompress(&locator->wire, locator);
	if (out) {
		locator_init(locator);
		return out;
	}

	return out;
}

bool locator_is_populated(const Locator* locator) {
	return locator->grid[0] != '\0';
}

const char* locator_strerror(locator_err err) {
	static const char* MSGS[] = {
		"unknown error",
		"length exceeded",
		"locator must be 2, 4, 6, or 8 characters",
		"locator uses invalid characters",
		"locator has invalid field (first pair)",
		"locator has invalid square (second pair)",
		"locator has invalid subsquare (third pair)",
		"locator has invalid extended square (fourth pair)",
	};

	static_assert(sizeof(MSGS) / sizeof(MSGS[0]) == LOCATOR_ERR_MAX_, "error string size mismatch");
	if (err >= LOCATOR_ERR_MAX_)
		return MSGS[0];

	return MSGS[err];
}
