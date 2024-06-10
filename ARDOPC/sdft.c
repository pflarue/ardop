#include <assert.h>
#include <complex.h>
#include <math.h>

#include "../ARDOPCommonCode/ardopcommon.h"
#include "sdft.h"

// For validation/debugging only
void GoertzelRealImag(short intRealIn[], int intPtr, int N, float m, float * dblReal, float * dblImag);


// obsolete versions of this code accommodated multi-carrier FSK modes
/*
sdft() implements a variation on a Sliding DFT as described in Streamlining
Digital Signal Processing by Richard Lyons 2007 Figure 14-6(a) with Comb
coefficient = +1 and in the accompanying text.  This differs from a more
typical SDFT in that the frequency bins are offset by 1/2*N.  Thus, for a 240
sample SDFT with a sample rate of 12kHz, a normal SDFT (or any traditional
DFT/FFT algorithm) could produce output for frequencies that are multiples of
50Hz.  These would include 1400, 1450, 1500, 1550, and 1600 Hz, which does
not include the 1425, 1475, 1525, and 1575 Hz frequencies that Ardop uses for
single carrier 50 baud 4FSK.  However, this SDFT variant will produce output for
these desired frequencies that are offset by 25 Hz from those available from
other DFT algorithms.  It is also suitable for Ardop's 100 baud 4FSK frames
that use frequencies of 1350, 1450, 1550, and 1650 with a 120 sample SDFT.

Lyon 2007 Eq 14-4:
Sk(n) = cmath.exp(2j * pi * k / N) * (Sk(n-1) + x(n) - x(n-N))
For frequencies offset by 1/2*N this becomes:
Sk(n) = cmath.exp(2j * pi * (k + 1/2)/ N) * (Sk(n-1) + x(n) + x(n-N))

Of course, the Goertzel algorithm can be used to calculate results for
arbitrary frequencies.  Thus, it has been used for the Ardop tones.  However,
it only produces results for a single block of 240 (or 120) samples each time.  Thus,
to get results for an adjacent overlapping block of 240 (or 120) tones, such as might
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

sdft_s holds results (a complex DFT output value) for each sample
and frqNum spanning two symbol periods.  Each time sdft() is called,
it will calculate sdft_s for one symbol's worth of points.  However, the
decision point, where the values corresponding to the four tones are
compared to evaluate which tone was present, will be near the middle of the
results calculated by that execution of sddft().  This allows the results
extending before and after the decision point to be used to generate an
improved estimate of where that decision point should be.  To initiate this
process init_sdft() will process the first half a symbol's worth of samples.
Because calculating sdft_s for each sample uses the prior sdft_s value,
the last results from the prior execution of sdft() or init_sdft() stored
as sdft_s[frqNum][-1] will be used to calculate the first result for
the following execution of sdft().  The prior symbol's worth of input
samples are also required to calculate each sdft_s value.  Thus, the input
samples supplied to sdft() must include samples from the prior symbol as
well as the new samples.
*/
float complex sdft_s[FRQCNT][MAXDFTLEN];

/*
sdft_coeff[frqNum] are the "constant" coefficients for calulating
sdft_s for a given frequency and intDftLen, and sdft_coeff_cfrq is
the corresponding center frequency that those sdft_coeff values are
appropriate for.  Thus, sdft_coeff must be recalculated whenever
sdft_coef_cfrq or intDftLen values change.  For example, changing between
a 50 baud 1 carrier 4FSK frame and a 100 baud 1 carrier 4FSK frame would
result in a change to individual tone frequencies, and so the sdft_coeff values
would need to be recalculated.  As an alternative, precalculation and hard
coding of of these coefficients for all tones used by all Ardop FSK modes
might be worthwhile.
*/
float complex sdft_coeff[FRQCNT];
float sdft_coeff_cfrq = 0;
int sdft_coeff_dftlen = 0;

