/*	Written by Simon Rockliff 1989/1991
	downloaded from www.jjj.de/crs4 2024
	Modified by Peter LaRue 2024 for use in ardopcf

	For use outside of ardopcf, replace all ZF_LOGE() and ZF_LOGD with printf()
	and add to the end of error and debugging messages.

	This program is an encoder/decoder for Reed-Solomon codes. Encoding is in
	systematic form, decoding via the Berlekamp iterative algorithm.
	In the present form , the constants mm, nn, tt, and kk=nn-2tt must be
	specified  (the double letters are used simply to avoid clashes with
	other n,k,t used in other programs into which this was incorporated!)
	Also, the irreducible polynomial used to generate GF(2**mm) must also be
	entered -- these can be found in Lin and Costello, and also Clark and Cain.

	The representation of the elements of GF(2**m) is either in index form,
	where the number is the power of the primitive element alpha, which is
	convenient for multiplication (add the powers modulo 2**m-1) or in
	polynomial form, where the bits represent the coefficients of the
	polynomial representation of the number, which is the most convenient form
	for addition.  The two forms are swapped between via lookup tables.
	This leads to fairly messy looking expressions, but unfortunately, there
	is no easy alternative when working with Galois arithmetic.

	The code is not written in the most elegant way, but to the best
	of my knowledge, (no absolute guarantees!), it works.
	However, when including it into a simulation program, you may want to do
	some conversion of global variables (used here because I am lazy!) to
	local variables where appropriate, and passing parameters (eg array
	addresses) to the functions  may be a sensible move to reduce the number
	of global variables and thus decrease the chance of a bug being introduced.

	This program does not handle erasures at present, but should not be hard
	to adapt to do this, as it is just an adjustment to the Berlekamp-Massey
	algorithm. It also does not attempt to decode past the BCH bound -- see
	Blahut "Theory and practice of error control codes" for how to do this.

		Simon Rockliff, University of Adelaide   21/9/89

	26/6/91 Slight modifications to remove a compiler dependent bug which hadn't
		previously surfaced. A few extra comments added for clarity.
		Appears to all work fine, ready for posting to net!

		Notice
		--------
	This program may be freely modified and/or given to whoever wants it.
	A condition of such distribution is that the author's contribution be
	acknowledged by his name being left in the comments heading the program,
	however no responsibility is accepted for any financial or other loss which
	may result from some unforseen errors or malfunctioning of the program
	during use.
		Simon Rockliff, 26th June 1991
*/

/*	Up to nn-rslen bytes of source data may be provided.  encode_rs() always
	works on a padded block of data of length nn-rslen and returns a set of
	parity bytes of length rslen.  decode_rs() always works on a padded
	receieved data block of length nn nominally consisting of the nn-rslen
	bytes of source data followed by rslen parity bytes.  Of course this
	received data may contain errors.

	When source data of length d is to be encoded and d is less than nn-rslen,
	then the padded data block to be passed to encode_rs() shall be a block of
	nn-rslen-d zeros, followed by the source data of length d.  The source data
	of length d followed by the rslen parity bytes are then transmitted without
	the zeros used as padding.  The received data block of length d+rslen may
	contain some errors.  Before passing this data to decode_rs(), a padded data
	block of length nn must be reconstructed consisting of nn-rslen-d zeros
	followed by the d+rslen received bytes.  If decode_rs() is successfull, then
	the bytes nn-rslen-d to nn-rslen of the returned data block will be
	identical to the source data of length d originally passed to encode_rs().
	If decode_rs() is unsuccessful, then that same range of bytes will still
	contain the uncorrected noisy approximation of the source data.

	rs_append() takes the source data of length d and returns that data with
	the parity bytes appended to its end.  rs_correct() takes the received data
	of length d+rslen and returns the corrected data, if possible.  It also
	indicates whether errors were found.
*/

// Note that there may be up to 255-rslen bytes of data.  NOT 256-rslen.

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/log.h"
#include "rrs.h"
#define mm 8  // RS code over GF(2**8)
// Notice that the length of an 8 bit codeword is 255, NOT 256!
#define nn 255  // nn=2**mm -1   length of codeword
/*	In the earlier version of this code, tt and kk were hard coded.
	Now, rslen = 2*tt is a variable.  The following define terms used
	in some of the comments:
		tt is the number of errors that can be corrected
		kk = nn-2*tt
*/

