//
// KISS Code for ARDOPC. 
//
//	Allows Packet modes and ARDOP to coexist in same program
//	Mainly for Teensy Version.
//
//	Teensy will probably only support KISS over i2c,
//	but for testing Windows version uses a real com port

//	New idea is to support via SCS Host Channel 250, but will
//	probably leave serial/i2c support in


#ifdef WIN32
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>
#include <winioctl.h>
#else
#define HANDLE int
#endif

#include "ARDOPC.h"

#ifdef USE_DIREWOLF

#include "direwolf/fsk_demod_state.h"

struct demodulator_state_s Demodstate;

void demod_afsk_init (int samples_per_sec, int baud, int mark_freq,
			int space_freq, char profile, struct demodulator_state_s *D);
void demod_afsk_process_sample (int chan, int subchan, int sam, struct demodulator_state_s *D);

#endif

#include "soundio.h"

void DemodAFSKinit(void *state);

extern UCHAR KISSBUFFER[500]; // Long enough for stuffed KISS frame
extern int KISSLength;


VOID EncodePacket(UCHAR * Data, int Len);
VOID AddTxByteDirect(UCHAR Byte);
VOID AddTxByteStuffed(UCHAR Byte);
unsigned short int compute_crc(unsigned char *buf,int len);
void PacketStartTX();
BOOL GetNextKISSFrame();
VOID SendAckModeAck();
void demod_afsk_init (int samples_per_sec, int baud, int mark_freq,
			int space_freq, char profile, struct demodulator_state_s *D);

void DemodAFSK(short * buffer, int count);

extern int TXDelay;	

// FSK Params

int Samplerate = 12000;
int Baudrate = 300;
int centreFreq = 2000;
int CarFreqLo = 1900;
int CarFreqHi = 2100;

BOOL AFSK = TRUE;
BOOL FSK = FALSE;

#ifdef USE_SOUNDMODEM
struct demodulator *demodchain = &afskdemodulator;
#endif

float dblAngle;		// Angle in radians
float dblCarPhaseInc[2]; 

extern float dbl2Pi;

float NCOPhase = 0;
float NCOFreq;
float NCOPhaseInc;

int TXBit = 0;		// Current bit for NRZI

BOOL afskInit()
{
#ifdef USE_SOUNDMODEM
	struct modemchannel *chan;
	int sr;
	int P1 = 0;
	int P2 = 0;
	int P3 = 0;
	int P4 = 0;
#endif

	dblCarPhaseInc[0] = 2 * M_PI * CarFreqLo / Samplerate;
	dblCarPhaseInc[1] = 2 * M_PI * CarFreqHi / Samplerate;

#ifdef USE_DIREWOLF
	demod_afsk_init (Samplerate, Baudrate, CarFreqLo, CarFreqHi, 'A', &Demodstate);
#endif
	
#ifdef USE_SOUNDMODEM

	// using Soundmodem code

	afskdemodulator.next = NULL;

// Set up single channel

	if (!(chan = malloc(sizeof(struct modemchannel))))
		WriteDebugLog(MLOG_FATAL, "out of memory\n");

	memset(chan, 0, sizeof(struct modemchannel));
	chan->next = state.channels;
	chan->state = &state;

	if (AFSK)
	{
		P1 = Baudrate;

		if (Baudrate == 300)
		{
			P2 = centreFreq - 100;
			P3 = centreFreq + 100;
		}
		else if (Baudrate == 1200)
		{
			P2 = centreFreq - 500;
			P3 = centreFreq + 500;
		}
		else
		{
			P2 = 500;
			P3 = 2900;
			P2 = centreFreq - 1200;
			P3 = centreFreq + 1200;
		}
		chan->demod = &afskdemodulator;
	}

	chan->demodstate = NULL;

	pktinit(chan);

 	chan->demodstate = chan->demod->config(chan, &sr, P1, P2, P3);

	state.channels = chan;

	// if 300 add more channels

	if (Baudrate == 300)
	{
		if (0)
		{
			if (!(chan = malloc(sizeof(struct modemchannel))))
				WriteDebugLog(MLOG_FATAL, "out of memory\n");

			memset(chan, 0, sizeof(struct modemchannel));
			chan->next = state.channels;
			chan->state = &state;

			chan->demod = &afskdemodulator;
			chan->demodstate = NULL;

			pktinit(chan);

		 	chan->demodstate = chan->demod->config(chan, &sr, P1, P2, P3);
			state.channels = chan;
		}
	}

	for (chan = state.channels; chan; chan = chan->next)
	{
		if (chan->demod)
			chan->demod->init(chan->demodstate, Samplerate, &chan->rxbitrate);
  
		DemodAFSKinit(chan->demodstate);		// G8BPQ 
	}

#endif	

	WriteDebugLog(LOGALERT, "Packet interface Initialised Speed %d Center Freq %d", Baudrate, centreFreq); 

	return TRUE;
}

