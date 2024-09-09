/*
 * This header is internal to log_control.c and should not
 * be included elsewhere
 */

#ifndef ARDOP_COMMON_LOG_FILE_H_
#define ARDOP_COMMON_LOG_FILE_H_

 // for gmtime_r()
#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "common/mustuse.h"

// Flooring integer division
//
// Computes the equivalent of floor(a/b) where a and b are
// integers of the same type.
//
// In C99, integer division always rounds towards zero.
// This method always rounds towards -∞. If there is a
// remainder, and the operands have matching signs, we
// subtract one from the result.
#define ARDOP_LOG_DIV_FLOOR(a,b) \
	a/b - (a%b!=0 && (a^b)<0)

/**
 * @def ARDOP_PLATFORM_PATHSEP
 * @brief Platform preferred file path separator
 */
#if defined(_WIN32) || defined(_WIN64)
#define ARDOP_PLATFORM_PATHSEP "\\"
#else
#define ARDOP_PLATFORM_PATHSEP "/"
#endif

/**
 * @brief Log file definition
 *
 * Each log file has a fixed stem and extension. This datatype
 * also records the time the file was opened and a handle to
 * the currently open file, if any.
 */
typedef struct ArdopLogFile {
	/**
	 * @brief Directory for log files
	 */
	const char* logdir;

	/**
	 * @brief First part of the filename
	 */
	const char* stem;

	/**
	 * @brief File extension, including the dot
	 */
	const char* extension;

	/**
	 * @brief TCP control port (permits concurrent ARDOP instances)
	 */
	uint16_t port;

	/**
	 * @brief UNIX time the file was opened, in seconds
	 */
	time_t opened_at;

	/**
	 * @brief Open file handle (may be NULL)
	 */
	FILE* handle;
} ArdopLogFile;

/**
 * @brief Initialize log file
 *
 * @param[in] logfile  Log file to initialize
 *
 * @param[in] logdir Directory for log files. This buffer must
 *            have a lifetime that exceeds `logfile`
 *            (preferably `static`).
 *
 * @param[in] stem Log file prefix. This buffer must
 *            have a lifetime that exceeds `logfile`
 *            (preferably `static`).
 *
 * @param[in] extension Log file extension, including the dot.
 *            This buffer must have a lifetime that exceeds
 *            `logfile` (preferably `static`).
 *
 * @param[in] port TCP control port. This permits multiple
 *            concurrent instances of ARDOP. If zero, no log
 *            file can be opened.
 *
 * @warning This method will leak file descriptors if you init
 * a `logfile` that is already open.
 */
void ardop_logfile_init(
	ArdopLogFile* logfile,
	const char* logdir,
	const char* stem,
	const char* extension,
	const uint16_t port
);

/**
 * @brief Write to log file
 *
 * Write the given bytes to the `logfile`. The log file will
 * be opened and/or rotated if need be.
 *
 * @param[in] logfile  Destination log
 *
 * @param[in] msg Exact bytes to write. If newlines or other
 *            delimiters are desired, they must be included
 *            in the byte stream.
 *
 * @param[in] msglen Number of bytes of `msg` to write.
 *            Text logs should use a `strlen()` or similar
 *            count to exclude the NUL terminator.
 *
 * @return true if all bytes were written, false if otherwise
 */
bool ardop_logfile_write(
	ArdopLogFile* logfile,
	const void* msg,
	const size_t msglen
);

/**
 * @brief Write to log file (don't rotate)
 *
 * Write the given bytes to the `logfile`. The log file must
 * already be open and will *not* be rotated. If the log file
 * is not open, this method will return false.
 *
 * @param[in] logfile  Destination log
 *
 * @param[in] msg Exact bytes to write. If newlines or other
 *            delimiters are desired, they must be included
 *            in the byte stream.
 *
 * @param[in] msglen Number of bytes of `msg` to write.
 *            Text logs should use a `strlen()` or similar
 *            count to exclude the NUL terminator.
 *
 * @return true if all bytes were written, false if otherwise
 */
bool ardop_logfile_write_norotate(
	ArdopLogFile* logfile,
	const void* msg,
	const size_t msglen
);

/**
 * @brief Close the log file
 *
 * Close the log file, if it is open.
 *
 * @return the result value from `fclose(3)`. If the log file
 * was not open, returns 0.
 */
int ardop_logfile_close(ArdopLogFile* logfile);

/**
 * @brief Get log file handle
 *
 * Rotate the log file if necessary. Returns a handle to the
 * current log file or NULL if no log file is available or can
 * be opened.
 *
 * Unless you need the handle for some reason, you are better
 * off just using \ref ardop_logfile_write() or
 * \ref ardop_logfile_write_norotate().
 *
 * @param[in] logfile  Log file for which a handle is needed
 *
 * @param[in] now   Current UNIX time
 *
 * @return Open and writeable file handle for the `logfile`'s
 * backing file. If no log file is or can be opened, returns
 * `NULL`.
 *
 * @note The caller should take care not to cache the returned
 * handle. Instead, call this method again to check if the file
 * needs to be rotated.
 */
FILE* ardop_logfile_handle(
	ArdopLogFile* logfile,
	const time_t now
);

/**
 * @brief Calculate log filename
 *
 * Calculates a log filename based on the given or current
 * time. Populates a filename in `out` which includes
 * directory information.
 *
 * @param[in] logfile  Log file for which a handle is needed
 *
 * @param[in] now  Current system UNIX time, in seconds
 *
 * @param[out] out  Write computed filename to this
 *             buffer.
 *
 * @param[in] outsize  The size of out, per `sizeof(out)`.
 *
 * @return true if the entire filename was stored in `out`
 * or false if not. If this method returns false, the
 * output must not be used.
 */
ARDOP_MUSTUSE bool ardop_logfile_calc_filename(
	ArdopLogFile* logfile,
	const time_t now,
	char* out,
	const size_t outsize
);

/**
 * @brief Check if log rollover is needed
 *
 * Returns true if the current UTC day has changed, indicating
 * that we need to rollover to a new log file. This check is
 * designed with speed in mind.
 *
 * @param[in] current_file_tm  UNIX time at which the current
 *            file was opened.
 *
 * @param[in] now  Current system UNIX time, from clock_gettime()
 *            or similar
 *
 * @return true if a new log file is needed or false if the
 * current log file is OK.
 */
inline bool ardop_logfile_need_rollover(
	const time_t current_file_tm,
	const time_t now
) {
	// a UNIX day is always 86400 seconds long
	// yes, even if there's a leap second
	const time_t SECONDS_PER_DAY = 86400;

	// Compare based on UNIX day number. We always round
	// the division down to handle negative times correctly.
	time_t daynum_file = ARDOP_LOG_DIV_FLOOR(current_file_tm, SECONDS_PER_DAY);
	time_t daynum_now = ARDOP_LOG_DIV_FLOOR(now, SECONDS_PER_DAY);

	return (daynum_file != daynum_now);
}

#endif /* ARDOP_COMMON_LOG_FILE_H_ */
