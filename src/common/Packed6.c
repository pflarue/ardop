#include "common/Packed6.h"

#include <string.h>
#include <stdio.h>

// Converts vector of exactly four characters to a vector of exactly
// three bytes using DEC SIXBIT compression. The compressed
// alphabet can only represent ASCII character 32 (space) through
// ASCII character 63 (underscore). Returns false if any character
// is not representable.
static bool compress_four_to_three(
	const char uncompressed[4], uint8_t compressed[3])
{
	const uint8_t SPACE = (uint8_t)' ';
	const uint8_t UNDERSCORE = (uint8_t)'_';
	const uint8_t UPPERCASE_A = (uint8_t)'A';
	const uint8_t LOWERCASE_A = (uint8_t)'a';
	const uint8_t LOWERCASE_Z = (uint8_t)'z';
	const uint8_t LOWER6 = (uint8_t)(0x3F);
	const uint32_t BYTE = (uint32_t)(0xFF);

	bool ok = true;
	uint32_t pack = 0;

	for (size_t ipos = 0; ipos < 4; ++ipos) {
		uint8_t b = (uint8_t)uncompressed[ipos];
		if (b >= SPACE && b <= UNDERSCORE) {
			// packed alphabet starts at ASCII 32 (space)
			// use characters in this range verbatim
			b = b - SPACE;
		}
		else if (b >= LOWERCASE_A && b <= LOWERCASE_Z) {
			// lowercase → uppercase
			b = b - (LOWERCASE_A - UPPERCASE_A) - SPACE;
		} else {
			// out of range; replace with space
			b = 0;
			ok = false;
		}

		// take only the lower 6 bits and store them
		// in the next-highest 6 bits of the pack word
		pack = pack << 6;
		pack |= ((uint32_t)(b & LOWER6));
	}

	// shift one byte at a time off the pack word, starting
	// with the lowest byte (end of data).
	for (size_t opos = 0; opos < 3; ++opos) {
		compressed[2 - opos] = pack & BYTE;
		pack = pack >> 8;
	}

	return ok;
}

// Converts vector of exactly three bytes to a vector of exactly
// four ASCII characters using DEC SIXBIT decompression. This
// undoes compress_four_to_three(). This method always succeeds.
//
// The output is *NOT* NUL-terminated
static void decompress_three_to_four(
	const uint8_t compressed[3], char uncompressed[4])
{
	const uint8_t SPACE = (uint8_t)' ';
	const uint32_t LOWER6 = (uint32_t)(0x3F);

	uint32_t unpack = 0;

	// shift one byte at a time into the unpack word, starting
	// with the lowest byte.
	for (size_t ipos = 0; ipos < 3; ++ipos) {
		unpack = unpack << 8;
		unpack |= (uint32_t)(compressed[ipos]);
	}

	// take 6-bit chunks off the unpack word, starting with the
	// highest byte (end of data).
	for (size_t opos = 0; opos < 4; ++opos) {
		uint8_t b = (uint8_t)(unpack & LOWER6);
		uncompressed[3 - opos] = (char)(b + SPACE);
		unpack = unpack >> 6;
	}
}

bool packed6_from_str(const char* str, Packed6* packed) {
	const char* s = str ? str : "";

	return packed6_from_str_slice(s, strnlen(s, PACKED6_MAX+1), packed);
}

bool packed6_from_str_slice(const char* str, size_t len, Packed6* packed) {
	const char* s = str ? str : "";

	bool ok = true;

	// ensure input is exactly 8 characters
	if (len > PACKED6_MAX) {
		// truncate
		ok = false;
		len = PACKED6_MAX;
	}
	char work[9] = "";
	snprintf(work, sizeof(work), "%-8.*s", (int)len, s);

	ok &= compress_four_to_three(&work[0], &packed->b[0]);
	ok &= compress_four_to_three(&work[4], &packed->b[3]);

	return ok;
}

void packed6_from_bytes(const uint8_t bytes[PACKED6_SIZE], Packed6* packed)
{
	memcpy(&packed->b[0], bytes, sizeof(packed->b));
}

bool packed6_to_str(const Packed6* packed, char* out, size_t outsize) {
	if (outsize < PACKED6_MAX + 1) {
		if (outsize >= 1) {
			out[0] = '\0';
		}
		return false;
	}

	decompress_three_to_four(&packed->b[0], &out[0]);
	decompress_three_to_four(&packed->b[3], &out[4]);
	out[PACKED6_MAX] = '\0';
	return true;
}

void packed6_to_fixed_str(const Packed6* packed, char out[PACKED6_MAX + 1]) {
	decompress_three_to_four(&packed->b[0], &out[0]);
	decompress_three_to_four(&packed->b[3], &out[4]);
	out[PACKED6_MAX] = '\0';
}
