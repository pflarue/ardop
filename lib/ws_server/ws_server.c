#ifdef WIN32
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
// Not Win32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>

// For Windows, accept() returns a SOCKET type, while Linux returns an int.
// So, always use SOCKET, but for Linux, define SOCKET to be int.
#define SOCKET int
// For Windows, SOCKET is an unsigned type.  So, where Linux uses -1 to
// indicate an invalid socket, Windows uses INVALID_SOCKET.  So, always
// use INVALID_SOCKET, but for Linux, define INVALID_SOCKET to be -1.
//     To confuse the issue even further, while a Windows SOCKET is an
//     unsigned type, winsock2.h contains:
//       #define INVALID_SOCKET (SOCKET)(~0)
//     This is equivalent to setting INVALID_SOCKET to -1 on a two's
//     complement system.  Furthermore, the Microsoft documentation for the
//     WSAPoll function says "Members of the fdarray which have their fd
//     memeber set to a negative value are ignored and their revents will
//     be set to POLLNVAL upon return."  So, this explicitly says to assign
//     a negative value to a member with an unsigned SOCKET type.  Here, I
//     will use INVALID_SOCKET instead, and that seems to work.
#define INVALID_SOCKET -1
// For windows, closesocket() must be used to close a SOCKET rather than
// close(), which is used for Linux.  So, always use closesocket(), but
// for Linux, define closesocket to be close.
#define closesocket close
// Windows uses the typedef WSAPOLLFD where Linux uses struct pollfd.  So,
// always use WSAPOLLFD, but for Linux, construct typedef WSAPOLLFD using
// struct pollfd
typedef struct pollfd WSAPOLLFD;
// Windows uses the function WSAPoll() where Linux uses poll().  So,
// always use WSAPoll(), but for Linux, define WLAPoll to be poll.
#define WSAPoll poll
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ws_server.h"

// SHEADSIZE is the maximum supported length of a WebSocket header sent by
// this server.  The WebSocket spec (RFC6455) defines a maximum header
// length of 10 for frames sent by servers, but this maximum length is not
// required for the frames no longer than 64kB supported by this server.
#define SHEADSIZE 4
// RHEADSIZE is the maximum supported length of a WebSocket header received
// by this server.  The WebSocket spec (RFC6455) defines a maximum header
// length of 14 for frames sent by servers, but this maximum length is not
// required for the frames no longer than 64kB supported by this server.
#define RHEADSIZE 8

// opcodes defined in RFC6455
#define OP_CONTINUATION 0x00
// #define OP_TEXT 0x01  // Not supported by this implementation
#define OP_BINARY 0x02
#define OP_CLOSE 0x08
#define OP_PING 0x09
#define OP_PONG 0x0A

// state values
#define WS_UNUSED -1   // An unused/empty slot
#define WS_HTTP 0      // HTTP rather than WebSocket connection.
#define WS_ACTIVE 1    // Active WebSocket connection.
// Change WS_ACTIVE to WS_CLOSING when a OP_CLOSE frame has been
// sent, but a response has not yet been received from the client.
#define WS_CLOSING 2
#define WS_LISTENER 3  // The master/listener port

// Maximum supported length of ws_uri and any ws_http_targets[].uri
#define WS_URILEN 128
// Maximum supported length of any ws_http_targets[].path
#define WS_PATHLEN 256
// Maximum supported length of any ws_http_targets[].contenttype
#define WS_CONTENTTYPELEN 128
// Maximum supported number of ws_http_targets that may be added.
// This is not intended to be used as a general purpose http server.
#define WS_MAX_HTTP_TARGETS 20

// ws_listening is changed to true by ws_init().
bool ws_listening = false;

// ws_http_targets is an initially empty array of available http targets.
// Each ws_http_target is defined by its uri, contenttype and either
// pathname or data and data_len.  If data_len is not 0, pathname will be
// ignored.
struct ws_http_target {
    char uri[WS_URILEN];
    char contenttype[WS_URILEN];
    char pathname[WS_PATHLEN];
    char *data;
    size_t data_len;
};
struct ws_http_target *ws_http_targets = NULL;
int ws_num_http_targets = 0;  // The length of ws_http_targets list.

// Two arrays, pfds and state, will be allocated in ws_init(), each with a
// length of ws_max_clients + 1.  The zeroth element of these arrays is used
// for the master/listener socket.  ws_server is intended for use with a
// relatively small number of client sockets, so ws_max_clients defaults to 10.
// ws_max_clients may be set with ws_set_max_clients() before ws_init().
unsigned int ws_max_clients = 10;
int *state;   // WS_ACTIVE, WS_CLOSING, WS_HTTP, or WS_UNUSED
WSAPOLLFD *pfds;  // members are fd, events, and revents.
// global persistent [between calls to ws_poll()] bytes_remaining, cnum, and
// mask are used to store information about a partially read frame.  If
// bytes_remaining is greater than zero at the start of ws_poll(), then
// that number of bytes still need to be read and unmasked from cnum.
// Otherwise, check for any socket with data to be read.
int bytes_remaining;
int cnum;
char mask[4];

char ws_port_str[7] = "8088";  // Default.  Can be changed with ws_set_port()
// ws_uri default can be changed with ws_set_uri().  For the typical expected
// usage, the uri of an html file will be set with ws_add_httptarget_file()
// or ws_add_httptarget_data().  This will typically be given an easy to type
// uri, possibly "/".  Within that HTML file, may be links to js and/or css
// files.  Within the js (which may be a separate file or be within html file),
// will be hard coded the value of ws_uri[], either this default or a value set
// with ws_set_uri().  Since the user will not have to type this, and will
// often never see it, it doesn't usually have to be memorable nor easily typed.
char ws_uri[WS_URILEN] = "/ws";

