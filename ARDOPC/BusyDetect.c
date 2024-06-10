
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#define _CRT_SECURE_NO_DEPRECATE

#include <windows.h>
#endif

#include "ARDOPC.h"

VOID SortSignals2(float * dblMag, int intStartBin, int intStopBin, int intNumBins, float *  dblAVGSignalPerBin, float *  dblAVGBaselinePerBin);

int intLastStart, intLastStop;

int LastBusyOn;
int LastBusyOff;

BOOL blnLastBusy = FALSE;

float dblAvgStoNSlowNarrow;
float dblAvgStoNFastNarrow;
float dblAvgStoNSlowWide;
float dblAvgStoNFastWide;
int intLastStart = 0;
int intLastStop  = 0;
int intBusyOnCnt  = 0;  // used to filter Busy ON detections 
int intBusyOffCnt  = 0; // used to filter Busy OFF detections 
int dttLastBusyTrip = 0;
int dttPriorLastBusyTrip = 0;
int dttLastBusyClear = 0;
unsigned int dttLastTrip;
float dblAvgPk2BaselineRatio, dblAvgBaselineSlow, dblAvgBaselineFast;
unsigned int intHoldMs = 5000;


VOID ClearBusy()
{
	dttLastBusyTrip = Now;
	dttPriorLastBusyTrip = dttLastBusyTrip;
	dttLastBusyClear = dttLastBusyTrip + 610;	// This insures test in ARDOPprotocol ~ line 887 will work 
	dttLastTrip = dttLastBusyTrip -intHoldMs;	// This clears the busy detect immediatly (required for scanning when re enabled by Listen=True
	blnLastBusy = False;
	intBusyOnCnt = 0;
	intBusyOffCnt = 0;
	intLastStart = 0;
	intLastStop = 0;		// This will force the busy detector to ignore old averages and initialze the rolling average filters
}

