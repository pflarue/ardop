#include "common/log.h"

#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "common/log_file.h"

// Configure log decoration, see zf_log.c
#define ZF_LOG_MESSAGE_CTX_FORMAT (\
	HOUR, S(":"), MINUTE, S(":"), SECOND, S("."), MILLISECOND, S(ZF_LOG_DEF_DELIMITER), \
	LEVEL, S(ZF_LOG_DEF_DELIMITER) \
)

// Return minimum of comparable arguments a and b
#define MIN(a,b) (a <= b ? a : b)

// Retry POSIX syscall until it returns non-zero
#define RETVAL_UNUSED(expr) \
	do { while(expr) break; } while(0)

/*
 * Discard Log-writing callback
 *
 * Before we call ardop_log_start(), all log messages are discarded
 * by default.
 */
static void log_callback_discard(const zf_log_message* msg, void* param);

/*
 * Log-writing callback
 *
 * Receives formatted messages from zf_log for printing. This method
 * is based on zf_log_out_stderr_callback from zf_log.c.
 */
static void log_callback(const zf_log_message* msg, void* param);

/*
 * Global log verbosity level
 */
int _zf_log_global_output_lvl = ZF_LOG_DEBUG;

/*
 * Global log output facility
 *
 * The default callback discards all messages until we enable them
 * via ardop_log_start()
 */
zf_log_output _zf_log_global_output = { ZF_LOG_PUT_STD, 0, log_callback_discard };

/*
 * Console log verbosity
 */
static int ArdopLogVerbosityConsole = ZF_LOG_INFO;

/*
 * File log verbosity
 */
static int ArdopLogVerbosityFile = ZF_LOG_DEBUG;

/*
 * True if file output is enabled
 */
static bool ArdopLogFileEnabled = false;

/*
 * Log output directory (may be empty)
 *
 * If empty, logs are output to the working directory
 */
static char ArdopLogDir[512] = "";

/*
 * Debug log file
 *
 * The debug log replicates the console log stream, potentially
 * with a different verbosity level.
 */
static ArdopLogFile DebugLog = {
	ArdopLogDir,
	"ARDOPDebug",
	".log",
	0,
	0,
	NULL
};

/*
 * Session log file
 *
 * The session log records details of ARQ sessions. The session log
 * accepts raw, pre-formatted input and does not use ZF_LOG.
 */
static ArdopLogFile SessionLog = {
	ArdopLogDir,
	"ARDOPSession",
	".log",
	0,
	0,
	NULL
};

static ArdopLogFile* ALL_LOG_FILES[] = {
	&DebugLog,
	&SessionLog
};

static void log_callback_discard(const zf_log_message* _msg, void* _param) {}

static void log_callback(const zf_log_message* msg, void* param) {
	(void)param; /* unused */

	const char EOL[] = "\n";
	const size_t EOL_SZ = sizeof(EOL) - 1;

	// zf_log always leaves enough space in the buffer for a newline
	memcpy(msg->p, EOL, EOL_SZ);

	if (msg->lvl >= ArdopLogVerbosityConsole)
	{
		// write undecorated message to stdout
#if defined(_WIN32) || defined(_WIN64)
		/* WriteFile() is atomic for local files opened with FILE_APPEND_DATA and
		without FILE_WRITE_DATA */
		DWORD written;
		WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), msg->msg_b,
			(DWORD)(msg->p - msg->msg_b + EOL_SZ), &written, 0);
#else
		/* write() is atomic for buffers less than or equal to PIPE_BUF. */
		RETVAL_UNUSED(write(STDOUT_FILENO, msg->msg_b,
			(size_t)(msg->p - msg->msg_b) + EOL_SZ));
