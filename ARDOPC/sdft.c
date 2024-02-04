#include <assert.h>
#include <complex.h>
#include <math.h>

#include "../ARDOPCommonCode/ardopcommon.h"
#include "sdft.h"

// For validation/debugging only
void GoertzelRealImag(short intRealIn[], int intPtr, int N, float m, float * dblReal, float * dblImag);


/*
sdft() implements a variation on a Sliding DFT as described in Streamlining
Digital Signal Processing by Richard Lyons 2007 Figure 14-6(a) with Comb
coefficient = +1 and in the accompanying text.  This differs from a more
typical SDFT in that the frequency bins are offset by 1/2*N.  Thus, for a 240
sample SDFT with a sample rate of 12kHz, a normal SDFT (or any traditional
DFT/FFT algorithm) could produce output for frequencies that are multiples of
50Hz.  These would include 1400, 1450, 1500, 1550, and 1600 Hz, which does
not include the 1425, 1475, 1525, and 1575 Hz frequencies that Ardop uses for
single carrier 4FSK.  However, this SDFT variant will produce output for
these desired frequencies that are offset by 25 Hz from those available from
other DFT algorithms.

Lyon 2007 Eq 14-4:
Sk(n) = cmath.exp(2j * pi * k / N) * (Sk(n-1) + x(n) - x(n-N))
For frequencies offset by 1/2*N this becomes:
Sk(n) = cmath.exp(2j * pi * (k + 1/2)/ N) * (Sk(n-1) + x(n) + x(n-N))

Of course, the Goertzel algorithm can be used to calculate results for
arbitrary frequencies.  Thus, it has been used for the Ardop tones.  However,
it only produces results for a single block of 240 samples each time.  Thus,
to get results for an adjacent overlapping block of 240 tones, such as might
be useful to check or adjust symbol timing, would require repeated execution
of the full Goertzel algorithm.  In contrast, the SDFT algorithm produces
results for all overlapping blocks of samples at one sample intervals.

For comparison purposes, the Goertzel algoritm uses N+1 real multiplies
and 2N+2 real additions for an N-sample result.  The SDFT requires a complex
multiply and three complex additions per sample.  Assuming that each complex
multiplication requires four real multiplies and two real additions and that
a complex addition requries two real additions, the SDFT requires four real
multiplies and eight real additions per sample.  This results in 4N real
multiplies and 8N real additions to get a result from SDFT, which is about
4 times as many operations as for the Goertzel algorithm.  However, if
improving symbol timing requires more than 4 results per symbol period, then
the SDFT is probably more efficient than repeated use of the Goertzel
algorithm.  Of course, additional calculations are required to use these
SDFT results to evaluate symbol timing, but these additional calculations
would be similar whether the Goertzel algorithm or the SDFT was used.

sdft_s holds results (a complex DFT output value) for each sample for each
Carrier and frqNum spanning two symbol periods.  Each time sdft() is called,
it will calculate sdft_s for one symbol's worth of points.  However, the
decision point, where the values corresponding to the four tones are
compared to evaluate which tone was present, will be near the middle of the
results calculated bt that execution of sddft().  This allows the results
extending before and after the decision point to be used to generate an
improved estimate of where that decision point should be.  To initiate this
process init_sdft() will process the first half a symbol's worth of samples.
Because calculating sdft_s for each sample uses the prior sdft_s value,
the last results from the prior execution of sdft() or init_sdft() stored
as sdft_s[cnum][frqNum][-1] will be used to calculate the first result for
the following execution of sdft().  The prior symbol's worth of input
samples are also required to calculate each sdft_s value.  Thus, the input
samples supplied to sdft() must include samples from the prior symbol as
the new samples.
*/
float complex sdft_s[CARCNT][FRQCNT][DFTLEN];

/*
sdft_coeff[Carrier][frqNum] are the "constant" coefficients for calulating
sdft_s for a given frequency, and sdft_coeff_cfrqs[Carrier] are the
corresponding center frequencies that those sdft_coeff values are
appropriate for.  Thus, sdft_coeff must be recalculated whenever the
sdft_coef_cfrqs values change.  For example, changing between a 1 carrier
4FSK frame and a 2 carrier 4FSK frame would result in a change to the
center frequency of the first carrier, and so the sdft_coeff values would
need to be recalculated.  As an alternative, precalculation and hard
coding of of these coefficients for all tones used by all Ardop FSK modes
might be worthwhile.
*/
float complex sdft_coeff[CARCNT][FRQCNT];
float sdft_coeff_cfrqs[CARCNT];

