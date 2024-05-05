// FFT Code
/*
    '********************************************************************
    ' Execution time for a 2048 point FFT on a 1700 MHz P4 was about 5 ms)
    ' Some optimization could be made if only real inputs are insured.
    '   Rick Muething KN6KB, Mar 31, 2004
    '********************************************************************
    '--------------------------------------------------------------------
    ' VB FFT Release 2-B
    ' by Murphy McCauley (MurphyMc@Concentric.NET)
    ' 10/01/99
    '--------------------------------------------------------------------
    ' About:
    ' This code is very, very heavily based on Don Cross's fourier.pas
    ' Turbo Pascal Unit for calculating the Fast Fourier Transform.
    ' I've not implemented all of his functions, though I may well do
    ' so in the future.
    ' For more info, you can contact me by email, check my website at:
    ' http://www.fullspectrum.com/deeth/
    ' or check Don Cross's FFT web page at:
    ' http://www.intersrv.com/~dcross/fft.html
    ' You also may be intrested in the FFT.DLL that I put together based
    ' on Don Cross's FFT C code.  It's callable with Visual Basic and
    ' includes VB declares.  You can get it from either website.
    '--------------------------------------------------------------------
    ' History of Release 2-B:
    ' Fixed a couple of errors that resulted from me mucking about with
    '   variable names after implementation and not re-checking.  BAD ME.
    '  --------
    ' History of Release 2:
    ' Added FrequencyOfIndex() which is Don Cross's Index_to_frequency().
    ' FourierTransform() can now do inverse transforms.
    ' Added CalcFrequency() which can do a transform for a single
    '   frequency.
    '--------------------------------------------------------------------
    ' Usage:
    ' The useful functions are:
    ' FourierTransform() performs a Fast Fourier Transform on an pair of
    '  Double arrays -- one real, one imaginary.  Don't want/need
    '  imaginary numbers?  Just use an array of 0s.  This function can
    '  also do inverse FFTs.
    ' FrequencyOfIndex() can tell you what actual frequency a given index
    '  corresponds to.
    ' CalcFrequency() transforms a single frequency.
    '--------------------------------------------------------------------
    ' Notes:
    ' All arrays must be 0 based (i.e. Dim TheArray(0 To 1023) or
    '  Dim TheArray(1023)).
    ' The number of samples must be a power of two (i.e. 2^x).
    ' FrequencyOfIndex() and CalcFrequency() haven't been tested much.
    ' Use this ENTIRELY AT YOUR OWN RISK.
    '--------------------------------------------------------------------
*/

#include <math.h>

#ifdef M_PI
#undef M_PI
#endif

#define M_PI       3.1415926f

int ipow(int base, int exp)
{
    int result = 1;
    while (exp)
    {
        if (exp & 1)
            result *= base;
        exp >>= 1;
        base *= base;
    }

    return result;
}

int NumberOfBitsNeeded(int PowerOfTwo)
{
	int i;

	for (i = 0; i <= 16; i++)
	{
		if ((PowerOfTwo & ipow(2, i)) != 0)
			return i;
		
	}	
	return 0;
}


int ReverseBits(int Index, int NumBits)
{
	int i, Rev = 0;
	
    for (i = 0; i < NumBits; i++)
	{
		Rev = (Rev * 2) | (Index & 1);
		Index = Index /2;
	}

    return Rev;
}

void FourierTransform(int NumSamples, float * RealIn, float * RealOut, float * ImagOut, int InverseTransform)
{
	float AngleNumerator;
	unsigned char NumBits;
	
	int I, j, K, n, BlockSize, BlockEnd;
	float DeltaAngle, DeltaAr;
	float Alpha, Beta;
	float TR, TI, AR, AI;
	
	if (InverseTransform)
		AngleNumerator = -2.0f * M_PI;
	else
		AngleNumerator = 2.0f * M_PI;

	NumBits = NumberOfBitsNeeded(NumSamples);

	for (I = 0; I < NumSamples; I++)
	{
		j = ReverseBits(I, NumBits);
		RealOut[j] = RealIn[I];
		ImagOut[j] = 0.0f; // Not using I in ImageIn[I];
	}

	BlockEnd = 1;
	BlockSize = 2;

	while (BlockSize <= NumSamples)
	{
		DeltaAngle = AngleNumerator / BlockSize;
		Alpha = sinf(0.5f * DeltaAngle);
		Alpha = 2.0f * Alpha * Alpha;
		Beta = sinf(DeltaAngle);
		
		I = 0;

		while (I < NumSamples)
		{
			AR = 1.0f;
			AI = 0.0f;
			
			j = I;
			
			for (n = 0; n <  BlockEnd; n++)
			{
				K = j + BlockEnd;
				TR = AR * RealOut[K] - AI * ImagOut[K];
				TI = AI * RealOut[K] + AR * ImagOut[K];
				RealOut[K] = RealOut[j] - TR;
				ImagOut[K] = ImagOut[j] - TI;
				RealOut[j] = RealOut[j] + TR;
				ImagOut[j] = ImagOut[j] + TI;
				DeltaAr = Alpha * AR + Beta * AI;
				AI = AI - (Alpha * AI - Beta * AR);
				AR = AR - DeltaAr;
				j = j + 1;
			}
			I = I + BlockSize;
		}
		BlockEnd = BlockSize;
		BlockSize = BlockSize * 2;
	}
	
	if (InverseTransform)
	{
		//	Normalize the resulting time samples...
		
		for (I = 0; I < NumSamples; I++)
		{
			RealOut[I] = RealOut[I] / NumSamples;
			ImagOut[I] = ImagOut[I] / NumSamples;
		}
	}
}
 