// Define two functions for printing.  One for error messages and one for
// information/debugging messages.  Both are intially set to
// ws_default_print() which behaves similarly to printf(), except that it
// automatically adds \n at the end.  Both can be overriden with alternative
// functions using ws_set_error() and ws_set_debug().
int ws_default_print(const char* format, ...) {
    va_list arglist;
    va_start(arglist, format);
    vprintf(format, arglist);
    printf("\n");
}
int (*ws_error)(const char*, ...) = &ws_default_print;
int (*ws_debug)(const char*, ...) = &ws_default_print;
void ws_set_error(int (*err)(const char*, ...)) { ws_error = err; }
void ws_set_debug(int (*dbg)(const char*, ...)) { ws_debug = dbg; }

// A program may optionally set a callback function to be called whenever
// a client connection is closed.  This is NULL by default.
// This allows the program to flush buffers or reset other values when a
// client disconnects, so that later data received from a newly connected
// client assigned the same client number is not mistaken for additional
// data from the prior client connection.
void (*close_notify)(const int cnum) = NULL;
void ws_set_close_notify(void (*callback)(const int cnum)) {
    close_notify = callback;
}

// The WebSocket protocol requires that a server calculate a SHA1 hash and
// then convert the result to a Base64 string as a part of the handshake
// required to upgrade a HTTP connection to a WebSocket connection.
// The following functions are used for this handshake.  They are somewhat
// simplified from general SHA1 and Base64 functions because their intputs
// (and outputs) are always of known fixed lengths.

// Process one block of data for SHA1 hash.
// See FIPS PUB 180-3 dated Oct 2008 for a description of SHA1
static void sha1_block(unsigned char m[64], unsigned int h[5]) {
    unsigned int a, b, c, d, e, f, t;
    unsigned int w[80];
    // constants per section 4.2.1
    unsigned int k[4] = {0x5a827999, 0x6ed9eba1, 0x8f1bbcdc, 0xca62c1d6};
    // prepare the message schedule per section 6.1.2 step 1
    for (int i = 0; i < 16; i++)
        w[i] = (m[i * 4]<<24) + m[ i * 4 + 1] * 65536
            + m[i * 4 + 2]*256 + m[i * 4 + 3];
    for (int i = 16; i < 80; i++) {
        t = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
        w[i] = (t + t) | (t >> 31);
    }
    // initialize the working variables per section 6.1.2 step 2
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
    // section 6.1.2 step 3
    for(int i = 0; i < 80; i++) {
        // calculate f per section 4.1.1
        if (i < 20)
            f = d ^ (b & (c ^ d));  // Ch
        else if (i < 40 || i >= 60)
            f = b ^ c ^ d;  // Parity
        else
            f = (b & c) + (d & (b ^ c));  // Maj
        t = (a << 5) + (a >> 27) + f + e + k[i / 20] + w[i];
        e = d; d = c;
        c = (b << 30) + (b >> 2);
        b = a; a = t;
    }
    // update hash values per section 6.1.2 step 4
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}


// SHA1 hash of 60-bytes of input to produce 20 bytes of output
void sha1(unsigned char output[20], unsigned char input[60]) {
    // After the required padding, a 60 byte (480 = 0x01e0 bits) input must
    // be processed as 2 blocks
    unsigned char block0[64] = {0};
    unsigned char block1[64] = {0};
    // initial hash value from section 5.3.1
    unsigned int h[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476,
        0xc3d2e1f0};
    memcpy((char *)block0, (char *)input, 60);
    block0[60] = 0x80;  // first bit after end of input is 1 (then zeros)
    // Final 64 bits of the last block indicate the length in input in bits.
    block1[62] = 0x01; block1[63] = 0xe0;
    sha1_block(block0, h);
    sha1_block(block1, h);
    // assemble hash digest
    for (int i=0; i < 5; ++i) {
        output[i*4 + 0] = h[i] >> 24;
        output[i*4 + 1] = h[i] >> 16;
        output[i*4 + 2] = h[i] >>  8;
        output[i*4 + 3] = h[i] >>  0;
    }
}


// Base64 encoding 20 bytes of input to produce a 29 byte null terminated
// string.  The Null is the 29th byte.
void b64(const unsigned char in[20], unsigned char out[29])
{
    static const char *estr = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned int i;
    unsigned int j = 0;
    for (i = 0; i < 18; i+=3) {
        out[j++] = estr[(in[i] >> 2) & 0x3F];
        out[j++] = estr[((in[i] & 0x3) << 4) | ((in[i + 1] >> 4) & 0xF)];
        out[j++] = estr[((in[i + 1] & 0xF) << 2) | ((in[i + 2] >> 6) & 0x3)];
        out[j++] = estr[in[i + 2] & 0x3F];
    }
    out[j++] = estr[(in[i] >> 2) & 0x3F];
    out[j++] = estr[((in[i] & 0x3) << 4) | ((in[i + 1] >> 4) & 0xF)];
    out[j++] = estr[((in[i + 1] & 0xF) << 2)];  // partial
    out[j++] = '=';  // padding
    out[j] = 0x00;  // make out a null terminated string
}

// Set the maximum number of clients that the WebSocket server will allow to
// connect at any one time.
// This is optional, but is ignored if used after ws_init()
// Return 0 on success, -1 on failure.
int ws_set_max_clients(unsigned int max_clients) {
    if (ws_listening) {
        ws_error("WS ERROR: Ignoring ws_set_max_clients() after ws_init()");
        return (-1);
    }
    ws_max_clients = max_clients;
    ws_debug("WS: WebSocket server will  allow up to %d simultaneous clients.",
        ws_max_clients);
    return (0);
}

// Set the uri for the Websocket server.
// This is optional, but is ignored if used after ws_init()
// Return 0 on success, -1 on failure.
int ws_set_uri(char *uri) {
    if (ws_listening) {
        ws_error("WS ERROR: Ignoring ws_set_uri() after ws_init()");
        return (-1);
    }
    if (strnlen(uri, WS_URILEN) == WS_URILEN) {
        ws_error("WS ERROR: URI length must be less than %d.", WS_URILEN);
        return (-1);
    }
    strcpy(ws_uri, uri);
    ws_debug("WS: URI set to '%s'.", ws_uri);
    return (0);
}


