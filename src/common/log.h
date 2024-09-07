/**
 * @brief ARDOP Logging functions
 *
 * Include this header to submit logging information for the
 * console or log files.
 *
 * See logging macros:
 *
 * - \ref ZF_LOGV()
 * - \ref ZF_LOGD()
 * - \ref ZF_LOGI()
 * - \ref ZF_LOGW()
 * - \ref ZF_LOGE()
 * - \ref ZF_LOGF()
 *
 * @warning While zf_log is thread-safe, the ardopcf logger is
 * \em NOT thread-safe. The rollover functionality may race,
 * and the use of stdio buffered output may produce "bad" results.
 * Substantial revisions are required for multi-thread
 * compatibility.
 */
#ifndef ARDOP_COMMON_LOG_H_
#define ARDOP_COMMON_LOG_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

// for gmtime_r()
#define _POSIX_C_SOURCE 200809L
#include <time.h>

/* UTC log timestamps */
#define ZF_LOG_USE_UTC_TIME 1

/* Disable source file location, even in debug mode */
#define ZF_LOG_SRCLOC ZF_LOG_SRCLOC_NONE

/* use our definition of _zf_log_global_output_lvl */
#define ZF_LOG_EXTERN_GLOBAL_OUTPUT_LEVEL

/* use our definition of _zf_log_global_output */
#define ZF_LOG_EXTERN_GLOBAL_OUTPUT

/* by default, log messages have an empty tag */
#define ZF_LOG_DEF_TAG ""

#ifdef NDEBUG
#define ZF_LOG_LEVEL ZF_LOG_DEBUG
#else
#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#include "zf_log/zf_log/zf_log.h"

/**
 * @brief Start logging
 *
 * Start logging to the console and, optionally, to files. This
 * method must be called once per process to initialize logging.
 * Log messages submitted prior to this function call will be
 * suppressed.
 *
 * You should configure the log directory, port number,
 * and verbosity before you call this function.
 *
 * @param[in] enable_files  If true, enable the debug log and
 *            the stats log files.
 *
 * @see ardop_log_set_directory()
 * @see ardop_log_set_port()
 * @see ardop_log_set_level_console()
 * @see ardop_log_set_level_file()
 */
void ardop_log_start(const bool enable_files);

/**
 * @brief Stop logging
 *
 * Closes all log files and halts all log output.
 */
void ardop_log_stop();

/**
 * @brief Enable or disable logging to files
 *
 * Closes all log files. If `file_output` is true, file logs
 * are enabled. Logs will be (re)opened when the next log
 * message is received.
 *
 * @param[in] file_output   True to enable log files
 *
 * @see ardop_log_start
 * @see ardop_log_set_level_file
 * @see ardop_log_set_directory
 *
 * @note This method has no effect unless logging has been
 * started with \ref ardop_log_start().
 */
void ardop_log_enable_files(const bool file_output);

/**
 * @brief True if file logging is enabled
 *
 * @return true if file logging has been enabled via either
 * \ref ardop_log_start() or \ref ardop_log_enable_files()
 *
 * @see ardop_log_enable_files
 *
 * @note Files are only created if a log message with a
 * level of at least \ref ardop_log_get_level_file() is
 * submitted to the logger.
 */
bool ardop_log_is_enabled_files();

/**
 * @brief Set log output directory
 *
 * Sets the directory where log files are output. If NULL or
 * empty, the current working directory is used. The
 * directory must exist or file logging will fail.
 *
 * The new directory takes effect the next time log files
 * are opened. Use \ref ardop_log_start() to make the new
 * directory take effect immediately.
 *
 * @param[in] logdir  Directory where log files will be output
 *            If empty, the current working directory is used.
 *
 * @return true if `logdir` was accepted as the log directory.
 * false if `logdir` is not permissible because it is too long.
 */
bool ardop_log_set_directory(const char* const logdir);

/**
 * @brief Return current logging directory
 *
 * @return A null-terminated C string which gives the current
 * `--logdir` logging directory. The returned pointer is non-NULL
 * but may be an empty string. The log directory is not guaranteed
 * to exist or to have any particular format.
 *
 * @see ardop_log_set_directory
 */