// MAXRSLEN_COUNT and MAXRSLEN can be modified to allow a wider range
// of rslen values.
#define MAXRSLEN_COUNT 10
#define MAXRSLEN 64
int rslen_set[MAXRSLEN_COUNT];
int rslen_count = 0;
/*	gg is dependent on kk, the number of errors that can be corrected.
	Since only a small set of kk values are used, generate gg tables
	for each of these at startup.

	First index value of gg[i][j] is from i=0 to i=MAXRSLEN_COUNT - 1, and holds
	gg values for rslen = rslen_set[i]
*/
int gg[MAXRSLEN_COUNT][MAXRSLEN + 1];

// alpha_to[] and index_of[] will be generated (once) by
// generate_gf(), usually via init_rs()
int alpha_to[nn + 1];
int index_of[nn + 1];

// TODO: Consider precalculating and hard coding these values
void generate_gf()
/*	generate GF(2**mm) from the irreducible polynomial p(X) in pp[0]..pp[mm]
	lookup tables:  index->polynomial form   alpha_to[] contains j=alpha**i;
					polynomial form -> index form  index_of[j=alpha**i] = i
	alpha=2 is the primitive element of GF(2**mm)
*/
{
	/*	specify irreducible polynomial coeffts
		There are a few suitable choices for this irreducible polynomial.
		This one happens to be the one specified by RFC5510 for m=8.

		This polynomial is sometimes refered to in hexidecimal form as 0x171
	*/
	int pp[mm + 1] = {1, 0, 1, 1, 1, 0, 0, 0, 1};
	int mask = 1;

	alpha_to[mm] = 0;
	for (int i = 0; i < mm; i++) {
		alpha_to[i] = mask;
		index_of[alpha_to[i]] = i;
		if (pp[i] != 0)
			alpha_to[mm] ^= mask;
		mask <<= 1;
	}
	index_of[alpha_to[mm]] = mm;
	mask >>= 1;
	for (int i = mm + 1; i < nn; i++) {
		if (alpha_to[i - 1] >= mask)
			alpha_to[i] = alpha_to[mm] ^ ((alpha_to[i - 1] ^ mask) << 1);
		else
			alpha_to[i] = alpha_to[i - 1] << 1;
		index_of[alpha_to[i]] = i;
	}
	index_of[0] = -1;
}

// Return a pointer to gg[] for a specified rslen.
// Return NULL on failure.
int* get_gg(int rslen) {
	for (int i = 0; i < rslen_count; i++)
		if (rslen_set[i] == rslen)
			return gg[i];
	return (NULL);
}

// TODO: Consider precalculating and hard coding these values
// Return 0 on success, 1 on failure
int gen_polys(int *lengths, int count)
/*	Obtain the generator polynomial of the tt-error correcting, length
	nn=(2**mm -1) Reed Solomon code from the product of
	(X+alpha**i), i=1..2*tt
*/
{
	int *ggl;
	int rslen;

	if (count > MAXRSLEN_COUNT) {
		ZF_LOGE(
			"ERROR in gen_polys().  count is limited to %d but %d provided."
			" This limit can be increased by changing a value defined in rs.c.",
			MAXRSLEN_COUNT, count);
		return (1);
	}
	rslen_count = count;
	for (int l = 0; l < rslen_count; l++) {
		if (lengths[l] > MAXRSLEN){
			ZF_LOGE(
				"ERROR in gen_polys().  Values in lengths are limited to %d but"
				" %d provided. This limit can be increased by changing a value"
				" defined in rs.c.",
				MAXRSLEN, lengths[l]);
			return (1);
		}
		rslen_set[l] = lengths[l];
	}

	for (int l = 0; l < rslen_count; l++) {
		rslen = rslen_set[l];
		ggl = get_gg(rslen);
		ggl[0] = 2;  // primitive element alpha = 2 for GF(2**mm)
		ggl[1] = 1;  // g(x) = (X+alpha) initially
		for (int i = 2; i <= rslen; i++) {
			ggl[i] = 1;
			for (int j = i - 1; j > 0; j--)
				if (ggl[j] != 0)
					ggl[j] = ggl[j - 1] ^ alpha_to[(index_of[ggl[j]] + i) % nn];
				else
					ggl[j] = ggl[j - 1];
			ggl[0] = alpha_to[(index_of[ggl[0]] + i) % nn];  // gg[0] can never be zero
		}
		// convert gg[] to index form for quicker encoding
		for (int i = 0; i <= rslen; i++)
			ggl[i] = index_of[ggl[i]];
	}
	return (0);
}