// Set the port for the WebSocket Server.
// This is optional, but is ignored if used after ws_init()
// Return 0 on success, -1 on failure.
int ws_set_port(int port) {
    if (ws_listening) {
        ws_error("WS ERROR: Ignoring ws_set_port() after ws_init()");
        return (-1);
    }
    snprintf(ws_port_str, sizeof(ws_port_str), "%d", port);
    ws_debug("WS: WebSocket server port set to %s.", ws_port_str);
    return (0);
}


// ws_add_httptarget_file() and/or ws_add_httptarget_data() may be used to
// set a small number of uris which may be served as http resources (html,
// js, css, etc.).

// Return pointer to struct ws_http_target or NULL on error
struct ws_http_target* ws_set_httptarget(char *uri, char *contenttype) {
    if (strnlen(uri, WS_URILEN) == WS_URILEN) {
        ws_error("WS ERROR: HTTP URI length must be less than %d.",
            WS_URILEN);
        return (NULL);
    }
    if (strnlen(contenttype, WS_CONTENTTYPELEN) == WS_CONTENTTYPELEN) {
        ws_error("WS ERROR: contenttype length must be less than %d.",
            WS_CONTENTTYPELEN);
        return (NULL);
    }
    // If uri already defined, clear contenttype, pathname, data, and data_len
    for (int i=0; i<ws_num_http_targets; i++) {
        if (strcmp(ws_http_targets[i].uri, uri) == 0) {
            ws_debug("WS: Updating HTTP uri='%s' with new content and"
                " contenttype", uri);
            ws_http_targets[i].contenttype[0] = 0x00;  // zero length string
            ws_http_targets[i].pathname[0] = 0x00;  // zero length string
            ws_http_targets[i].data = NULL;
            ws_http_targets[i].data_len = 0;
            return (&(ws_http_targets[i]));
        }
    }
    if (ws_num_http_targets == WS_MAX_HTTP_TARGETS) {
        ws_error("WS ERROR: Limit (%d) to number of HTTP URIs has been"
            " reached (ignoring additional URIs).", WS_MAX_HTTP_TARGETS);
        return (NULL);
    }
    if (ws_http_targets == NULL) {
        ws_http_targets = malloc(sizeof(struct ws_http_target));
        ws_num_http_targets = 1;
    }
    else
        ws_http_targets = realloc(ws_http_targets,
            ++ws_num_http_targets * sizeof(struct ws_http_target));
    strcpy(ws_http_targets[ws_num_http_targets - 1].uri, uri);
    strcpy(ws_http_targets[ws_num_http_targets - 1].contenttype, contenttype);
    return (&(ws_http_targets[ws_num_http_targets - 1]));
}


// Return 0 on success, -1 on failure.
int ws_add_httptarget_file(char *uri, char *pathname, char *contenttype) {
    struct ws_http_target *target;
    FILE *fp;
    // check path
    if (strnlen(pathname, WS_PATHLEN) == WS_PATHLEN) {
        ws_error("WS ERROR: Length of pathname for HTTP target must be less"
            " than %d.", WS_PATHLEN);
        return (-1);
    }
    if ((fp = fopen(pathname, "rb")) == NULL) {
        ws_error("WS ERROR: pathname='%s' for HTTP uri='%s' cannot be opened.",
            pathname, uri);
        return (-1);
    }
    else
        fclose(fp);

    target = ws_set_httptarget(uri, contenttype);
    if (target == NULL)
        return (-1);
    strcpy(target->pathname, pathname);
    target->data_len = 0;
    ws_debug("WS: Add HTTP uri='%s' with pathname='%s' and contenttype='%s'.",
        uri, pathname, contenttype);
    return (0);
}


// Return 0 on success, -1 on failure.
int ws_add_httptarget_data(char *uri, char *data, size_t data_len,
    char *contenttype)
{
    struct ws_http_target *target;
    if (data_len < 1) {
        ws_error("WS ERROR: Invalid data for http target (data_len=0)");
        return (-1);
    }
    target = ws_set_httptarget(uri, contenttype);
    if (target == NULL)
        return (-1);
    target->data_len = data_len;
    target->data = data;
    ws_debug("WS: Add HTTP uri='%s' with data_len=%d and contenttype='%s'.",
        uri, data_len, contenttype);
    return (0);
}


// Close a socket and reset pfds and state.
void ws_close_socket(int c_cnum)
{
    if (close_notify != NULL && (
        state[c_cnum] == WS_ACTIVE ||
        state[c_cnum] == WS_CLOSING)
    )
        close_notify(c_cnum);
    closesocket(pfds[c_cnum].fd);
    pfds[c_cnum].fd = INVALID_SOCKET;
    state[c_cnum] = WS_UNUSED;
}


// Send some data to the client socket number s_cnum.
// Return -1 on error or else the number of bytes sent.
int send_to_socket(int s_cnum, const char *data, int data_len)
{
    int sent;
    if((sent = send(pfds[s_cnum].fd, data, data_len, 0)) != data_len) {
        // If sent is less than data_len, but not zero, then data was
        // partially sent.  TODO: buffer unsent data and try again
        // rather than closing connection
        ws_error("WS: send failed. Closing connection to client %d.", s_cnum);
        ws_close_socket(s_cnum);
        return (-1);
    }
    return (sent);
}


