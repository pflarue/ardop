#ifndef ARDOP_COMMON_PACKED6_H_
#define ARDOP_COMMON_PACKED6_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/mustuse.h"

/**
 * @def PACKED6_SIZE
 * @brief Number of compressed bytes in a Packed6
 */
#define PACKED6_SIZE 6

/**
 * @def PACKED6_MAX
 * @brief Maximum uncompressed ASCII characters in a Packed6
 */
#define PACKED6_MAX 8

/**
 * @brief Compressed ASCII bytes (fits 8 characters in 6 bytes)
 *
 * This type is guaranteed to be cast-compatible with `uint8_t*`.
 * It represents 8 ASCII characters in the range 32 (space) to
 * 95 (underscore). This does *not* include lowercase letters,
 * which cannot be represented in this encoding.
 */
typedef struct {
	uint8_t b[PACKED6_SIZE];
} Packed6;

/**
 * @brief Compress to eight characters from `str`
 *
 * Compresses up to eight ASCII characters in six bytes. If
 * fewer than eight characters are provided, the remaining
 * characters are padded with spaces (ASCII 32).
 *
 * Lowercase letters are silently converted to uppercase.
 * Other invalid characters are replaced with spaces and will
 * return an error.
 *
 * @param[in]  str      Null-terminated C string
 * @param[out] packed   Packed representation of `str`
 *
 * @return true if the entire input was compressed into
 * `packed` or false if it was not.
 */
ARDOP_MUSTUSE bool packed6_from_str(const char* str, Packed6* packed);

/**
 * @brief Compress to eight characters from `str`
 *
 * Compresses up to `len` ASCII characters, `len <= 8`,
 * in six bytes. If fewer than eight characters are provided,
 * the remaining characters are padded with spaces (ASCII 32).
 *
 * Lowercase letters are silently converted to uppercase.
 * Other invalid characters are replaced with spaces and will
 * return an error.
 *
 * @param[in]  str      C string (NUL termination optional)
 * @param[in]  len      Number of characters in `str`, per `strlen()`
 * @param[out] packed   Packed representation of `str`
 *
 * @return true if the entire input was compressed into
 * `packed` or false if it was not.
 */
ARDOP_MUSTUSE bool packed6_from_str_slice(
	const char* str, size_t len, Packed6* packed);

/**
 * @brief Construct from packed bytes
 *
 * Creates a Packed6 from the given compressed `bytes`.
 *
 * @param[in]  bytes    Packed bytes. This method requires
 *                      *exactly* six bytes from `bytes`.
 * @param[out] packed   A Packed6 which is ready for unpacking
 */
void packed6_from_bytes(const uint8_t bytes[PACKED6_SIZE], Packed6* packed);

/**
 * @brief Decompress a Packed6 to a C string
 *
 * Decompress the `packed` bytes into the specified C string,
 * which must have a capacity of `outsize` bytes.
 *
 * @param[in]  packed   Packed bytes
 * @param[out] out      Destination C string buffer
 * @param[out] outsize  Capacity of `out` in bytes, per `sizeof()`
 *
 * @return true if the entire input was uncompressed into `out`
 * or false if it was not. `out` will always be NUL-terminated
 * if `size > 0`.
 */
ARDOP_MUSTUSE bool packed6_to_str(
	const Packed6* packed,
	char* out,
	size_t outsize
);

/**
 * @brief Decompress a Packed6 to a fixed C string
 *
 * Decompress the `packed` bytes into the specified C string
 * that is at least 9 bytes in capacity. This method requires
 * a string of known size at compile time. Unlike
 * \ref packed6_to_fixed_str(), this method always succeeds.
 *
 * @param[in]  packed   Packed bytes
 * @param[out] out      Destination C string buffer. The
 *             output is always NUL-terminated.
 */
void packed6_to_fixed_str(
	const Packed6* packed,
	char out[PACKED6_MAX + 1]
);

#endif /* ARDOP_COMMON_PACKED6_H_ */
