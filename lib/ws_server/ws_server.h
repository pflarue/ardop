#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// function handle required for output handlers
// ws_set_debug and ws_set_error
typedef void (*ws_vprintf_fcn)(const char*, va_list);

// ws_set_max_clients(), ws_set_port(), and ws_set_uri() are
// optional, but will be ignored after ws_init()
int ws_set_max_clients(unsigned int max_clients);  // override max_clients=10
int ws_set_port(int port);  // override default port=8088
int ws_set_uri(char *uri);  // override default uri="/ws"

// ws_set_error() and ws_set_debug() may be used to provide
// callback functions to redirect error and debug messages.
// These may be called at any time.
void ws_set_error(ws_vprintf_fcn handler);
void ws_set_debug(ws_vprintf_fcn handler);

// ws_set_close_notify() may be used to provide a callback function
// to be called whenever an active WebSocket connection closes.
void ws_set_close_notify(void (*callback)(const int cnum));

// ws_add_httptarget_file() and ws_add_httptarget_data() may be used to set a
// small number of uris which may be served as http resources (html. js.
// css, etc.).
int ws_add_httptarget_file(char *uri, char *pathname, char *contenttype);
int ws_add_httptarget_data(char *uri, char *data, size_t data_len, char*contenttype);

// ws_init() must be called before ws_poll(), ws_send(),
// or ws_close().
// Return 0 on success, -1 on failure, 1 if already called (succecssfully)
int ws_init();

// After init_ws(), ws_poll() shall be called repeatedly to
// check for recieved data.
int ws_poll(int *client_num, char *data, int data_size);

// After init_ws(), ws_send() may be used to send binary data
// to one or more connected WebSocket clients, and those
// connections may be closed with ws_close().
//
// For ws_send() and ws_close():
//   If cnum > 0, send to only that client
//   If cnum == 0, multicast to all clients
//   If cnum < 0, multicast to all but -cnum
// return the number of clients the data was successfully sent to.
int ws_send(int client_num, const char *data, int data_len);
int ws_close(int client_num, unsigned short status);

// ws_get_client_count() may be used at any time to get the total
// number of currently connected WebSocket clients.
// If bitmap is not NULL, then on return *bitmap indicates which of the
// first 32 client_num values currently represent open WebSocket client
// connections.  If ((*bitmap >> (n - 1)) & 0x01) is 1, then client_num = n
// is an active WebSocket connection for 1 <= n <= 32.
// This function does not provide a way to check the status of individual
// clients numbered greater than 32, which can occur if ws_set_max_clients()
// was used to set max_clients > 32.  However, in this case the return value
// will still be an accurate count of the total number of currently
// connected WebSocket clients.
// Before ws_init(), return 0, with *bitmap=0 if bitmap is not NULL.
int ws_get_client_count(uint32_t *bitmap);