// Build a header for sending of a WebSocket frame.
// On return, *frhlen is the size of the created frame header in frh.
// Return 0 on success, -1 on failure.
int ws_build_header(int opcode, int data_len, char frh[SHEADSIZE],
    int *frhlen)
{
    if (opcode != OP_BINARY && opcode != OP_CLOSE
        && opcode != OP_PING && opcode != OP_PONG
    )
        // Invalid or unsupported opcode (including OP_TEXT)
        return (-1);
    frh[0] = 0x80 | opcode;
    if(data_len <= 0x7D) {  // 2-byte frame header
        frh[1] = data_len;
        *frhlen = 2;
        return (0);
    }
    if (data_len <= 0xFFFF) {  // 4-byte frame header
        frh[1] = 0x7E;
        frh[2] = data_len >> 8;
        frh[3] = data_len;
        *frhlen = 4;
        return (0);
    }
    // This implementation does not support payload length > 0xFFFF (64 kB)
    // Thus, it does not support 10-byte frame headers
    return (-1);
}


// Build the HTTP header to be sent to the client in response to a request
// to upgrade the connection from HTTP to WebSocket.
// Return the length of the handshake response.  Return -1 on error.
// key is 24 bytes ending in "==" (not null terminated)
int ws_handshake(const char key[24], char *buf, int buf_size)
{
    // The first 24 bytes of catkey will be overwritten with the contents of
    // key, while the remaining 36 bytes are a fixed value defined in RFC6455.
    char catkey[61] = "123456789012345678901234"
        "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char newkey[29];
    int out_len;
    uint8_t sha[20];  // SHA1 always produces a 20-byte output
    memcpy(catkey, key, 24);
    sha1(sha, (unsigned char*)catkey);  // SHA1 hash of catkey
    b64(sha, newkey);  // Base64 encoding of SHA1 hash

    out_len = snprintf(
        buf, buf_size,
        "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",
        newkey);
    if (out_len >= buf_size || out_len == -1) {
        ws_error("WS: ws_handshake() buffer too small.");
        return (-1);
    }
    return (out_len);
}


// If s_cnum > 0, send to only that client
// If s_cnum == 0, multicast to all clients
// If s_cnum < 0, multicast to all but -s_cnum
// return the number of clients that frame was successfully sent to.
int multicast_fr(int s_cnum, char frh[SHEADSIZE], int frhlen,
    const char *data, int data_len)
{
    int start_cnum;
    int stop_cnum;
    int target_count = 0;
    if (s_cnum > 0) {
        start_cnum = s_cnum;
        stop_cnum = s_cnum + 1;
    } else {
        start_cnum = 1;
        stop_cnum = ws_max_clients;
    }
    for (int this_cnum = start_cnum; this_cnum < stop_cnum; this_cnum++) {
        if (s_cnum < 0 && this_cnum == -s_cnum)
            // For a negative s_cnum, send to all clients except -s_cnum
            continue;
        if (state[this_cnum] == WS_ACTIVE) {
            if (send_to_socket(this_cnum, frh, frhlen) == -1) {
                // sent_to_socket() closed connection.
                ws_error("WS: Error sending frame header to client %d.",
                    this_cnum);
                return (-1);
            }
            // While all frames have a header, some do not have a payload
            if (data_len && send_to_socket(this_cnum,
                data, data_len) == -1)
            {
                // send_to_socket() closed connection.
                ws_error("WS: Error sending frame payload to client %d.",
                    this_cnum);
                return (-1);
            }
            target_count++;
        }
    }
    return (target_count);
}


// Send a binary WebSocket message containing the provided payload.
// If s_cnum > 0, send to only that client
// If s_cnum == 0, multicast to all clients
// If s_cnum < 0, multicast to all but -s_cnum
// return the number of clients that frame was successfully sent to.
// data is a buffer of length data_len.
int ws_send(int s_cnum, const char *data, int data_len) {
    if (!ws_listening) {
        ws_error("WS ERROR: ws_init() must be called before ws_send().");
        return (0);
    }
    char frh[SHEADSIZE];
    int frhlen;
    if (ws_build_header(OP_BINARY, data_len, frh, &frhlen) == -1) {
        ws_error("WS: Error creating BINARY frame header.");
        return (0);
    }
    return multicast_fr(s_cnum, frh, frhlen, data, data_len);
}


// status is a unsigned short (2-bytes) indicating the reason for
// closing.  RFC6455 Section 7.4.1 defines several status codes and
// indicates that private unregistered codes in the range of 4000-4999
// may also be used.  Use 0 to not specify a reason for closing.  1000
// is used to indicate normal closure because the purpose for which
// the connection was established has been fulfilled,  Status values are
// not checked for conformance with restrictions specified in RFC6455.
//
// WebSocket close frames may also contain a utf-8 string in addition to
// the status code indicating the reason for closing.  However, this
// ability is not currently supported.
//
// If s_cnum > 0, send to only that client
// If s_cnum == 0, multicast to all clients
// If s_cnum < 0, multicast to all but -s_cnum
// return the number of clients that frame was successfully sent to.
int ws_close(int s_cnum, unsigned short status) {
    // ws_debug("WS: ws_close(s_cnum=%d, status=%d)", s_cnum, status);
    if (!ws_listening) {
        ws_error("WS ERROR: ws_init() must be called before ws_close().");
        return (0);
    }
    char frh[SHEADSIZE];
    int frhlen;
    char data[2];
    int ret;
    if (status == 0) {
        if (ws_build_header(OP_CLOSE, 0, frh, &frhlen) == -1) {
            ws_error("WS: Error creating CLOSE frame header.");
            return (0);
        }
        ret = multicast_fr(s_cnum, frh, frhlen, NULL, 0);
        // update state after multicast_fr()
        state[s_cnum] = WS_CLOSING;
        return (ret);
    }
    data[0] = status >> 8;
    data[1] = status & 0xFF;
    if (ws_build_header(OP_CLOSE, 2, frh, &frhlen) == -1) {
        ws_error("WS: Error creating CLOSE frame header.");
        return (0);
    }
    ret = multicast_fr(s_cnum, frh, frhlen, (char *)data, 2);
    // update state after multicast_fr()
    state[s_cnum] = WS_CLOSING;
    return (ret);
}