void update_sdft_coeff(int intCenterFrq, int intDftLen)
{
    // Define/redefine sdft_coeff if not already defined.
    float cfrq = (float) intCenterFrq;

    if (intCenterFrq == sdft_coeff_cfrq && intDftLen == sdft_coeff_dftlen)
    {
        // sdft_coeff already evaluated for this center frequency and dftlen
        return;
    }
    sdft_coeff_cfrq = cfrq;
    for (int frqNum = 0; frqNum < FRQCNT; frqNum ++)
    {
        /*
        Sk[n] = cexpf(2 * I * M_PI * (k + 1/2)/ intDftLen) * (Sk[n-1] + x[n] + x[n-intDftLen])
        Sk[n] = coeff * (Sk[n-1] + x[n] + x[n-intDftLen])
        coeff = cexpf(2 * I * M_PI * (k + 1/2)/ intDftLen)
        frq = (k + 1/2) * SRATE / intDftLen
        k = (intDftLen * frq / SRATE) - 1/2
        coeff = cexpf(2 * I * M_PI * ((intDftLen * frq / SRATE) - 1/2 + 1/2)/ intDftLen)
        coeff = cexpf(2 * I * M_PI * frq / SRATE)
        dfrq = SRATE / intDftLen
        frq = cfrq + dfrq * (FRQCNT/2 - 0.5) - dfrq * frqNum.= cfrq + dfrq * ((FRQCNT/2 - 0.5) - frqNum)
        coeff = cexpf(2 * I * M_PI * (cfrq + dfrq * ((FRQCNT/2 - 0.5) - frqNum)) / SRATE)
        coeff = cexpf(2 * I * M_PI * (cfrq + SRATE / intDftLen * ((FRQCNT/2 - 0.5) - frqNum)) / SRATE)
        coeff = cexpf(2 * I * M_PI * SRATE * (cfrq / SRATE + ((FRQCNT/2 - 0.5) - frqNum) / intDftLen) / SRATE)
        coeff = cexpf(2 * I * M_PI * (cfrq / SRATE + ((FRQCNT/2 - 0.5) - frqNum) / intDftLen))
        */
        sdft_coeff[frqNum] = cexp(2 * I * M_PI * (cfrq / SRATE + ((FRQCNT/2 - 0.5) - frqNum) / intDftLen));
        WriteDebugLog(LOGDEBUGPLUS, "sdft_coeff[%d]=%.6f j%.6f", frqNum, creal(sdft_coeff[frqNum]), cimagf(sdft_coeff[frqNum]));
    }
    sdft_coeff_dftlen = intDftLen;
}


bool blnSdftInitialized = false;
void init_sdft(int intCenterFrq, short * intSamples, int intDftLen)
{
    // Call init_sdft() before calling sdft() for the first 4FSK symbol in a
    // frame or before calling sdft() when intDftLen may have changed, such
    // as after demodulating the 50 baud 4FSK frame type for a 100 baud 4FSK
    // frame.
    // intSamples shall contain at least intDftLen/2 samples.
    // Any additional samples will be ignored.

    update_sdft_coeff(intCenterFrq, intDftLen);
    for (int frqNum = 0; frqNum < FRQCNT; frqNum++)
    {
        int snum;
        for (snum = 0; snum < intDftLen/2; snum++)
        {
            sdft_s[frqNum][snum] = 0.0;
        }
        for (snum = intDftLen/2; snum < intDftLen; snum++)
        {
            // Sk[n] = coeff * (Sk[n-1] + x[n] + x[n-intDftLen])
            sdft_s[frqNum][snum] = (
                sdft_coeff[frqNum] * (
                    // Prior sdft_s value.
                    sdft_s[frqNum][snum - 1]
                    // The new sample.  intSamples has an implied
                    // set of intDftLen/2 leading zeros for which sdft_s
                    // was automatically set to 0.0.  So, adjust
                    // the index into intSamples by intDftLen/2
                    + intSamples[snum - intDftLen / 2]
                    // The sample from intDftLen samples samples back.
                    + 0.0
                )
            );
        }
    }
    // This is reset to FALSE at the end of demodulating the a frame type
    // (whether successful or not), and after demodulating a frame.
    // All frames use 50 baud 4FSK for the frame type.  Some frame types
    // use 50 or 100 baud 4FSK for the frame data as well.  The possible
    // change in 4FSK baud rate from the frame type to the frame data is
    // why blnSdftInitialized is reset to false after demodulating the
    // frame type.  Alternatively, this could be done only when the frame
    // type indicates the use of 100 baud 4FSK data.
    blnSdftInitialized = true;
}