// Packet Transmit code. Called when CSMA says ok to send

void PacketStartTX()
{
	// Key PTT and send TXDelay

	int TXDFlags = (TXDelay * Baudrate) / 8000;	// Character times
	int n;

	if (GetNextKISSFrame() == FALSE)
		return;			// nothing to send
	
	// Have a Data Frame, so start sending

	dblCarPhaseInc[0] = 2 * M_PI * CarFreqLo / Samplerate;
	dblCarPhaseInc[1] = 2 * M_PI * CarFreqHi / Samplerate;

	initFilter(500, centreFreq);			// Raises PTT

	for (n = 0; n < TXDFlags; n++)
	{
		AddTxByteDirect(0x7e);
	}


	while (TRUE)				// loop till we run out of packets
	{
		WriteDebugLog(LOGALERT, "Sending Packet Frame Len %d", KISSLength); 
	
		switch(KISSBUFFER[0])
		{
		case 0:			// Normal Data

			EncodePacket(KISSBUFFER + 1, KISSLength - 1);
			break;

		case 12:
		
		// Ackmode frame. Return ACK Bytes (first 2) to host when TX complete

			EncodePacket(KISSBUFFER + 3, KISSLength - 3);

			// Returns when Complete so can send ACK

			SendAckModeAck();
			break;
		}

		// See if any more

		if (GetNextKISSFrame() == FALSE)
			break;			// no more to send
	}

	// EncodePacket adds a starting flag, but not an ending one, so successive packets can be sent with a single flag 
	// between them. So at end add a teminating flag and a pad to give txtail

	AddTxByteDirect(0x7e);	// End Flag - add without stuffing
	AddTxByteDirect(0xff);	// Tail Padding
	AddTxByteDirect(0xff);	// Tail Padding

	SoundFlush();			// Drops PTT

}

// HDLC Encode Code


TXOneBits = 0;



VOID EncodePacket(UCHAR * Data, int Len)
{
	unsigned short CRC = compute_crc(Data, Len);
	UCHAR * Msg = Data;
	UCHAR * Msgend = Data + Len;

	AddTxByteDirect(0x7e);			// Start Flag - add without stuffing

	while (Msg < Msgend)
	{
		AddTxByteStuffed(*(Msg++));	// Send byte with stuffing
	}

	CRC ^= 0xffff;

	AddTxByteStuffed(CRC & 0xFF);	//Low Byte first
	AddTxByteStuffed(CRC >> 8);

//	AddTxByteDirect(0x7e);	// End Flag - add without stuffing
//	AddTxByteDirect(0x7e);	// End Flag - add without stuffing
}

VOID AddTXBit(UCHAR Bit)
{
	int n = 0;
	float sample;
	
	int intSampPerSym = 12000 / Baudrate; // Sample Rate/Baud Rate

	if (Bit)
		TXOneBits++;
	else
	{
		TXOneBits = 0;
		TXBit ^= 1;			// Invert
	}

	// Send the bit to the soundcard

	for (n = 0; n < intSampPerSym; n++)	
	{
		sample = 26000 * sinf(dblAngle);
		dblAngle += dblCarPhaseInc[TXBit];
		if (dblAngle >= 2 * M_PI)
			dblAngle -= 2 * M_PI;
	
		SampleSink(sample);
	}
}

VOID AddTxByteDirect(UCHAR Byte)				// Add unstuffed byte to output
{
	int i;
	UCHAR Data = Byte;

	for (i = 0; i < 8; i++)
	{
		AddTXBit(Byte & 1);
		Byte >>= 1;
	}
}

VOID AddTxByteStuffed(UCHAR Byte)				// Add unstuffed byte to output
{
	int i;
	UCHAR Data = Byte;

	for (i = 0; i < 8; i++)
	{
		AddTXBit(Byte & 1);
		Byte >>= 1;

		if (TXOneBits == 5)
			AddTXBit(0);
	}
}


// Packet Decode
	int active = 0;