// Return the number of active WebSocket client connections.
// If bitmap is not NULL, then on return it also indicates which of the
// first 32 client number values currently represent open WebSocket
// client connections.  ((*bitmap >> (n - 1)) & 0x01) == 1 true means that
// client number n is an active WebSocket connection for 1 <= n <= 32.
// If ws_max_clients is greater than 32, this function does not provide
// a way to check the status of individual clients numbered greater than 32.
// Client number 0 is never used.
// If bitmap is NULL, then information about specific client numbers is not
// provided.
// Before ws_init(), return 0 (with *bitmap=0);
int ws_get_client_count(uint32_t *bitmap) {
    if (!ws_listening) {
        if (bitmap != NULL)
            *bitmap = 0;
        return (0);
    }
    unsigned int client_count = 0;
    uint32_t bitmap_value = 0;
    // this_cnum=0 is not used.
    for (unsigned int this_cnum = ws_max_clients; this_cnum > 0; this_cnum--) {
        bitmap_value <<= 1;
        if (state[this_cnum] == WS_ACTIVE) {
            client_count++;
            bitmap_value++;
        }
    }
    if (bitmap != NULL)
        *bitmap = bitmap_value;
    return (client_count);
}


// Return 0 on success, -2 on failure
int serve_http(struct ws_http_target *target, int s_cnum) {
    char headerbuf[WS_CONTENTTYPELEN + 40];
    sprintf(headerbuf, "HTTP/1.1 200 OK\r\nContent-type: %s\r\n\r\n\r\n",
        target->contenttype);
    if (target->data_len > 0) {
        ws_debug("WS: Serving data_len=%d with contenttype='%s'"
            " for http uri='%s'.",
            target->data_len, target->contenttype, target->uri);
        send_to_socket(s_cnum, headerbuf, strlen(headerbuf));
        send_to_socket(s_cnum, target->data, target->data_len);
        return (0);
    }

    ws_debug("WS: Serving pathname='%s' with contenttype='%s' for http uri='%s'.",
        target->pathname, target->contenttype, target->uri);
    FILE *fp;
    int nextc;
    char *data, *bptr;
    int data_len;
    int len_increment = 1024;
    data = (char *) malloc(len_increment);
    if (data == NULL) {
         ws_error("WS Error:  Memory allocation error reading uri=%s.", target->uri);
         return (-2);
    }
    data_len = len_increment;
    bptr = data;

    if ((fp = fopen(target->pathname, "rb")) == NULL) {
         ws_error("WS Error: Unable to open file for uri=%s.", target->uri);
         free(data);
         return (-2);
    }
    while ((nextc = fgetc(fp)) != EOF) {
        *(bptr++) = nextc;
        if (bptr - data == data_len) {
            data = (char *) realloc(data, data_len + len_increment);
            if (data == NULL) {
                ws_error("WS Error:  Memory reallocation error reading uri=%s.", target->uri);
                return (-2);
            }
            bptr = data + data_len;
            data_len += len_increment;
        }
    }
    bptr--;
    fclose(fp);
    // No need to check for failure of send_to_socket()?
    send_to_socket(s_cnum, headerbuf, strlen(headerbuf));
    send_to_socket(s_cnum, data, bptr-data);
    free(data);
    return (0);
}


int sendHttpErr(const char *errstr, const char *uri, int s_cnum) {
    // errstr is expected to have the form "HTTP/1.1 %s\r\n\r\n"
    // Don't bother checking for failure of send_to_socket().
    send_to_socket(s_cnum, errstr, strlen(errstr));
    ws_error("WS: Sending '%.*s' error for URI='%s'.", strlen(errstr) - 4, errstr, uri);
    ws_close_socket(s_cnum);
    return (-1);
}


