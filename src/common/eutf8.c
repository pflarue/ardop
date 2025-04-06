#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "common/log.h"
#include "common/eutf8.h"

// See https://GitHub.com/pflarue/eutf8 for a full description of eutf8 (escaped
// UTF-8) encoding along with reference implementations of to_eutf() and
// from_eutf() in Python, Javascript, and c.

// to_eutf8() is used to convert byte sequences that may contain a mix of UTF-8
// encoded text and other data that is not valid UTF-8 encoded text into a
// format that is guaranteed to be valid UTF-8 encoded text such that is can
// be displayed on screen or written to the log file.  test/python/eutf8.py
// contains the python implementation of from_eutf8(), which takes the output of
// to_eutf8(), and produces a copy of the byte data that was used to produce it.

// Return true if escapes written.  If there is not enough space in dstdata,
// fill with question marks (0x3F) and return false.
bool escbytes(char *dstdata, int *didx, char *srcdata, int *sidx,
	int count, int dstsize
) {
	if (*didx + 3 * count > dstsize) {
		// There isn't sufficient space to write count 3-byte escape sequences.
		// Fill with question marks and return false;
		while(*didx < dstsize)
			dstdata[(*didx)++] = 0x3F;  // question mark
		return false;
	}
	for (int i = 0; i < count; ++i) {
		dstdata[*didx] = 0x5C;  // backslash '\'
		char high = ((unsigned char) srcdata[*sidx]) >> 4;
		if (high <= 9)
			high += '0';
		else
			high = 'A' + (high - 10);
		char low = ((unsigned char) srcdata[*sidx]) & 0x0F;
		if (low <= 9)
			low += '0';
		else
			low = 'A' + (low - 10);
		dstdata[*didx + 1] = high;
		dstdata[*didx + 2] = low;
		*didx += 3;
		*sidx += 1;
	}
	return true;
}

// Return true if bytes copies.  If there is not enough space in dstdata,
// fill with question marks (0x3F) and return false.
bool copybytes(char *dstdata, int *didx, char *srcdata, int *sidx,
	int count, int dstsize
) {
	if (*didx + count > dstsize) {
		// There isn't sufficient space to write count bytes.
		// fill with question marks and return false;
		while(*didx < dstsize)
			dstdata[(*didx)++] = 0x3F;  // question mark
		return false;
	}
	memcpy(dstdata + *didx, srcdata + *sidx, count);
	*didx += count;
	*sidx += count;
	return true;
}