/*
// Function to implement a busy detector based on 1024 point FFT

BOOL BusyDetect2(float * dblMag, int intStart, int intStop)        // this only called while searching for leader ...once leader detected, no longer called.
{
	// each bin is about 12000/1024 or 11.72 Hz
	// this only called while searching for leader ...once leader detected, no longer called.
	// First sort signals and look at highes signals:baseline ratio..

	float dblAVGSignalPerBinNarrow, dblAVGSignalPerBinWide, dblAVGBaselineNarrow, dblAVGBaselineWide;
	float dblFastAlpha = 0.4f;
	float dblSlowAlpha = 0.2f;
	float dblAvgStoNNarrow, dblAvgStoNWide;
	int intNarrow = 8;  // 8 x 11.72 Hz about 94 z
	int intWide = ((intStop - intStart) * 2) / 3; //* 0.66);
	int blnBusy = FALSE;
	float dblAvgStoNSlowNarrow = 0;
	float dblAvgStoNFastNarrow = 0;
	float dblAvgStoNSlowWide = 0;
	float dblAvgStoNFastWide = 0;

	// First narrow band (~94Hz)

	SortSignals(dblMag, intStart, intStop, intNarrow, &dblAVGSignalPerBinNarrow, &dblAVGBaselineNarrow);

	if (intLastStart == intStart && intLastStop == intStop)
	{
		dblAvgStoNSlowNarrow = (1 - dblSlowAlpha) * dblAvgStoNSlowNarrow + dblSlowAlpha * dblAVGSignalPerBinNarrow / dblAVGBaselineNarrow;
		dblAvgStoNFastNarrow = (1 - dblFastAlpha) * dblAvgStoNFastNarrow + dblFastAlpha * dblAVGSignalPerBinNarrow / dblAVGBaselineNarrow;
	}
	else
	{
		dblAvgStoNSlowNarrow = dblAVGSignalPerBinNarrow / dblAVGBaselineNarrow;
		dblAvgStoNFastNarrow = dblAVGSignalPerBinNarrow / dblAVGBaselineNarrow;
		intLastStart = intStart;
		intLastStop = intStop;
	}
	
	dblAvgStoNNarrow = max(dblAvgStoNSlowNarrow, dblAvgStoNFastNarrow); // computes fast attack, slow release

	// Wide band (66% ofr current bandwidth) 

	SortSignals(dblMag, intStart, intStop, intWide, &dblAVGSignalPerBinWide, &dblAVGBaselineWide);

	if (intLastStart == intStart && intLastStop == intStop)
	{
		dblAvgStoNSlowWide = (1 - dblSlowAlpha) * dblAvgStoNSlowWide + dblSlowAlpha * dblAVGSignalPerBinWide / dblAVGBaselineWide;
		dblAvgStoNFastWide = (1 - dblFastAlpha) * dblAvgStoNFastWide + dblFastAlpha * dblAVGSignalPerBinWide / dblAVGBaselineWide;
	}
	else
	{
		dblAvgStoNSlowWide = dblAVGSignalPerBinWide / dblAVGBaselineWide;
		dblAvgStoNFastWide = dblAVGSignalPerBinWide / dblAVGBaselineWide;
		intLastStart = intStart;
		intLastStop = intStop;
	}

	dblAvgStoNNarrow = max(dblAvgStoNSlowNarrow, dblAvgStoNFastNarrow); // computes fast attack, slow release
	dblAvgStoNWide = max(dblAvgStoNSlowWide, dblAvgStoNFastWide); // computes fast attack, slow release

	// Preliminary calibration...future a function of bandwidth and BusyDet.
   
	switch (ARQBandwidth)
	{
	case B200MAX:
	case B200FORCED:
		if (dblAvgStoNNarrow > 1.5 * BusyDet|| dblAvgStoNWide > 2.5 * BusyDet)
			blnBusy = True;
		break;

	case B500MAX:
	case B500FORCED:
		if (dblAvgStoNNarrow > 1.5 * BusyDet || dblAvgStoNWide > 2.5 * BusyDet)
			blnBusy = True;
		break;

	case B1000MAX:
	case B1000FORCED:

		if (dblAvgStoNNarrow > 1.4 * BusyDet || dblAvgStoNWide > 2 * BusyDet)
			blnBusy = True;
		break;

	case B2000MAX:
	case B2000FORCED:
		if (dblAvgStoNNarrow > 1.4 * BusyDet || dblAvgStoNWide > 2 * BusyDet)
			blnBusy = True;
	}

	if (blnBusy) // This used to skip over one call busy nuisance trips. Busy must be present at least 2 consecutive times to be reported
	{
		intBusyOnCnt += 1;
		intBusyOffCnt = 0;
        if (intBusyOnCnt > 1)
			blnBusy = True;
		else if (!blnBusy)
		{
			intBusyOffCnt += 1;
			intBusyOnCnt = 0;
			if (intBusyOffCnt > 3)
				blnBusy = False;
		}
	}
	if (blnLastBusy == False && blnBusy)
	{
		int x = round(dblAvgStoNNarrow);	// odd, but PI doesnt print floats properly 
		int y = round(dblAvgStoNWide);
		
		blnLastBusy = True;
		LastBusyOn = Now;
#ifdef __ARM_ARCH
		WriteDebugLog(LOGDEBUG, "[BusyDetect2: BUSY ON  StoN Narrow = %d StoN Wide %d", x, y);
#else
		WriteDebugLog(LOGDEBUG, "[BusyDetect2: BUSY ON  StoN Narrow = %f StoN Wide %f", dblAvgStoNNarrow, dblAvgStoNWide);
#endif
	}
	else if (blnLastBusy == True && !blnBusy)
	{
		int x = round(dblAvgStoNNarrow);	// odd, but PI doesnt print floats properly 
		int y = round(dblAvgStoNWide);

		blnLastBusy = False;
		LastBusyOff = Now;
#ifdef __ARM_ARCH
		WriteDebugLog(LOGDEBUG, "[BusyDetect2: BUSY OFF StoN Narrow = %d StoN Wide %d", x, y);
#else
		WriteDebugLog(LOGDEBUG, "[BusyDetect2: BUSY OFF StoN Narrow = %f StoN Wide %f", dblAvgStoNNarrow, dblAvgStoNWide);
#endif
	}

	return blnLastBusy;
}


*/




