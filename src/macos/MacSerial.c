// macOS serial (COM) implementation using POSIX termios.
// Scope: Provide parity with Linux implementation for CAT/PTT control while
// keeping code confined to src/macos/. No header changes (WriteCOMBlock
// retains bool return). Requirement to "return number of bytes written" can't
// be expressed via current prototype; we log the actual byte count instead.
// If future cross-platform refactor permits, consider changing prototype.

#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <stdlib.h>

#include "common/os_util.h"
#include "common/log.h"

static void *(*serial_malloc_fn)(size_t) = malloc;
static void *(*serial_realloc_fn)(void *, size_t) = realloc;
static char *(*serial_strdup_fn)(const char *) = NULL;

static char *default_strdup(const char *s)
{
    return strdup(s);
}

static void ensure_default_allocators(void)
{
    if (!serial_strdup_fn)
        serial_strdup_fn = default_strdup;
}

void macserial_test_set_allocators(void *(*malloc_fn)(size_t), void *(*realloc_fn)(void *, size_t), char *(*strdup_fn)(const char *))
{
    serial_malloc_fn = malloc_fn ? malloc_fn : malloc;
    serial_realloc_fn = realloc_fn ? realloc_fn : realloc;
    serial_strdup_fn = strdup_fn ? strdup_fn : default_strdup;
}

void macserial_test_reset_allocators(void)
{
    macserial_test_set_allocators(NULL, NULL, NULL);
}

// Speed mapping table (mirrors Linux selection)
struct speed_struct
{
    int user_speed;
    speed_t termios_speed;
};

static const struct speed_struct speed_table[] = {
    {300, B300}, {600, B600}, {1200, B1200}, {2400, B2400}, {4800, B4800}, {9600, B9600}, {19200, B19200}, {38400, B38400}, {57600, B57600}, {115200, B115200}, {-1, B0}};

static const struct speed_struct *lookup_speed(int speed)
{
    const struct speed_struct *s = speed_table;
    while (s->user_speed != -1)
    {
        if (s->user_speed == speed)
            return s;
        s++;
    }
    return NULL;
}

HANDLE OpenCOMPort(void *Port, int speed)
{
    if (Port == NULL)
    {
        ZF_LOGE("Com Open failed: Port pointer NULL");
        return 0;
    }
    const char *path = (const char *)Port;
    // NOTE(macOS): /dev/cu.* is preferred for outgoing connections; caller
    // supplies full device path. We do not auto-translate /dev/tty.* to
    // /dev/cu.* to avoid surprising behavior.
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd == -1)
    {
        ZF_LOGE("Com Open failed: %s could not be opened (%s)", path, strerror(errno));
        return 0;
    }
    const struct speed_struct *sp = lookup_speed(speed);
    if (!sp)
    {
        ZF_LOGE("Invalid baud rate (%d) specified for com port (%s)", speed, path);
        close(fd);
        return 0;
    }
    struct termios tio;
    if (tcgetattr(fd, &tio) == -1)
    {
        ZF_LOGE("ERROR: Unable to get attributes of %s (%s)", path, strerror(errno));
        close(fd);
        return 0;
    }
    cfmakeraw(&tio);
    // 8N1, disable flow control explicitly
    tio.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
    tio.c_cflag |= (CS8 | CREAD | CLOCAL);
    tio.c_iflag &= ~(IXON | IXOFF | IXANY);
    cfsetispeed(&tio, sp->termios_speed);
    cfsetospeed(&tio, sp->termios_speed);
    if (tcsetattr(fd, TCSANOW, &tio) == -1)
    {
        ZF_LOGE("Error setting baud rate for %s to %d (%s)", path, speed, strerror(errno));
        close(fd);
        return 0;
    }
    // Clear RTS & DTR to known state
    COMClearRTS(fd);
    COMClearDTR(fd);
    ZF_LOGI("Serial port '%s' opened (fd=%d baud=%d)", path, fd, speed);
    return fd;
}

void CloseCOMPort(HANDLE *fd)
{
    if (fd && *fd)
    {
        ZF_LOGI("Serial port fd %d closing", *fd);
        close(*fd);
        *fd = 0;
    }
}

bool COMSetRTS(HANDLE fd)
{
    int status;
    if (ioctl(fd, TIOCMGET, &status) == -1)
    {
        ZF_LOGE("ARDOP COMSetRTS TIOCMGET: %s", strerror(errno));
        return false;
    }
    status |= TIOCM_RTS;
    if (ioctl(fd, TIOCMSET, &status) == -1)
    {
        ZF_LOGE("ARDOP COMSetRTS TIOCMSET: %s", strerror(errno));
        return false;
    }
    ZF_LOGD("RTS set on fd %d", fd);
    return true;
}

