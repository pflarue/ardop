#ifndef ARDOP_COMMON_STATION_ID_H_
#define ARDOP_COMMON_STATION_ID_H_

#include <stdint.h>

#include "common/mustuse.h"
#include "common/Packed6.h"

/**
 * @def STATIONID_CALL_SIZE
 * @brief Station ID callsign portion buffer length
 *
 * Maximum length of the callsign-only portion of a station
 * ID, including the trailing NUL.
 */
#define STATIONID_CALL_SIZE 8

/**
 * @def STATIONID_SSID_SIZE
 * @brief Station ID SSID portion buffer length
 *
 * Maximum length of the SSID portion of a station ID,
 * including the trailing NUL.
 */
#define STATIONID_SSID_SIZE 3

/**
 * @def STATIONID_BUF_SIZE
 * @brief Station ID string representation buffer length
 *
 * Maximum length of a callsign's string representation,
 * including the trailing NUL.
 */
#define STATIONID_BUF_SIZE 11

/**
 * @brief Error codes returned by Station ID functions
 */
typedef enum {
	/**
	 * @brief The station ID is valid
	 */
	STATIONID_OK = 0,

	/**
	 * @brief The callsign and/or SSID portions are too long
	 */
	STATIONID_ERR_TOOLONG = 1,

	/**
	 * @brief The callsign has invalid characters
	 */
	STATIONID_ERR_CALLSIGN_CHARS = 2,

	/**
	 * @brief The callsign is too short
	 */
	STATIONID_ERR_CALLSIGN_SHORT = 3,

	/**
	 * @brief The callsign is too long
	 */
	STATIONID_ERR_CALLSIGN_LONG = 4,

	/**
	 * @brief The SSID has invalid length or invalid characters
	 */
	STATIONID_ERR_SSID_INVALID = 5,

	STATIONID_ERR_MAX_ = 6,
} station_id_err;

/**
 * @brief Station ID: callsign + SSID
 *
 * ARDOP identifies stations by their callsign and an optional SSID.
 * The callsign and SSID are separated by a minus sign like
 * "`N0CALL-S`." If not explicitly provided, the SSID is assumed to
 * be `0`. Different SSIDs—and even the zero SSID—are treated as
 * entirely separate nodes for all purposes, including ARQ sessions.
 */
typedef struct {
	/**
	 * @brief Station Callsign
	 *
	 * A human-readable string like `N0CALL` which contains only
	 * the callsign. ARDOP callsigns may contain 2 – 7 uppercase
	 * ASCII letters and numbers. Some implementations require
	 * at least three-character callsigns.
	 */
	char call[STATIONID_CALL_SIZE];

	/**
	 * @brief Secondary Station ID (SSID)
	 *
	 * A human-readable string like `A` or `15` which contains
	 * the SSID portion. If the station ID does not include an
	 * explicit SSID, the string is "`0`".
	 */
	char ssid[STATIONID_SSID_SIZE];

	/**
	 * @brief Formatted Callsign and SSID
	 *
	 * The canonical text representation of a station ID is
	 *
	 * ```c
	 * printf("%s-%s", station_id.call, station_id.ssid);
	 * ```
	 *
	 * with the callsign and SSID separated by a dash. If
	 * the SSID is "`0`," this field omits it.
	 *
	 * ```c
	 * printf("%s", station_id.call);
	 * ```
	 *
	 * This field contains a pre-formatted station ID string
	 * as a convenience.
	 */
	char str[STATIONID_BUF_SIZE];

	/**
	 * @brief Wireline representation
	 *
	 * A compressed representation suitable for transmitting
	 * in ARDOP protocol frames. The compressed representation
	 * is always 6 bytes long.
	 */
	Packed6 wire;
} StationId;

/**
 * @brief Zeroize station ID
 *
 * The given `station` ID is initialized to zero. The zero ID is
 * not a valid station ID.
 *
 * @param[in] station    Station ID to zeroize.
 */
void stationid_init(StationId* station);

/**
 * @brief Create a station ID from a string
 *
 * Accepts a string of the form "`N0CALL-15`" and parses it into a
 * station ID. If this method returns zero, all fields of the
 * station ID are populated and are ready for either user display
 * or frame transmission.
 *
 * The input ID is canonicalized with a round-trip through
 * the compression and decompression algorithm. Lowercase letters
 * are silently converted to uppercase. An SSID of "`-0`" is
 * assumed if none is specified. Invalid IDs are rejected and will
 * return an error.
 *
 * @param[in]  str      Null-terminated C string
 * @param[out] station  Constructed station ID
 *
 * @return zero if the input was parsed into a valid station ID
 * or a non-zero value if it was not. The entry in \ref station_id_err
 * indicates the exact error that occurred. If this method returns
 * an error, the contents of `station` are undefined and must not be
 * transmitted.
 */
ARDOP_MUSTUSE station_id_err
stationid_from_str(const char* str, StationId* station);

/**
 * @brief Create a station ID from a bounded string
 *
 * Accepts a string of the form "`N0CALL-15`" and parses it into a
 * station ID. If this method returns zero, all fields of the
 * station ID are populated and are ready for either user display
 * or frame transmission.
 *
 * The input ID is canonicalized with a round-trip through
 * the compression and decompression algorithm. Lowercase letters
 * are silently converted to uppercase. An SSID of "`-0`" is
 * assumed if none is specified. Invalid IDs are rejected and will
 * return an error.
 *
 * @param[in]  str      C string (NUL termination optional)
 * @param[in]  len      Number of characters in `str`, per `strlen()`
 * @param[out] station  Constructed station ID
 *
 * @return zero if the input was parsed into a valid station ID
 * or a non-zero value if it was not. The entry in \ref station_id_err
 * indicates the exact error that occurred. If this method returns
 * an error, the contents of `station` are undefined and must not be
 * transmitted.
 */