int sdft(short * intSamples, int intToneMags[4096], int *intToneMagsIndex, int intDftLen)
{
    /*
    intSamples shall have a length of at least 2 * intDftLen +
    search_distance * decision_damping.  If it is longer than this, then
    any additional samples will be ignored.  Samples beyond
    2 * intDftLen will only be used if the new decision_index is found
    to be greater than the value found based on leader/sync and prior calls
    to sdft().  The first intDftLen samples shall be the samples
    processed by the prior call to sdft() or init_sdft(), and the following
    intDftLen samples are those to be processed now.  The external
    varialble sdft_s shall hold the results produced by that prior call to
    init_sdft() or sdft(), though it is actually only the final value of
    these that are used.  The current estimated location of the decision
    index, which is based on evaluation of the prior symbols and the frame
    leader/sync is at a decision_index of intDftLen +
    intDftLen / 2 relative to intSamples, which will corrspond to a
    decision_index of intDftLen / 2 relative to sdft_s.

    sdft_var[] is the variance (square of standard deviation)
    between the FRQCNT sdft_s values for each sample
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
    float sdft_var[MAXDFTLEN];
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
    int sdft_var_sumlen = intDftLen/4 - 1;  // [odd valued] length of moving average for sdft_var
    assert(sdft_var_sumlen % 2 == 1);
    float sdft_var_sum[MAXDFTLEN];

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
    int search_dist = intDftLen / 8;
    /*
    The distance from decision_index to the new peak_index is multiplied by
    decision_damping to calculate the amount that decision_index should be
    changed by.
    */
    float decision_damping = 0.2;
    int decision_index;

    int frqNum;
    int snum;
    int peak_index;
    int index_advance;
    float sms;  // the magnitude of a sdft_c value squared
    float sms_sum;  // sum of sdft_s magnitude squared
    float smssq_sum;  // sum of (sdft_s magnitude squared) squared
    float peak_index_var_sum;

    /*
    To more easily facilitate using only a subset of the samples to
    calculate an improved estimate of the symbol timing, calculation of
    variance and related values will be done in separate loops after all
    of the sdft_s values are calculated for this symbol.
    */
    for (frqNum = 0; frqNum < FRQCNT; frqNum++)
    {
        // sdft_s[frqNum][0] uses the value from the end of sdft_s.
        // So, calculate this one outside the loop used for the others.
        sdft_s[frqNum][0] = sdft_coeff[frqNum] * (
            sdft_s[frqNum][intDftLen - 1]  // prior sdft_s
            + intSamples[intDftLen]  // new sample
            + intSamples[0]  // sample from intDftLen samples back
        );
        for (snum = 1; snum < intDftLen; snum++)
        {
            // Sk[n] = coeff * (Sk[n-1] + x[n] + x[n-intDftLen])
            sdft_s[frqNum][snum] = sdft_coeff[frqNum] * (
                sdft_s[frqNum][snum - 1]  // prior sdft_s
                + intSamples[intDftLen + snum]  // new sample
                + intSamples[snum]  // sample from intDftLen samples back
            );
        }
    }
    /*
    For now, because it is useful for diagnostic purposes, calculate
    sdft_var for all samples.  The range should be reduced to only those
    used for calculation of sdft_var_sum.
    */
    for (snum = 0; snum < intDftLen; snum++)
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
            sms = crealf(sdft_s[frqNum][snum] * conjf(sdft_s[frqNum][snum]));
            sms_sum += sms;  // sum of sms
            smssq_sum += sms * sms;  // sum of sms squared
        }
        // Variance (standard deviation squared) of the FRQCNT values of sms
        sdft_var[snum] = (smssq_sum - sms_sum*sms_sum/4.0)/3.0;
    }

    /*
    Calculate a running sum of sdft_var_sumlen values of sdft_var.
    This represents (moving average of sdft_var) * sdft_var_sumlen.
    Calculate sdft_var_sum only within a range of search_dist from
    the nominal decision index at intDftLen/2.
    Calculate the first value explicitly, then calculate the others
    incrementally.
    */
    sdft_var_sum[intDftLen / 2 - search_dist] = 0.0;
    for (
        snum = intDftLen/2 - search_dist - (sdft_var_sumlen - 1) / 2;
        snum < intDftLen/2 - search_dist + (sdft_var_sumlen - 1) / 2 + 1;
        snum++)
       {
           sdft_var_sum[intDftLen / 2 - search_dist] += sdft_var[snum];
       }
    peak_index = intDftLen / 2 - search_dist;
    peak_index_var_sum = sdft_var_sum[intDftLen / 2 - search_dist];
    for (
        snum = intDftLen / 2 - search_dist + 1;
        snum < intDftLen / 2 + search_dist + 1;
        snum++)
    {
        sdft_var_sum[snum] = (
            sdft_var_sum[snum - 1]
            - sdft_var[snum - (sdft_var_sumlen - 1) / 2 - 1]
            + sdft_var[snum + (sdft_var_sumlen - 1) / 2 + 1]
        );
        if (sdft_var_sum[snum] > peak_index_var_sum)
        {
            peak_index = snum;
            peak_index_var_sum = sdft_var_sum[snum];
        }
    }
    /*
    If decision_index based on leader/sync and prior symbols is already
    correct, then peak_index will be equal to intDftLen / 2.  If
    not, then adjust decision_index to be closer to peak_index.
    */
    // Use +0.5 so that implicit conversion from float to int converts
    // to the nearest int rather than always rounding down.
    decision_index = 0.5 + (
        intDftLen / 2
        + decision_damping*(peak_index - intDftLen / 2)
    );

    // Put results from decision_index into intToneMags[][].
    // The values in intToneMags[] are sdft_s magnitude squared.
    for (frqNum = 0; frqNum < FRQCNT; frqNum++)
    {
        // Copy magnitude squared of sdft_s (scaled to match output
        // from GoertzelRealImag() to intToneMags.
        intToneMags[*intToneMagsIndex] = creal(
            sdft_s[frqNum][decision_index]
            * conj(sdft_s[frqNum][decision_index]))/((intDftLen*intDftLen)>>2);

/* // UNCOMMENT THIS BLOCK TO SHOW SDFT VALIDATION COMPARISONS IN DEBUG LOG
        float cfrq = 1500.0;
        float gReal, gImag;  // Used to validate sdft by comparing to Goertzel
        // For validation/debugging purposes, compare the sdft result
        // to an equivalent calculation using the Goertzel algorithm
        // used by the original 4FSK demodulator.
        // This shows that the results are not a perfect match, but that
        // they are generally differ by less than 1%, with the largest
        // relative differences occuring for the smallest magnitudes, not
        // the larger magnitudes of the target tone.
        WriteDebugLog(LOGDEBUGPLUS, "intToneMags[%d], %d",
            *intToneMagsIndex,
            intToneMags[*intToneMagsIndex]
           );
        GoertzelRealImag(intSamples, decision_index + 1, intDftLen,
            // m = intDftLen * frq / SRATE
            // frq = sdft_coeff_cfrq + (frqNum - 1.5) * SRATE / intDftLen
            // m = intDftLen * (sdft_coeff_cfrq + (frqNum - 1.5) * SRATE / intDftLen) / SRATE
            intDftLen * (sdft_coeff_cfrq - (frqNum - 1.5) * SRATE / intDftLen) / SRATE,
            &gReal, &gImag);
        WriteDebugLog(LOGDEBUGPLUS, "Goertzel = %.0f diff=%.02f\% (m=%.02f)",
            gReal*gReal + gImag*gImag,
            100 * (gReal*gReal + gImag*gImag - intToneMags[*intToneMagsIndex]) / (gReal*gReal + gImag*gImag),
            intDftLen * (sdft_coeff_cfrq - (frqNum - 1.5) * SRATE / intDftLen) / SRATE
        );
*/ // END OF VALIDATION BLOCK
        *intToneMagsIndex += 1;
    }

    /*
    index_advance is the amount that decision_index has changed.
    This will be the return value of this function so that it can be used
    to select which data points to include in intSamples for the next
    call of this function.
    */
    index_advance = decision_index - intDftLen / 2;
    /*
    The next call of this function will use sdft_s[][-1] to compute a
    new value for sdft_s[][0].  So, sdft_s[][-1] must be corrected
    by shifting each sdft[] by index_advance.
    */
    if (index_advance < 0)
    {
        for (frqNum = 0; frqNum < FRQCNT; frqNum++)
        {
            // Only the value of sdft_s[frqNum][intDftLen - 1] matters, and
            // its value is already known.  So, just copy it to this location.
            sdft_s[frqNum][intDftLen - 1] = sdft_s[frqNum][intDftLen - 1 + index_advance];
        }
    }
    else if (index_advance > 0)
    {
        for (frqNum = 0; frqNum < FRQCNT; frqNum++)
        {
            // Only the value of sdft_s[frqNum][intDftLen - 1] matters, but
            // its value is not yet known, and it can only be calculated by
            // calculating all of the sdft_s values from the current
            // sdft_s[frqNum][intDftLen - 1] forward by index_advance
            /// samples.
            for (snum = intDftLen; snum < intDftLen + index_advance; snum++)
            {
                // Sk[n] = coeff * (Sk[n-1] + x[n] + x[n-intDftLen])
                // Repeatedly overwrite sdft_s[cnum][frqNum][intDftLen - 1]
                sdft_s[frqNum][intDftLen - 1] = sdft_coeff[frqNum] * (
                    sdft_s[frqNum][intDftLen - 1]  // prior sdft_s
                    + intSamples[intDftLen + snum]  // new sample
                    + intSamples[snum]  // sample from intDftLen samples back
                );
            }
        }
    }
    return index_advance;
}