// to_eutf8() takes srcdata of length srclen, which is arbitrary byte data, and
// populates dstdata with up to dstsize bytes of data that is guaranteed to be
// valid utf-8 encoding, using eutf8 escape sequences for data in srcdata that
// is not valid utf-8, and also inserting 3-byte utf-8 encoded Zero Width Space
// characters as necessary to allow from_eutf8() to reconstruct srcdata from
// dstdata.  srcdata may be, but is not required to be, null terminated.  For
// arbitrary srcdata, dstsize should be greater than three times srclen.
//
// If escape_tab is true, then an ascii horizontal tab ('\t' or '\x09') in
// srcdata will be converted to '\\09' in dstdata.  Otherwise, this character in
// in srcdata will be left unchanged in dstdata.
//
// If escape_lf is true, then an ascii line feed ('\n' or '\x0A') in srcdata
// will be converted to '\\0A' in dstdata.  Otherwise, this character in srcdata
// will be left unchanged in dstdata.
//
// If escape_cr is true, then an ascii carriage return ('\r' or '\x0D') in
// srcdata will be converted to '\\0D' in dstdata.  Otherwise, this character in
// srcdata will be left unchanged in dstdata.
//
// Return the length of the data in dstdata.  If the converted contents of
// srcdata would require dstsize or more bytes, then the return value will be
// exactly equal to dstsize.  One or more question marks may be used to pad the
// end of dstdata in this case if a multi-byte utf-8 sequence would have
// required more than dstsize bytes to fully write.  dstdata will not be null
// terminated.
size_t to_eutf8(char* dstdata, int dstsize, char* srcdata, int srclen,
	bool escape_tab, bool escape_lf, bool escape_cr
) {
	//Return eutf8 encoded data created from any data sequence
	char upperhex[] = "0123456789ABCDEF";
	int sidx = 0;  // Index in srcdata of the current byte
	int didx = 0;  // Index in dstdata where the next byte shall be written

	while (sidx < srclen && didx < dstsize) {
		// The above test ensures that at least one more byte can be written to
		// dstdata.  Wherever more than one byte will be written, check dstsize.
		//
		// Based on Unicode Table 3-7 Well-Formed UTF-8 Byte Sequences
		unsigned char c = (unsigned char) srcdata[sidx];
		unsigned char c1 = (unsigned char) srcdata[sidx + 1];
		unsigned char c2 = (unsigned char) srcdata[sidx + 2];
		unsigned char c3 = (unsigned char) srcdata[sidx + 3];
		if ((0x80 <= c && c <= 0xC1) || 0xF5 <= c) {
			// Not UTF-8
			// Escape 1 byte
			if (!escbytes(dstdata, &didx, srcdata, &sidx, 1, dstsize))
				break;
			continue;
		}

		if (c <= 0x7F) {
			// Valid 1-byte UTF-8
			if (
				c <= 0x08
				|| (c == 0x09 && escape_tab)
				|| (c == 0x0A && escape_lf)
				|| (0x0B <= c && c <= 0x0C)
				|| (c == 0x0D && escape_cr)
				|| (0x0E <= c && c <= 0x1F)
				|| (c == 0x7F)
			) {
				// This is a C0 control code, so escape anyways
				if (!escbytes(dstdata, &didx, srcdata, &sidx, 1, dstsize))
					break;
				continue;
			}
			if (c == 0x5C) {
				// Insert a Zero Width Space after the backslash only if it is
				// followed by zero or more additional Zero Width Sapces and
				// then two uppercase hex digits.
				int zws_cnt = 0;  // Number of existing Zero Width Spaces
				while (
					srclen > sidx + 3 * zws_cnt + 3
					&& (unsigned char) srcdata[sidx + 3 * zws_cnt + 1] == 0xE2
					&& (unsigned char) srcdata[sidx + 3 * zws_cnt + 2] == 0x80
					&& (unsigned char) srcdata[sidx + 3 * zws_cnt + 3] == 0x8B
				)
					zws_cnt += 1;
				if (
					// strchr(char *s, char c) matches the terminating NULL of s
					// if c is NULL, which is undesirable here.  So, include tests
					// to exclude c == NULL which is not an uppercase hex digit.
					srclen > sidx + 3 * zws_cnt + 2
					&& (unsigned char) srcdata[sidx + 3 * zws_cnt + 1] != 0x00
					&& (unsigned char) srcdata[sidx + 3 * zws_cnt + 2] != 0x00
					&& strchr(upperhex, srcdata[sidx + 3 * zws_cnt + 1]) != NULL
					&& strchr(upperhex, srcdata[sidx + 3 * zws_cnt + 2]) != NULL
				) {
					// Add a Unicode Zero Width Space (U+200b) after the
					// backslash '\' (0x5C).  This prevents from_eutf8() from
					// mistaking this this for an escape sequence or from
					// removing the zws_cnt Zero Width Spaces that are a part of
					// the source data.  The latter is probably unlikely, but is
					// possible.  UTF-8 encoding of U+200b is the three byte
					// sequence 0xE2, 0x80, 0x8B.
					if (didx + 4 > dstsize) {
						// There isn't sufficient space to write a backslash
						// plus the 3-byte Zero Width Space.  Fill remaining
						// space with question marks (0x3F) and return.
						while(didx < dstsize)
							dstdata[didx++] = 0x3F;  // question mark
						break;
					}
					dstdata[didx++] = 0x5C;  // backslash '\'
					dstdata[didx++] = 0xE2;  // first byte of 0 Width Space
					dstdata[didx++] = 0x80;  // second byte of 0 Width Space
					dstdata[didx++] = 0x8B;  // third byte of 0 Width Space
					sidx += 1;
					continue;
				}
			}
			// copy 1-byte output.  This might be a backslash
			if (!copybytes(dstdata, &didx, srcdata, &sidx, 1, dstsize))
				break;
			continue;
		}

		if (srclen <= sidx + 1) {
			// c may be the first byte of a multi-byte UTF-8 sequence, but the
			// required number of additional bytes are not available.
			// Escape 1 byte
			if (!escbytes(dstdata, &didx, srcdata, &sidx, 1, dstsize))
				break;
			continue;
		}

		if (0xC2 <= c && c <= 0xDF) {
			if (0x80 <= c1 && c1 <= 0xBF) {
				// Valid 2-byte UTF-8
				if (c == 0xC2 && c1 <= 0x9F) {
					// This is a C1 control code, so escape anyways
					// When valid UTF-8 is escaped, escape all bytes since
					// following bytes are not valid as first byte of UTF-8.
					if (!escbytes(dstdata, &didx, srcdata, &sidx, 2, dstsize))
						break;
					continue;
				}
				// copy 2-bytes to output
				if (!copybytes(dstdata, &didx, srcdata, &sidx, 2, dstsize))
					break;
				continue;
			}
			// Not UTF-8.  Escape 1 byte
			if (!escbytes(dstdata, &didx, srcdata, &sidx, 1, dstsize))
				break;
			continue;
		}

		if (srclen <= sidx + 2) {
			// c may be the first byte of a multi-byte UTF-8 sequence, but
			// the required number of additional bytes are not available.
			// Escape 1 byte
			if (!escbytes(dstdata, &didx, srcdata, &sidx, 1, dstsize))
				break;
			continue;
		}

		if (c == 0xE0) {
			if ((0xA0 <= c1 && c1 <= 0xBF) && (0x80 <= c2 && c2 <= 0xBF)) {
				// Valid 3-byte UTF-8
				// copy 3-bytes to output
				if (!copybytes(dstdata, &didx, srcdata, &sidx, 3, dstsize))
					break;
				continue;
			}
			// Not UTF-8.  Escape 1 byte
			if (!escbytes(dstdata, &didx, srcdata, &sidx, 1, dstsize))
				break;
			continue;
		}
		if (0xE1 <= c && c <= 0xEC) {
			if ((0x80 <= c1 && c1 <= 0xBF) && (0x80 <= c2 && c2 <= 0xBF) ) {
				// Valid 3-byte UTF-8
				// copy 3-bytes to output
				if (!copybytes(dstdata, &didx, srcdata, &sidx, 3, dstsize))
					break;
				continue;
			}
			// Not UTF-8.  Escape 1 byte
			if (!escbytes(dstdata, &didx, srcdata, &sidx, 1, dstsize))
				break;
			continue;
		}
		if (c == 0xED) {
			if ((0x80 <= c1 && c1 <= 0x9F) && (0x80 <= c2 && c2 <= 0xBF)) {
				// Valid 3-byte UTF-8
				// copy 3-bytes to output
				if (!copybytes(dstdata, &didx, srcdata, &sidx, 3, dstsize))
					break;
				continue;
			}
			// Not UTF-8.  Escape 1 byte
			if (!escbytes(dstdata, &didx, srcdata, &sidx, 1, dstsize))
				break;
			continue;
		}

		if (0xEE <= c && c <= 0xEF) {
			if ((0x80 <= c1 && c1 <= 0xBF) && (0x80 <= c2 && c2 <= 0xBF)) {
				// Valid 3-byte UTF-8
				if (c1 <= 0xA3) {
					// This is a private use character, so escape anyways.
					// When valid UTF-8 is escaped, escape all bytes since
					// following bytes are not valid as first byte of UTF-8.
					if (!escbytes(dstdata, &didx, srcdata, &sidx, 3, dstsize))
						break;
					continue;
				}
				if ((c == 0xEF) && (((c1 == 0xB7) && (0x90 <= c2 && c2 <= 0xAF))
						|| ((c1 == 0xBF) && (0xBE <= c2)))
				) {
					// This is a noncharacter, so escape it anyways.
					// When valid UTF-8 is escaped, escape all bytes since
					// following bytes are not valid as first byte of UTF-8.
					if (!escbytes(dstdata, &didx, srcdata, &sidx, 3, dstsize))
						break;
					continue;
				}
				// Valid 3-byte UTF-8
				// copy 3-bytes to output
				if (!copybytes(dstdata, &didx, srcdata, &sidx, 3, dstsize))
					break;
				continue;
			}
			// Not UTF-8.  Escape 1 byte
			if (!escbytes(dstdata, &didx, srcdata, &sidx, 1, dstsize))
				break;
			continue;
		}

		if (srclen <= sidx + 3) {
			// c may be the first byte of a multi-byte UTF-8 sequence, but the
			// required number of additional bytes are not available.
			// Escape 1 byte
			if (!escbytes(dstdata, &didx, srcdata, &sidx, 1, dstsize))
				break;
			continue;
		}

		if (c == 0xF0) {
			if ((0x90 <= c1 && c1 <= 0xBF)
				&& (0x80 <= c2 && c2 <= 0xBF)
				&& (0x80 <= c3 && c3 <= 0xBF)
			) {
				// Valid 4-byte UTF-8
				if (((c1 & 0x0F) == 0x0F) && (c2 == 0xBF) && (c3 >= 0xBE)) {
					// This is a noncharacter, so escape it anyways.
					// When valid UTF-8 is escaped, escape all bytes since
					// following bytes are not valid as first byte of UTF-8.
					if (!escbytes(dstdata, &didx, srcdata, &sidx, 4, dstsize))
						break;
					continue;
				}
				// Valid 4-byte UTF-8
				// copy 4-bytes to output
				if (!copybytes(dstdata, &didx, srcdata, &sidx, 4, dstsize))
					break;
				continue;
			}
			// Not UTF-8.  Escape 1 byte
			if (!escbytes(dstdata, &didx, srcdata, &sidx, 1, dstsize))
				break;
			continue;
		}
		if (0xF1 <= c && c <= 0xF3) {
			if ((0x80 <= c1 && c1 <= 0xBF)
				&& (0x80 <= c2 && c2 <= 0xBF)
				&& (0x80 <= c3 && c3 <= 0xBF)
			) {
				// Valid 4-byte UTF-8
				if (((c1 & 0x0F) == 0x0F) && (c2 == 0xBF) && (c3 >= 0xBE)) {
					// This is a noncharacter, so escape it anyways.
					// When valid UTF-8 is escaped, escape all bytes since
					// following bytes are not valid as first byte of UTF-8.
					if (!escbytes(dstdata, &didx, srcdata, &sidx, 4, dstsize))
						break;
					continue;
				}
				if ((c == 0xF3) && (0xB0 <= c1) && (c3 <= 0xBD)) {
					// This is a private use character, so escape anyways.
					// When valid UTF-8 is escaped, escape all bytes since
					// following bytes are not valid as first byte of UTF-8.
					if (!escbytes(dstdata, &didx, srcdata, &sidx, 4, dstsize))
						break;
					continue;
				}
				// Valid 4-byte UTF-8
				// copy 4-bytes to output
				if (!copybytes(dstdata, &didx, srcdata, &sidx, 4, dstsize))
					break;
				continue;
			}
			// Not UTF-8.  Escape 1 byte
			if (!escbytes(dstdata, &didx, srcdata, &sidx, 1, dstsize))
				break;
			continue;
		}

		if (c == 0xF4) {
			if ((0x80 <= c1 && c1 <= 0x8F)
				&& (0x80 <= c2 && c2 <= 0xBF)
				&& (0x80 <= c3 && c3 <= 0xBF)
			) {
				// Valid 4-byte UTF-8
				if ((c1 == 0x8F) && (c2 == 0xBF) && (c3 >= 0xBE)) {
					// This is a noncharacter, so escape it anyways.
					// When valid UTF-8 is escaped, escape all bytes since
					// following bytes are not valid as first byte of UTF-8.
					if (!escbytes(dstdata, &didx, srcdata, &sidx, 4, dstsize))
						break;
					continue;
				}
				if (c3 <= 0xBD) {
					// This is a private use character, so escape anyways.
					// When valid UTF-8 is escaped, escape all bytes since
					// following bytes are not valid as first byte of UTF-8.
					if (!escbytes(dstdata, &didx, srcdata, &sidx, 4, dstsize))
						break;
					continue;
				}
				// Valid 4-byte UTF-8
				// copy 4-bytes to output
				if (!copybytes(dstdata, &didx, srcdata, &sidx, 4, dstsize))
					break;
				continue;
			}
			// Not UTF-8.  Escape 1 byte
			if (!escbytes(dstdata, &didx, srcdata, &sidx, 1, dstsize))
				break;
			continue;
		}
		// shouldn't get here
		// The following replaces a printf() in the reference implementation
		// with ZF_LOGE().
		ZF_LOGE("Logic error in to_eutf8().  srcdata[%i] = %02X",
			sidx, c);
		if (!escbytes(dstdata, &didx, srcdata, &sidx, 1, dstsize))
			break;
	}
	return didx;
}