// Return 0 for a valid/expected request
// Return -1 for a bad or unexpected request.
// Return -2 for an unexpected failure while processing a request.
int process_http_req(){
    const int BUFLEN = 1024;
    char http_buf[BUFLEN];
    char uri[WS_URILEN];
    int bytes_read;
    char discard;
    int num_discarded = 0;
    int sendfrSize;
    char *tmpptr;
    char hs_resp[150];
    if((bytes_read = recv(pfds[cnum].fd, http_buf, BUFLEN-1, 0)) == 0){
        ws_close_socket(cnum);
        ws_error("WS: recv failed");
        return (-2);
    }
    // convert http_buf to a null terminated string
    http_buf[bytes_read] = 0;
    // Read and discard remaining data
    while ((bytes_read = recv(pfds[cnum].fd, &discard, 1, 0)) == 1) {
        num_discarded++;
    }
    if (bytes_read == 0) {
        // socket closed (unexpectedly)
        ws_close_socket(cnum);
        ws_error("WS: recv failed while discarding");
        return (-2);
    }
    if (num_discarded > 0)
        ws_error(
            "WS: The last %d of %d bytes of http request were "
            "discarded because http buffer is only %d bytes long.",
            num_discarded, num_discarded + BUFLEN - 1, BUFLEN-1
        );
    if (strstr(http_buf, "\r\n\r\n") == NULL) {
        ws_error("WS: Don't attempt to parse invalid or incomplete http.\n%s", http_buf);
        return (-1);
    }

    // It is not necessary to fully parse the http header.
    // Expecting first line to be: GET uri HTTP/1.1
    if (strncmp(http_buf, "GET", 3) != 0)
        return sendHttpErr("HTTP/1.1 405 Method Not Allowed\r\n\r\n", "N/A", cnum);
    if ((tmpptr = strstr(http_buf + 4, " HTTP"))  == NULL)
        return sendHttpErr("HTTP/1.1 404 Not Found\r\n\r\n", "(malformed)", cnum);
    tmpptr[0] = 0x00;  // null terminator after uri
    if (strlen(http_buf + 4) > WS_URILEN)
        // all known uri have length less than WS_URILEN, so this does not match
        return sendHttpErr("HTTP/1.1 414 URI Too Long\r\n\r\n", uri, cnum);
    strcpy(uri, http_buf + 4);
    tmpptr[0] = ' ';  // eliminate null terminator after uri for later strstr(http_buf, ...)

    // Check whether uri is in ws_http_targets
    for (int i=0; i<ws_num_http_targets; i++) {
        if (strcmp(ws_http_targets[i].uri, uri) == 0) {
            // Serve the file from path associated with uri
            int ret = serve_http(&ws_http_targets[i], cnum);
            ws_debug("WS: closing client after serving http (%d)", cnum);
            ws_close_socket(cnum);
            return (ret);
        }
    }
    if (strcmp(uri, ws_uri) != 0)
        return sendHttpErr("HTTP/1.1 404 Not Found\r\n\r\n", uri, cnum);
    // URI matches ws_uri.  So, see if this is a valid WebSocket upgrade request
    if (
        (strstr(http_buf, "\r\nUpgrade: websocket\r\n") == NULL) ||
        (strstr(http_buf, "\r\nSec-WebSocket-Version: 13\r\n") == NULL) ||
        ((tmpptr = strstr(http_buf, "\r\nSec-WebSocket-Key: ")) == NULL) ||
        // tmpptr + 21 must be a the start of 26 bytes ending in '==\r\n'
        (strncmp(tmpptr + 21 + 22, "==\r\n", 4) != 0)
    )
        // Not a valid request to upgrade to WebSocket
        return sendHttpErr("HTTP/1.1 400 Bad Request\r\n\r\n", uri, cnum);
    if ((sendfrSize = ws_handshake(tmpptr + 21, hs_resp, sizeof(hs_resp))) == -1)
        return sendHttpErr("HTTP/1.1 400 Bad Request\r\n\r\n", uri, cnum);
    if (send_to_socket(cnum, hs_resp, sendfrSize) == -1) {
        ws_close_socket(cnum);
        ws_error("WS: Error sending handshake response.");
        return (-1);
    }
    ws_debug("WS: Sent handshake response to Upgrade from HTTP to WebSocket (%d)", cnum);
    state[cnum] = WS_ACTIVE;
    return (0);
}


// Return data_len on success, -1 on failure
// If successful, data is populated and opcode is set.
// data_len may be limited by data_size, the length of the frame payload, or the
// amount of data available from the socket.
int read_websocket_frame(char *data, int data_size, int *opcode) {
    // read WebSocket frame
    // To read a single WebSocket frame, first read the frame header.
    // Since all frames sent from client to server must be masked,
    // the minimum length of the header for a short frame is 6 bytes,
    // and this is also more than enough to parse the length of all
    // supported (length <= 64kB) frames.
    char wshead[RHEADSIZE];
    int bytes_read;
    if((bytes_read = recv(pfds[cnum].fd, (char*)wshead, 6, 0)) != 6){
        // An error such as incorrect payload length in the last frame
        // sent to pfds[cnum].fd can cause EAGAIN/EWOULDBLOCK here.
        // An error in the handshake header can cause the upgrade from
        // HTTP to WebSocket to fail so that the client does not accept
        // the upgrade, and closes the socket.
        ws_error("WS: head recv failed.");
        ws_error("WS: Closing websocket connection %d.", cnum);
        ws_close_socket(cnum);
        return (-1);
    }
    if ((wshead[1] & 0x7F) == 0x7F) {
        ws_error("WS: Payloads longer than 0xFFFF (64kB) are not supported.");
        ws_close_socket(cnum);
        return (-1);
    }
    if ((wshead[0] & 0x70) != 0 || (wshead[1] & 0x80) == 0 ) {
        ws_error("WS: Invalid WebSocket header (RSV or MASK).");
        ws_close_socket(cnum);
        return (-1);
    }
    bytes_remaining = wshead[1] & 0x7F;
    if (bytes_remaining == 0x7E) {
        // Frame uses 2-byte extended payload length. Read remainder of header
        if((bytes_read += recv(pfds[cnum].fd, (char*)(wshead + 6), 2, 0)) != 8) {
            ws_error("WS: extra head recv failed");
            ws_close_socket(cnum);
            return (-1);
        }
        bytes_remaining = (wshead[2] << 8) + wshead[3];
        memcpy(mask, wshead + 4, 4);
    } else
        memcpy(mask, wshead + 2, 4);
    *opcode = wshead[0] & 0x0F;

    if (*opcode != OP_BINARY && *opcode != OP_CLOSE
        && *opcode != OP_PING && *opcode != OP_PONG
    ) {
        ws_error("WS: Received unsupported WebSocket opcode=%02X.", *opcode);
        return (-1);
    }
    if (state[cnum] == WS_CLOSING && *opcode != OP_CLOSE) {
        // A WebSocket close notification has been sent, but a responding close
        // frame has not yet been received from the client.  Ignore any other
        // frame.
        ws_error("WS: Received other than a close frame from client after a close frame was sent.  Discarding.");
        return (0);
    }

    // If there is a payload, read and unmask it
    if (bytes_remaining > 0) {
        // bytes_read is redefined to mean the number of bytes of the payload read.
        if((bytes_read = recv(
                pfds[cnum].fd,
                (char*)data,
                data_size > bytes_remaining ? bytes_remaining : data_size,
                0
            )) == 0)
        {
            ws_close_socket(cnum);
            ws_error("WS: payload recv failed");
            return (-1);
        }
        bytes_remaining -= bytes_read;
        // unmask payload
        for (int i=0; i<bytes_read; i++)
            data[i] ^= mask[i % 4];
    }
    return (bytes_read);
};


