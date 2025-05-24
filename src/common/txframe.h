#include <stdbool.h>
#include <stddef.h>

// Write a copy of src to dst with any instances of "\r" or "\n" replaced with
// "\\r" and "\\n".  If src contains any character that is not a printable ASCII
// character in the range of 0x20 to 0x7E, or if after the above substitutions
// dst would would require more than size bytes (including the terminating
// NULL), then return -1 with the contents of dst undefined.  Else return
// the length of dst excluding the terminating NULL on success.
int escapeeol(char *dst, size_t size, const char *src);

// Write a copy of src to dst with any instances of "\\r" or "\\n" replaced with
// "\r" and "\n". except that any instances of \\\\r" or "\\\\n" are replaced
// "\\r" and "\\n" instead.  All other instances of "\\" remain unchanged.  If
// src contains any character that is not a printable ASCII character in the
// range of 0x20 to 0x7E, then return -1 with the contents of dst undefined.
// Else return the length of dst excluding the terminating NULL on success.
int parseeol(char *dst, size_t size, const char *src);

// return 0 on success, 1 on failure
// len is the number of bytes to return which required
// 2*len hex digits.
// ptr must be a null terminated string of length >= 2*len
int hex2bytes(char *ptr, unsigned int len, unsigned char *output);

// Write an uppercase hexidecimal representation of data to outputStr, creating
// a null terminated string (unless count = 0).  datalen is length of data.
// count is the maximum length of outputStr including the terminating NULL.  If
// spaces is true, then add a space between bytes.  If count is too small to
// write all of data, write as much as will fit.
// Return the length of outputStr (excluding the terminating null), or -1 if
// count == 0 so that outputStr could not even be set to a zero length string.
int bytes2hex(char *outputStr, size_t count, unsigned char *data,
	size_t datalen, bool spaces);

// return 0 on success, 1 on failure
int txframe(char * frameParams);
