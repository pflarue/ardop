// macOS implementation of platform utilities.
// Keep changes macOS-specific and minimal. Provide parity with Linux where
// required by common code, while using native primitives. Any functionality
// not meaningful on macOS (GPIO) is implemented as safe no-ops that warn once.

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/select.h>
#include <mach/mach_time.h>

#include "common/os_util.h"
#include "common/log.h"
#include "common/ardopcommon.h"

// Globals used similarly on Linux.
struct timespec time_start; // reference used for getNow()
extern char DecodeWav[5][256];
extern int WavNow; // Time since start of WAV file (ms) when decoding
extern bool blnClosing;
extern int closedByPosixSignal;
extern char PlaybackDevice[80];

// --- Sleep ---------------------------------------------------------------
// Match Linux behavior: skip sleeping when NOSOUND to accelerate tests.
void Sleep(long unsigned int mS)
{
    if (strcmp(PlaybackDevice, "NOSOUND") == 0)
        return;
    struct timespec req, rem;
    req.tv_sec = mS / 1000;
    req.tv_nsec = (long)(mS % 1000) * 1000000L;
    while (nanosleep(&req, &rem) == -1 && errno == EINTR)
    {
        req = rem; // retry remaining time
    }
}

// --- UTC Time String ----------------------------------------------------
// Required format for existing common code is 15 chars: YYYYMMDD_HHMMSS.
// Use gmtime_r + strftime for thread safety & clarity; caller supplies >=16B.
void get_utctimestr(char *out)
{
    time_t T = time(NULL);
    struct tm tm_utc;
    gmtime_r(&T, &tm_utc);
    // strftime returns 0 on failure; fall back to empty string if that occurs.
    if (strftime(out, 16, "%Y%m%d_%H%M%S", &tm_utc) == 0)
        out[0] = '\0';
}

// --- Monotonic Time (ms) -----------------------------------------------
// Use clock_gettime(CLOCK_MONOTONIC) when available; fallback to
// mach_absolute_time() otherwise. Ensure WAV decode path mirrors Linux.
unsigned int getNow()
{
    if (DecodeWav[0][0])
        return WavNow;
#ifdef CLOCK_MONOTONIC
    struct timespec tp;
    if (clock_gettime(CLOCK_MONOTONIC, &tp) == 0)
    {
        return (unsigned int)((tp.tv_sec - time_start.tv_sec) * 1000u +
                              (tp.tv_nsec - time_start.tv_nsec) / 1000000u);
    }
#endif
    // Fallback: mach time (never expected unless older macOS SDK)
    static mach_timebase_info_data_t tb = {0, 0};
    if (tb.denom == 0)
        mach_timebase_info(&tb);
    uint64_t now = mach_absolute_time();
    // Convert to nanoseconds then to ms.
    uint64_t ns = now * tb.numer / tb.denom;
    // On first call, establish reference using time_start (monotonic-ish)
    static uint64_t start_ns = 0;
    if (!start_ns)
    {
        start_ns = ns;
    }
    return (unsigned int)((ns - start_ns) / 1000000u);
}

// --- Signals ------------------------------------------------------------
static void signal_handler_trigger_shutdown(int sig)
{
    blnClosing = true;
    closedByPosixSignal = sig;
}

int platform_init()
{
    // Initialize monotonic reference.
#ifdef CLOCK_MONOTONIC
    clock_gettime(CLOCK_MONOTONIC, &time_start);
#else
    // Fallback: approximate using REALTIME; only used if monotonic absent.
    clock_gettime(CLOCK_REALTIME, &time_start);
#endif

    // Install minimal signal handlers matching Linux intent so common
    // shutdown logic works uniformly. If any fail, continue (best effort).
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = signal_handler_trigger_shutdown;
    if (sigaction(SIGINT, &act, NULL) < 0)
        ZF_LOGE("sigaction SIGINT failed: %s", strerror(errno));
    if (sigaction(SIGTERM, &act, NULL) < 0)
        ZF_LOGE("sigaction SIGTERM failed: %s", strerror(errno));

    // Ignore SIGHUP & SIGPIPE similar to Linux implementation.
    act.sa_handler = SIG_IGN;
    if (sigaction(SIGHUP, &act, NULL) < 0)
        ZF_LOGE("sigaction SIGHUP failed: %s", strerror(errno));
    if (sigaction(SIGPIPE, &act, NULL) < 0)
        ZF_LOGE("sigaction SIGPIPE failed: %s", strerror(errno));

    ZF_LOGD("platform_init (macOS) complete");
    return 0;
}