ARDOP_MUSTUSE station_id_err
stationid_from_str_slice(const char* str, size_t len, StationId* station);

/**
 * @brief Read array of station IDs
 *
 * Read multiple station IDs from `str` into an array `station`.
 * Station IDs may be delimited by commas or whitespace.
 * Each ID is read and canonicalized as per
 * \ref stationid_from_str_slice().
 *
 * @param[in]  str      Null-terminated C string
 * @param[out] station  Array of station IDs
 * @param[in] capacity  Maximum number of elements in `station`
 * @param[out] len      Number of station IDs read into `station`.
 *                      If this method returns an error, this value
 *                      is the array index at which the error
 *                      ocurred.
 *
 * @return zero if the entire input was parsed into a valid station ID
 * array or a non-zero value if it was not. The entry in
 * \ref station_id_err indicates the exact error that occurred. If this
 * method returns an error, the contents of `station` are undefined and
 * must not be transmitted.
 */
ARDOP_MUSTUSE station_id_err
stationid_from_str_to_array(
	const char* str,
	StationId* stations,
	const size_t capacity,
	size_t* len
);

/**
 * @brief Create a station ID from wireline bytes
 *
 * Creates a StationId from a byte buffer. `bytes` must have at
 * least 6 bytes available for reading. If this method
 * returns zero, all fields of the station ID are populated and
 * are ready for either user display or frame transmission.
 *
 * Not all byte sequences represent valid station IDs, and
 * station IDs may fail to decode.
 *
 * @param[in]  bytes    Packed bytes. This method requires
 *                      *exactly* six bytes from `bytes`.
 *                      Must be exactly 6 (`sizeof(Packed6)`)
 * @param[out] station  Decoded station ID
 *
 * @return zero if the wireline input was decoded into a valid
 * station ID, or a non-zero value if it was not. The entry in
 * \ref station_id_err indicates the exact error that occurred.
 * If this method returns an error, the contents of `station`
 * are undefined and must not be relied upon.
 */
ARDOP_MUSTUSE station_id_err
stationid_from_bytes(const uint8_t bytes[PACKED6_SIZE], StationId* station);

/**
 * @brief Copy wireline bytes to buffer
 *
 * If the station ID is valid, copies the ARDOP wireline
 * representation to the given `dest`ination buffer. Exactly
 * 6 (`sizeof(Packed6)`) bytes are written to `dest`. If the
 * station ID is not valid, nothing is written.
 *
 * @param[in] station   Station ID, may be NULL or invalid
 * @param[out] dest     Destination buffer, which must have
 *                      at least 6 bytes of space available.
 *
 * @return true if the `station` ID is valid and was copied, or
 * false if the station ID is not valid.
 *
 * @see stationid_ok()
 */
ARDOP_MUSTUSE bool
stationid_to_buffer(const StationId* station, uint8_t dest[PACKED6_SIZE]);

/**
 * @brief Return wireline bytes for transmission
 *
 * Returns the compressed representation of this `station`.
 * If the given `station` is NULL or not populated, returns a
 * nullptr.
 *
 * @return A struct which contains the wireline representation
 * of `station`. The return value is cast-compatible with a
 * `uint8_t[6]`.
 *
 * @see stationid_ok()
 */
const Packed6* stationid_as_bytes(const StationId* station);

/**
 * @brief Convert array of station IDs to string representation
 *
 * Converts the given `stations` array to their canonical ASCII
 * string representations, joining each with the given string
 * `delim`. The given `dest` must be non-NULL and will be
 * NUL-terminated if its size is at least 1 byte.
 *
 * @param[in] stations   Array of station IDs
 * @param[in] len        Number of populated entries in `stations`
 * @param[out] dest      Destination string buffer. Must be
 *                       non-NULL
 * @param[in] dest_size  Capacity of `dest`, in bytes
 * @param[in] delim      Delimiter string, like `", "`
 * @param[in] header     Header string, like `"MYAUX"`.
 *                       May be NULL.
 * @param[in] separator  String separating header and station IDs,
 *                       like `" "`. May be NULL.
 *
 * @return true if the entire output was written or if truncation
 * has occurred. The output is always NUL-terminated, even if
 * truncated.
 */
bool stationid_array_to_str(
	const StationId* stations,
	const size_t len,
	char* dest,
	const size_t dest_size,
	const char* delim,
	const char* header,
	const char* separator
);

/**
 * @brief True if the Station ID is valid
 *
 * @return true if the station ID is populated and valid or false
 * if otherwise.
 */
bool stationid_ok(const StationId* station);

/**
 * @brief Equality test
 *
 * @return true if the two station IDs are equivalent—i.e.,
 * that they represent the same node.
 */
bool stationid_eq(const StationId* a, const StationId* b);

/**
 * @brief Obtain human-readable explanation for the given error
 *
 * @param[in] err An error code from a station_id function
 *
 * @return Error string. This method will always return a
 * non-nullptr NUL-terminated string with a static lifetime.
 */
const char* stationid_strerror(station_id_err err);

#endif /* ARDOP_COMMON_STATION_ID_H_ */