bool COMClearRTS(HANDLE fd)
{
    int status;
    if (ioctl(fd, TIOCMGET, &status) == -1)
    {
        ZF_LOGE("ARDOP COMClearRTS TIOCMGET: %s", strerror(errno));
        return false;
    }
    status &= ~TIOCM_RTS;
    if (ioctl(fd, TIOCMSET, &status) == -1)
    {
        ZF_LOGE("ARDOP COMClearRTS TIOCMSET: %s", strerror(errno));
        return false;
    }
    ZF_LOGD("RTS cleared on fd %d", fd);
    return true;
}

bool COMSetDTR(HANDLE fd)
{
    int status;
    if (ioctl(fd, TIOCMGET, &status) == -1)
    {
        ZF_LOGE("ARDOP COMSetDTR TIOCMGET: %s", strerror(errno));
        return false;
    }
    status |= TIOCM_DTR;
    if (ioctl(fd, TIOCMSET, &status) == -1)
    {
        ZF_LOGE("ARDOP COMSetDTR TIOCMSET: %s", strerror(errno));
        return false;
    }
    ZF_LOGD("DTR set on fd %d", fd);
    return true;
}

bool COMClearDTR(HANDLE fd)
{
    int status;
    if (ioctl(fd, TIOCMGET, &status) == -1)
    {
        ZF_LOGE("ARDOP COMClearDTR TIOCMGET: %s", strerror(errno));
        return false;
    }
    status &= ~TIOCM_DTR;
    if (ioctl(fd, TIOCMSET, &status) == -1)
    {
        ZF_LOGE("ARDOP COMClearDTR TIOCMSET: %s", strerror(errno));
        return false;
    }
    ZF_LOGD("DTR cleared on fd %d", fd);
    return true;
}

// Internal helper implementing write with retry logic similar to Linux
static ssize_t mac_write_with_retry(int fd, const unsigned char *buf, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        ssize_t ret = write(fd, buf + total, len - total);
        if (ret >= (ssize_t)(len - total))
        {
            // All remaining bytes written
            return (ssize_t)len;
        }
        if (ret > 0)
        {
            // Partial write, continue trying
            total += (size_t)ret;
        }
        else if (ret == -1)
        {
            if (errno == EINTR)
            {
                // Signal interrupted, retry immediately
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Would block, wait briefly and retry like Linux does
                usleep(10000); // 10ms delay matching Linux
                continue;
            }
            // Hard error
            return -1;
        }
        else
        {
            // ret == 0 unexpected for serial write
            return (ssize_t)total > 0 ? (ssize_t)total : -1;
        }
    }
    return (ssize_t)total;
}

bool WriteCOMBlock(HANDLE fd, unsigned char *Block, int BytesToWrite)
{
    if (BytesToWrite <= 0)
        return true;
    ssize_t written = mac_write_with_retry(fd, Block, (size_t)BytesToWrite);
    if (written != BytesToWrite)
    {
        if (written == -1)
            ZF_LOGE("Serial write error fd %d (%s)", fd, strerror(errno));
        else
            ZF_LOGE("Serial partial write fd %d: %zd/%d bytes", fd, written, BytesToWrite);
        return false;
    }
    // Debug-level logging for successful serial I/O
    ZF_LOGD("Serial fd %d write %d bytes complete", fd, BytesToWrite);
    return true;
}

int ReadCOMBlock(HANDLE fd, unsigned char *Block, int MaxLength)
{
    if (MaxLength <= 0)
        return 0;
    for (;;)
    {
        ssize_t ret = read(fd, Block, (size_t)MaxLength);
        if (ret > 0)
        {
            ZF_LOGD("Serial fd %d read %zd bytes", fd, ret);
            return (int)ret;
        }
        if (ret == 0)
        {
            // EOF (device closed?)
            ZF_LOGD("Serial fd %d EOF", fd);
            return 0;
        }
        if (errno == EINTR)
            continue; // retry
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0; // no data available now
        ZF_LOGE("Serial read error fd %d (%s)", fd, strerror(errno));
        return -1;
    }
}

static void free_suffix_tracking(char **suffixes, int count)
{
    for (int i = 0; i < count; ++i)
    {
        if (suffixes[i])
        {
            free(suffixes[i]);
            suffixes[i] = NULL;
        }
    }
}

