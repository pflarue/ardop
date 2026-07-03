// embed_internal.h - internal glue between the embedded host shim and facade
//
// These functions connect host_shim.c (which provides the TCP* symbols the
// modem core links against) to ardop_lib.c (which owns the inbound queue and
// the application callbacks).  All of them run on the modem thread.

#ifndef ARDOP_EMBED_INTERNAL_H
#define ARDOP_EMBED_INTERNAL_H

// Drain the inbound command/data queue into the modem core.  Called from the
// host shim's TCPHostPoll() on the modem thread, so it runs with the same
// timing and threading context the TCP data path used to.
void ardop_embed_drain_inbound(void);

// Deliver a command-channel event line to the registered on_event callback.
// Called from the host shim's TCPSend*ToHost() functions.
void ardop_embed_emit_event(const char *line);

// Deliver received application data to the registered on_data callback.
// tag is the 3-byte type tag ("ARQ", "FEC", "ERR", "IDF").
void ardop_embed_emit_data(const char *tag, const unsigned char *data, int len);

#endif  // ARDOP_EMBED_INTERNAL_H
