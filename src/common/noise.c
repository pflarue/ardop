#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

short next_grand_s16 = 0;
bool next_grand_s16_avail = false;

// Recall that approximatelye 99.7% of values will fall within 3 standard
// deviations of the mean, which is this case is 0.

// Return a (signed) 16-bit integer with a gaussian distribution
// about 0 and the specified stddev.  Recall that approximatelye 99.7% of
// values will fall within 3 standard deviations of the mean, which is this
// case is 0.
// The algorithm used generates pairs of values.  The extra value is stored and
// returned the next time grand_s16 is called.  Whenever the value to be used
// for stddev is changed, the first result from grand_s16() should be discarded.
// If the raw result is less than -32768 or greater than 32767, it is truncated
// to fit within this range.
short grand_s16(short stddev) {
	if (next_grand_s16_avail) {
		next_grand_s16_avail = false;
		return next_grand_s16;
	}
	double s, u0, u1;
	int value;
	while (true) {
		u0 = (double) rand() / RAND_MAX * 2.0 - 1.0;
		u1 = (double) rand() / RAND_MAX * 2.0 - 1.0;
		s = u0 * u0 + u1 * u1;
		if (s < 1.0 && s > 0.0)
			break;
	}
	s = sqrt(-2.0 * log(s) / s);

	value = round(stddev * u0 * s);
	if (value < -32768)
		next_grand_s16 = -32768;
	else if (value > 32767)
		next_grand_s16 = 32767;
	else
		next_grand_s16 = (short) value;
	next_grand_s16_avail = true;

	value = round(stddev * u1 * s);
	if (value < -32768)
		return (short) -32768;
	if (value > 32767)
		return (short) 32767;
	return (short) value;
}


// Return number of samples that were clipped
int add_noise(short *samples, unsigned int nSamples, short stddev) {
	int clipcount = 0;
	if (stddev == 0)
		return 0;

	// In case stsddev has changed, discard the first value.
	grand_s16(stddev);

	short noise = 0;
	for (unsigned i = 0; i < nSamples; i++) {
		noise = grand_s16(stddev);
		if (samples[i] + noise < -32769) {
			samples[i] = -32768;
			++clipcount;
		}
		else if (samples[i] + noise > 32767) {
			samples[i] = 32767;
			++clipcount;
		}
		else
			samples[i] += noise;
	}
	return clipcount;
}