void encode_rs(int *data, int *bb, int rslen)
/*	take the string of symbols in data[i], i=0..(k-1) and encode systematically
	to produce 2*tt parity symbols in bb[0]..bb[2*tt-1]
	data[] is input and bb[] is output in polynomial form.
	Encoding is done by using a feedback shift register with appropriate
	connections specified by the elements of gg[], which was generated above.
	Codeword is   c(X) = data(X)*X**(nn-kk)+ b(X)

	This function doesn't do error checking for valid rslen or to verify that
	gg[][] has been populated.  Use rs_append() to include these checks.
*/
{
	int feedback;
	int* ggl = get_gg(rslen);
	// int kk = nn - 2 * tt;
	int kk = nn - rslen;

	for (int i = 0; i < rslen; i++)
		bb[i] = 0;
	for (int i = kk - 1; i >= 0; i--) {
		feedback = index_of[data[i] ^ bb[rslen - 1]];
		if (feedback != -1) {
			for (int j = rslen - 1; j > 0; j--) {
				if (ggl[j] != -1)
					bb[j] = bb[j - 1] ^ alpha_to[(ggl[j] + feedback) % nn];
				else
					bb[j] = bb[j - 1];
			}
			bb[0] = alpha_to[(ggl[0] + feedback) % nn];
		} else {
			for (int j = rslen - 1; j > 0; j--)
				bb[j] = bb[j - 1];
			bb[0] = 0;
		}
	}
}