// eutf8() takes srcdata of length srclen, which is arbitrary byte data, and
// populates dstdata with up to dstsize bytes of data that is guaranteed to be
// valid utf-8 encoding, using eutf8 escape sequences for data in srcdata that
// is not valid utf-8, and also inserting 3-byte utf-8 encoded Zero Width Space
// characters as necessary to allow from_eutf8() to reconstruct srcdata from
// dstdata.  srcdata is not required to be null terminated.  For arbitrary
// srcdata, dstsize should be at least three times srclen plus one.
//
// Return the length of the data in dstdata including a terminating null.  If
// the converted contents of srcdata would require dstsize or more bytes, then
// the return value will be exactly equal to dstsize, and an error message is
// logged.  One or more question marks may be used to pad the end of dstdata in
// this case if a multi-byte utf-8 sequence would have required more than
// dstsize bytes to fully write.
//
// eutf8() is a wrapper around the reference implementation of to_eutf8() that
// always escapes horizontal tab (\t \09), line feed (\n \0A), and carriage
// return characters, writes an error to the log if dstsize is too small, adds a
// terminating null to the end of dstdata, and adjusts the return value to
// include the byte used for the terminating null.  Escaping tab, line feed, and
// carriage return characters may slightly reduce readability, but it is more
// compact.  It also ensures that the appearance of displayed eutf8 is
// independent of the type of line endings that the OS/soffware uses.  Since the
// first unescaped \n then indicates the end of the data, it also makes it
// easier to automatically extract the eutf8 encoded data from the log file.
// See test/python/test_wav_io.py for an example of such extraction of data from
// a log file.
size_t eutf8(char* dstdata, int dstsize, char* srcdata, int srclen) {

	size_t dstlen = to_eutf8(dstdata, dstsize - 1, srcdata, srclen,
		true, true, true);
	if ((int) dstlen == dstsize - 1) {
		ZF_LOGE("ERROR: dstsize was inadequate to eutf8 encode the provided"
			" data.  The result is truncated to %i bytes (including the"
			" terminating NULL", dstsize);
	}
	dstdata[dstlen] = 0x00;  // terminating NULL
	return dstlen + 1;
}
