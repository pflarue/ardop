/*****************************************************************************/

/*
 *      kisspkt.h  --  Internal kisspkt data structures.
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

#ifndef _KISSPKT_H
#define _KISSPKT_H

/* ---------------------------------------------------------------------- */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* ---------------------------------------------------------------------- */

#define MAXFLEN           512U
#define RXBUFFER_SIZE     ((MAXFLEN*6U/5U)+8U)
#define TXBUFFER_SIZE     4096U  /* must be a power of 2 and >= MAXFLEN*6/5+8; NOTE: in words */

#define KISSINBUF_SIZE    (2*MAXFLEN+8)

#define IFNAMELEN 128

/* ---------------------------------------------------------------------- */

struct chacc {
	unsigned int txdelay;
	unsigned int ppersist;
	unsigned int slottime;
	unsigned int fullduplex;
	unsigned int txtail;
};

struct kisspkt {
	unsigned int dcd;
	unsigned int inhibittx;

	struct {
		unsigned rd, wr, txend;
		unsigned char buf[TXBUFFER_SIZE];
	} htx;
	
	struct {
		unsigned int bitbuf, bitstream, numbits, state;
		unsigned char *bufptr;
		int bufcnt;
		unsigned char buf[RXBUFFER_SIZE];
	} hrx;

	struct {
		int fd, fdmaster, ioerr;
		unsigned iframelen, ibufptr;
		char ifname[IFNAMELEN];
		unsigned char ibuf[KISSINBUF_SIZE];
	} kiss;

	struct {
		unsigned int kiss_in;
		unsigned int kiss_inerr;
		unsigned int kiss_out;
		unsigned int kiss_outerr;
		unsigned int pkt_in;
		unsigned int pkt_out;
	} stat;
};

/* ---------------------------------------------------------------------- */
#endif /* _KISSPKT_H */