int decode_rs(int *rcvd, int rslen, bool quiet, bool test_only)
/*	assume we have received bits grouped into mm-bit symbols in rcvd[i],
	i=0..(nn-1),  and rcvd[i] is index form (ie as powers of alpha).
	We first compute the 2*tt syndromes by substituting alpha**i into rec(X) and
	evaluating, storing the syndromes in s[i], i=1..2tt (leave s[0] zero) .
	Then we use the Berlekamp iteration to find the error location polynomial
	elp[i].   If the degree of the elp is >tt, we cannot correct all the errors
	and hence just put out the information symbols uncorrected. If the degree of
	elp is <=tt, we substitute alpha**i , i=1..n into the elp to get the roots,
	hence the inverse roots, the error location numbers. If the number of errors
	located does not equal the degree of the elp, we have more than tt errors
	and cannot correct them.  Otherwise, we then solve for the error value at
	the error location and correct the error.  The procedure is that found in
	Lin and Costello. For the cases where the number of errors is known to be too
	large to correct, the information symbols as received are output (the
	advantage of systematic encoding is that hopefully some of the information
	symbols will be okay and that if we are in luck, the errors are in the
	parity part of the transmitted codeword).  Of course, these insoluble cases
	can be returned as error flags to the calling routine if desired.

	Return 0 if no errors were detected.
	Return 1 if errors were detected and (maybe) corrected.
		Cases where some errors cannot be corrected, but 1 is still returned
		are more likely for small rslen.  ~50% for rslen=2, ~33% for rslen=4,
		~2.7% for rslen=8, < 0.002% for rslen=16.  The testing to develop
		these estimates were done with a random number of errors greater than
		rslen/2, but less than or equal to rslen.
	Return -1 if errors were detected that could not be corrected.
	If errors were detected, they may or may not have been corrected.
	If test_only is true, do not attempt to fix, returning 0 or 1 as described.
	When test_only is true, rcvd will NOT be valid upon return because it will
	not have been converted from the index format provided as input to polynomial
	form normally provided upon return.

	This function doesn't do error checking for valid rslen or to verify that
	gg[][] has been populated.  Use rs_correct() to include these checks.
	*/
{
	int retval = 0;
	int i, j, u, q;
	int elp[MAXRSLEN + 2][MAXRSLEN];
	int d[MAXRSLEN + 2];
	int l[MAXRSLEN + 2];
	int u_lu[MAXRSLEN + 2];
	int s[MAXRSLEN + 1];
	int count = 0;
	int syn_error = 0;
	int root[MAXRSLEN / 2];
	int loc[MAXRSLEN / 2];
	int z[MAXRSLEN / 2 + 1];
	int err[nn];
	int reg[MAXRSLEN / 2 + 1];
	// first form the syndromes
	for (i = 1; i <= rslen; i++) {
		s[i] = 0;
		for (j = 0; j < nn; j++) {
			if (rcvd[j] != -1)
				s[i] ^= alpha_to[(rcvd[j] + i * j) % nn];  // rcvd[j] in index form
		}
		// convert syndrome from polynomial form to index form
		if (s[i] != 0) {
			syn_error = 1;  // set flag if non-zero syndrome => error
			retval = 1;
			if (test_only) {
				if (!quiet)
					ZF_LOGD("decode_rs() Errors Detected.");
				return(retval);
			}
		}
		s[i] = index_of[s[i]];
	}
	if (test_only) {
		if (!quiet)
			ZF_LOGD("decode_rs() No Errors Detected.");
		return(retval);
	}
	if (syn_error) {  // if errors, try and correct
		/*	compute the error location polynomial via the Berlekamp iterative algorithm,
			following the terminology of Lin and Costello :   d[u] is the 'mu'th
			discrepancy, where u='mu'+1 and 'mu' (the Greek letter!) is the step number
			ranging from -1 to 2*tt (see L&C),  l[u] is the
			degree of the elp at that step, and u_l[u] is the difference between the
			step number and the degree of the elp.
		*/
		if (!quiet)
			ZF_LOGD("decode_rs() Errors Detected.");
		// initialise table entries
		d[0] = 0;  // index form
		d[1] = s[1];  // index form
		elp[0][0] = 0;  // index form
		elp[1][0] = 1;  // polynomial form
		for (int i = 1; i < rslen; i++) {
			elp[0][i] = -1;  // index form
			elp[1][i] = 0;  // polynomial form
		}
		l[0] = 0;
		l[1] = 0;
		u_lu[0] = -1;
		u_lu[1] = 0;
		u = 0;

		do {
			u++;
			if (d[u] == -1) {
				l[u + 1] = l[u];
				for (i = 0; i <= l[u]; i++) {
					elp[u + 1][i] = elp[u][i];
					elp[u][i] = index_of[elp[u][i]];
				}
			} else {
				// search for words with greatest u_lu[q] for which d[q]!=0
				q = u - 1;
				while ((d[q] == -1) && (q > 0))
					q--;  // skip
				// have found first non-zero d[q]
				if (q > 0) {
					j = q;
					do {
						j--;
						if ((d[j] != -1) && (u_lu[q] < u_lu[j]))
							q = j;
					} while (j > 0);
				}

				// have now found q such that d[u] != 0 and u_lu[q] is maximum
				// store degree of new elp polynomial
				if (l[u] > l[q] + u - q)
					l[u + 1] = l[u];
				else
					l[u + 1] = l[q] + u - q;
				// form new elp(x)
				for (i = 0; i < rslen; i++)
					elp[u + 1][i] = 0;
				for (i = 0; i <= l[q]; i++)
					if (elp[q][i] != -1)
						elp[u + 1][i + u - q] = alpha_to[(d[u] + nn - d[q] + elp[q][i]) % nn];
				for (i = 0; i <= l[u]; i++) {
					elp[u + 1][i] ^= elp[u][i];
					elp[u][i] = index_of[elp[u][i]];  // convert old elp value to index
				}
			}
			u_lu[u + 1] = u - l[u + 1] ;

			// form (u+1)th discrepancy
			if (u < rslen) {  // no discrepancy computed on last iteration
				if (s[u + 1] != -1)
					d[u + 1] = alpha_to[s[u + 1]];
				else
					d[u + 1] = 0;
				for (i = 1; i <= l[u + 1]; i++) {
					if ((s[u + 1 - i] != -1) && (elp[u + 1][i] != 0))
						d[u + 1] ^= alpha_to[(s[u + 1 - i] + index_of[elp[u + 1][i]]) % nn];
				}
				d[u + 1] = index_of[d[u + 1]];  // put d[u+1] into index form
			}
		} while ((u < rslen) && (l[u + 1] <= rslen / 2));

		u++;
		if (l[u] <= rslen / 2) {  // can correct error
			// put elp into index form
			for (i = 0; i <= l[u]; i++)
				elp[u][i] = index_of[elp[u][i]];
			// find roots of the error location polynomial
			for (i = 1; i <= l[u]; i++)
				reg[i] = elp[u][i];
			count = 0;
			for (i = 1; i <= nn; i++) {
				q = 1;
				for (j = 1; j <= l[u]; j++) {
					if (reg[j] != -1) {
						reg[j] = (reg[j] + j) % nn;
						q ^= alpha_to[reg[j]];
					}
				}
				if (!q) {  // store root and error location number indices
					root[count] = i;
					loc[count] = nn - i;
					count++;
					if (!quiet)
						ZF_LOGD("decode_rs() At offset %d", nn-i);
				}
			}
			if (count == l[u]) {  // no. roots = degree of elp hence <= tt errors
				// form polynomial z(x)
				for (i = 1; i <= l[u]; i++) {  // Z[0] = 1 always - do not need
					if ((s[i] != -1) && (elp[u][i] != -1))
						z[i] = alpha_to[s[i]] ^ alpha_to[elp[u][i]];
					else if ((s[i] != -1) && (elp[u][i] == -1))
						z[i] = alpha_to[s[i]];
					else if ((s[i] == -1) && (elp[u][i] != -1))
						z[i] = alpha_to[elp[u][i]];
					else
						z[i] = 0;
					for (j = 1; j < i; j++) {
						if ((s[j] != -1) && (elp[u][i - j] != -1))
							z[i] ^= alpha_to[(elp[u][i - j] + s[j]) % nn];
					}
					z[i] = index_of[z[i]];  // put into index form
				}

				// evaluate errors at locations given by error location numbers loc[i]
				for (i = 0; i < nn; i++) {
					err[i] = 0;
					if (rcvd[i] != -1)  // convert rcvd[] to polynomial form
						rcvd[i] = alpha_to[rcvd[i]];
					else
						rcvd[i] = 0;
				}
				for (i = 0; i < l[u]; i++) {  // compute numerator of error term first
					err[loc[i]] = 1;  // accounts for z[0]
					for (j = 1; j <= l[u]; j++) {
						if (z[j] != -1)
							err[loc[i]] ^= alpha_to[(z[j] + j * root[i]) % nn];
					}
					if (err[loc[i]] != 0) {
						err[loc[i]] = index_of[err[loc[i]]];
						q = 0;  // form denominator of error term
						for (j = 0; j < l[u]; j++) {
							if (j != i)
								q += index_of[1 ^ alpha_to[(loc[j] + root[i]) % nn]];
						}
						q = q % nn;
						err[loc[i]] = alpha_to[(err[loc[i]] - q + nn) % nn];
						rcvd[loc[i]] ^= err[loc[i]];  // rcvd[i] must be in polynomial form
					}
				}
				if (!quiet)
					ZF_LOGD("decode_rs() Errors Corrected");
			} else {  // no. roots != degree of elp => >tt errors and cannot solve
				retval = -1;
				if (!quiet)
					ZF_LOGD("decode_rs() Errors Not Corrected");
				for (i = 0; i < nn; i++) {  // could return error flag if desired
					if (rcvd[i] != -1)  // convert rcvd[] to polynomial form
						rcvd[i] = alpha_to[rcvd[i]];
					else
						rcvd[i] = 0;  // just output received codeword as is
				}
			}
		} else {  // elp has degree >tt hence cannot solve
			retval = -1;
			if (!quiet)
				ZF_LOGD("decode_rs() Errors Not Corrected.");
			for (i = 0; i < nn; i++) {  // could return error flag if desired
				if (rcvd[i] != -1)  // convert rcvd[] to polynomial form
					rcvd[i] = alpha_to[rcvd[i]];
				else
					rcvd[i] = 0;  // just output received codeword as is
			}
		}
	} else {  // no non-zero syndromes => no errors: output received codeword
		if (!quiet)
			ZF_LOGD("decode_rs() No Errors.");
		for (i = 0; i < nn; i++) {
			if (rcvd[i] != -1)  // convert rcvd[] to polynomial form
				rcvd[i] = alpha_to[rcvd[i]];
			else
				rcvd[i] = 0;
		}
	}
	return (retval);
}

