/*****************************************************************************/

/*
 *      pttio.h  --  Internal PTT input/output data structures and routines.
 *
 *      Copyright (C) 2000, 2014
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

#ifndef _PTTIO_H
#define _PTTIO_H

/* ---------------------------------------------------------------------- */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef WIN32
#include <windows.h>
#endif

/* ---------------------------------------------------------------------- */

#ifdef WIN32

struct pttio {
	HANDLE h;
	unsigned int ptt;
	unsigned int dcd;
};

#else

#ifdef HAVE_LIBHAMLIB
#include <hamlib/rig.h>
#endif

struct pttio {
	enum { noport, serport, parport, hamlibport, cm108, sysfsgpio } mode;
	unsigned int ptt;
	unsigned int dcd;
	unsigned int gpio;
	
	union {
		int fd;
#ifdef HAVE_LIBHAMLIB
		RIG *rig_ptr;
#endif
	} u;
};

#endif

/* ---------------------------------------------------------------------- */

extern struct modemparams pttparams[];
extern int pttinit(struct pttio *state, const char *params[]);
extern void pttsetptt(struct pttio *state, int pttx);
extern void pttsetdcd(struct pttio *state, int dcd);
extern void pttrelease(struct pttio *state);

/* ---------------------------------------------------------------------- */
#endif /* _PTTIO_H */
