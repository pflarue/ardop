#include <stdbool.h>
#include <stddef.h>

// See https://GitHub.com/pflarue/eutf8 for a full description of eutf8 (escaped
// UTF-8) encoding, along with reference implementations of to_eutf() and
// from_eutf() in Python, Javascript, and c.


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
// return (\r 0x0D) characters, writes an error to the log if dstsize is too
// small, adds a terminating null to the end of dstdata, and adjusts the return
// value to include the byte used for the terminating null.  Escaping tab, line
// feed, and carriage return characters may slightly reduce readability, but it
// is more compact.  It also ensures that the appearance of displayed eutf8 is
// independent of the type of line endings that the OS/soffware uses.  Since the
// first unescaped line ending then indicates the end of the logged eutf8
// encoded data, it also makes it easier to automatically extract the eutf8
// encoded data from the log file.
// See test/python/test_wav_io.py for an example of such extraction of data from
// a log file.
size_t eutf8(char* dstdata, int dstsize, char* srcdata, int srclen);