/*
A new estimate of Start is independently calculated for each carrier, but
the data symbols for each carrier are assumed to actually be aligned.  Thus,
a single Start value is calculated from the individual Start values of all of
the carriers.

This suggests another way that the computational load requried for estimated
symbol timing could be reduced if necessary.  Rather than using sdft_s values
from all carriers, it could be based on only the values from the first
carrier, though this might reduce the robustness of the estimate for noisy
signals.
*/

void update_sdft_coeff(int * intCenterFrqs)
{
    // Define/redefine sdft_coeff if not already defined.
    int cnum;
    int frqNum;
    float cfrq;

    for (cnum = 0; cnum <= CARCNT; cnum ++)
    {
        cfrq = intCenterFrqs[cnum];
        if (cfrq == 0)
        {
            // No more carriers to evaluate
            break;
        }
        if (cfrq < 0)
        {
            // CarrierOK[cnum] is True, so skip demodulating this carrier
            continue;
        }
        if (cfrq == sdft_coeff_cfrqs[cnum])
        {
            // sdft_coeff already evaluated for this center frequency
            continue;
        }
        sdft_coeff_cfrqs[cnum] = cfrq;
        for (frqNum = 0; frqNum < FRQCNT; frqNum ++)
        {
            /*
            Sk[n] = cexpf(2 * I * M_PI * (k + 1/2)/ DFTLEN) * (Sk[n-1] + x[n] + x[n-DFTLEN])
            Sk[n] = coeff * (Sk[n-1] + x[n] + x[n-DFTLEN])
            coeff = cexpf(2 * I * M_PI * (k + 1/2)/ DFTLEN)
            frq = (k + 1/2) * SRATE / DFTLEN
            k = (DFTLEN * frq / SRATE) - 1/2
            coeff = cexpf(2 * I * M_PI * ((DFTLEN * frq / SRATE) - 1/2 + 1/2)/ DFTLEN)
            coeff = cexpf(2 * I * M_PI * frq / SRATE)
            dfrq = SRATE / DFTLEN
            frq = cfrq + dfrq * (FRQCNT/2 - 0.5) - dfrq * frqNum.= cfrq + dfrq * ((FRQCNT/2 - 0.5) - frqNum)
            coeff = cexpf(2 * I * M_PI * (cfrq + dfrq * ((FRQCNT/2 - 0.5) - frqNum)) / SRATE)
            coeff = cexpf(2 * I * M_PI * (cfrq + SRATE / DFTLEN * ((FRQCNT/2 - 0.5) - frqNum)) / SRATE)
            coeff = cexpf(2 * I * M_PI * SRATE * (cfrq / SRATE + ((FRQCNT/2 - 0.5) - frqNum) / DFTLEN) / SRATE)
            coeff = cexpf(2 * I * M_PI * (cfrq / SRATE + ((FRQCNT/2 - 0.5) - frqNum) / DFTLEN))
            */
            sdft_coeff[cnum][frqNum] = cexp(2 * I * M_PI * (cfrq / SRATE + ((FRQCNT/2 - 0.5) - frqNum) / DFTLEN));
            WriteDebugLog(LOGDEBUGPLUS, "sdft_coeff[%d][%d]=%.6f j%.6f", cnum, frqNum, creal(sdft_coeff[cnum][frqNum]), cimagf(sdft_coeff[cnum][frqNum]));
        }
    }
}