// Process WebSocket control frames (close, ping, pong)
// Return 0 on success, -1 on failure.
int process_ws_cframe(char *data, int data_len, int opcode) {
    if(opcode == OP_CLOSE) {
        if (state[cnum] != WS_CLOSING) {
            ws_debug("WS: Received close frame from client.  Responding.");
            unsigned short close_status = 1000;  // Default 1000 (normal closure)
            if (data_len >= 2)
                // mirror the close_status from received close frame
                close_status = (data[0] << 8) + data[1];
            // No need to check success of ws_close()
            ws_close(cnum, close_status);
        } else
            ws_debug("WS: Received close confirmation frame.");
        ws_close_socket(cnum);
        return (0);
    }

    if(opcode == OP_PONG) {
        // Sending of ping is not currently supported, so don't expect pong.
        // However, the client may send pong without having receieved a ping.
        ws_debug("WS: Received pong frame from client %i (do nothing).", cnum);
        return (0);
    }

    if(opcode == OP_PING) {
        char frh[SHEADSIZE];
        int frhlen;
        ws_debug("WS: Received ping frame from client %i.  Respond with pong.", cnum);
        // If ping contained a payload (data), include the same data in the pong.
        if (ws_build_header(OP_PONG, data_len, frh, &frhlen) == -1) {
            ws_error("WS: Error creating PONG header.");
            return (-1);
        }
        if (send_to_socket(cnum, frh, frhlen) == -1) {
            ws_error("WS: Error sending PONG frame header.");
            return (-1);
        }
        if (data_len && send_to_socket(cnum, data, data_len) == -1) {
            ws_error("WS: Error sending PONG frame payload.");
            return (-1);
        }
        return (0);
    }

    // This should not be possible
    ws_error("WS: Logic Error.  Unknown fr.opcode = %d.  Closing connection.", opcode);
    ws_close_socket(cnum);
    return (-1);
}


// Return value is the length of the received payload or less than 0 if an
// error occured.  On return, if return value is not less than 0, then *data
// is the payload data from the received frame.  If the data read is a
// WebSocket control frame (Close, Ping, or Pong) that has no data, or it
// is a valid and expected http request, then return 0.  This occurs for
// an http upgrade request, an http GET for a recognized uri.
// data_size is the maximum number of bytes that can be stored in *data.  If the
// payload of a binary frame is larger than data_size, then the return value
// will be greater than data_size, but *data will contain only the first data_size
// bytes, and any remaining bytes will have been discarded.  This behavior
// is modeled after the stdio function snprintf().
// The caller of client_handler() must decide how to proceed when an
// oversized frame is received.  Closing the WebSocket connection with
// status code 1009 (message too big to process) is one option.
//
// TODO: Handle processing of frames with payloads larger than data_size?
//       Perhaps this could be done by buffering the data and returning
//       more on the next call as if it were sent in fragments?
int client_handler(char *data, int data_size)
{
    int data_len;
    int opcode;
    if (state[cnum] == WS_HTTP)
        return process_http_req(cnum);

    if ((data_len = read_websocket_frame(data, data_size, &opcode)) < 0)
        return (-1);

    if (opcode == OP_BINARY || opcode == OP_CONTINUATION)
        return (data_len);

    return process_ws_cframe(data, data_len, opcode);
}


// The following socket (not WebSocket) related stuff is based on
// pollserver.c from Beej’s Guide to Network Programming Using Internet
// Sockets by Brian “Beej Jorgensen” Hall v3.1.11, Copyright © April 8,
// 2023. https://beej.us/guide/bgnet.  Per that document, "The C source
// code presented in this document is hereby granted to the public
// domain, and is completely free of any license restriction."
//
// Information about open sockets are kept in two arrays.
// WSAPOLLFD pfds[ws_max_clients] has the structure required for WSAPoll()
// state[ws_max_clients] carries information about socket state
// Both of these have a fixed (and equal) size, and use a common index.
// Unused entries in pfds are indicated by a fd=INVALID_SOCKET
// Unused entries in state are indicated by WS_UNUSED (though this is redundant)
// pfds[0] and state[0] are always used for the master listener
// socket

// Get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Get port, IPv4 or IPv6:
unsigned short get_in_port(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
        return (((struct sockaddr_in*)sa)->sin_port);
    return (((struct sockaddr_in6*)sa)->sin6_port);
}

// Return a listening socket
SOCKET get_listener_socket(void)
{
    int listener;     // Listening socket descriptor
    char yes=1;        // For setsockopt() SO_REUSEADDR, below
    int rv;
    struct addrinfo hints, *ai, *p;

    // Get us a socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;  // AF_UNSPEC would allow ipv4 or ipv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, ws_port_str, &hints, &ai)) != 0) {
        // TODO:  While gai_strerror() is available for WIN32, the
        // Microsoft documentation recommends using WSAGetLastError()
        // instead.
        ws_error("WS ERROR: %s\n", gai_strerror(rv));
        return (-1);
    }

    for(p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0)
            continue;
        // Set listener socket to be non-blocking
#ifdef WIN32
        unsigned long mode = 1;
        ioctlsocket(listener, FIONBIO, &mode);
#else
        fcntl(listener, F_SETFL, O_NONBLOCK);
#endif
        // Lose the pesky "address already in use" error message
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            closesocket(listener);
            continue;
        }
        break;
    }
    if (p == NULL) {
        ws_error("WS: Bind failure.");
        return (-1);  // bind failure
    }
    freeaddrinfo(ai);

    if (listen(listener, 10) == -1)
        return (-1);
    return (listener);
}