void LookforPacket(float * dblMag, float dblMagAvg, int count, float * real, float * imag)
{
	float peak1 = 0.0f;
	float peak2 = 0.0f;
	float Quinn = 0.0f;
	int i, ip1, ip2 = 0;
	float avg = dblMagAvg / count;
	
	float peakfreq1, peakfreq2;

	struct tm * tm;
	char Stamp[20];
	time_t LT;
	LT = time(NULL);
	tm = gmtime(&LT);

	return;


	// This receives magnitute and real/imsg components from a 1024 
	// point FFT of the 1200 samples at 12000 sample rate.

	// Each bin is 12000/1024 = 11.71875 Hz wide
	// We get 206 bins starting at 25 (approx 300 to 2700 Hz)
	
	sprintf(Stamp,"%02d%02d%02d %02d:%02d:%02d ",
				tm->tm_year-100, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);



	dblMagAvg /= count;

	for (i = 119; i < 163; i++)
	{
		if (dblMag[i] > peak1)
		{
			ip1 = i;
			peak1 = dblMag[i];
		}
	}

	// look for next highest
	
	for (i = 119; i < 163; i++)
	{
		if (dblMag[i] > peak2 && (i > ip1 + 4 || i < ip1 - 4))
		{
			ip2 = i;
			peak2 = dblMag[i];
		}
	}


	peakfreq1 = ip1 * 11.71875f + 300.0f;
	peakfreq2 = ip2 * 11.71875f + 300.0f;

	if ((dblMag[ip1] / avg) > 10.0)
	{
		if (active == 0)
		{
			// New signal. See if it looks like packet

			if (peakfreq1 > peakfreq2)
			{
				// Could be hi tone 

				if ((peakfreq1 > (CarFreqHi - 100)) && (peakfreq1 < (CarFreqHi + 100)))
				{
					// Reasonable tones

					// Try running Quinn

//					Quinn = QuinnSpectralPeakLocator(real[ip1 - 1], imag[ip1 - 1], real[ip1], imag[ip1], real[ip1 + 1], imag[ip1 + 1]);

					WriteDebugLog(LOGALERT, "Could be packet, centre freq = %f Quinn %f %f %f %f\n", peakfreq1 - 100, Quinn, dblMag[ip1 - 1] / avg, dblMag[ip1] / avg, dblMag[ip1 + 1] / avg);
				}
			}
			else
			{
				// Could be lo tone 

				if ((peakfreq1 > (CarFreqLo - 100)) && (peakfreq1 < (CarFreqLo + 100)))
				{
					// Reasonable tones

					// Try running Quinn

//					Quinn = QuinnSpectralPeakLocator(real[ip1 - 1], imag[ip1 - 1], real[ip1], imag[ip1], real[ip1 + 1], imag[ip1 + 1]);

					WriteDebugLog(LOGALERT, "Could be packet, centre freq = %f Quinn %f %f %f %f\n", peakfreq1 + 100, Quinn, dblMag[ip1 - 1] / avg, dblMag[ip1] / avg, dblMag[ip1 + 1] / avg);
				}
			}
		
		}

		active = 1;
		WriteDebugLog(LOGALERT, "%s %f %f %f %f %d", Stamp, peakfreq1, peakfreq2, dblMag[ip1] / avg, dblMag[ip2] / avg, active);
	}
	else 
	{
		if (active == 1)
			WriteDebugLog(LOGALERT, "%s %f %f %f %f %d", Stamp, peakfreq1, peakfreq2, dblMag[ip1] / avg, dblMag[ip2] / avg, 0);
		active = 0;
	}
}

void packet_process_samples(short * Samples, int count)
{
#ifdef USE_DIREWOLF
	int i;
	
	for (i = 0; i < 1199; i++)
		demod_afsk_process_sample (1, 1, Samples[i], &Demodstate);
#else

#endif
}

// Soundmodem interface routines




unsigned short audiocurtime(struct modemchannel *chan)
{
	struct audioio *audioio = chan->state->audioio;

	if (!audioio || !audioio->curtime)
		return 0;
	
	return audioio->curtime(audioio);
}

int logcheck(int x)
{
	return 1;
}

#ifdef WIN32

UCHAR EEPROM[32] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 'A'};

void SaveEEPROM(int reg, int val)
{
	EEPROM[reg] = val;
}
int GetEEPROM(int Reg)
{
	return EEPROM[Reg];
}
#endif