// --- Signal Abbreviation ------------------------------------------------
const char *PlatformSignalAbbreviation(int sig)
{
    switch (sig)
    {
    case SIGHUP:
        return "SIGHUP";
    case SIGINT:
        return "SIGINT";
    case SIGQUIT:
        return "SIGQUIT";
    case SIGILL:
        return "SIGILL";
    case SIGABRT:
        return "SIGABRT";
    case SIGBUS:
        return "SIGBUS";
    case SIGFPE:
        return "SIGFPE";
    case SIGKILL:
        return "SIGKILL"; // cannot be caught
    case SIGSEGV:
        return "SIGSEGV";
    case SIGPIPE:
        return "SIGPIPE";
    case SIGALRM:
        return "SIGALRM";
    case SIGTERM:
        return "SIGTERM";
    case SIGUSR1:
        return "SIGUSR1";
    case SIGUSR2:
        return "SIGUSR2";
    default:
        return "Unknown";
    }
}

// --- TCP Helpers --------------------------------------------------------
// Return fd on success, -1 on failure. When testing==true suppress ERROR
// logging (match Linux semantics) and use a short timeout for connect.
int tcpconnect(char *address, int port, bool testing)
{
    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints, *res = NULL, *rp = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    int gai = getaddrinfo(address, portstr, &hints, &res);
    if (gai != 0)
    {
        if (!testing)
            ZF_LOGE("getaddrinfo failed for %s:%d: %s", address, port, gai_strerror(gai));
        return -1;
    }
    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next)
    {
        fd = (int)socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0)
            continue;
        // Set non-blocking for timed connect path.
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0)
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int ret = connect(fd, rp->ai_addr, (socklen_t)rp->ai_addrlen);
        if (ret == 0)
        {
            ZF_LOGD("tcpconnect connected to %s:%d", address, port);
            break; // success
        }
        if (errno == EINPROGRESS)
        {
            // Wait for writability (connect complete) with timeout.
            struct timeval tv;
            tv.tv_sec = testing ? 0 : 3;       // shorter for silent probe
            tv.tv_usec = testing ? 300000 : 0; // 300 ms when testing else 3s total
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            int sel = select(fd + 1, NULL, &wfds, NULL, &tv);
            if (sel > 0)
            {
                int soerr = 0;
                socklen_t slen = sizeof(soerr);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &slen) == 0 && soerr == 0)
                {
                    ZF_LOGD("tcpconnect (delayed) connected to %s:%d", address, port);
                    break;
                }
                if (!testing)
                    ZF_LOGE("tcpconnect failed post-select to %s:%d: %s", address, port, strerror(soerr));
            }
            else
            {
                if (!testing)
                    ZF_LOGE("tcpconnect timeout to %s:%d", address, port);
            }
        }
        else
        {
            if (!testing)
                ZF_LOGE("tcpconnect immediate failure to %s:%d: %s", address, port, strerror(errno));
        }
        close(fd);
        fd = -1; // try next addrinfo
    }
    freeaddrinfo(res);
    if (fd >= 0)
    {
        // Ensure non-blocking (may already be) for later nbrecv.
        int flags2 = fcntl(fd, F_GETFL, 0);
        if (flags2 >= 0 && !(flags2 & O_NONBLOCK))
            fcntl(fd, F_SETFL, flags2 | O_NONBLOCK);
        return fd;
    }
    return -1;
}

int tcpsend(int fd, unsigned char *data, size_t datalen)
{
    ssize_t sent = send(fd, (const char *)data, datalen, 0);
    if (sent != (ssize_t)datalen)
    {
        ZF_LOGE("tcpsend error: %s", strerror(errno));
        return -1;
    }
    ZF_LOGD("tcpsend %zu bytes", datalen);
    return 0;
}

// Similar to recv() but returns 0 (not -1) when no data (EAGAIN/EWOULDBLOCK).
int nbrecv(int sockfd, char *data, size_t len)
{
    int ret = (int)recv(sockfd, data, len, MSG_DONTWAIT);
    if (ret == -1 && (errno == EWOULDBLOCK || errno == EAGAIN))
        return 0;
    return ret;
}

void tcpclose(int *fd)
{
    if (fd && *fd)
    {
        close(*fd);
        *fd = 0;
    }
}

// --- GPIO Stubs (macOS) -------------------------------------------------
// Not applicable. Provide no-ops that warn only on first use so code that
// expects these symbols links cleanly.
static bool gpio_warned_init = false;
static bool gpio_warned_write = false;
static bool gpio_warned_setup = false;

int gpioInitialise()
{
    if (!gpio_warned_init)
    {
        ZF_LOGW("gpioInitialise not supported on macOS (no-op)");
        gpio_warned_init = true;
    }
    return 0; // success no-op
}

void gpioWrite(unsigned gpio, unsigned level)
{
    (void)gpio;
    (void)level;
    if (!gpio_warned_write)
    {
        ZF_LOGW("gpioWrite no-op on macOS");
        gpio_warned_write = true;
    }
}

void SetupGPIOPTT(int pin, bool invert)
{
    (void)pin;
    (void)invert;
    if (!gpio_warned_setup)
    {
        ZF_LOGW("SetupGPIOPTT no-op on macOS");
        gpio_warned_setup = true;
    }
}

// CM108 HID PTT is implemented separately in MacCM108.c (not part of this milestone).