BOOL BusyDetect3(float * dblMag, int intStart, int intStop)        // this only called while searching for leader ...once leader detected, no longer called.
{
	// each bin is about 12000/1024 or 11.72 Hz
	// this only called while searching for leader ...once leader detected, no longer called.
	// First sort signals and look at highes signals:baseline ratio..

	float dblAVGSignalPerBinNarrow, dblAVGSignalPerBinWide, dblAVGBaselineNarrow, dblAVGBaselineWide;
	float dblSlowAlpha = 0.2f;
	float dblAvgStoNNarrow = 0, dblAvgStoNWide = 0;
	int intNarrow = 8;  // 8 x 11.72 Hz about 94 z
	int intWide = ((intStop - intStart) * 2) / 3; //* 0.66);
	int blnBusy = FALSE;

	// First sort signals and look at highest signals:baseline ratio..
	// First narrow band (~94Hz)

	SortSignals2(dblMag, intStart, intStop, intNarrow, &dblAVGSignalPerBinNarrow, &dblAVGBaselineNarrow);

	if (intLastStart == intStart && intLastStop == intStop)
		dblAvgStoNNarrow = (1 - dblSlowAlpha) * dblAvgStoNNarrow + dblSlowAlpha * dblAVGSignalPerBinNarrow / dblAVGBaselineNarrow;
	else
	{
		// This initializes the Narrow average after a bandwidth change

		dblAvgStoNNarrow = dblAVGSignalPerBinNarrow / dblAVGBaselineNarrow;
 		intLastStart = intStart;
		intLastStop = intStop;
	}
	
	// Wide band (66% of current bandwidth)
	
	SortSignals2(dblMag, intStart, intStop, intWide, &dblAVGSignalPerBinWide, &dblAVGBaselineWide);

	if (intLastStart == intStart && intLastStop == intStop)
		dblAvgStoNWide = (1 - dblSlowAlpha) * dblAvgStoNWide + dblSlowAlpha * dblAVGSignalPerBinWide / dblAVGBaselineWide;
	else
	{
		// This initializes the Wide average after a bandwidth change
		
		dblAvgStoNWide = dblAVGSignalPerBinWide / dblAVGBaselineWide;
		intLastStart = intStart;
		intLastStop = intStop;
	}

	// Preliminary calibration...future a function of bandwidth and BusyDet.
   
	switch (ARQBandwidth)
	{
	case B200MAX:
	case B200FORCED:	
		blnBusy = (dblAvgStoNNarrow > (3 + 0.008 * powf(BusyDet, 4))) || (dblAvgStoNWide > (5 + 0.02 * powf(BusyDet, 4)));
		break;

	case B500MAX:
	case B500FORCED:
		blnBusy = (dblAvgStoNNarrow > (3 + 0.008 * powf(BusyDet, 4))) || (dblAvgStoNWide > (5 + 0.02 * powf(BusyDet, 4)));
		break;

	case B1000MAX:
	case B1000FORCED:
		blnBusy = (dblAvgStoNNarrow > (3 + 0.008 * powf(BusyDet, 4))) || (dblAvgStoNWide > (5 + 0.016 * powf(BusyDet, 4)));  		
		break;

	case B2000MAX:
	case B2000FORCED:
		blnBusy = (dblAvgStoNNarrow > (3 + 0.008 * powf(BusyDet, 4))) || (dblAvgStoNWide > (5 + 0.016 * powf(BusyDet, 4)));  		
 	}

	if (BusyDet == 0)
		blnBusy = FALSE;		// 0 Disables check ?? Is this the best place to do this?

//	WriteDebugLog(LOGDEBUG, "Busy %d Wide %f Narrow %f", blnBusy, dblAvgStoNWide, dblAvgStoNNarrow); 

	if (blnBusy)
	{
		// This requires multiple adjacent busy conditions to skip over one nuisance Busy trips. 
		// Busy must be present at least 3 consecutive times ( ~250 ms) to be reported

		intBusyOnCnt += 1;
		intBusyOffCnt = 0;
        if (intBusyOnCnt > 3)
			dttLastTrip = Now;
	}
	else
	{
		intBusyOffCnt += 1;
		intBusyOnCnt = 0;
	}

	if (blnLastBusy == False && intBusyOnCnt >= 3)
	{
		dttPriorLastBusyTrip = dttLastBusyTrip;  // save old dttLastBusyTrip for use in BUSYBLOCKING function
		dttLastBusyTrip = Now;
		blnLastBusy = True;
	}
	else
	{
		if (blnLastBusy && (Now - dttLastTrip) > intHoldMs && intBusyOffCnt >= 3)
		{
			dttLastBusyClear = Now;
			blnLastBusy = False;
		}
	}
	return blnLastBusy;
}

