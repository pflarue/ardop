#ifndef ARDOP_COMMON_LOCATOR_H_
#define ARDOP_COMMON_LOCATOR_H_

#include <stdint.h>

#include "common/mustuse.h"
#include "common/Packed6.h"

/**
 * @def LOCATOR_SIZE
 * @brief LOCATOR_SIZE
 *
 * Maximum size of the string representation of a Locator,
 * including the trailing NUL.
 */
#define LOCATOR_SIZE 9

/**
 * @brief Error codes returned by Locator functions
 */
typedef enum {
	/**
	 * @brief The Locator is accepted
	 *
	 * A `LOCATOR_OK` may still be empty
	 */
	LOCATOR_OK = 0,

	/**
	 * @brief The Locator is too long
	 */
	LOCATOR_ERR_TOOLONG = 1,

	/**
	 * @brief The Locator does not contain an even number of characters
	 *
	 * Grid squares which are not 2, 4, 6, or 8-characters are invalid.
	 */
	LOCATOR_ERR_FMT_LENGTH = 2,

	/**
	 * @brief The Locator uses invalid characters
	 */
	LOCATOR_ERR_FMT_CHARSET = 3,

	/**
	 * @brief The Locator has an invalid first part
	 */
	LOCATOR_ERR_FMT_FIELD = 4,

	/**
	 * @brief The Locator has an invalid second part
	 */
	LOCATOR_ERR_FMT_SQUARE = 5,

	/**
	 * @brief The Locator has an invalid third part
	 */
	LOCATOR_ERR_FMT_SUBSQUARE = 6,

	/**
	 * @brief The Locator has an invalid fourth part
	 */
	LOCATOR_ERR_FMT_EXTSQUARE = 7,

	LOCATOR_ERR_MAX_ = 8,
} locator_err;

/**
 * @brief Maidenhead Locator System
 *
 * Identifies the Maidenhead Locator System grid square of an
 * ARDOP station. The grid square is an optional protocol feature
 * which is strongly encouraged. A failure to transmit and/or
 * decode the grid square does \em not void the frame.
 *
 * Maidenhead grid squares like `BL11BH16` have the following
 * format:
 *
 * | Example | What                 | Range  | Size (geodetic) |
 * |---------|----------------------|--------|-----------------|
 * |       B | Field, longitude     | A–R    | 20°             |
 * |       L | Field, latitude      | A–R    | 10°             |
 * |       1 | Square, longitude    | 0–9    | 2°              |
 * |       1 | Square, latitude     | 0–9    | 1°              |
 * |       b | Subsquare, longitude | a–x    | 5 min           |
 * |       h | Subsquare, latitude  | a–x    | 2.5 min         |
 * |       1 | Extended square, lon | 0–9    | 30 sec          |
 * |       6 | Extended square, lat | 0–9    | 15 sec          |
 *
 * In the 8-character representation, the Earth is divided into
 * a total of 43200 longitude squares and 43200 latitude squares.
 *
 * ARDOP, like many other programs, uses the lowercase
 * representation of the second letter pair.
 */
typedef struct {
	/**
	 * @brief Maidenhead Locator System Grid Square
	 *
	 * The human-readable `grid` square is a string of zero, two,
	 * four, six, or eight characters like `BL11bh16`, alternating
	 * between letters and digits. If no grid square was provided,
	 * this will be the empty string.
	 */
	char grid[LOCATOR_SIZE];

	/**
	 * @brief Wireline representation
	 *
	 * A compressed representation suitable for transmitting
	 * in ARDOP protocol frames. The compressed representation
	 * is always 6 bytes long.
	 *
	 * @warning Instead of accessing this field directly, use
	 * \ref locator_as_bytes().
	 */
	Packed6 wire;
} Locator;

/**
 * @brief Create empty locator
 *
 * The given `locator` is initialized to empty. The empty
 * locator is valid for all frames but conveys no useful
 * information.
 *
 * @param[in] locator    Locator to zeroize.
 */