bool blnSdftInitialized = false;
void init_sdft(int * intCenterFrqs, short * intSamples)
{
    // Call init_sdft() before calling sdft() for the first 4FSK symbol in a frame
    // intSamples shall contain at least DFTLEN/2 samples.
    // Any additional samples will be ignored.

    int cnum;
    int frqNum;
    int snum;
    float cfrq;

    update_sdft_coeff(intCenterFrqs);
    for (cnum = 0; cnum < CARCNT; cnum++)
    {
        cfrq = intCenterFrqs[cnum];
        if (cfrq == 0)
        {
            // No more carriers to evaluate
            break;
        }
        if (cfrq < 0)
        {
            // CarrierOK[cnum] is True, so skip demodulating this carrier
            continue;
        }
        for (frqNum = 0; frqNum < FRQCNT; frqNum++)
        {
            for (snum = 0; snum < DFTLEN/2; snum++)
            {
                sdft_s[cnum][frqNum][snum] = 0.0;
            }
            for (snum = DFTLEN/2; snum < DFTLEN; snum++)
            {
                // Sk[n] = coeff * (Sk[n-1] + x[n] + x[n-DFTLEN])
                sdft_s[cnum][frqNum][snum] = (
                    sdft_coeff[cnum][frqNum] * (
                        // Prior sdft_s value.
                        sdft_s[cnum][frqNum][snum - 1]
                        // The new sample.  intSamples has an implied
                        // set of DFTLEN/2 leaving zeros for which sdft_s
                        // was automatically set to 0.0.  So, adjust
                        // the index into intSamples by DFTLEN/2
                        + intSamples[snum - DFTLEN / 2]
                        // The sample from DFTLEN samples samples back.
                        + 0.0
                    )
                );
            }
        }
    }
    // This is reset to FALSE at the end of demodulating the frame or after
    // frame type decode fail.  All frames use 4FSK for the frame type.
    // Some use it for the frame data as well.
    blnSdftInitialized = true;
}