VOID SortSignals(float * dblMag, int intStartBin, int intStopBin, int intNumBins, float *  dblAVGSignalPerBin, float *  dblAVGBaselinePerBin)
{
	// puts the top intNumber of bins between intStartBin and intStopBin into dblAVGSignalPerBin, the rest into dblAvgBaselinePerBin
    // for decent accuracy intNumBins should be < 75% of intStopBin-intStartBin)

	float dblAVGSignal[200] = {0};//intNumBins
	float dblAVGBaseline[200] = {0};//intStopBin - intStartBin - intNumBins

	float dblSigSum = 0;
	float dblTotalSum = 0;
	int intSigPtr = 0;
	int intBasePtr = 0;
	int i, j, k;

	for (i = 0; i <  intNumBins; i++)
	{
		for (j = intStartBin; j <= intStopBin; j++)
		{
			if (i == 0)
			{
				dblTotalSum += dblMag[j];
				if (dblMag[j] > dblAVGSignal[i])
					dblAVGSignal[i] = dblMag[j];
			}
			else
			{
				if (dblMag[j] > dblAVGSignal[i] && dblMag[j] < dblAVGSignal[i - 1])
					dblAVGSignal[i] = dblMag[j];
			}
		}
	}
	
	for(k = 0; k < intNumBins; k++)
	{
		dblSigSum += dblAVGSignal[k];
	}
	*dblAVGSignalPerBin = dblSigSum / intNumBins;
	*dblAVGBaselinePerBin = (dblTotalSum - dblSigSum) / (intStopBin - intStartBin - intNumBins + 1);
}

 BOOL compare(const void *p1, const void *p2)
{
    float x = *(const float *)p1;
    float y = *(const float *)p2;

    if (x < y)
        return -1;  // Return -1 if you want ascending, 1 if you want descending order. 
    else if (x > y)
        return 1;   // Return 1 if you want ascending, -1 if you want descending order. 

    return 0;
}

VOID SortSignals2(float * dblMag, int intStartBin, int intStopBin, int intNumBins, float *  dblAVGSignalPerBin, float *  dblAVGBaselinePerBin)
{
	// puts the top intNumber of bins between intStartBin and intStopBin into dblAVGSignalPerBin, the rest into dblAvgBaselinePerBin
    // for decent accuracy intNumBins should be < 75% of intStopBin-intStartBin)

	// This version uses a native sort function which is much faster and reduces CPU loading significantly on wide bandwidths. 
  
	float dblSort[200]; 
	float dblSum1 = 0, dblSum2 = 0;
	int numtoSort = (intStopBin - intStartBin) + 1, i;
	
	memcpy(dblSort, &dblMag[intStartBin], numtoSort * sizeof(float));

	qsort((void *)dblSort, numtoSort, sizeof(float), compare);

	for (i = numtoSort -1; i >= 0; i--)
	{
		if (i >= (numtoSort - intNumBins))
			dblSum1 += dblSort[i];
		else
			dblSum2 += dblSort[i];
	}

	*dblAVGSignalPerBin = dblSum1 / intNumBins;
	*dblAVGBaselinePerBin = dblSum2 / (intStopBin - intStartBin - intNumBins - 1);
}

 
