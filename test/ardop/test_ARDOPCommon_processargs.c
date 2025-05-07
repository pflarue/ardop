#ifdef WIN32
// Windows specific socket stuff
// Build for Windows Vista (0x0600) or later for WSAPoll() from winsock2.
// mingw32 defaults to Windows XP (0x0501) defined in _mingw.h.
#define _WIN32_WINNT 0x0600

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
// Linux specific socket stuff
#include <sys/socket.h>
#endif

#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include <stdio.h>
#include <unistd.h>

#include "setup.h"
#include "common/ardopcommon.h"
#include "common/ptt.h"

// The following #define lines are copied from ptt.c.  Except for testing, they
// are not used outside of ptt.c, and thus are not included in ptt.h
#define PTTRTS 1
#define PTTDTR 2
#define PTTCM108 4
#define PTTGPIO 8
#define PTTCAT 16
#define PTTTCPCAT 32

// The following extern variables are defined in ptt.c  Except for testing, they
// are not used outside of ptt.c, and thus are not included in ptt.h
extern int PTTmode;  // PTT Control Flags.
extern char CATstr[200];  // CAT str
extern char PTTstr[200];  // non-cat PTT str

extern HANDLE hCATdevice;  // HANDLE/file descriptor for CAT device
extern HANDLE hPTTdevice;  // HANDLE/file descriptor for PTT by RTS/DTR device
extern HANDLE hCM108device;  // HANDLE/file descriptor for PTT by CM108 device
extern int tcpCATport;  // TCP port (usually on 127.0.0.1) for CAT

extern unsigned char ptt_on_cmd[MAXCATLEN];
extern unsigned int ptt_on_cmd_len;
extern unsigned char ptt_off_cmd[MAXCATLEN];
extern unsigned int ptt_off_cmd_len;
extern int GPIOpin;
extern bool GPIOinvert;


// The following global variables defined someplace other than ptt.c are used
// by processargs()
extern char CaptureDevice[80];
extern char PlaybackDevice[80];
extern char *HostCommands;
extern bool UseLeftRX;
extern bool UseRightRX;
extern bool UseLeftTX;
extern bool UseRightTX;
extern int wg_port;
extern bool WG_DevMode;
extern char DecodeWav[5][256];
extern bool WriteRxWav;
extern bool WriteTxWav;
extern bool UseSDFT;

int processargs(int argc, char * argv[]);
int bytes2hex(char *outputStr, size_t count, unsigned char *data, size_t datalen, bool spaces);

// Testing setup:
// printstrs will contain the first portion of each output captured by
// __wrap_printf(), __wrap_puts(), and mock_output_callback().  The first
// character of each string will be 'P' for printf(), 'S' for puts(), or 'Z'
// for test_callback().
#define MAXPRINTSTRS 20
#define MAXSTRLEN 120
char printstrs[MAXPRINTSTRS][MAXSTRLEN + 1];
int printindex = -1;
// If suppressprintf is true, printf() output is supressed.
// It may be set to false for debugging purposes.
bool suppressprintf = true;

int __real_printf(const char *restrict format, ...);

void reset_printstrs() {
	for (int i = 0; i < MAXPRINTSTRS; i++)
		printstrs[i][0] = 0x00;
	printindex = -1;
}

int __wrap_printf(const char *restrict format, ...) {
	(void) format;   // This line avoids an unused parameter warning
	if (++printindex == MAXPRINTSTRS) {
		__real_printf("P MAXPRINTSTRS exceeded.  Unable to store additional text.\n");
		return 1;
	}
	printstrs[printindex][0] = 'P';
	va_list args;
	va_start(args, format);
	vsnprintf(&(printstrs[printindex][1]), MAXSTRLEN - 1, format, args);
	if (suppressprintf) {
		va_end(args);
		return strlen(printstrs[printindex]);
	}
	int ret = vprintf(format, args);
	va_end(args);
	return ret;
}

// gcc optimization may replace printf() with puts().  So, also wrap it.
int __real_puts(const char *s);
int __wrap_puts(const char *s) {
	if (++printindex == MAXPRINTSTRS) {
		__real_printf("S MAXPRINTSTRS exceeded.  Unable to store additional text.\n");
		return 1;
	}
	printstrs[printindex][0] = 'S';
	snprintf(&(printstrs[printindex][1]), MAXSTRLEN - 1, "%s", s);
	if (suppressprintf)
		return strlen(s);
	return __real_puts(s);
}

static void test_callback(const zf_log_message *msg, void *arg) {
	(void) arg;  // This line avoids an unused parameter warning
	if (++printindex == MAXPRINTSTRS) {
		__real_printf("Z MAXPRINTSTRS exceeded.  Unable to store additional text.\n");
		return;
	}
	printstrs[printindex][0] = 'Z';
	snprintf(&(printstrs[printindex][1]), MAXSTRLEN - 1, "%s", msg->buf);
	if (suppressprintf)
		return;
	__real_puts(msg->buf);
	return;
}

void __real_ardop_log_start(const bool enable_files, const bool syslog);
void __wrap_ardop_log_start(const bool enable_files, const bool syslog) {
	(void) enable_files;  // This line avoids an unused parameter warning
	(void) syslog;  // This line avoids an unused parameter warning
	// Always start log with enable_files false and syslog false.
	// Thus, no log files will be created, while test_callback() will store
	// log messages for testing purposes.
	__real_ardop_log_start(false, false);
	zf_log_set_output_v(ZF_LOG_PUT_STD, 0, test_callback);
}

void __wrap_InitAudio(bool quiet) {
	(void) quiet;
	return;
}

bool __wrap_OpenSoundCapture(char *devstr, int ch) {
	(void) devstr;
	(void) ch;
	RXEnabled = true;
	strcpy(CaptureDevice, devstr);
	return true;
}

bool __wrap_OpenSoundPlayback(char *devstr, int ch) {
	(void) devstr;
	(void) ch;
	TXEnabled = true;
	strcpy(PlaybackDevice, devstr);
	return true;
}


HANDLE __wrap_OpenCOMPort(void * Port, int speed) {
	(void) Port;  // This line avoids an unused parameter warning
	(void) speed;  // This line avoids an unused parameter warning
	// return mock_ptr_type(HANDLE);  // This works on Linux but not Windows.
	// The following works on both Linux and Windows.
	// Without first casting to size_t, it works but gives a warning when
	// compiling for 32-bit Windows.
	return (HANDLE) ((size_t) mock());
}

int __wrap_tcpconnect(char *address, int port) {
	(void) address;  // This line avoids an unused parameter warning
	(void) port;  // This line avoids an unused parameter warning
	return (int) ((size_t) mock());
}

int __wrap_OpenCM108(char * devstr) {
	(void) devstr;  // This line avoids an unused parameter warning
	return (int) ((size_t) mock());
}

// For diagnostic purposes, print the results capture by some of the __wrap
// functions.
void print_printstrs() {
	for (int i = 0; i <= printindex; i++)
		__real_printf("%i: %s\n", i, printstrs[i]);
}

// Restore the variables that processarg() may have changed back to their
// default values in preparation for another test.
void reset_defaults() {
	optind = 1;  // reset getopt_long()
	ardop_log_set_level_file(2);
	ardop_log_set_level_console(3);
	PTTmode = 0;  // PTT Control Flags.
	CATstr[0] = 0x00;
	PTTstr[0] = 0x00;

	hCATdevice = 0;
	hPTTdevice = 0;
	hCM108device = 0;
	GPIOpin = 0;

	TXEnabled = false;
	RXEnabled = false;

	ptt_on_cmd_len = 0;
	ptt_off_cmd_len = 0;
	UseLeftRX = true;
	UseRightRX = true;
	UseLeftTX = true;
	UseRightTX = true;
	GPIOinvert = false;
	host_port = 8515;
	wg_port = 0;
	WG_DevMode = false;
	WriteRxWav = false;
	WriteTxWav = false;
	CaptureDevice[0] = 0x00;  // zero length string
	PlaybackDevice[0] = 0x00;  // zero length string
	for (long unsigned int i = 0; i < sizeof(DecodeWav) / sizeof(DecodeWav[0]); i++) {
		DecodeWav[i][0] = 0x00;  // zero length null terminated string
	}
	UseSDFT = false;
}