const char* ardop_log_get_directory();

/**
 * @brief Set port number for log files
 *
 * Log files from different ARDOP instances are distinguished by
 * their TCP control port number. If `port` is non-zero, a log
 * file will be opened for appending in the current working
 * directory. File creation is deferred until the first loggable
 * message.
 *
 * The new port number takes effect the next time log files
 * are opened. Use \ref ardop_log_start() to make the new
 * port take effect immediately.
 *
 * @param[in] port  TCP port number of the control port. If
 *            zero, only console logging will be enabled.
 */
void ardop_log_set_port(const uint16_t port);

/**
 * @brief Set minimum significance level of console logs
 *
 * ARDOP log levels ranges from `ZF_LOG_VERBOSE` (`1`) to
 * `ZF_LOG_FATAL` (`6`). A higher value like
 * `ZF_LOG_NONE` (`255`) will disable all log output.
 *
 * This setting only affects the console logs. Log file level
 * is controlled via \ref ardop_log_set_level_file()
 *
 * @param[in] level A log verbosity level such as `ZF_LOG_INFO`
 *
 * @see ardop_log_set_level_file
 */
void ardop_log_set_level_console(const int level);

/**
 * @brief Current minimum significance level for console logs
 *
 * @return A log level such as `ZF_LOG_INFO` which indicates
 * the minimum significance required for console output.
 *
 * @see ardop_log_set_level_console
 */
int ardop_log_get_level_console();

/**
 * @brief Set minimum significance level of file logs
 *
 * ARDOP log levels ranges from `ZF_LOG_VERBOSE` (`1`) to
 * `ZF_LOG_FATAL` (`6`). A higher value like
 * `ZF_LOG_NONE` (`255`) will disable all log output.
 *
 * This setting only affects the contents of log files. A
 * non-zero port number must be specified to
 * \ref ardop_log_set_port() in order for a log file to be
 * created.
 *
 * @param[in] level A log level such as `ZF_LOG_INFO`
 *
 * @see ardop_log_set_level_console
 */
void ardop_log_set_level_file(const int level);

/**
 * @brief Current minimum significance level for file logs
 *
 * @return A log level such as `ZF_LOG_INFO` which indicates
 * the minimum significance required for file output.
 *
 * @see ardop_log_set_level_file
 */
int ardop_log_get_level_file();

/**
 * @brief Write session log "start of record" header
 *
 * The session log records information about every established
 * ARQ session. Each record captures many lines of detailed
 * information about the session.
 *
 * This method writes the banner that occurs at the start of
 * every record.
 *
 * @param[in] peer_callsign  The callsign of the remote station
 * @param[in] now  UNIX time as of session end
 * @param[in] duration  The duration of the session, as a UNIX
 *            time in seconds
 *
 * @see ardop_log_session_info
 * @see ardop_log_session_footer
 */
void ardop_log_session_header(
	const char* peer_callsign,
	const struct timespec* now,
	const time_t duration
);

/**
 * @def ardop_log_session_info
 * @brief Write formatted data to the session log
 *
 * The session log records information about every established
 * ARQ session. Each record captures many lines of detailed
 * information about the session.
 *
 * This method is a `printf()`-like macro that writes the
 * ARQ session details.
 *
 * At the conclusion of your session record, call
 * \ref ardop_log_session_footer() to complete it.
 *
 * @param[in] fmt  `printf`-style format string. Remaining
 *            arguments are printf arguments.
 *
 * @pre Call \ref ardop_log_session_header() first to
 * select the correct log file and write the banner line.
 */
#define ardop_log_session_info(fmt, ...) \
	ZF_LOG_WRITE(ZF_LOG_INFO, "a", fmt, ## __VA_ARGS__)

/**
 * @brief Write session log "end of record" foooter
 *
 * The session log records information about every established
 * ARQ session. Each record captures many lines of detailed
 * information about the session. The session log is written
 * directly and does not use `ZF_LOG` macros.
 *
 * This method writes the banner that occurs at the end of
 * every record.
 */
void ardop_log_session_footer();

#endif /* ARDOP_COMMON_LOG_H_ */