int sdft(int * intCenterFrqs, short * intSamples, int intToneMags[CARCNT][4096], int intToneMagsIndex[CARCNT])
{
    /*
    intSamples shall have a length of at least 2 * DFTLEN +
    search_distance * decision_damping.  If it is longer than this, then
    any additional samples will be ignored.  Samples beyond
    2 * DFTLEN will only be used if the new decision_index is found
    to be greater than the value found based on leader/sync and prior calls
    to sdft().  The first DFTLEN samples shall be the samples
    processed by the prior call to sdft() or init_sdft(), and the following
    DFTLEN samples are those to be processed now.  The external
    varialble sdft_s shall hold the results produced by that prior call to
    init_sdft() or sdft(), though it is actually only the final value of
    these that are used.  The current estimated location of the decision
    index, which is based on evaluation of the prior symbols and the frame
    leader/sync is at a decision_index of DFTLEN +
    DFTLEN / 2 relative to intSamples, which will corrspond to a
    decision_index of DFTLEN / 2 relative to sdft_s.

    intCenterFrqs is a list of center frequencies for up to CARCNT carriers to
    be evaluated.  If less than CARCNT carriers are to be evaluated, their
    center frequencies shall occur at the start of this list, and be followed
    by zeros.

    For a multicarrier frame if CarrierOK[] is true for some of the
    frequencies, such that for those frequencies demodulation has already
    been completed successfully and need not be repeated, then a negative
    frequency will be given in intCenterFrqs for those carriers.

    sdft_var[cnum][] is the variance (square of standard deviation)
    between the FRQCNT sdft_s values for a given carrier for each sample
    number.  While the SDFT algorithm requires sdft_s to be caluculated for
    every sample, sdft_var, sdft_var_sum, and the other values calculated to
    estimate symbol timing could use only every Mth sdft_s value so as to
    reduce the required computations.  For example, using M=2 to use every
    other sdft_s value or M=4 to use every fourth sdft_s value could reduce
    the required computational effort by half or by three quarters
    respectively.  The use of only a fraction of the sdft_s values for
    estimating symbol timing is not currently implemented, but this might be
    worth exploring if these symbol timing calculations are found to be too
    much for some low powered processors.  It seems likely that doing so
    would not significantly degrade the value of the symbol timing results
    for reasonable values of M such as these.
    Since the amount of error in the symbol timing index is assumed to be
    small, sdft_var and sdft_var_sum could be calculated for only a limited
    number of samples around the prior decision index.
    */
    float sdft_var[CARCNT][DFTLEN];
    /*
    A sdft_var_sumlen point moving average of sddft_var values is used to
    filter out noise in in the sdft_var values.  Thus, sdft_var_sum is the
    sum of the sdft_var_sumlen values in sdft_var.  Thus,
    sdft_var_sum[snum]/sdft_var_sumlen is a good filtered estimate of the value
    of sdft_var[snum].  Since it is only the relative magnitude of sdft_var_sum
    that is important, division by sdft_var_sumlen is never done to get the
    actual moving average.
    Using an odd value for sdft_var_sumlen is advantageous so that
    (sdft_var_sumlen - 1) / 2 gives the gives the offset in each direction
    to the first and last sdft_var values included in sdft_var_sum.
    */
    int sdft_var_sumlen = 49;  // [odd valued] length of moving average for sdft_var
    assert(sdft_var_sumlen % 2 == 1);
    float sdft_var_sum[CARCNT][DFTLEN];

    /*
    Tuning search_dist and decision_damping might improve (or damage)
    performance.  These parameters are intended to allow decision_index to
    track the optimal sample where the symbol should be evaluated.  If one or
    both are too small, then the the optimal index location might change
    faster than decision_index can, or decision_index may approach that
    optimal index value too slowly to be useful.  However, if one or both are
    too large, then the erratic and unstable variation in decision_index may
    also give poor results.  The latter is especially likely to occur when
    several identical tones occur in a row, not providing clear local peaks
    in variance (standard deviation squared).
    search_dist is the maximum distance to search for a new peak_index from
    the prior value of decision_index.  This explicitly limits the impact
    of multiple sequential identical tone values.
    */
    int search_dist = DFTLEN / 8;
    /*
    The distance from decision_index to the new peak_index is multiplied by
    decision_damping to calculate the amount that decision_index should be
    changed by.
    */
    float decision_damping = 0.2;
    float decision_index[CARCNT];

    int cnum;
    int frqNum;
    int snum;
    int peak_index;
    int composite_decision_index;
    int index_advance;
    // While CARCNT is the maximum number of carriers, ncarriers is the
    // actual number of carriers being demodulated.
    int ncarriers;
    float cfrq;
    float sms;  // the magnitude of a sdft_c value squared
    float sms_sum;  // sum of sdft_s magnitude squared
    float smssq_sum;  // sum of (sdft_s magnitude squared) squared
    float peak_index_var_sum;
    float composite_decision_sum;

    for (cnum = 0; cnum < CARCNT; cnum++)
    {
        cfrq = intCenterFrqs[cnum];
        if (cfrq == 0)
        {
            // No more carriers to evaluate
            break;
        }
        if (cfrq < 0)
        {
            // CarrierOK[cnum] is True, so skip demodulating this carrier
            continue;
        }
        /*
        To more easily facilitate using only a subset of the samples to
        calculate an improved estimate of the symbol timing, calculation of
        variance and related values will be done in separate loops after all
        of the sdft_s values are calculated for this symbol.
        */
        for (frqNum = 0; frqNum < FRQCNT; frqNum++)
        {
            // sdft_s[cnum][frqNum][0] uses the value from the end of sdft_s.
            // So, calculate this one outside the loop used for the others.
            sdft_s[cnum][frqNum][0] = sdft_coeff[cnum][frqNum] * (
                sdft_s[cnum][frqNum][DFTLEN - 1]  // prior sdft_s
                + intSamples[DFTLEN]  // new sample
                + intSamples[0]  // sample from DFTLEN samples back
            );
            for (snum = 1; snum < DFTLEN; snum++)
            {
                // Sk[n] = coeff * (Sk[n-1] + x[n] + x[n-DFTLEN])
                sdft_s[cnum][frqNum][snum] = sdft_coeff[cnum][frqNum] * (
                    sdft_s[cnum][frqNum][snum - 1]  // prior sdft_s
                    + intSamples[DFTLEN + snum]  // new sample
                    + intSamples[snum]  // sample from DFTLEN samples back
                );
            }
        }
        /*
        For now, because it is useful for diagnostic purposes, calculate
        sdft_var for all samples.  The range should be reduced to only those
        used for calculation of sdft_var_sum.
        */
        for (snum = 0; snum < DFTLEN; snum++)
        {
            sms_sum = 0;
            smssq_sum = 0;
            for (frqNum = 0; frqNum < FRQCNT; frqNum++)
            {
                /*
                Rather than use the magnitude of the complex sdft_s value,
                Use the magnitude squared.  This is computationally simpler
                since it avoids the need to do a sqrt().
                While a complex number multiplied by its conjugate is its
                magnitude squared, and thus will be a real number, it will
                be a real number stored as a complex value whose imaginary
                part is 0.  Thus, extracting its real part is just discarding
                the zero imaginary part.
                sms is sddt_s magnitude squared
                */
                sms = crealf(sdft_s[cnum][frqNum][snum] * conjf(sdft_s[cnum][frqNum][snum]));
                sms_sum += sms;  // sum of sms
                smssq_sum += sms * sms;  // sum of sms squared
            }
            // Variance (standard deviation squared) of the FRQCNT values of sms
            sdft_var[cnum][snum] = (smssq_sum - sms_sum*sms_sum/4.0)/3.0;
        }

        /*
        Calculate a running sum of sdft_var_sumlen values of sdft_var.
        This represents (moving average of sdft_var) * sdft_var_sumlen.
        Calculate sdft_var_sum only within a range of search_dist from
        the nominal decision index at DFTLEN/2.
        Calculate the first value explicitly, then calculate the others
        incrementally.
        */
        sdft_var_sum[cnum][DFTLEN / 2 - search_dist] = 0.0;
        for (
            snum = DFTLEN/2 - search_dist - (sdft_var_sumlen - 1) / 2;
            snum < DFTLEN/2 - search_dist + (sdft_var_sumlen - 1) / 2 + 1;
            snum++)
           {
               sdft_var_sum[cnum][DFTLEN / 2 - search_dist] += sdft_var[cnum][snum];
           }
        peak_index = DFTLEN / 2 - search_dist;
        peak_index_var_sum = sdft_var_sum[cnum][DFTLEN / 2 - search_dist];
        for (
            snum = DFTLEN / 2 - search_dist + 1;
            snum < DFTLEN / 2 + search_dist + 1;
            snum++)
        {
            sdft_var_sum[cnum][snum] = (
                sdft_var_sum[cnum][snum - 1]
                - sdft_var[cnum][snum - (sdft_var_sumlen - 1) / 2 - 1]
                + sdft_var[cnum][snum + (sdft_var_sumlen - 1) / 2 + 1]
            );
            if (sdft_var_sum[cnum][snum] > peak_index_var_sum)
            {
                peak_index = snum;
                peak_index_var_sum = sdft_var_sum[cnum][snum];
            }
        }
        /*
        If decision_index based on leader/sync and prior symbols is already
        correct, then peak_index will be equal to DFTLEN / 2.  If
        not, then adjust decision_index to be closer to peak_index.
        decision_index values are floats rather than integers.  After these
        values are combined from all carriers, the result will be converted
        to an integer to be used as an index array.
        */
        decision_index[cnum] = (
            DFTLEN / 2
            + decision_damping*(peak_index - DFTLEN / 2)
        );
    }

    /*
    For now, use avg decision_index over all active carriers.
    If the second derivative of sdft_var is low (flat sdft_var
    rather than a peak), this could indicate adjacent identical
    symbols, and thus no clear signal of symbol center.
    Consider implementing this.  Alternatively, this second
    derivative could be used to modulate start_damping as used
    above for each individual carrier.
    */
    ncarriers = 0;
    composite_decision_sum = 0.0;
    for (cnum = 0; cnum < CARCNT; cnum++)
    {
        cfrq = intCenterFrqs[cnum];
        if (cfrq == 0)
        {
            // No more carriers to evaluate
            break;
        }
        if (cfrq < 0)
        {
            // CarrierOK[cnum] is True, so skip demodulating this carrier
            continue;
        }
        ncarriers += 1;
        composite_decision_sum += decision_index[cnum];
    }
    // Use +0.5 so taht implicit conversion from float to int converts
    // to the nearest int rather than always rounding down.
    composite_decision_index = 0.5 + composite_decision_sum/ncarriers;

    // Put results from decision_index into intToneMags[][].
    // The values in intToneMags[][] are sdft_s magnitude squared.
    for (cnum = 0; cnum < CARCNT; cnum++)
    {
        cfrq = intCenterFrqs[cnum];
        if (cfrq == 0)
        {
            // No more carriers to evaluate
            break;
        }
        if (cfrq < 0)
        {
            // CarrierOK[cnum] is True, so skip demodulating this carrier
            continue;
        }
        for (frqNum = 0; frqNum < FRQCNT; frqNum++)
        {
            // Copy magnitude squared of sdft_s (scaled to match output
            // from GoertzelRealImag() to intToneMags.
            intToneMags[cnum][intToneMagsIndex[cnum]] = creal(
                sdft_s[cnum][frqNum][composite_decision_index]
                * conj(sdft_s[cnum][frqNum][composite_decision_index]))/((DFTLEN*DFTLEN)>>2);

/* // UNCOMMENT THIS BLOCK TO SHOW SDFT VALIDATION COMPARISONS IN DEBUG LOG
            float gReal, gImag;  // Used to validate sdft by comparing to Goertzel
            // For validation/debugging purposes, compare the sdft result
            // to an equivalent calculation using the Goertzel algorithm
            // used by the original 4FSK demodulator.
            // This shows that the results are not a perfect match, but that
            // they are generally differ by less than 1%, with the largest
            // relative differences occuring for the smallest magnitudes, not
            // the larger magnitudes of the target tone.
            WriteDebugLog(LOGDEBUGPLUS, "intToneMags[%d][%d], %d",
                cnum,
                intToneMagsIndex[cnum],
                intToneMags[cnum][intToneMagsIndex[cnum]]
               );
            GoertzelRealImag(intSamples, composite_decision_index + 1, DFTLEN,
                // m = DFTLEN * frq / SRATE
                // frq = sdft_coeff_cfrqs[cnum] + (frqNum - 1.5) * SRATE / DFTLEN
                // m = DFTLEN * (sdft_coeff_cfrqs[cnum] + (frqNum - 1.5) * SRATE / DFTLEN) / SRATE
                DFTLEN * (sdft_coeff_cfrqs[cnum] - (frqNum - 1.5) * SRATE / DFTLEN) / SRATE,
                &gReal, &gImag);
            WriteDebugLog(LOGDEBUGPLUS, "Goertzel = %.0f diff=%.02f\% (m=%.02f)",
                gReal*gReal + gImag*gImag,
                100 * (gReal*gReal + gImag*gImag - intToneMags[cnum][intToneMagsIndex[cnum]]) / (gReal*gReal + gImag*gImag),
                DFTLEN * (sdft_coeff_cfrqs[cnum] - (frqNum - 1.5) * SRATE / DFTLEN) / SRATE
            );
*/ // END OF VALIDATION BLOCK
            intToneMagsIndex[cnum] += 1;
        }
       }

       /*
    index_advance is the amount that composite_decision_index has changed.
    This will be the return value of this function so that it can be used
    to select which data points to include in intSamples for the next
    call of this function.
    */
    index_advance = composite_decision_index - DFTLEN / 2;
    /*
    The next call of this function will use sdft_s[][][-1] to compute a
    new value for sdft_s[][][0].  So, sdft_s[][][-1] must be corrected
    by shifting each sdft[][] by index_advance.
    */
    if (index_advance < 0)
    {
        for (cnum = 0; cnum < CARCNT; cnum++)
        {
            cfrq = intCenterFrqs[cnum];
            if (cfrq == 0)
            {
                // No more carriers to evaluate
                break;
            }
            if (cfrq < 0)
            {
                // CarrierOK[cnum] is True, so skip demodulating this carrier
                continue;
            }
            for (frqNum = 0; frqNum < FRQCNT; frqNum++)
            {
                // Only the value of sdft_s[cnum][frqNum][DFTLEN - 1] matters, and
                // its value is already known.  So, just copy it to this location.
                sdft_s[cnum][frqNum][DFTLEN - 1] = sdft_s[cnum][frqNum][DFTLEN - 1 + index_advance];
            }
        }
    }
    else if (index_advance > 0)
    {
        for (cnum = 0; cnum < CARCNT; cnum++)
        {
            cfrq = intCenterFrqs[cnum];
            if (cfrq == 0)
            {
                // No more carriers to evaluate
                break;
            }
            if (cfrq < 0)
            {
                // CarrierOK[cnum] is True, so skip demodulating this carrier
                continue;
            }
            for (frqNum = 0; frqNum < FRQCNT; frqNum++)
            {
                // Only the value of sdft_s[cnum][frqNum][DFTLEN - 1] matters, but
                // its value is not yet known, and it can only be calculated by
                // calculating all of the sdft_s values from the current
                // sdft_s[cnum][frqNum][DFTLEN - 1] forward by index_advance
                /// samples.
                for (snum = DFTLEN; snum < DFTLEN + index_advance; snum++)
                {
                    // Sk[n] = coeff * (Sk[n-1] + x[n] + x[n-DFTLEN])
                    // Repeatedly overwrite sdft_s[cnum][frqNum][DFTLEN - 1]
                    sdft_s[cnum][frqNum][DFTLEN - 1] = sdft_coeff[cnum][frqNum] * (
                        sdft_s[cnum][frqNum][DFTLEN - 1]  // prior sdft_s
                        + intSamples[DFTLEN + snum]  // new sample
                        + intSamples[snum]  // sample from DFTLEN samples back
                    );
                }
            }
        }
    }
    return index_advance;
}
