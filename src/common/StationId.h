#ifndef ARDOP_COMMON_STATION_ID_H_
#define ARDOP_COMMON_STATION_ID_H_

#include <stdint.h>

#include "common/mustuse.h"
#include "common/Packed6.h"

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
	 * @brief The callsign has invalid length or invalid characters
	 */
	STATIONID_ERR_INVALID_CALLSIGN = 2,

	/**
	 * @brief The SSID has invalid length or invalid characters
	 */
	STATIONID_ERR_INVALID_SSID = 3,

	/**
	 * @brief The station ID cannot be compressed
	 */
	STATIONID_ERR_COMPRESS = 4,

	/**
	 * @brief The station ID cannot be decompressed
	 */
	STATIONID_ERR_DECOMPRESS = 5,
} station_id_err;

/**
 * @brief Station ID: callsign + SSID
 *
 * ARDOP identifies stations by their callsign and an optional SSID.
 * The callsign and SSID are separated by a minus sign like
 * "`N0CALL-S`." If not explicitly provided, the SSID is assumed to
 * be `0`. Different SSIDs—and even the zero SSID—are treated as
 * entirely separate nodes for all purposes, including ARQ sessions.
 *
 * The canonical text representation of a station ID is
 *
 * ```c
 * printf("%s-%s", station_id.call, station_id.ssid);
 * ```
 *
 * with the callsign and SSID separated by a dash.
 */
typedef struct {
	/**
	 * @brief Station Callsign
	 *
	 * A human-readable string like `N0CALL` which contains only
	 * the callsign. ARDOP callsigns may contain 3 – 7 uppercase
	 * ASCII letters and numbers.
	 */
	char call[8];

	/**
	 * @brief Secondary Station ID (SSID)
	 *
	 * A human-readable string like `A` or `15` which contains
	 * the SSID portion. If the station ID does not include an
	 * explicit SSID, the string is "`0`".
	 */
	char ssid[3];

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
 * @brief Make a CQ call
 *
 * Construct a station ID that represents a broadcast message to
 * all stations. CQ calls are not valid as source callsigns and
 * should never be used in IDFRAMEs.
 *
 * @param[in] station    Station ID to populate.
 */
void stationid_make_cq(StationId* station);

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
 * @brief Create a station ID from wireline bytes
 *
 * Creates a StationId from a byte buffer. `bytes` must have at
 * least `nbytes >= 6` bytes available for reading. If this method
 * returns zero, all fields of the station ID are populated and
 * are ready for either user display or frame transmission.
 *
 * Not all byte sequences represent valid station IDs, and
 * station IDs may fail to decode.
 *
 * @param[in]  bytes    Packed bytes. This method requires
 *                      *exactly* six bytes from `bytes`.
 * @param[in]  nbytes   Number of bytes to read from `bytes`.
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
stationid_from_bytes(const void* bytes, size_t nbytes, StationId* station);

/**
 * @brief True if the Station ID is valid
 *
 * @return true if the station ID is populated and valid or false
 * if otherwise.
 */
bool stationid_ok(const StationId* station);

/**
 * @brief True if the station ID is a "CQ" broadcast
 *
 * Messages directed at "`CQ`" with any SSID are intended for
 * all stations. A station ID of "`CQ`" is not valid for use
 * as a source callsign or for use in IDFRAMEs.
 *
 * @return true if the station ID is a CQ call
 */
bool stationid_is_cq(const StationId* station);

/**
 * @brief Equality test
 *
 * @return true if the two station IDs are equivalent—i.e.,
 * that they represent the same node.
 */
bool stationid_eq(const StationId* a, const StationId* b);

#endif /* ARDOP_COMMON_STATION_ID_H_ */