// Return 0 on success, 1 on failure
int init_rs(int *lengths, int count) {
	generate_gf();
	if (gen_polys(lengths, count) != 0)
		return (1);
}

/*	While only the first datalen bytes of data contain the data to be
	encoded, data shall have a length of at least datalen + rslen so
	that rslen parity bytes will be appended at data[datalen...] upon
	return.

	Return 0 on success, or -1 on failure.
*/
int rs_append(unsigned char *data, int datalen, int rslen) {
	int bb[MAXRSLEN];
	int padded[nn] = {0};  // Need kk = nn - rslen.  This works for any rslen

	if (rslen_count == 0) {
		ZF_LOGE("Error in rs_append().  init_rs() must be called first.");
		return (-1);
	}
	if (get_gg(rslen) == NULL) {
		ZF_LOGE(
			"Error in rs_append().  rslen=%d was not in the list of lengths"
			" passed to init_rs().", rslen);
		return (-1);
	}

	if (datalen + rslen > nn)
		return (-1);  // invalid inputs

	for (int i = 0; i < datalen; i++)
		padded[i + nn - rslen - datalen] = data[i];

	encode_rs(padded, bb, rslen);

	for (int i = 0; i < rslen; i++)
		data[datalen + i] = bb[i];
	return (0);
}