#endif
	}

	if (!ArdopLogFileEnabled || msg->lvl < ArdopLogVerbosityFile)
		return;

	// write decorated message to debug log file
	ardop_logfile_write(&DebugLog, msg->buf, (size_t)(msg->p - msg->buf) + EOL_SZ);

	// tags for alternate log files
	switch (msg->tag[0]) {
		case 'A':
			// start ARQ session log record
			ardop_logfile_write(&SessionLog, msg->msg_b, (size_t)(msg->p - msg->msg_b) + EOL_SZ);
			break;
		case 'a':
			// continue ARQ session log record
			ardop_logfile_write_norotate(&SessionLog, msg->msg_b, (size_t)(msg->p - msg->msg_b) + EOL_SZ);
			break;
	}
}

void ardop_log_start(const bool enable_files) {
	zf_log_set_output_v(ZF_LOG_PUT_STD, NULL, log_callback);
	ardop_log_enable_files(enable_files);
}

void ardop_log_stop() {
	ardop_log_enable_files(false);
	zf_log_set_output_v(ZF_LOG_PUT_STD, NULL, log_callback_discard);
}

void ardop_log_enable_files(const bool file_output) {
	// Close all log files
	// They are reopened on the next loggable message
	for (size_t i = 0; i < sizeof(ALL_LOG_FILES) / sizeof(ALL_LOG_FILES[0]); ++i) {
		ardop_logfile_close(ALL_LOG_FILES[i]);
	}

	ArdopLogFileEnabled = file_output;
}

bool ardop_log_is_enabled_files() {
	return ArdopLogFileEnabled;
}

bool ardop_log_set_directory(const char* const logdir) {
	const char* d = logdir ? logdir : "";
	int rv = snprintf(ArdopLogDir, sizeof(ArdopLogDir), "%s", d);
	if (rv < 0 || rv >= sizeof(ArdopLogDir)) {
		ArdopLogDir[0] = '\0';
		return false;
	}

	return true;
}

const char* ardop_log_get_directory() {
	return ArdopLogDir;
}

void ardop_log_set_port(const uint16_t port) {
	for (size_t i = 0; i < sizeof(ALL_LOG_FILES)/sizeof(ALL_LOG_FILES[0]); ++i) {
		ALL_LOG_FILES[i]->port = port;
	}
}

int ardop_log_level_filter(const int level) {
	if (level > ZF_LOG_FATAL)
		return ZF_LOG_NONE;
	else if (level < ZF_LOG_VERBOSE)
		return ZF_LOG_VERBOSE;
	else
		return level;
}

void ardop_log_set_level_console(const int level) {
	ArdopLogVerbosityConsole = ardop_log_level_filter(level);
	zf_log_set_output_level(MIN(ArdopLogVerbosityConsole, ArdopLogVerbosityFile));
}

int ardop_log_get_level_console() {
	return ArdopLogVerbosityConsole;
}

void ardop_log_set_level_file(const int level) {
	ArdopLogVerbosityFile = ardop_log_level_filter(level);
	zf_log_set_output_level(MIN(ArdopLogVerbosityConsole, ArdopLogVerbosityFile));
}

int ardop_log_get_level_file() {
	return ArdopLogVerbosityFile;
}

void ardop_log_session_header(
	const char* remote_callsign,
	const struct timespec* now,
	const time_t duration
)
{
	if (!ZF_LOG_ON_INFO)
		return;

	char datefmt[32] = "";

	// convert time_t to calendar time
	struct tm cal;
	gmtime_r(&now->tv_sec, &cal);
	// 2024-08-10T17:57:36
	strftime(datefmt, sizeof(datefmt), "%Y-%m-%dT%H:%M:%S", &cal);

	ZF_LOG_WRITE(
		ZF_LOG_INFO,
		"A",
		"%s,%09ld+00:00\n************************* ARQ session stats with %s  %d minutes ****************************\n",
		datefmt,
		now->tv_nsec,
		remote_callsign,
		(int)duration
	);
}

void ardop_log_session_footer() {
	ZF_LOG_WRITE(\
		ZF_LOG_INFO, \
		"a", \
		"************************************************************************************************" \
	);
}

// build zf_log with our custom config macros
#include "zf_log/zf_log/zf_log.c"
