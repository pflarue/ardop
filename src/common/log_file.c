#include "common/log_file.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "common/log.h"

/* emit inline symbol */
extern bool ardop_logfile_need_rollover(
	const time_t current_file_tm,
	const time_t now
);

void ardop_logfile_init(
	ArdopLogFile* logfile,
	const char* logdir,
	const char* stem,
	const char* extension,
	const uint16_t port
) {
	memset(logfile, 0, sizeof(*logfile));
	logfile->logdir = logdir;
	logfile->stem = stem;
	logfile->extension = extension;
	logfile->port = port;
	logfile->opened_at = INT32_MIN;
}

bool ardop_logfile_write(
	ArdopLogFile* logfile,
	const void* msg,
	const size_t msglen)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);

	FILE* outfile = ardop_logfile_handle(logfile, tv.tv_sec);
	if (!!outfile) {
		size_t nwritten = fwrite(msg, msglen, 1, outfile);
		fflush(outfile);
		return (nwritten == msglen);
	}
	else
	{
		return false;
	}
}

bool ardop_logfile_write_norotate(
	ArdopLogFile* logfile,
	const void* msg,
	const size_t msglen)
{
	if (!! logfile->handle) {
		size_t nwritten = fwrite(msg, msglen, 1, logfile->handle);
		fflush(logfile->handle);
		return (nwritten == msglen);
	}
	else
	{
		return false;
	}
}

int ardop_logfile_close(ArdopLogFile* logfile) {
	logfile->opened_at = INT32_MIN;

	if (!! logfile->handle) {
		int rv = fclose(logfile->handle);
		logfile->handle = 0;
		return rv;
	}
	else
	{
		return 0;
	}
}

FILE* ardop_logfile_handle(
	ArdopLogFile* logfile,
	const time_t now)
{
	// no port number → no log file
	if (!logfile->port) {
		return NULL;
	}

	// previous failed open attempt → no log file
	if (!logfile->handle && logfile->opened_at > INT32_MIN) {
		return NULL;
	}

	// open log if there is none or it is time to rollover
	if (!logfile->handle || ardop_logfile_need_rollover(logfile->opened_at, now)) {
		logfile->opened_at = now;

		char log_file_name[1024] = "";
		if (ardop_logfile_calc_filename(
			logfile,
			now,
			log_file_name,
			sizeof(log_file_name)))
		{
			if (! logfile->handle) {
				logfile->handle = fopen(log_file_name, "a");
			} else {
				// close the old log file and open the new one
				// the glibc implementation of this is atomic with dup3(2),
				// but the standard does not guarantee this
				logfile->handle = freopen(log_file_name, "a", logfile->handle);
			}

			if (!logfile->handle) {
				ZF_LOGE("Unable to open log file: \"%s\": %s", log_file_name, strerror(errno));
			}
		}
		else
		{
			logfile->opened_at = INT32_MIN + 1;
			ZF_LOGE("Unable to determine log file name");
		}
	}

	return logfile->handle;
}

ARDOP_MUSTUSE bool ardop_logfile_calc_filename(
	ArdopLogFile* logfile,
	const time_t now,
	char* out,
	const size_t outsize
) {
	// convert time_t to calendar time
	struct tm cal;
	gmtime_r(&now, &cal);

	// format time
	char cal_fmt[32];
	strftime(cal_fmt, sizeof(cal_fmt), "%Y%m%d", &cal);

	// log directory
	const char* d = logfile->logdir ? logfile->logdir : "";

	size_t logdir_len = strlen(d);
	bool need_pathsep =
		logdir_len >= 1 &&
		0 != strncmp(&d[logdir_len - 1], ARDOP_PLATFORM_PATHSEP, 1);

	int nout = snprintf(
		out,
		outsize,
		"%s%.*s%s%hu_%s%s",
		d,
		(int)(need_pathsep),
		ARDOP_PLATFORM_PATHSEP,
		logfile->stem,
		logfile->port,
		cal_fmt,
		logfile->extension
	);

	return (nout > 0 && nout < outsize);
}