/*	Return 0 if no errors were detected.
	If errors were detected and corrected, return the number of corrections.
		Include corrections to Parity bytes, since these help to give an
		accurate metric of the level of corruption of the data.
	Return -1 if errors were detected and not corrected.
	Return -2 if inputs are invalid
	If errors were detected, they may or may not have been corrected.
	If test_only is true, do not attempt to fix errors, returning values as described.

	combinedlen is a total length that includes rslen
*/
int rs_correct(unsigned char *data, int combinedlen, int rslen, bool quiet, bool test_only) {
	int retval;
	int corrcount = 0;
	int padded[nn] = {0};  // Need kk = nn - rslen.  This works for any rslen

	if (rslen_count == 0) {
		ZF_LOGE("Error in rs_correct().  init_rs() must be called first.");
		return (-2);
	}
	if (get_gg(rslen) == NULL) {
		ZF_LOGE(
			"Error in rs_correct().  rslen=%d was not in the list of lengths"
			" passed to init_rs().", rslen);
		return (-2);
	}

	if (combinedlen > nn) {
		ZF_LOGE(
			"Error in rs_correct().  Invalid inputs."
			" (combinedlen=%d)>(nn-%d)", combinedlen, nn);
		return (-2);
	}

	for (int i = 0; i < combinedlen; i++)
		padded[i + nn - combinedlen] = data[i];

	for (int i = 0; i < nn; i++)
		padded[i] = index_of[padded[i]];  // put padded[i] into index form

	if ((retval = decode_rs(padded, rslen, quiet, test_only)) == 0) {
		// padded[] is returned in polynomial form
		//	No errors were detected, so there is no need to update rxdata.
		return (retval);
	}
	if (test_only)
		return (retval);
	if (retval == -1)
		// unrecoverable errors
		return (-1);
	// Errors were detected and possibly corrected.
	for (int i = 0; i < combinedlen; i++) {
		if (padded[i + nn - combinedlen] != data[i]) {
			corrcount++;
			data[i] = padded[i + nn - combinedlen];
		}
	}

	// TODO: Further evaluation of the usefulness of this heuristic.
	if (retval == 1) {
		/*	Check for non-zero values in padding.
			A non-zero padding value indicates that some invalid
			corrections were made.  This increases the likelihood
			that the unpadded data is incorrect.  Using this additional
			check, specially for small rslen, can reduce the number
			of false positives where the corrected data is believed
			to be valid, but is not.  The usefulness of this check
			probably depends on the amount of padding used.  For
			random message lengths (average padding about 50%):
			For rslen=2, false positives may be reduced from about
			50% to about 25%. For rslen=4, reduced from ~32% to ~11%.
			For rslen=8, reduced from ~2.7% to ~0,6%.  The testing to
			develop these estimates were done with a random number of
			errors greater than rslen/2, but less than or equal to rslen.
		*/
		for (int i = 0; i < nn - combinedlen; i++) {
			if (padded[i] != 0)	{
				ZF_LOGD("In rs_correct() Non-zero padding found.");
				return (-1);
			}
		}
	}
	return (corrcount);
}
