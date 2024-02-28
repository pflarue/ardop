/*****************************************************************************/

/*
 *      modem.h  --  Defines for the modem.
 *
 *      Copyright (C) 1999-2015  Thomas Sailer (sailer@ife.ee.ethz.ch)
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
 *  Please note that the GPL allows you to use the driver, NOT the radio.
 *  In order to use the radio, you need a license from the communications
 *  authority of your country.
 *
 */

/*****************************************************************************/

#ifndef _MODEM_H
#define _MODEM_H

/* ---------------------------------------------------------------------- */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <stdarg.h>

#define int8_t char
#define u_int8_t unsigned char

#define int16_t short
#define u_int16_t unsigned short

#define int32_t int
#define u_int32_t unsigned int

#define only_inline 
#define only_inline 

#define alloca _alloca
struct modemchannel;

extern void audiowrite(struct modemchannel *chan, const int16_t *samples, unsigned int nr);
extern void audioread(struct modemchannel *chan, int16_t *samples, unsigned int nr, u_int16_t tim);
extern u_int16_t audiocurtime(struct modemchannel *chan);

extern int pktget(struct modemchannel *chan, unsigned char *data, unsigned int len);
extern void pktput(struct modemchannel *chan, const unsigned char *data, unsigned int len);
extern void pktsetdcd(struct modemchannel *chan, int dcd);

extern void p3dreceive(struct modemchannel *chan, const unsigned char *pkt, u_int16_t crc);
extern void p3drxstate(struct modemchannel *chan, unsigned int synced, unsigned int carrierfreq);

#define MLOG_FATAL    0
#define MLOG_ERROR    1
#define MLOG_WARNING  2
#define MLOG_NOTICE   3
#define MLOG_INFO     4
#define MLOG_DEBUG    5

extern void logvprintf(unsigned int level, const char *fmt, va_list args);
extern void logprintf(unsigned int level, const char *fmt, ...);
extern void logerr(unsigned int level, const char *st);
extern unsigned int log_verblevel;
void WriteDebugLog(int LogLevel, const char * format, ...);
void SampleSink(short Sample);

#define MODEMPAR_STRING      0
#define MODEMPAR_COMBO       1
#define MODEMPAR_NUMERIC     2
#define MODEMPAR_CHECKBUTTON 3

struct modemparams {
	const char *name;
	const char *label;
	const char *tooltip;
	const char *dflt;
	unsigned int type;
	union {
		struct {
			float min;
			float max;
			float step;
			float pagestep;
		} n;
		struct {
			const char *combostr[8];
		} c;
	} u;
};

struct modulator {
	struct modulator *next;
	const char *name;
	const struct modemparams *params;
	void *(*config)(struct modemchannel *chan, unsigned int *samplerate, int P1, int P2, int P3);
	void (*init)(void *, unsigned int samplerate);
	void (*modulate)(void *, unsigned int txdelay);
	void (*free)(void *);
};

struct demodulator {
	struct demodulator *next;
	const char *name;
	const struct modemparams *params;
	void *(*config)(struct modemchannel *chan, unsigned int *samplerate, int P1, int P2, int P3);
	void (*init)(void *, unsigned int samplerate, unsigned int *bitrate);
	void (*demodulate)(void *);
	void (*free)(void *);
};	

/* ---------------------------------------------------------------------- */

extern struct modulator afskmodulator;
extern struct demodulator afskdemodulator;

extern struct modulator fskmodulator;
extern struct demodulator fskdemodulator;
extern struct demodulator fskpspdemodulator;
extern struct demodulator fskeqdemodulator;

extern struct modulator pammodulator;
extern struct demodulator pamdemodulator;

extern struct modulator pskmodulator;
extern struct demodulator pskdemodulator;

extern struct modulator newqpskmodulator;
extern struct demodulator newqpskdemodulator;

extern struct modulator p3dmodulator;
extern struct demodulator p3ddemodulator;

/* ---------------------------------------------------------------------- */
#endif /* _MODEM_H */