// For each test of processargs(), the return value is checked, and where one or
// more variables is set, the value of those is also checked.  Many inputs
// result in errors or warnings being printed or logged.  Rather than
// checking the content of those error or warning messages, the number of
// strings printed or logged is checked to ensure that at least the right number
// of these occured.  This seems sufficient to check whether the expected
// behavior is occuring.  For debugging purposes, or to further evaluate why
// a test is failing, the call to either print_printstrs() or
// __real_printf("%i: %s\n", printindex, printstrs[printindex]) included with
// each test can be uncommented.  The former prints the first part of each
// printed/logged message, while the latter prints only the final message.
// If this is insufficient, setting suppressprintf = false may be used to print
// each message as it is issued rather than doing so after processargs() has
// returned.

// Each test condition is wrapped in "if (true) {}".  This allows one or more
// test conditions to be temporarily disabled by changing "true" to "false".
// The {} also provides a local/block scope within which testargc and testargv
// are defined, unique to each test condition.
static void test_processargs(void** state) {
	int ret;
	(void) state;  // This line avoids an unused parameter warning

	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// no options
		int testargc = 1;
		char *testargv[] = {"ardopcf"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
	}

	// Test the options that are evaluated early
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --nologfile
		int testargc = 2;
		char *testargv[] = {"ardopcf", "--nologfile"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		// nothing to verify since forcing no log files for testing
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -m
		int testargc = 2;
		char *testargv[] = {"ardopcf", "-m"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		// nothing to verify since forcing no log files for testing
	}
#ifdef WIN32
	// for non-Linux, --syslog and -S are invalid options
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --syslog as Unknown option (ERROR)
		int testargc = 2;
		char *testargv[] = {"ardopcf", "--syslog"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + err + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -S as Unknown option (ERROR)
		int testargc = 2;
		char *testargv[] = {"ardopcf", "-S"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + err + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
	}
#else
	// option only valid on Linux
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --syslog
		int testargc = 2;
		char *testargv[] = {"ardopcf", "--syslog"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		// nothing to verify since forcing not syslog for testing
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --S
		int testargc = 2;
		char *testargv[] = {"ardopcf", "-S"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		// nothing to verify since forcing not syslog for testing
	}
#endif
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --help
		int testargc = 2;
		char *testargv[] = {"ardopcf", "--help"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		assert_int_equal(printindex, 4);  // nstartstrings=3 + cmdstr + help
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -h
		int testargc = 2;
		char *testargv[] = {"ardopcf", "-h"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		assert_int_equal(printindex, 4);  // nstartstrings=3 + cmdstr + help
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// unknown option, then -h
		// Because -h is processed early, being preceeded by an invalid option
		// does not change its behavior.
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-X", "-h"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		assert_int_equal(printindex, 4);  // nstartstrings=3 + cmdstr + help
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -h, then Unknown option
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-h", "-X"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		assert_int_equal(ret, 1);  // 1 = print help and exit
		assert_int_equal(printindex, 4);  // nstartstrings=3 + cmdstr + help
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --logdir
		int testargc = 3;
		char *testargv[] = {"ardopcf", "--logdir", "some/log/dir"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(ardop_log_get_directory(), "some/log/dir");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -l
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-l", "some/log/dir"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(ardop_log_get_directory(), "some/log/dir");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -l (path too long > 512 char)
		int testargc = 3;
		char *testargv[] = {
			"ardopcf", "-l",
			"0_234567891_234567892_234567893_234567894_23456789"
			"5_234567896_234567897_234567898_234567899_23456789"
			"10_345678911_345678912_345678913_345678914_3456789"
			"15_345678916_345678917_345678918_345678919_3456789"
			"20_345678921_345678922_345678923_345678924_3456789"
			"25_345678926_345678927_345678928_345678929_3456789"
			"30_345678931_345678932_345678933_345678934_3456789"
			"35_345678936_345678937_345678938_345678939_3456789"
			"40_345678941_345678942_345678943_345678944_3456789"
			"45_345678946_345678947_345678948_345678949_3456789"
			"50_345678941"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + ERR + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_string_equal(ardop_log_get_directory(), "");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -H single command
		// Host commands not starting with LOGLEVEL or CONSOLELOG
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-H", "MYCALL AI7YN"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(HostCommands, "MYCALL AI7YN");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --hostcommands single command
		// Host commands not starting with LOGLEVEL or CONSOLELOG
		int testargc = 3;
		char *testargv[] = {"ardopcf", "--hostcommands", "MYCALL AI7YN"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(HostCommands, "MYCALL AI7YN");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -H with only LOGLEVEL command
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-H", "LOGLEVEL 1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(HostCommands, "");
		assert_int_equal(ardop_log_get_level_file(), 1);  // As set;
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -H with only LOGLEVEL command, value too low
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-H", "LOGLEVEL 0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		// Bad and Non-Priority host command not evaluated in processargs
		assert_int_equal(printindex, 5);
		assert_int_equal(ardop_log_get_level_file(), 2);  // still default
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -H with only LOGLEVEL command, value too high
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-H", "LOGLEVEL 7"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		// Bad and Non-Priority host command not evaluated in processargs
		assert_int_equal(printindex, 5);
		assert_int_equal(ardop_log_get_level_file(), 2);  // still default
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -H with only LOGLEVEL command, non-numerical argument
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-H", "LOGLEVEL X"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		// Bad and Non-Priority host command not evaluated in processargs
		assert_int_equal(printindex, 5);
		assert_int_equal(ardop_log_get_level_file(), 2);  // still default
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -H with only LOGLEVEL command, no argument
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-H", "LOGLEVEL"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		// Bad and Non-Priority host command not evaluated in processargs
		assert_int_equal(printindex, 5);
		assert_int_equal(ardop_log_get_level_file(), 2);  // still default
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -H with no space between LOGLEVEL command and value
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-H", "LOGLEVEL1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		// Bad and Non-Priority host command not evaluated in processargs
		assert_int_equal(printindex, 5);
		assert_int_equal(ardop_log_get_level_file(), 2);  // still default
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -H with only CONSOLELOG command
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-H", "CONSOLELOG 1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(HostCommands, "");
		assert_int_equal(ardop_log_get_level_console(), 1);  // As set;
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -H with only CONSOLELOG command, value too low
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-H", "CONSOLELOG 0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		// Bad and Non-Priority host command not evaluated in processargs
		assert_int_equal(printindex, 5);
		assert_int_equal(ardop_log_get_level_console(), 3);  // still default
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -H with only CONSOLELOG command, value too high
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-H", "CONSOLELOG 7"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		// Bad and Non-Priority host command not evaluated in processargs
		assert_int_equal(printindex, 5);
		assert_int_equal(ardop_log_get_level_console(), 3);  // still default
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -H with only CONSOLELOG command, non-numerical argument
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-H", "CONSOLELOG X"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		// Bad and Non-Priority host command not evaluated in processargs
		assert_int_equal(printindex, 5);
		assert_int_equal(ardop_log_get_level_console(), 3);  // still default
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -H with only CONSOLELOG command, no argument
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-H", "CONSOLELOG"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		// Bad and Non-Priority host command not evaluated in processargs
		assert_int_equal(printindex, 5);
		assert_int_equal(ardop_log_get_level_console(), 3);  // still default
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -H with no space between CONSOLELOG command and value
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-H", "CONSOLELOG1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		// Bad and Non-Priority host command not evaluated in processargs
		assert_int_equal(printindex, 5);
		assert_int_equal(ardop_log_get_level_console(), 3);  // still default
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -H with LOGLEVEL and CONSOLELOG command
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-H", "LOGLEVEL 1;CONSOLELOG 6"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(HostCommands, "");
		assert_int_equal(ardop_log_get_level_file(), 1);  // As set;
		assert_int_equal(ardop_log_get_level_console(), 6);  // As set;
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -H with multiple LOGLEVEL and CONSOLELOG commands.  Last set values.
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-H",
			"CONSOLELOG 5;LOGLEVEL 3;LOGLEVEL 1;CONSOLELOG 6"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(HostCommands, "");
		assert_int_equal(ardop_log_get_level_file(), 1);  // As set;
		assert_int_equal(ardop_log_get_level_console(), 6);  // As set;
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -H with LOGLEVEL followed by other command.
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-H", "CONSOLELOG 1;MYCALL AI7YN"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(HostCommands, "MYCALL AI7YN");
		assert_int_equal(ardop_log_get_level_console(), 1);  // As set;
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -H with non-log command followed by LOGLEVEL (should not be used).
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-H", "MYCALL AI7YN;CONSOLELOG 1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(HostCommands, "MYCALL AI7YN;CONSOLELOG 1");
		assert_int_equal(ardop_log_get_level_console(), 3);  // still default
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// Unknown option (ERROR)
		int testargc = 2;
		char *testargv[] = {"ardopcf", "-X"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + ERR + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
	}

	// In the following --ptt and --cat tests, the device/port names do not
	// need to be machine appropriate since this is not checked in
	// processargs() or the mocked functions it calls.

	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --ptt by itself w/o prefix (so default to PTTRTS).
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 3;
		char *testargv[] = {"ardopcf", "--ptt", "/dev/ttyUSB0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + ptt +info about no audio devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(PTTstr, "/dev/ttyUSB0");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 5);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, PTTRTS);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --ptt by itself w/o prefix (so default to PTTRTS).
		// test error in OpenCOMPort
		will_return(__wrap_OpenCOMPort, 0);  // error
		int testargc = 3;
		char *testargv[] = {"ardopcf", "--ptt", "/dev/ttyUSB0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// Unlike __wrap_OpenCOMPort(), OpenCOMPort() would also log an error
		// nstartstrings=3 + cmdstr + ptt +info about no audio devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -p by itself w/o prefix (so default to PTTRTS).
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-p", "/dev/ttyUSB0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + ptt +info about no audio devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(PTTstr, "/dev/ttyUSB0");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 5);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, PTTRTS);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -p by itself w/o prefix (so default to PTTRTS).
		// test error in OpenCOMPort
		will_return(__wrap_OpenCOMPort, 0);  // error
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-p", "/dev/ttyUSB0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// Unlike __wrap_OpenCOMPort(), OpenCOMPort() would also log an error
		// nstartstrings=3 + cmdstr + ptt +info about no audio devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -p by itself w/ RTS: prefix
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-p", "RTS:/dev/ttyUSB0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + ptt +info about no audio devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(PTTstr, "/dev/ttyUSB0");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 5);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, PTTRTS);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -p by itself w/ RTS: prefix
		// test error in OpenCOMPort
		will_return(__wrap_OpenCOMPort, 0);  // error
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-p", "RTS:/dev/ttyUSB0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + ptt +info about no audio devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -p by itself w/ DTR: prefix
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-p", "DTR:/dev/ttyUSB0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + ptt +info about no audio devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(PTTstr, "/dev/ttyUSB0");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 5);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, PTTDTR);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -p by itself w/ DTR: prefix
		// test error in OpenCOMPort
		will_return(__wrap_OpenCOMPort, 0);  // error
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-p", "DTR:/dev/ttyUSB0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + ptt +info about no audio devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -p twice, both with same port and DTR: prefix (2nd overrides first)
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 5;
		char *testargv[] = {"ardopcf", "-p", "DTR:/dev/ttyUSB0",  "-p",
			 "DTR:/dev/ttyUSB0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + ptt +info about no audio devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(PTTstr, "/dev/ttyUSB0");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 5);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, PTTDTR);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -p twice, with different ports, but both with DTR: prefix
		// second overrides first
		will_return(__wrap_OpenCOMPort, 5);
		will_return(__wrap_OpenCOMPort, 6);
		int testargc = 5;
		char *testargv[] = {"ardopcf", "-p", "DTR:/dev/ttyUSB0",  "-p",
			 "DTR:/dev/ttyUSB1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + ptt + ptt disable + ptt +info about no
		// audio devices specified.
		assert_int_equal(printindex, 7);
		assert_string_equal(PTTstr, "/dev/ttyUSB1");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 6);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, PTTDTR);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// two of -p with the same port, one with DTR prefix
		// second overrides first
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 5;
		char *testargv[] = {"ardopcf", "-p", "/dev/ttyUSB0",  "-p",
			 "DTR:/dev/ttyUSB0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + ptt + mode change + info about no audio
		// devices specified.
		assert_int_equal(printindex, 6);
		assert_string_equal(PTTstr, "/dev/ttyUSB0");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 5);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, PTTDTR);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -p with CM108: prefix
		will_return(__wrap_OpenCM108, 5);
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-p", "CM108:/dev/rawhid0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + ptt +info about no audio devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(PTTstr, "CM108:/dev/rawhid0");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 5);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, PTTCM108);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -p with CM108: prefix
		// test error in OpenCM108
		will_return(__wrap_OpenCM108, 0);  // error
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-p", "CM108:/dev/rawhid0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + ptt +info about no audio devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --cat with the pseudo-device RIGCTLD.  This is equivalent to
		// -c TCP:4532 -k 5420310A -u 5420300A
		will_return(__wrap_tcpconnect, 5);  // fd for success, -1 for error
		int testargc = 3;
		char *testargv[] = {"ardopcf", "--cat", "RIGCTLD"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*TCPCAT + 3*CATPTT +info about no audio
		// devices specified.
		assert_int_equal(printindex, 9);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "TCP:4532");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 5);
		assert_int_equal(PTTmode, PTTTCPCAT);
		assert_int_equal(ptt_on_cmd_len, 4);
		assert_int_equal(ptt_off_cmd_len, 4);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --cat with the pseudo-device RIGCTLD.  This is equivalent to
		// -c TCP:4532 -k 5420310A -u 5420300A.
		// Test failure due to failure in tcpconnect()
		will_return(__wrap_tcpconnect, -1);  // fd for success, -1 for error
		int testargc = 3;
		char *testargv[] = {"ardopcf", "--cat", "RIGCTLD"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*TCPCAT ERR + 2*info about no audio/ptt
		//  devices specified.
		assert_int_equal(printindex, 7);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c with the pseudo-device RIGCTLD.  This is equivalent to
		// -c TCP:4532 -k 5420310A -u 5420300A
		will_return(__wrap_tcpconnect, 5);  // fd for success, -1 for error
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-c", "RIGCTLD"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*TCPCAT + 3*CATPTT +info about no audio
		// devices specified.
		assert_int_equal(printindex, 9);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "TCP:4532");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 5);
		assert_int_equal(PTTmode, PTTTCPCAT);
		assert_int_equal(ptt_on_cmd_len, 4);
		assert_int_equal(ptt_off_cmd_len, 4);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c with the pseudo-device RIGCTLD.  This is equivalent to
		// -c TCP:4532 -k 5420310A -u 5420300A.
		// Test failure due to failure in tcpconnect()
		will_return(__wrap_tcpconnect, -1);  // fd for success, -1 for error
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-c", "RIGCTLD"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*TCPCAT ERR + 2*info about no audio/ptt
		//  devices specified.
		assert_int_equal(printindex, 7);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --cat w/o -k, or -u.
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 3;
		char *testargv[] = {"ardopcf", "--cat", "/dev/ttyUSB0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + CAT + 2*info about no audio/ptt
		//  devices specified.
		assert_int_equal(printindex, 6);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "/dev/ttyUSB0");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 5);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --cat w/o -k, or -u.
		// test failure in OpenCOMPort
		will_return(__wrap_OpenCOMPort, 0);  // error
		int testargc = 3;
		char *testargv[] = {"ardopcf", "--cat", "/dev/ttyUSB0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c w/o -k, or -u.
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-c", "/dev/ttyUSB0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + CAT + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "/dev/ttyUSB0");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 5);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c w/o -k, or -u.
		// test failure in OpenCOMPort
		will_return(__wrap_OpenCOMPort, 0);  // error
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-c", "/dev/ttyUSB0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --cat TCP w/o -k, or -u.
		will_return(__wrap_tcpconnect, 5);  // fd for success, -1 for error
		int testargc = 3;
		char *testargv[] = {"ardopcf", "--cat", "TCP:1234"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*TCPCAT + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 7);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "TCP:1234");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 5);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --cat TCP w/o -k, or -u.
		// Test failure due to failure in tcpconnect()
		will_return(__wrap_tcpconnect, -1);  // fd for success, -1 for error
		int testargc = 3;
		char *testargv[] = {"ardopcf", "--cat", "TCP:1234"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*TCPCAT ERR + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 7);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c TCP w/o -k, or -u.
		will_return(__wrap_tcpconnect, 5);  // fd for success, -1 for error
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-c", "TCP:1234"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*TCPCAT ERR + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 7);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "TCP:1234");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 5);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c TCP w/o -k, or -u.
		// Test failure due to failure in tcpconnect()
		will_return(__wrap_tcpconnect, -1);  // fd for success, -1 for error
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-c", "TCP:1234"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*TCPCAT ERR + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 7);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c and -c TCP w/o -k, or -u.
		// Test failure of -c TCP to failure in tcpconnect()
		will_return(__wrap_OpenCOMPort, 5);
		will_return(__wrap_tcpconnect, -1);  // fd for success, -1 for error
		int testargc = 5;
		char *testargv[] = {"ardopcf", "-c", "/dev/ttyUSB0", "-c", "TCP:1234"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + CAT + 2*TCPCAT ERR + 2*info about no
		// audio/ptt devices specified.
		assert_int_equal(printindex, 8);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c and -c TCP w/o -k, or -u.  TCP overrides first -c
		will_return(__wrap_OpenCOMPort, 5);
		will_return(__wrap_tcpconnect, 6);  // fd for success, -1 for error
		int testargc = 5;
		char *testargv[] = {"ardopcf", "-c", "/dev/ttyUSB0", "-c", "TCP:1234"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + CAT + 3*PTTCAT + info about no
		// audio devices specified.
		assert_int_equal(printindex, 8);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "TCP:1234");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 6);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 0);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c and --keystring and --unkeystring
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 7;
		char *testargv[] = {
			"ardopcf", "-c", "/dev/ttyUSB0", "--keystring", "54583B",
			"--unkeystring", "523B"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + CAT + 3*PTTCAT + info about no
		// audio devices specified.
		assert_int_equal(printindex, 8);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "/dev/ttyUSB0");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 5);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, PTTCAT);
		assert_int_equal(ptt_on_cmd_len, 3);
		assert_int_equal(ptt_off_cmd_len, 2);
		char pttonhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		char pttoffhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		bytes2hex(pttonhex, sizeof(pttonhex), ptt_on_cmd, ptt_on_cmd_len, false);
		bytes2hex(pttoffhex, sizeof(pttoffhex), ptt_off_cmd, ptt_off_cmd_len, false);
		assert_string_equal(pttonhex, "54583B");
		assert_string_equal(pttoffhex, "523B");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c TCP and --keystring and --unkeystring
		will_return(__wrap_tcpconnect, 5);  // fd for success, -1 for error
		int testargc = 7;
		char *testargv[] = {
			"ardopcf", "-c", "TCP:1234", "--keystring", "54583B",
			"--unkeystring", "523B"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*TCPCAT + 3*PTTCAT + info about no
		// audio devices specified.
		assert_int_equal(printindex, 9);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "TCP:1234");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 5);
		assert_int_equal(PTTmode, PTTTCPCAT);
		assert_int_equal(ptt_on_cmd_len, 3);
		assert_int_equal(ptt_off_cmd_len, 2);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c and -k and -u
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 7;
		char *testargv[] = {
			"ardopcf", "-c", "/dev/ttyUSB0", "-k", "54583B", "-u", "523B"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + CAT + 3*PTTCAT + info about no
		// audio devices specified.
		assert_int_equal(printindex, 8);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "/dev/ttyUSB0");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 5);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, PTTCAT);
		assert_int_equal(ptt_on_cmd_len, 3);
		assert_int_equal(ptt_off_cmd_len, 2);
		char pttonhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		char pttoffhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		bytes2hex(pttonhex, sizeof(pttonhex), ptt_on_cmd, ptt_on_cmd_len, false);
		bytes2hex(pttoffhex, sizeof(pttoffhex), ptt_off_cmd, ptt_off_cmd_len, false);
		assert_string_equal(pttonhex, "54583B");
		assert_string_equal(pttoffhex, "523B");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c TCP and --k and --u
		will_return(__wrap_tcpconnect, 5);  // fd for success, -1 for error
		int testargc = 7;
		char *testargv[] = {
			"ardopcf", "-c", "TCP:1234", "-k", "54583B", "--u", "523B"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*TCPCAT + 3*PTTCAT + info about no
		// audio devices specified.
		assert_int_equal(printindex, 9);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "TCP:1234");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 5);
		assert_int_equal(PTTmode, PTTTCPCAT);
		assert_int_equal(ptt_on_cmd_len, 3);
		assert_int_equal(ptt_off_cmd_len, 2);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c and -c TCP and --k and --u.  -c TCP overrides first -c
		will_return(__wrap_OpenCOMPort, 5);
		will_return(__wrap_tcpconnect, 6);  // fd for success, -1 for error
		int testargc = 9;
		char *testargv[] = {
			"ardopcf", "-c", "/dev/ttyUSB0", "-c", "TCP:1234", "-k", "54583B", "--u", "523B"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + CAT + 2*TCPCAT + 3*PTTCAT + info about no
		// audio devices specified.
		assert_int_equal(printindex, 10);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "TCP:1234");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 6);
		assert_int_equal(PTTmode, PTTTCPCAT);
		assert_int_equal(ptt_on_cmd_len, 3);
		assert_int_equal(ptt_off_cmd_len, 2);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -p and -c (different port) and -k and -u.
		will_return(__wrap_OpenCOMPort, 5);
		will_return(__wrap_OpenCOMPort, 6);
		int testargc = 9;
		char *testargv[] = {
			"ardopcf", "-p", "/dev/ttyUSB1", "-c", "/dev/ttyUSB0", "-k",
			"54583B", "-u", "523B"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + PTT + CAT + 3*PTTCAT + info about no
		// audio devices specified.
		assert_int_equal(printindex, 9);
		assert_string_equal(PTTstr, "/dev/ttyUSB1");
		assert_string_equal(CATstr, "/dev/ttyUSB0");
		assert_int_equal(hPTTdevice, 5);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 6);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, PTTRTS | PTTCAT);
		assert_int_equal(ptt_on_cmd_len, 3);
		assert_int_equal(ptt_off_cmd_len, 2);
		char pttonhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		char pttoffhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		bytes2hex(pttonhex, sizeof(pttonhex), ptt_on_cmd, ptt_on_cmd_len, false);
		bytes2hex(pttoffhex, sizeof(pttoffhex), ptt_off_cmd, ptt_off_cmd_len, false);
		assert_string_equal(pttonhex, "54583B");
		assert_string_equal(pttoffhex, "523B");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -p and -c TCP and -k and -u.
		will_return(__wrap_OpenCOMPort, 5);
		will_return(__wrap_tcpconnect, 6);  // fd for success, -1 for error
		int testargc = 9;
		char *testargv[] = {
			"ardopcf", "-p", "/dev/ttyUSB1", "-c", "TCP:1234", "-k",
			"54583B", "-u", "523B"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + PTT + 2*TCPCAT + 3*PTTCAT + info about no
		// audio devices specified.
		assert_int_equal(printindex, 10);
		assert_string_equal(PTTstr, "/dev/ttyUSB1");
		assert_string_equal(CATstr, "TCP:1234");
		assert_int_equal(hPTTdevice, 5);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 6);
		assert_int_equal(PTTmode, PTTRTS | PTTTCPCAT);
		assert_int_equal(ptt_on_cmd_len, 3);
		assert_int_equal(ptt_off_cmd_len, 2);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -p and -c (same port) and -k and -u.
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 9;
		char *testargv[] = {
			"ardopcf", "-p", "/dev/ttyUSB0", "-c", "/dev/ttyUSB0", "-k",
			"54583B", "-u", "523B"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + PTT + CAT + 3*PTTCAT + info about no
		// audio devices specified.
		assert_int_equal(printindex, 9);
		assert_string_equal(PTTstr, "/dev/ttyUSB0");
		assert_string_equal(CATstr, "/dev/ttyUSB0");
		assert_int_equal(hPTTdevice, 5);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 5);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, PTTRTS | PTTCAT);
		assert_int_equal(ptt_on_cmd_len, 3);
		assert_int_equal(ptt_off_cmd_len, 2);
		char pttonhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		char pttoffhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		bytes2hex(pttonhex, sizeof(pttonhex), ptt_on_cmd, ptt_on_cmd_len, false);
		bytes2hex(pttoffhex, sizeof(pttoffhex), ptt_off_cmd, ptt_off_cmd_len, false);
		assert_string_equal(pttonhex, "54583B");
		assert_string_equal(pttoffhex, "523B");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c and -k (too long) and -u
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 7;
		char *testargv[] = {
			"ardopcf", "-c", "/dev/ttyUSB0", "-k",
			"000102030405060708091011121314151617181920212223242526272829"
			"303132333435363738394041424344454647484950515253545556676869"
			"6061626364", "-u", "523B"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + CAT + hexerr + hex + 2*info about no
		// audio/ptt devices specified.
		assert_int_equal(printindex, 8);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "/dev/ttyUSB0");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 5);
		assert_int_equal(tcpCATport, 0);;
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);  // failed -k before -u
		assert_int_equal(ptt_off_cmd_len, 2);
		char pttonhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		char pttoffhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		bytes2hex(pttonhex, sizeof(pttonhex), ptt_on_cmd, ptt_on_cmd_len, false);
		bytes2hex(pttoffhex, sizeof(pttoffhex), ptt_off_cmd, ptt_off_cmd_len, false);
		assert_string_equal(pttonhex, "");
		assert_string_equal(pttoffhex, "523B");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c TCP and -k (too long) and -u
		will_return(__wrap_tcpconnect, 5);  // fd for success, -1 for error
		int testargc = 7;
		char *testargv[] = {
			"ardopcf", "-c", "TCP:1234", "-k",
			"000102030405060708091011121314151617181920212223242526272829"
			"303132333435363738394041424344454647484950515253545556676869"
			"6061626364", "-u", "523B"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*TCPCAT + hexerr + hex + 2*info about no
		// audio/ptt devices specified.
		assert_int_equal(printindex, 9);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "TCP:1234");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 5);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 2);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c and -u and -k (too long)
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 7;
		char *testargv[] = {
			"ardopcf", "-c", "/dev/ttyUSB0", "-u", "523B", "-k",
			"000102030405060708091011121314151617181920212223242526272829"
			"303132333435363738394041424344454647484950515253545556676869"
			"6061626364"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + CAT + hexerr + hex + 2*info about no
		// audio/ptt devices specified.
		assert_int_equal(printindex, 8);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "/dev/ttyUSB0");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 5);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);  // -u before failed -k
		assert_int_equal(ptt_off_cmd_len, 2);
		char pttonhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		char pttoffhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		bytes2hex(pttonhex, sizeof(pttonhex), ptt_on_cmd, ptt_on_cmd_len, false);
		bytes2hex(pttoffhex, sizeof(pttoffhex), ptt_off_cmd, ptt_off_cmd_len, false);
		assert_string_equal(pttonhex, "");
		assert_string_equal(pttoffhex, "523B");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c and -k and -u (too long) without -p
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 7;
		char *testargv[] = {
			"ardopcf", "-c", "/dev/ttyUSB0", "-k", "54583B", "-u",
			"000102030405060708091011121314151617181920212223242526272829"
			"303132333435363738394041424344454647484950515253545556676869"
			"6061626364"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + CAT + hexerr + hex + 2*info about no
		// audio/ptt devices specified.
		assert_int_equal(printindex, 8);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "/dev/ttyUSB0");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 5);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 3);  // -k before failed -u
		assert_int_equal(ptt_off_cmd_len, 0);
		char pttonhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		char pttoffhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		bytes2hex(pttonhex, sizeof(pttonhex), ptt_on_cmd, ptt_on_cmd_len, false);
		bytes2hex(pttoffhex, sizeof(pttoffhex), ptt_off_cmd, ptt_off_cmd_len, false);
		assert_string_equal(pttonhex, "54583B");
		assert_string_equal(pttoffhex, "");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c and -k (odd length) and -u
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 7;
		char *testargv[] = {
			"ardopcf", "-c", "/dev/ttyUSB0", "-k", "54583", "-u", "523B"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + CAT + hexerr + hex + 2*info about no
		// audio/ptt devices specified.
		assert_int_equal(printindex, 8);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "/dev/ttyUSB0");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 5);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);  // failed -k before -u
		assert_int_equal(ptt_off_cmd_len, 2);
		char pttonhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		char pttoffhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		bytes2hex(pttonhex, sizeof(pttonhex), ptt_on_cmd, ptt_on_cmd_len, false);
		bytes2hex(pttoffhex, sizeof(pttoffhex), ptt_off_cmd, ptt_off_cmd_len, false);
		assert_string_equal(pttonhex, "");
		assert_string_equal(pttoffhex, "523B");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c and -k and -u (odd length) without -p
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 7;
		char *testargv[] = {
			"ardopcf", "-c", "/dev/ttyUSB0", "-k", "54583B", "-u", "523"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + CAT + hexerr + hex + 2*info about no
		// audio/ptt devices specified.
		assert_int_equal(printindex, 8);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "/dev/ttyUSB0");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 5);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 3);  // -k before failed -u
		assert_int_equal(ptt_off_cmd_len, 0);
		char pttonhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		char pttoffhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		bytes2hex(pttonhex, sizeof(pttonhex), ptt_on_cmd, ptt_on_cmd_len, false);
		bytes2hex(pttoffhex, sizeof(pttoffhex), ptt_off_cmd, ptt_off_cmd_len, false);
		assert_string_equal(pttonhex, "54583B");
		assert_string_equal(pttoffhex, "");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c and -k (non-hex) and -u without -p
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 7;
		char *testargv[] = {
			"ardopcf", "-c", "/dev/ttyUSB0", "-k", "54583X", "-u", "523B"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + CAT + hexerr + hex + 2*info about no
		// audio/ptt devices specified.
		assert_int_equal(printindex, 8);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "/dev/ttyUSB0");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 5);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);  // failed -k before -u
		assert_int_equal(ptt_off_cmd_len, 2);
		char pttonhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		char pttoffhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		bytes2hex(pttonhex, sizeof(pttonhex), ptt_on_cmd, ptt_on_cmd_len, false);
		bytes2hex(pttoffhex, sizeof(pttoffhex), ptt_off_cmd, ptt_off_cmd_len, false);
		assert_string_equal(pttonhex, "");
		assert_string_equal(pttoffhex, "523B");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c and -k and -u (non-hex) without -p
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 7;
		char *testargv[] = {
			"ardopcf", "-c", "/dev/ttyUSB0", "-k", "54583B", "-u", "523X"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + CAT + hexerr + hex + 2*info about no
		// audio/ptt devices specified.
		assert_int_equal(printindex, 8);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "/dev/ttyUSB0");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 5);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 3);  // -k before failed -u
		assert_int_equal(ptt_off_cmd_len, 0);
		char pttonhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		char pttoffhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		bytes2hex(pttonhex, sizeof(pttonhex), ptt_on_cmd, ptt_on_cmd_len, false);
		bytes2hex(pttoffhex, sizeof(pttoffhex), ptt_off_cmd, ptt_off_cmd_len, false);
		assert_string_equal(pttonhex, "54583B");
		assert_string_equal(pttoffhex, "");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -k without -c or -u
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-k", "54583B"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + hex + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 3);  // -k
		assert_int_equal(ptt_off_cmd_len, 0);
		char pttonhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		char pttoffhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		bytes2hex(pttonhex, sizeof(pttonhex), ptt_on_cmd, ptt_on_cmd_len, false);
		bytes2hex(pttoffhex, sizeof(pttoffhex), ptt_off_cmd, ptt_off_cmd_len, false);
		assert_string_equal(pttonhex, "54583B");
		assert_string_equal(pttoffhex, "");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c and -k without -u
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 5;
		char *testargv[] = {"ardopcf", "-c", "/dev/ttyUSB0", "-k", "54583B"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + CAT + hex + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 7);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "/dev/ttyUSB0");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 5);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 3);  // -k
		assert_int_equal(ptt_off_cmd_len, 0);
		char pttonhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		char pttoffhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		bytes2hex(pttonhex, sizeof(pttonhex), ptt_on_cmd, ptt_on_cmd_len, false);
		bytes2hex(pttoffhex, sizeof(pttoffhex), ptt_off_cmd, ptt_off_cmd_len, false);
		assert_string_equal(pttonhex, "54583B");
		assert_string_equal(pttoffhex, "");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -u without -c or -k
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-u", "523B"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + hex + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 2);  // -u
		char pttonhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		char pttoffhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		bytes2hex(pttonhex, sizeof(pttonhex), ptt_on_cmd, ptt_on_cmd_len, false);
		bytes2hex(pttoffhex, sizeof(pttoffhex), ptt_off_cmd, ptt_off_cmd_len, false);
		assert_string_equal(pttonhex, "");
		assert_string_equal(pttoffhex, "523B");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -c and -u without -k
		will_return(__wrap_OpenCOMPort, 5);
		int testargc = 5;
		char *testargv[] = {"ardopcf", "-c", "/dev/ttyUSB0", "-u", "523B"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + CAT + hex + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 7);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "/dev/ttyUSB0");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 5);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 0);
		assert_int_equal(ptt_off_cmd_len, 2);  // -u
		char pttonhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		char pttoffhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		bytes2hex(pttonhex, sizeof(pttonhex), ptt_on_cmd, ptt_on_cmd_len, false);
		bytes2hex(pttoffhex, sizeof(pttoffhex), ptt_off_cmd, ptt_off_cmd_len, false);
		assert_string_equal(pttonhex, "");
		assert_string_equal(pttoffhex, "523B");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -k and -u without -c
		int testargc = 5;
		char *testargv[] = {"ardopcf", "-k", "54583B", "-u", "523B"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr +2* hex + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 7);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_int_equal(PTTmode, 0x00);
		assert_int_equal(ptt_on_cmd_len, 3);  // -k
		assert_int_equal(ptt_off_cmd_len, 2);  // -u
		char pttonhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		char pttoffhex[MAXCATLEN * 2 + 1] = "";  // Can produce MAXCATLEN bytes
		bytes2hex(pttonhex, sizeof(pttonhex), ptt_on_cmd, ptt_on_cmd_len, false);
		bytes2hex(pttoffhex, sizeof(pttoffhex), ptt_off_cmd, ptt_off_cmd_len, false);
		assert_string_equal(pttonhex, "54583B");
		assert_string_equal(pttoffhex, "523B");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -L (UseLeftRX)
		int testargc = 2;
		char *testargv[] = {"ardopcf", "-L"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_true(UseLeftRX);
		assert_false(UseRightRX);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -R (UseRightRX)
		int testargc = 2;
		char *testargv[] = {"ardopcf", "-R"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_false(UseLeftRX);
		assert_true(UseRightRX);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -L and -R (error)
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-L", "-R"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + ERR + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_true(UseLeftRX);
		assert_true(UseRightRX);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -R and -L (error)
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-R", "-L"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + ERR + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_true(UseLeftRX);
		assert_true(UseRightRX);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -y (UseLeftTX)
		int testargc = 2;
		char *testargv[] = {"ardopcf", "-y"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_true(UseLeftTX);
		assert_false(UseRightTX);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -z (UseRightTX)
		int testargc = 2;
		char *testargv[] = {"ardopcf", "-z"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_false(UseLeftTX);
		assert_true(UseRightTX);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -y and -z (error)
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-y", "-z"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + ERR + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_true(UseLeftTX);
		assert_true(UseRightTX);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -z and -y (error)
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-z", "-y"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + ERR + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_true(UseLeftTX);
		assert_true(UseRightTX);
	}

#ifdef __ARM_ARCH
	// -p GPIO:n
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-p", "GPIO:18"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + GPIO + info about no audio devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(PTTstr, "GPIO:18");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_false(GPIOinvert);
		assert_int_equal(PTTmode, PTTGPIO);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -p GPIOL-18 (-ve value)
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-p", "GPIO:-18"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + GPIO + info about no audio devices specified.
		assert_int_equal(printindex, 5);
		assert_string_equal(PTTstr, "GPIO:-18");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_true(GPIOinvert);  // due to -ve value
		assert_int_equal(PTTmode, PTTGPIO);
	}
#else
	// On non-ARM systems, -g is an invalid option
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -p GPIO:18 is an unknown PTT device
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-p", "GPIO:18"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + err + 2x info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_string_equal(PTTstr, "");
		assert_string_equal(CATstr, "");
		assert_int_equal(hPTTdevice, 0);
		assert_int_equal(hCM108device, 0);
		assert_int_equal(hCATdevice, 0);
		assert_int_equal(tcpCATport, 0);
		assert_false(GPIOinvert);
		assert_int_equal(PTTmode, 0x00);
	}
#endif
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -G with +ve value
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-G", "8514"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*info about no audio devices/ptt specified.
		assert_int_equal(printindex, 5);
		assert_int_equal(wg_port, 8514);  // value set
		assert_false(WG_DevMode);  // default
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -G with -ve value
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-G", "-8514"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*info about no audio devices/ptt specified.
		assert_int_equal(printindex, 5);
		assert_int_equal(wg_port, 8514);  // value set
		assert_true(WG_DevMode);  // set by -ve argument to -G
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -G 0 (error)
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-G", "0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + No WebGUI + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_int_equal(wg_port, 0);  // value set
		assert_false(WG_DevMode);  // default
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -G -0 (error)
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-G", "-0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + No Webgui + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_int_equal(wg_port, 0);  // value set
		assert_false(WG_DevMode);  // default
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -G matches default host_port (8515) (error)
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-G", "8515"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + bad wg_port + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_int_equal(wg_port, 8514);  // value set
		assert_false(WG_DevMode);  // default
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -G matches negative of default host_port (8515) (error)
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-G", "-8515"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + bad wg_port + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_int_equal(wg_port, 8514);  // value set
		assert_true(WG_DevMode);  // set by -ve argument
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -G matches default host_port + 1 (8516) (error)
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-G", "8516"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + bad wg_port + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_int_equal(wg_port, 8514);  // value set
		assert_false(WG_DevMode);  // default
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -G matches negative of default host_port + 1 (8516) (error)
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-G", "-8516"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + bad wg_port + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_int_equal(wg_port, 8514);  // value set
		assert_true(WG_DevMode);  // set by -ve argument
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -G matches specified host_port (error)
		int testargc = 4;
		char *testargv[] = {"ardopcf", "-G", "1001", "1001"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + bad wg_port + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_int_equal(wg_port, 1000);  // value set
		assert_false(WG_DevMode);  // default
		assert_int_equal(host_port, 1001);  // value set
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -G matches negative of specified host_port (error)
		int testargc = 4;
		char *testargv[] = {"ardopcf", "-G", "-1001", "1001"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + bad wg_port + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_int_equal(wg_port, 1000);  // value set
		assert_true(WG_DevMode);  // set by -ve argument
		assert_int_equal(host_port, 1001);  // value set

	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -G matches specified host_port + 1 (error)
		int testargc = 4;
		char *testargv[] = {"ardopcf", "-G", "1002", "1001"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + bad wg_port + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_int_equal(wg_port, 1000);  // value set
		assert_false(WG_DevMode);  // default
		assert_int_equal(host_port, 1001);  // value set
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -G matches negative of specified host_port + 1 (error)
		int testargc = 4;
		char *testargv[] = {"ardopcf", "-G", "-1002", "1001"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + bad wg_port + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_int_equal(wg_port, 1000);  // value set
		assert_true(WG_DevMode);  // set by -ve argument
		assert_int_equal(host_port, 1001);  // value set
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --writewav
		int testargc = 2;
		char *testargv[] = {"ardopcf", "--writewav"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_true(WriteRxWav);  // set
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -w
		int testargc = 2;
		char *testargv[] = {"ardopcf", "-w"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_true(WriteRxWav);  // set
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --writewav
		int testargc = 2;
		char *testargv[] = {"ardopcf", "--writetxwav"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_true(WriteTxWav);  // set
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --writewav
		int testargc = 2;
		char *testargv[] = {"ardopcf", "-T"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_true(WriteTxWav);  // set
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// single --decodewav
		int testargc = 3;
		char *testargv[] = {"ardopcf", "--decodewav", "wavpath1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr
		assert_int_equal(printindex, 3);
		assert_string_equal(DecodeWav[0], "wavpath1");
		assert_int_equal(DecodeWav[1][0], 0x00);  // next DecodeWav not set
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// single -d
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-d", "wavpath1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr
		assert_int_equal(printindex, 3);
		assert_string_equal(DecodeWav[0], "wavpath1");
		assert_int_equal(DecodeWav[1][0], 0x00);  // next DecodeWav not set
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// five -d (maximum supported)
		int testargc = 11;
		char *testargv[] = {"ardopcf", "-d", "wavpath1", "-d", "wavpath2",
			"-d", "wavpath3", "-d", "wavpath4", "-d", "wavpath5"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr
		assert_int_equal(printindex, 3);
		assert_string_equal(DecodeWav[0], "wavpath1");
		assert_string_equal(DecodeWav[1], "wavpath2");
		assert_string_equal(DecodeWav[2], "wavpath3");
		assert_string_equal(DecodeWav[3], "wavpath4");
		assert_string_equal(DecodeWav[4], "wavpath5");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// six -d (error)
		int testargc = 13;
		char *testargv[] = {"ardopcf", "-d", "wavpath1", "-d", "wavpath2",
			"-d", "wavpath3", "-d", "wavpath4", "-d", "wavpath5",
			"-d", "wavpath6"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + too many -d err
		assert_int_equal(printindex, 4);
		assert_string_equal(DecodeWav[0], "wavpath1");
		assert_string_equal(DecodeWav[1], "wavpath2");
		assert_string_equal(DecodeWav[2], "wavpath3");
		assert_string_equal(DecodeWav[3], "wavpath4");
		assert_string_equal(DecodeWav[4], "wavpath5");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// --sdft
		int testargc = 2;
		char *testargv[] = {"ardopcf", "--sdft"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_true(UseSDFT);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -s
		int testargc = 2;
		char *testargv[] = {"ardopcf", "-s"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_true(UseSDFT);
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// host_port only
		int testargc = 2;
		char *testargv[] = {"ardopcf", "9515"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 5);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "");  // default value
		assert_string_equal(PlaybackDevice, "");  // default value
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// host_port and positional capture device
		int testargc = 3;
		char *testargv[] = {"ardopcf", "9515", "plughw:1,0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + mono + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "plughw:1,0");
		assert_string_equal(PlaybackDevice, "");  // default value
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// host_port and positional capture device set to NOSOUND
		int testargc = 3;
		char *testargv[] = {"ardopcf", "9515", "NOSOUND"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + NOSOUND + mono + 2*info about no
		// audio/ptt devices specified.
		assert_int_equal(printindex, 7);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "NOSOUND");
		assert_string_equal(PlaybackDevice, "");  // default value
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// host_port and positional capture device set to -1.  This produces an
		// error because while -1 may be used as an alias for NOSOUND with the
		// -i option, it is not recognized as a postiional parameter.  In
		// addition to the "Unknown option" error message, this also logs a
		// message indicating that if this was intended as a positional
		// paramter to specify "NOSOUND", that this is not supported.
		int testargc = 3;
		char *testargv[] = {"ardopcf", "9515", "-1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2xERR + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 7);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "");
		assert_string_equal(PlaybackDevice, "");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// host_port, and positional capture device and playback device
		int testargc = 4;
		char *testargv[] = {"ardopcf", "9515", "plughw:1,0", "plughw:2,0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*mono + info about no ptt device specified.
		assert_int_equal(printindex, 6);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "plughw:1,0");
		assert_string_equal(PlaybackDevice, "plughw:2,0");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// host_port, and positional capture device and playback device both set
		// to NOSOUND
		int testargc = 4;
		char *testargv[] = {"ardopcf", "9515", "NOSOUND", "NOSOUND"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*NOSOUND + 2*mono + info about no ptt
		// device specified.
		assert_int_equal(printindex, 8);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "NOSOUND");
		assert_string_equal(PlaybackDevice, "NOSOUND");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// host_port, and positional capture device and playback device both set
		// to -1 for NOSOUND.  This produces an
		// error because while -1 may be used as an alias for NOSOUND with the
		// -i option, it is not recognized as a postiional parameter.  In
		// addition to the "Unknown option" error message, this also logs a
		// message indicating that if this was intended as a positional
		// paramter to specify "NOSOUND", that this is not supported.
		int testargc = 4;
		char *testargv[] = {"ardopcf", "9515", "-1", "-1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 4*ERR + 2*info about no audio/ptt
		// device specified.
		assert_int_equal(printindex, 9);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "");
		assert_string_equal(PlaybackDevice, "");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -i (capture device) only
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-i", "plughw:1,0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + mono + 2*info about no audio/ptt devices specified.
		assert_int_equal(printindex, 6);
		assert_int_equal(host_port, 8515);
		assert_string_equal(CaptureDevice, "plughw:1,0");
		assert_string_equal(PlaybackDevice, "");  // default value
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -i (capture device) only set to NOSOUND
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-i", "-1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + NOSOUND + mono + 2*info about no audio/ptt
		// devices specified.
		assert_int_equal(printindex, 7);
		assert_int_equal(host_port, 8515);
		assert_string_equal(CaptureDevice, "NOSOUND");
		assert_string_equal(PlaybackDevice, "");  // default value
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -i (capture device) only set to -1 for NOSOUND
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-i", "-1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + NOSOUND + mono + 2*info about no audio/ptt
		// devices specified.
		assert_int_equal(printindex, 7);
		assert_int_equal(host_port, 8515);
		assert_string_equal(CaptureDevice, "NOSOUND");
		assert_string_equal(PlaybackDevice, "");  // default value
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -i (capture device) and postional host_port
		int testargc = 4;
		char *testargv[] = {"ardopcf", "-i", "plughw:1,0", "9515"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + mono + 2*info about no audio/ptt
		// devices specified.
		assert_int_equal(printindex, 6);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "plughw:1,0");
		assert_string_equal(PlaybackDevice, "");  // default value
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -i (capture device) and postional host_port and CaptureDevice
		// (produces warning)
		int testargc = 5;
		char *testargv[] = {"ardopcf", "-i", "plughw:1,0", "9515", "plughw:0,0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + mono + override + 2*info about no audio/ptt
		// devices specified.
		assert_int_equal(printindex, 7);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "plughw:0,0");
		assert_string_equal(PlaybackDevice, "");  // default value
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -i (capture device) and postional host_port and CaptureDevice set to
		// -1 for NOSOUND.  Produces error for invalid use of -1 as positional
		// parameter
		int testargc = 5;
		char *testargv[] = {"ardopcf", "-i", "plughw:1,0", "9515", "-1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*ERR + mono + 2*info about no audio/ptt
		// devices specified.
		assert_int_equal(printindex, 8);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "plughw:1,0");
		assert_string_equal(PlaybackDevice, "");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -o (playback device) only
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-o", "plughw:1,0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + mono + 2*info about no audio/ptt
		// devices specified.
		assert_int_equal(printindex, 6);
		assert_int_equal(host_port, 8515);
		assert_string_equal(CaptureDevice, "");  // default value
		assert_string_equal(PlaybackDevice, "plughw:1,0");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -o (playback device) only set to -1 for NOSOUND
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-o", "-1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + NOSOUND + mono + 2*info about no audio/ptt
		// devices specified.
		assert_int_equal(printindex, 7);
		assert_int_equal(host_port, 8515);
		assert_string_equal(CaptureDevice, "");  // default value
		assert_string_equal(PlaybackDevice, "NOSOUND");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -o (playback device) only set to NOSOUND
		int testargc = 3;
		char *testargv[] = {"ardopcf", "-o", "NOSOUND"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + NOSOUND + mono + 2*info about no audio/ptt
		// devices specified.
		assert_int_equal(printindex, 7);
		assert_int_equal(host_port, 8515);
		assert_string_equal(CaptureDevice, "");  // default value
		assert_string_equal(PlaybackDevice, "NOSOUND");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -o (playback device) and postional host_port
		int testargc = 4;
		char *testargv[] = {"ardopcf", "-o", "plughw:1,0", "9515"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + mono + 2*info about no audio/ptt
		// devices specified.
		assert_int_equal(printindex, 6);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "");  // default value
		assert_string_equal(PlaybackDevice, "plughw:1,0");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -o (playback device) and postional host_port, CaptureDevice, and
		// PlaybackDevice. (produces warning about PlaybackDevice)
		int testargc = 6;
		char *testargv[] = {"ardopcf", "-o", "plughw:0,0", "9515", "plughw:1,0",
			"plughw:2,0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*mono + override + info about no ptt
		// device specified.
		assert_int_equal(printindex, 7);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "plughw:1,0");
		assert_string_equal(PlaybackDevice, "plughw:2,0");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -o (playback device) and postional host_port, CaptureDevice, and
		// PlaybackDevice set to NOSOUND. (produces warning about
		// PlaybackDevice)
		int testargc = 6;
		char *testargv[] = {"ardopcf", "-o", "plughw:0,0", "9515", "plughw:1,0",
			"NOSOUND"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		// nstartstrings=3 + cmdstr + info NOSOUND, default audio device
		// nstartstrings=3 + cmdstr + NOSOUND + 2*mono + override + info about
		// no ptt device specified.
		assert_int_equal(printindex, 8);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "plughw:1,0");
		assert_string_equal(PlaybackDevice, "NOSOUND");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -o (playback device) and postional host_port, CaptureDevice, and
		// PlaybackDevice set to -1 for NOSOUND. Produces error for invalid use
		// of -1 as postiional parameter
		int testargc = 6;
		char *testargv[] = {"ardopcf", "-o", "plughw:0,0", "9515", "plughw:1,0",
			"-1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*ERR + 2*mono + info about
		// no ptt device specified.
		assert_int_equal(printindex, 8);
		assert_int_equal(host_port, 9515);
		// err occurs before postiional arguments are parsed
		assert_string_equal(CaptureDevice, "plughw:1,0");
		assert_string_equal(PlaybackDevice, "plughw:0,0");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -i (capture device) and postional host_port, CaptureDevice, and
		// PlaybackDevice. (produces warning about CaptureDevice)
		int testargc = 6;
		char *testargv[] = {"ardopcf", "-i", "plughw:0,0", "9515", "plughw:1,0",
			"plughw:2,0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*mono + override + info about
		// no ptt device specified.
		assert_int_equal(printindex, 7);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "plughw:1,0");
		assert_string_equal(PlaybackDevice, "plughw:2,0");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -i (capture device) and postional host_port, Invalid attempt to set
		// CaptureDevice set to -1 for NOSOUND as positional, but this is
		// actually interpreted as an unknown non-positional, such that the
		// final positional is interpreted as a CaptureDevice.
		int testargc = 6;
		char *testargv[] = {"ardopcf", "-i", "plughw:0,0", "9515", "-1",
			"plughw:2,0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*ERR + mono + override + 2*info about
		// no audio/ptt devices specified.
		assert_int_equal(printindex, 9);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "plughw:2,0");
		assert_string_equal(PlaybackDevice, "");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -o (playback device) and -i (capture device)
		int testargc = 5;
		char *testargv[] = {"ardopcf", "-o", "plughw:1,0", "-i", "plughw:0,0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*mono + info about no ptt device specified.
		assert_int_equal(printindex, 6);
		assert_int_equal(host_port, 8515);
		assert_string_equal(CaptureDevice, "plughw:0,0");
		assert_string_equal(PlaybackDevice, "plughw:1,0");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -o set to -1 for NOSOUND (playback device) and -i (capture device)
		int testargc = 5;
		char *testargv[] = {"ardopcf", "-o", "-1", "-i", "plughw:0,0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + NOSOUND + 2*mono + info about no ptt
		// device specified.
		assert_int_equal(printindex, 7);
		assert_int_equal(host_port, 8515);
		assert_string_equal(CaptureDevice, "plughw:0,0");
		assert_string_equal(PlaybackDevice, "NOSOUND");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -o (playback device) and -i set to -1 for NOSOUND (capture device)
		int testargc = 5;
		char *testargv[] = {"ardopcf", "-o", "plughw:1,0", "-i", "-1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + NOSOUND + 2*mono + info about no ptt
		// device specified.
		assert_int_equal(printindex, 7);
		assert_int_equal(host_port, 8515);
		assert_string_equal(CaptureDevice, "NOSOUND");
		assert_string_equal(PlaybackDevice, "plughw:1,0");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -o (playback device) and -i (capture device) both set to -1 for NOSOUND
		int testargc = 5;
		char *testargv[] = {"ardopcf", "-o", "-1", "-i", "-1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*NOSOUND + 2*mono + info about no ptt
		// device specified.
		assert_int_equal(printindex, 8);
		assert_int_equal(host_port, 8515);
		assert_string_equal(CaptureDevice, "NOSOUND");
		assert_string_equal(PlaybackDevice, "NOSOUND");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -o (playback device) and -i (capture device) and all positional
		// parameters. (produces 2 warnings)
		int testargc = 8;
		char *testargv[] = {"ardopcf", "-o", "plughw:0,0", "-i", "plughw:1,0",
			"9515", "plughw:2,0", "plughw:3,0"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*mono + 2*override + info about no ptt
		// device specified.
		assert_int_equal(printindex, 8);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "plughw:2,0");
		assert_string_equal(PlaybackDevice, "plughw:3,0");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -o (playback device) and -i (capture device) and all positional
		// parameters set to NOSOUND. (produces 2 warnings)
		int testargc = 8;
		char *testargv[] = {"ardopcf", "-o", "plughw:0,0", "-i", "plughw:1,0",
			"9515", "NOSOUND", "NOSOUND"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 2*NOSOUND + 2*mono + 2*override + info
		// about no ptt device specified.
		assert_int_equal(printindex, 10);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "NOSOUND");
		assert_string_equal(PlaybackDevice, "NOSOUND");
	}
	reset_defaults();  // reset global variables changed by processargs
	reset_printstrs();  // reset captured strings from last test
	if (true) {
		// -o (playback device) and -i (capture device) and all positional
		// parameters set to -1 for NOSOUND. error for invalid use of -1 as
		// positional parameter
		int testargc = 8;
		char *testargv[] = {"ardopcf", "-o", "plughw:0,0", "-i", "plughw:1,0",
			"9515", "-1", "-1"};
		ret = processargs(testargc, testargv);
		//print_printstrs();
		//__real_printf("%i: %s\n", printindex, printstrs[printindex]);
		// nstartstrings=3 + cmdstr + 4*ERR + 2*mono + info about no ptt
		// device specified.
		assert_int_equal(printindex, 10);
		assert_int_equal(host_port, 9515);
		assert_string_equal(CaptureDevice, "plughw:1,0");
		assert_string_equal(PlaybackDevice, "plughw:0,0");
	}
}

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_processargs),
	};

	ardop_test_setup();
	return cmocka_run_group_tests(tests, NULL, NULL);
}
