/*****************************************************************************/

/*
 *      soundio.h  --  Internal data structures.
 *
 *      Copyright (C) 2000
 *        Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*****************************************************************************/

#ifndef _SOUNDIO_H
#define _SOUNDIO_H

/* ---------------------------------------------------------------------- */

#include "modem.h"
#include "kisspkt.h"
#include "pttio.h"
#include "audioio.h"

#ifdef WIN32
#include <windows.h>
#else
#define VOID void
#endif
#define pthread_t unsigned int


/* ---------------------------------------------------------------------- */

struct state {
	struct modemchannel *channels;
	struct state *next;
	struct audioio *audioio;

	struct chacc chacc;

	struct pttio ptt;

};

struct modemchannel
{
	struct modemchannel *next;
	struct state *state;
	struct modulator *mod;
	struct demodulator *demod;
	void *modstate;
	void *demodstate;
	unsigned int rxbitrate;
	pthread_t rxthread;
	struct kisspkt pkt;
};

extern struct state state;

/* ---------------------------------------------------------------------- */

extern struct modemparams pktkissparams[];
extern void pktinit(struct modemchannel *chan);
extern void pktrelease(struct modemchannel *chan);
extern void pkttransmitloop(struct state *state);

//extern int snprintpkt(char *buf, size_t sz, const u_int8_t *pkt, unsigned len);

extern void logrelease(void);
extern void loginit(unsigned int vl, unsigned int tosysl);

extern struct modulator *modchain;
extern struct demodulator *demodchain;

/* ---------------------------------------------------------------------- */
#endif /* _SOUNDIO_H */