int open_new_connection() {
    SOCKET newfd;        // Newly accept()ed socket descriptor
    struct sockaddr_storage remoteaddr; // Client address
    socklen_t addrlen;
    char remoteIP[INET6_ADDRSTRLEN];

    addrlen = sizeof remoteaddr;
    newfd = accept(pfds[0].fd,
        (struct sockaddr *)&remoteaddr,
        &addrlen);

    if (newfd == INVALID_SOCKET) {
        ws_error("WS: accept() failed");
        return (-1);
    } else {
        // find an unused cnum
        unsigned int j;
        for (j = 1; j < ws_max_clients; j++)
            // cnum=0 is the listener, so start search at 1
            if (pfds[j].fd == INVALID_SOCKET)
                break;
         if (j == ws_max_clients) {
             ws_error("WS: Don't accept new connection.  Limit reached.");
             closesocket(newfd);
             return (-1);
         }
         pfds[j].fd = newfd;
         pfds[j].events = POLLIN;
         state[j] = WS_HTTP;  // Set to accept http traffic

         ws_debug("WS: New connection (%d) from %s port %hu",
             j,  // cnum of new connection
             inet_ntop(remoteaddr.ss_family,
                 get_in_addr((struct sockaddr*)&remoteaddr),
                 remoteIP, INET6_ADDRSTRLEN),
             get_in_port((struct sockaddr*)&remoteaddr)
         );
         // set new socket to be non-blocking
#ifdef WIN32
         unsigned long mode = 1;
         ioctlsocket(newfd, FIONBIO, &mode);
#else
         fcntl(newfd, F_SETFL, O_NONBLOCK);
#endif
    }
    return (0);
}

// Return 0 on success, -1 on failure, 1 if already called (succecssfully)
int ws_init() {
    if (ws_listening) {
        ws_debug("WS: Duplicate call to ws_init() ignored.");
        return (1);
    }

    state = malloc((ws_max_clients + 1) * sizeof(int));
    if (state == NULL) {
        ws_error("WS: ws_init() failed to allocate memory for state array.");
        return (-1);
    }
    pfds = malloc((ws_max_clients + 1) * sizeof(WSAPOLLFD));
    if (pfds == NULL) {
        ws_error("WS: ws_init() failed to allocate memory for pfds array.");
        return (-1);
    }

#ifdef WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        // TODO: Show more specific error information?
        ws_error("WS: WSAStartup failed.");
        return (-1);
    }
    // TODO: Is 2.2 actually required?
    if (LOBYTE(wsaData.wVersion) != 2 ||
        HIBYTE(wsaData.wVersion) != 2
    ) {
        ws_error("WS: Versiion 2.2 of Winsock is not available.");
        WSACleanup();
        return (-1);
    }
#endif
    // initialize state[] and pfds[]
    for(unsigned int i = 0; i < ws_max_clients; i++) {
        state[i] = WS_UNUSED;
        pfds[i].fd = INVALID_SOCKET;
        pfds[i].events = 0;
    }

    // Set up and get a listening socket
    pfds[0].fd = get_listener_socket();
    if (pfds[0].fd == INVALID_SOCKET) {
        ws_error("WS ERROR: Unable to get listening socket");
        return (-1);
    }
    pfds[0].events = POLLIN; // Report ready to read on incoming connection
    state[0] = WS_LISTENER;
    ws_listening = true;
    return (0);
}


// ws_poll() must not block (always return quickly).
// Return value is the length of the received data which may be zero,  or
// less than 0 if an error occured.  On return, if return value is greater
// than 0, then *cnum identifies which client sent the data and *data
// contains that data sent by that client.
// data_size is the maximum number of bytes that can be stored in *data.  If
// more than data_size bytes are available, only data_size bytes will be
// read into *data.  Subsequent calls to ws_poll() will then return
// additional data from that same client until all of the available data has
// been returned.  Only then will ws_poll() check for available data from
// any client.
// ws_poll() will never return data from more than one WebSocket
// frame/fragment.  However, a single frame/fragment may contain some or all
// of more than one message sent by a single client.
// bytes_remaining, last_cnum, and mask are used to store information about
// a partially read frame.

// This is one cycle of the main loop in pollserver.c
int ws_poll(int *r_cnum, char *data, int data_size) {
    if (!ws_listening) {
        ws_error("WS ERROR: ws_init() must be called before ws_poll().");
        return (-1);
    }
    if (bytes_remaining != 0)
    {
        int bytes_read;
        if (state[cnum] != WS_ACTIVE) {
            // This socket is no longer open
            bytes_remaining = 0;
            return (0);
        }
        *r_cnum = cnum;
        if((bytes_read = recv(
                pfds[cnum].fd,
                (char*)data,
                data_size > bytes_remaining ? bytes_remaining : data_size,
                0
            )) == 0)
        {
            ws_close_socket(cnum);
            ws_error("WS: payload recv failed");
            return (-1);
        }
        bytes_remaining -= bytes_read;
        // unmask payload
        for (int i=0; i<bytes_read; i++)
            data[i] ^= mask[i % 4];
        return (bytes_read);
    }
    // Set timeout to 0 to not block
    int poll_count = WSAPoll(pfds, ws_max_clients, 0);
    if (poll_count == -1) {
        ws_error("WS: WSAPoll() failed");
        return (-1);
    }
    if (poll_count == 0)
        return (0);

    // Run through the existing connections looking for data to read
    // Always return after reading data from a single socket.  If a
    // large number of active clients were supported, this might be
    // a problem because if low numbered clients were very active,
    // then high numbered clients might never get read.  However, this
    // shouldn't be a problem for a small number of clients unless so
    // ws_poll() is not being called frequenctly enough to keep up
    // data being sent by the clients, in which case behavior would be
    // poor anyway.
    // Only check for new connections if none of the currently open
    // connections has data waiting.
    for(cnum = 1; cnum < (int) ws_max_clients; cnum++) {
        if (pfds[cnum].revents & POLLIN) {
            // Found a socket with data ready to read
            *r_cnum = cnum;
            return client_handler(data, data_size);
        }
    }
    // No currently open sockets had data read to read.
    // Check for new connections
    if (pfds[0].revents & POLLIN)
        return open_new_connection();
    return (0);
}
