/*****************************************************************************/

/*
 *      audioio.h  --  Internal audioio data structures.
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

#ifndef _AUDIOIO_H
#define _AUDIOIO_H

/* ---------------------------------------------------------------------- */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "modem.h"

/* ---------------------------------------------------------------------- */

struct audioio {
	void (*release)(struct audioio *audioio);
	void (*terminateread)(struct audioio *audioio);
	void (*transmitstart)(struct audioio *audioio);
	void (*transmitstop)(struct audioio *audioio);
	void (*write)(struct audioio *audioio, const int16_t *samples, unsigned int nr);
	void (*read)(struct audioio *audioio, int16_t *samples, unsigned int nr, u_int16_t tim);
	u_int16_t (*curtime)(struct audioio *audioio);
};

/* "private" audio IO functions */
extern struct modemparams ioparams_soundcard[];
extern struct modemparams ioparams_alsasoundcard[];
extern struct modemparams ioparams_filein[];
extern struct modemparams ioparams_sim[];

extern void ioinit_soundcard(void);
extern void ioinit_alsasoundcard(void);
extern void ioinit_filein(void);
extern void ioinit_sim(void);

#define IO_RDONLY   1
#define IO_WRONLY   2
#define IO_RDWR     (IO_RDONLY|IO_WRONLY)

extern struct audioio *ioopen_soundcard(unsigned int *samplerate, unsigned int flags, const char *params[]);
extern struct audioio *ioopen_alsasoundcard(unsigned int *samplerate, unsigned int flags, const char *params[]);
extern struct audioio *ioopen_filein(unsigned int *samplerate, unsigned int flags, const char *params[]);
extern struct audioio *ioopen_sim(unsigned int *samplerate, unsigned int flags, const char *params[]);

/* ---------------------------------------------------------------------- */
#endif /* _AUDIOIO_H */