char **GetSerialStrlist()
{
    ensure_default_allocators();
    // Enumerate macOS serial devices with these policies:
    //  - Prefer /dev/cu.* (callout devices) for active connections.
    //  - Include /dev/tty.* only if a corresponding /dev/cu.* variant wasn't found.
    //  - Filter out built-in Bluetooth placeholders (cu.Bluetooth-* / tty.Bluetooth-*)
    //    to mirror Linux behavior of omitting generic Bluetooth pseudo ports.
    //  - Return alternating name/description pairs terminated by NULL (description empty).
    const char *devdir = getenv("ARDOP_TEST_SERIAL_DEV_DIR");
    if (!devdir || devdir[0] == '\0')
        devdir = "/dev";

    DIR *d = opendir(devdir);
    if (!d)
    {
        ZF_LOGD("Directory %s could not be opened for serial enumeration (%s)", devdir, strerror(errno));
        return NULL;
    }
    struct dirent *dir;
    char **slist = NULL;
    int slistsize = 0; // pointer slots
    // Track which cu.* suffixes were added so we can suppress tty.* duplicates.
    // Simple fixed-size tracking for typical small device counts; fall back to linear scan.
    const int MAX_TRACK = 128;
    char *cu_suffixes[MAX_TRACK] = {0};
    int cu_count = 0;
    while ((dir = readdir(d)) != NULL)
    {
        const char *name = dir->d_name;
        if (name[0] == '.')
            continue;
        bool is_cu = strncmp(name, "cu.", 3) == 0;
        bool is_tty = !is_cu && strncmp(name, "tty.", 4) == 0;
        if (!(is_cu || is_tty))
            continue;
        // Filter Bluetooth placeholders
        if ((is_cu && strncmp(name, "cu.Bluetooth", 12) == 0) || (is_tty && strncmp(name, "tty.Bluetooth", 13) == 0))
            continue;
        const char *suffix = NULL;
        if (is_cu)
            suffix = name + 3;
        else if (is_tty)
            suffix = name + 4;
        // If tty.* and we already saw matching cu.* suffix, skip.
        if (is_tty && suffix)
        {
            bool dup = false;
            for (int i = 0; i < cu_count; ++i)
            {
                if (strcmp(cu_suffixes[i], suffix) == 0)
                {
                    dup = true;
                    break;
                }
            }
            if (dup)
                continue;
        }
        char fullpath[PATH_MAX];
        size_t dirlen = strlen(devdir);
        bool add_slash = (dirlen > 0 && devdir[dirlen - 1] != '/');
        int needed = snprintf(fullpath, sizeof(fullpath), "%s%s%s", devdir, add_slash ? "/" : "", name);
        if (needed < 0 || needed >= (int)sizeof(fullpath))
            continue;
        if (slist == NULL)
        {
            slistsize = 1;
            slist = (char **)serial_malloc_fn(sizeof(char *));
            if (!slist)
            {
                ZF_LOGE("malloc failed in GetSerialStrlist (%s)", strerror(errno));
                closedir(d);
                free_suffix_tracking(cu_suffixes, cu_count);
                return NULL;
            }
            slist[slistsize - 1] = NULL;
        }
        slistsize += 2;
        char **tmp = (char **)serial_realloc_fn(slist, slistsize * sizeof(char *));
        if (!tmp)
        {
            ZF_LOGE("realloc failed in GetSerialStrlist (%s)", strerror(errno));
            closedir(d);
            free_suffix_tracking(cu_suffixes, cu_count);
            return slist;
        }
        slist = tmp;
        size_t namesz = strlen(fullpath) + 1;
        slist[slistsize - 3] = (char *)serial_malloc_fn(namesz);
        if (!slist[slistsize - 3])
        {
            ZF_LOGE("malloc (name) failed in GetSerialStrlist (%s)", strerror(errno));
            slist[slistsize - 3] = NULL;
            slist[slistsize - 2] = NULL;
            closedir(d);
            free_suffix_tracking(cu_suffixes, cu_count);
            return slist;
        }
        memcpy(slist[slistsize - 3], fullpath, namesz);
        slist[slistsize - 2] = serial_strdup_fn("");
        if (!slist[slistsize - 2])
        {
            ZF_LOGE("strdup failed in GetSerialStrlist (%s)", strerror(errno));
            free(slist[slistsize - 3]);
            slist[slistsize - 3] = NULL;
            closedir(d);
            free_suffix_tracking(cu_suffixes, cu_count);
            return slist;
        }
        slist[slistsize - 1] = NULL;
        if (is_cu && suffix && cu_count < MAX_TRACK)
        {
            char *dup = serial_strdup_fn(suffix);
            if (dup)
            {
                cu_suffixes[cu_count] = dup;
                cu_count++;
            }
        }
    }
    closedir(d);
    // Free any suffix tracking allocations (not needed after enumeration)
    free_suffix_tracking(cu_suffixes, cu_count);
    if (slist)
    {
        int pairs = 0;
        for (int i = 0; slist[i]; i += 2)
            pairs++;
        ZF_LOGD("Serial enumeration: %d device(s) found (cu.* preferred, Bluetooth filtered)", pairs);
        for (int i = 0; slist[i]; i += 2)
            ZF_LOGD("  %s", slist[i]);
    }
    else
    {
        ZF_LOGD("Serial enumeration: no eligible devices found");
    }
    return slist;
}