void locator_init(Locator* locator);

/**
 * @brief Create a locator from a string
 *
 * Accepts a string of the form "`BL11bh16`" and parses it into a
 * Maidenhead Grid Square. If this method returns zero, the grid
 * square is valid and is ready to be transmitted.
 *
 * The grid square is canonicalized with a round-trip through
 * the compression and decompression algorithm. Lowercase letters
 * are silently converted to uppercase.
 *
 * @param[in]  str      Null-terminated C string, which must be
 *                      a valid grid square of length 0, 2, 4, 6,
 *                      or 8.
 * @param[out] locator  Constructed locator
 *
 * @return zero if the input was parsed into a valid locator or a
 * non-zero value if it was not. The entry in \ref locator_err
 * indicates the exact error that occurred. If this method returns
 * an error, `locator` will represent the empty grid square.
 */
ARDOP_MUSTUSE locator_err
locator_from_str(const char* str, Locator* locator);

/**
 * @brief Create a locator from a bounded string
 *
 * Accepts a string of the form "`BL11bh16`" and parses it into a
 * Maidenhead Grid Square. If this method returns zero, the grid
 * square is valid and is ready to be transmitted.
 *
 * The grid square is canonicalized with a round-trip through
 * the compression and decompression algorithm. Lowercase letters
 * are silently converted to uppercase.
 *
 * @param[in]  str      C string, which must be a valid
 *                      grid square of length 0, 2, 4, 6, or 8
 *                      (NUL termination optional)
 * @param[in]  len      Number of characters in `str`, per `strlen()`
 * @param[out] locator  Constructed locator
 *
 * @return zero if the input was parsed into a valid locator or a
 * non-zero value if it was not. The entry in \ref locator_err
 * indicates the exact error that occurred. If this method returns
 * an error, `locator` will represent the empty grid square.
 */
ARDOP_MUSTUSE locator_err
locator_from_str_slice(const char* str, size_t len, Locator* locator);

/**
 * @brief Create a locator from wireline bytes
 *
 * Creates a Locator from a byte buffer. `bytes` must have at
 * least 6 bytes available for reading. If this method
 * returns zero, all fields of the Locator are populated and
 * are ready for either user display or frame transmission.
 *
 * Not all byte sequences represent valid locators, and
 * locators may fail to decode. Failure to decode the Locator
 * does \em not void the frame.
 *
 * @param[in]  bytes    Packed bytes. This method requires
 *                      *exactly* six bytes from `bytes`.
 * @param[out] locator  Decoded locator
 *
 * @return zero if the input was parsed into a valid locator or a
 * non-zero value if it was not. The entry in \ref locator_err
 * indicates the exact error that occurred. If this method returns
 * an error, `locator` will represent the empty grid square.
 */
ARDOP_MUSTUSE locator_err
locator_from_bytes(const uint8_t bytes[PACKED6_SIZE], Locator* locator);

/**
 * @brief Return wireline bytes for transmission
 *
 * Returns the compressed representation of this `locator`.
 * If the locator is unpopulated or a nullptr, returns a special
 * "`NO GS`" byte sequence to use in place of a grid square.
 *
 * @return A struct which contains the wireline representation
 * of `locator`. The return value is cast-compatible with a
 * `uint8_t[6]`.
 */
const Packed6* locator_as_bytes(const Locator* locator);

/**
 * @brief True if the Locator is populated
 *
 * @return true if the locator is populated and valid or false
 * if otherwise. The empty locator will return false.
 */
bool locator_is_populated(const Locator* locator);

/**
 * @brief Obtain human-readable explanation for the given error
 *
 * @param[in] err An error code from a locator function
 *
 * @return Error string. This method will always return a
 * non-nullptr NUL-terminated string with a static lifetime.
 */
const char* locator_strerror(locator_err err);


#endif /* ARDOP_COMMON_LOCATOR_H_ */
