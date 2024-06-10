#include <stdio.h>
#include <math.h>

// Compile along with ardopSampleArrays.c so that results can be compared:
// gcc -o CalcTemplates CalcTemplates.c ardopSampleArrays.c -lm

//
// This code calculates and writes to files the templates
// used to generate modulation samples.

// Rick's code gererates them dynamically as program start, but
// that measns they have to be in RAM. By pregenerating and 
// compliling them they can be placed in program space
// This is necessary with the small RAM space of embedded CPUs

// This only needs to be run once to generate the source files

// Keep code in case we need to change, but don't compile

FILE * fout;

#pragma warning(disable : 4244)		// Code does lots of int float to int

// These are the values found in the existing ardopSampleArrays.c
// Newly calculated values will be compared to these so that a summary
// of the resulting changes can be printed
extern const short int50BaudTwoToneLeaderTemplate[120];
extern const short intFSK50bdCarTemplate[4][240];
extern const short intFSK600bdCarTemplate[4][20];
extern const short intFSK100bdCarTemplate[4][120];
extern const short intPSK100bdCarTemplate[9][4][120];

// These are the new values being calclated.
// They will be written to newArdopSamples.c
short intNew50BaudTwoToneLeaderTemplate[120];
short intNewFSK50bdCarTemplate[4][240];
short intNewFSK600bdCarTemplate[4][20];
short intNewFSK100bdCarTemplate[4][120];
short intNewPSK100bdCarTemplate[9][4][120];

static int intAmp = 26000;	   // Selected to have some margin in calculations with 16 bit values (< 32767) this must apply to all filters as well. 

void Generate50BaudTwoToneLeaderTemplate()
{
	int i;
	float x, y, z;
	int line = 0;

	char msg[256];
	int len;
	char suffix[10] = "";

	len = sprintf(msg,
		"// These Templates are used to save lots of calculations when\n"
		"// generating samples. They are pre-calculated by CalcTemplates.c.\n\n"
		"// Template for 1 symbol (20 ms) of the 50 Baud leader.\n"
		"const short int50BaudTwoToneLeaderTemplate[240] = {\n"
	);
	fwrite(msg, 1, len, fout);

	for (i = 0; i < 240; i++)
	{
		y = (sin(((1500.0 - 25) / 1500) * (i / 8.0 * 2 * M_PI)));
		z = (sin(((1500.0 + 25) / 1500) * (i / 8.0 * 2 * M_PI)));

		x = intAmp * 0.55 * (y - z);
		// The following previously used (short)x + 0.5
		// I believe it was intended to be (short)(x + 0.5) so as to use an
		// offset by 1/2 before truncate to get the effect of (short)round(x).
		// However, that would also be incorrect where x < 0.
		// So, I believe that this is an improvement, albeit minor one. --LaRue
		intNew50BaudTwoToneLeaderTemplate[i] = (short)round(x);

		if ((i - line) == 9)
		{
			// print the last 10 values
			if (i + 1 == 240)
				sprintf(suffix, "};\n\n");
			else
				sprintf(suffix, ",\n");
			len = sprintf(msg, "\t%d, %d, %d, %d, %d, %d, %d, %d, %d, %d%s",
				intNew50BaudTwoToneLeaderTemplate[line],
				intNew50BaudTwoToneLeaderTemplate[line + 1],
				intNew50BaudTwoToneLeaderTemplate[line + 2],
				intNew50BaudTwoToneLeaderTemplate[line + 3],
				intNew50BaudTwoToneLeaderTemplate[line + 4],
				intNew50BaudTwoToneLeaderTemplate[line + 5],
				intNew50BaudTwoToneLeaderTemplate[line + 6],
				intNew50BaudTwoToneLeaderTemplate[line + 7],
				intNew50BaudTwoToneLeaderTemplate[line + 8],
				intNew50BaudTwoToneLeaderTemplate[line + 9],
				suffix);

			line = i + 1;

			fwrite(msg, 1, len, fout);
		}
	}
}

// Subroutine to create the FSK symbol templates

void GenerateFSKTemplates()
{
	// Generate templates of 240 samples (each symbol template = 20 ms) for each of the 4 possible carriers used in 200 Hz BW 4FSK modulation.
	// Generate templates of 120 samples (each symbol template = 10 ms) for each of the 4 possible carriers used in 500 Hz BW 4FSK modulation.
	//Used to speed up computation of FSK frames and reduce use of Sin functions.
	//50 baud Tone values 

	// obsolete versions of this code accommodated multi-carrier FSK
	float dblCarFreq[] = {1425, 1475, 1525, 1575};

	float dblAngle;		// Angle in radians
	float dblCarPhaseInc[20]; 
	int i, k;

	char msg[256];
	char prefix[10] = "";
	char suffix[10] = "";
	int len;
	int line = 0;

	// Compute the phase inc per sample
	len = sprintf(msg,
		"// Template for 4FSK carriers spaced at 50 Hz, 50 baud\n\n"
		"const short intFSK50bdCarTemplate[4][240] = {\n"
	);
	fwrite(msg, 1, len, fout);

    for (i = 0; i < 4; i++) 
	{
		dblCarPhaseInc[i] = 2 * M_PI * dblCarFreq[i] / 12000;
	}
	
	// Now compute the templates: (960 16 bit values total)
	
	for (i = 0; i < 4; i++)			// across the 4 tones for 50 baud frequencies
	{
		dblAngle = 0;
		// 50 baud template

		line = 0;

		for (k = 0; k < 240; k++)	// for 240 samples (one 50 baud symbol)
		{
			// with no envelope control (factor 1.1 chosen emperically to keep
			// FSK peak amplitude slightly below 2 tone peak)
			intNewFSK50bdCarTemplate[i][k] = intAmp * 1.1 * sin(dblAngle);
			dblAngle += dblCarPhaseInc[i];

			if (dblAngle >= 2 * M_PI)
				dblAngle -= 2 * M_PI;
		
			if ((k - line) == 9)
			{
				// print the last 10 values
				if (line == 0)
					sprintf(prefix, "\t{");
				else
					sprintf(prefix, "\t");
				if (i == 3 && k + 1 == 240)
					sprintf(suffix, "}\n};\n\n\n");
				else if (k + 1 == 240)
					sprintf(suffix, "},\n\n");
				else
					sprintf(suffix, ",\n");
				len = sprintf(msg, "%s%d, %d, %d, %d, %d, %d, %d, %d, %d, %d%s",
				prefix,
				intNewFSK50bdCarTemplate[i][line],
				intNewFSK50bdCarTemplate[i][line + 1],
				intNewFSK50bdCarTemplate[i][line + 2],
				intNewFSK50bdCarTemplate[i][line + 3],
				intNewFSK50bdCarTemplate[i][line + 4],
				intNewFSK50bdCarTemplate[i][line + 5],
				intNewFSK50bdCarTemplate[i][line + 6],
				intNewFSK50bdCarTemplate[i][line + 7],
				intNewFSK50bdCarTemplate[i][line + 8],
				intNewFSK50bdCarTemplate[i][line + 9],
				suffix);

				line = k + 1;

				fwrite(msg, 1, len, fout);
			}
		}
	}

	//  100 baud Tone values for a single carrier case 
	// the 100 baud carrier frequencies in Hz

	// obsolete versions of this code accommodated multi-carrier FSK
	dblCarFreq[0] = 1350;
	dblCarFreq[1] = 1450;
	dblCarFreq[2] = 1550;
	dblCarFreq[3] = 1650;

	// Compute the phase inc per sample
   
	for (i = 0; i < 4; i++)
	{
		dblCarPhaseInc[i] = 2 * M_PI * dblCarFreq[i] / 12000;
	}

	// Now compute the templates: (480 16 bit values total)

	for (i = 0; i < 4; i++)	 // across 4 tones
	{
		dblAngle = 0;
		//'100 baud template
		for (k = 0; k < 120; k++)		// for 120 samples (one 100 baud symbol)
		{
			short work = intAmp * 1.1 * sin(dblAngle);
			// with no envelope control (factor 1.1 chosen emperically to keep
			// FSK peak amplitude slightly below 2 tone peak)
			intNewFSK100bdCarTemplate[i][k] = work;
			dblAngle += dblCarPhaseInc[i];
			if (dblAngle >= 2 * M_PI)
				dblAngle -= 2 * M_PI;
		}
	}


	// Now print them
	len = sprintf(msg,
		"// obsolete versions of this code accommodated multi-carrier FSK.\n"
		"// Template for 4FSK carriers spaced at 100 Hz, 100 baud\n\n"
		"const short intFSK100bdCarTemplate[4][120] = {\n"
	);
	fwrite(msg, 1, len, fout);

	for (i = 0; i < 4; i++)		// across 4 tones
	{
			line = 0;

			for (k = 0; k <= 119; k++) // for 120 samples (one 100 baud symbol)
			{
				if ((k - line) == 9)
				{
					// print 10 to line
					if (line == 0)
						sprintf(prefix, "\t{");
					else
						sprintf(prefix, "\t");
					if (i == 3 && k + 1 == 120)
						sprintf(suffix, "}\n};\n\n\n");
					else if (k + 1 == 120)
						sprintf(suffix, "},\n\n");
					else
						sprintf(suffix, ",\n");
					len = sprintf(msg, "%s%d, %d, %d, %d, %d, %d, %d, %d, %d, %d%s",
					prefix,
					intNewFSK100bdCarTemplate[i][line],
					intNewFSK100bdCarTemplate[i][line + 1],
					intNewFSK100bdCarTemplate[i][line + 2],
					intNewFSK100bdCarTemplate[i][line + 3],
					intNewFSK100bdCarTemplate[i][line + 4],
					intNewFSK100bdCarTemplate[i][line + 5],
					intNewFSK100bdCarTemplate[i][line + 6],
					intNewFSK100bdCarTemplate[i][line + 7],
					intNewFSK100bdCarTemplate[i][line + 8],
					intNewFSK100bdCarTemplate[i][line + 9],
					suffix);

					line = k + 1;
					fwrite(msg, 1, len, fout);
				}
		}
	}


	// 600 baud Tone values for a single carrier case
	// the 600 baud carrier frequencies in Hz

	dblCarFreq[0] = 600;
	dblCarFreq[1] = 1200;
	dblCarFreq[2] = 1800;
	dblCarFreq[3] = 2400;

	// Compute the phase inc per sample

	for (i = 0; i < 4; i++)
	{
		dblCarPhaseInc[i] = 2 * M_PI * dblCarFreq[i] / 12000;
	}

	// Now compute the templates:

	for (i = 0; i < 4; i++)	 // across 20 tones
	{
		dblAngle = 0;
		for (k = 0; k < 20; k++)		// for 20 samples (one 600 baud symbol)
		{
			short work = intAmp * 1.1 * sin(dblAngle);
			// with no envelope control (factor 1.1 chosen emperically to keep
			// FSK peak amplitude slightly below 2 tone peak)
			intNewFSK600bdCarTemplate[i][k] = work;
			dblAngle += dblCarPhaseInc[i];
			if (dblAngle >= 2 * M_PI)
				dblAngle -= 2 * M_PI;
		}
	}

	len = sprintf(msg,
		"// Template for 4FSK carriers spaced at 600 Hz, 600 baud"
		"  (used for FM only)\n\n"
		"const short intFSK600bdCarTemplate[4][20] = {\n");
	fwrite(msg, 1, len, fout);

	for (i = 0; i < 4; i++)		// across 4 tones
	{
			line = 0;
			for (k = 0; k < 20; k++) // for 20 samples (one 600 baud symbol)
			{
				if ((k - line) == 9)
				{
					// print 10 to line
					if (line == 0)
						sprintf(prefix, "\t{");
					else
						sprintf(prefix, "\t");
					if (i == 3 && k + 1 == 20)
						sprintf(suffix, "}\n};\n\n");
					else if (k + 1 == 20)
						sprintf(suffix, "},\n\n");
					else
						sprintf(suffix, ",\n");
					len = sprintf(msg, "%s%d, %d, %d, %d, %d, %d, %d, %d, %d, %d%s",
					prefix,
					intNewFSK600bdCarTemplate[i][line],
					intNewFSK600bdCarTemplate[i][line + 1],
					intNewFSK600bdCarTemplate[i][line + 2],
					intNewFSK600bdCarTemplate[i][line + 3],
					intNewFSK600bdCarTemplate[i][line + 4],
					intNewFSK600bdCarTemplate[i][line + 5],
					intNewFSK600bdCarTemplate[i][line + 6],
					intNewFSK600bdCarTemplate[i][line + 7],
					intNewFSK600bdCarTemplate[i][line + 8],
					intNewFSK600bdCarTemplate[i][line + 9],
					suffix);

					line = k + 1;
					fwrite(msg, 1, len, fout);
				}
		}
	}
}

//	 Subroutine to initialize valid frame types 

// obsolete versions of this code partially accommodated intBaud of 200 and 167 as well as 100.
void GeneratePSKTemplates()
{
	// Generate templates of 120 samples (each template = 10 ms) for each of the 9 possible carriers used in PSK modulation. 
	// Used to speed up computation of PSK frames and reduce use of Sin functions.
	// Amplitude values will have to be scaled based on the number of Active Carriers (1, 2, 4 or 8) initial values should be OK for 1 carrier
	// Tone values 
	// the carrier frequencies in Hz

	int i, j ,k;
	float dblCarFreq[]  = {800, 1000, 1200, 1400, 1500, 1600, 1800, 2000, 2200};
	char msg[300];
	int len;
	int line = 0;
	char prefix[10] = "";
	char suffix[10] = "";

	//  for 1 carrier modes use index 4 (1500)
	//  for 2 carrier modes use indexes 3, 5 (1400 and 1600 Hz)
	//  for 4 carrier modes use indexes 2, 3, 5, 6 (1200, 1400, 1600, 1800Hz) 
	//  for 8 carrier modes use indexes 0,1,2,3,5,6,7,8 (800, 1000, 1200, 1400, 1600, 1800, 2000, 2200 Hz) 

	float dblCarPhaseInc[9] ;	// the phase inc per sample

	float dblAngle;			 // Angle in radians

        //Dim dblPeakAmp As Double = intAmp * 0.5 ' may need to adjust 
	

		// Compute the phase inc per sample

	for (i = 0; i <= 8; i++)
	{
		dblCarPhaseInc[i] = 2 * M_PI * dblCarFreq[i] / 12000;
	}

	// Now compute the templates: (4320 16 bit values total)

	for (i = 0; i <= 8; i++)		// across 9 tones
	{
		for (j = 0; j <= 3; j++)	// ( using only half the values and sign compliment for the opposite phases)
		{
			dblAngle = 2 * M_PI * j / 8;
			for (k = 0; k <= 119; k++) // for 120 samples (one 100 baud symbol)
			{
				// This source file (CalcTemplates.c) was not functional as
				// inherited from g8bpq/ardop, though most of the calculations
				// were correctly implemented.  However, of the next two lines,
				// the one marked as "with no envelope control" was used while
				// the one marked as "with envelope control" was commented out.
				// While that may have been "correct", it did not produce values
				// which matched ardopSampleArrays.c as inherited from
				// g8bpq/ardop.  So, for now, "with envelope control" it is
				// enabled.  Whether this should be changed requires further
				// evaluation.  -- LaRue (June 2024)

				// with no envelope control
				// intNewPSK100bdCarTemplate[i][j][k] = (short)round(intAmp * sin(dblAngle));

				// with envelope control using Sin
				intNewPSK100bdCarTemplate[i][j][k] = (short)round(intAmp * sin(M_PI * k / 119) * sin(dblAngle));

				dblAngle += dblCarPhaseInc[i];
				if (dblAngle >= 2 * M_PI)
					dblAngle -= 2 * M_PI;
			}
		}
	}

// Now print them

	len = sprintf(msg,
		"// Templates over 9 carriers for 4 phase values and 120 samples\n"
		"// (only positive Phase values are in the table, sign reversal\n"
		"//  is used to get the negative phase values).\n"
		"// This reduces the table size from 8640 to 4320 integers\n\n"
		"const short intPSK100bdCarTemplate[9][4][120] = {\n");
	fwrite(msg, 1, len, fout);

	for (i = 0; i <= 8; i++)		// across 9 tones
	{
		for (j = 0; j <= 3; j++)	// ( using only half the values and sign compliment for the opposite phases) 
		{
			line = 0;

			for (k = 0; k <= 119; k++) // for 120 samples (one 100 baud symbol)
			{
				if ((k - line) == 9)
				{
					// print 10 to line
					if (line == 0 && j == 0)
						sprintf(prefix, "\t{{");
					else if (line == 0)
						sprintf(prefix, "\t{");
					else
						sprintf(prefix, "\t");
					if (i + 1 == 9 && j + 1 == 4 && k + 1 == 120)
						sprintf(suffix, "}}\n};\n\n");
					else if (j + 1 == 4 && k + 1 == 120)
						sprintf(suffix, "}},\n\n");
					else if (k + 1 == 120)
						sprintf(suffix, "},\n");
					else
						sprintf(suffix, ",\n");
					len = sprintf(msg, "%s%d, %d, %d, %d, %d, %d, %d, %d, %d, %d%s",
					prefix,
					intNewPSK100bdCarTemplate[i][j][line],
					intNewPSK100bdCarTemplate[i][j][line + 1],
					intNewPSK100bdCarTemplate[i][j][line + 2],
					intNewPSK100bdCarTemplate[i][j][line + 3],
					intNewPSK100bdCarTemplate[i][j][line + 4],
					intNewPSK100bdCarTemplate[i][j][line + 5],
					intNewPSK100bdCarTemplate[i][j][line + 6],
					intNewPSK100bdCarTemplate[i][j][line + 7],
					intNewPSK100bdCarTemplate[i][j][line + 8],
					intNewPSK100bdCarTemplate[i][j][line + 9],
					suffix);
					fwrite(msg, 1, len, fout);

					line = k + 1;
				}
			}
		}
	}
}


void CompareResults() {
	int oldvmax;
	int oldvmin;
	int vmax;
	int vmin;
	int delta;
	int dmax;
	int dmin;
	double oldvsum;
	double vsum;
	double oldvsquaredsum;
	double vsquaredsum;
	int iNum;
	int jNum;
	int kNum;

	printf(
		"\nComparison of existing to newly calculated results:\n"
		"                                 Existing    New Values  Change\n"
	);

	printf("\n");
	oldvmax = 0;
	oldvmin = 0;
	vmax = 0;
	vmin = 0;
	dmax = 0;
	dmin = 0;
	oldvsum = 0.0;
	vsum = 0.0;
	oldvsquaredsum = 0.0;
	vsquaredsum = 0.0;
	iNum = 120;
	for (int i = 0; i < iNum; i++) {
		oldvsum += int50BaudTwoToneLeaderTemplate[i];
		vsum += intNew50BaudTwoToneLeaderTemplate[i];
		oldvsquaredsum += pow(int50BaudTwoToneLeaderTemplate[i], 2);
		vsquaredsum += pow(intNew50BaudTwoToneLeaderTemplate[i], 2);
		if (int50BaudTwoToneLeaderTemplate[i] < oldvmin)
			oldvmin = int50BaudTwoToneLeaderTemplate[i];
		if (int50BaudTwoToneLeaderTemplate[i] > oldvmax)
			oldvmax = intNew50BaudTwoToneLeaderTemplate[i];

		if (intNew50BaudTwoToneLeaderTemplate[i] < vmin)
			vmin = intNew50BaudTwoToneLeaderTemplate[i];
		if (intNew50BaudTwoToneLeaderTemplate[i] > vmax)
			vmax = intNew50BaudTwoToneLeaderTemplate[i];

		delta = intNew50BaudTwoToneLeaderTemplate[i] -
			int50BaudTwoToneLeaderTemplate[i];

		if (delta < dmin)
			dmin = delta;
		if (delta > dmax)
			dmax = delta;
	}
	printf(
		"int50BaudTwoToneLeaderTemplate[] using %d values"
		" (max single value change: %d %d)\n"
		"  Maximum value:                 %-+10d  %-+10d  %-+10d\n"
		"  Minimum value:                 %-+10d  %-+10d  %-+10d\n"
		"  Mean value (ideally zero):     %-+10.2f  %-+10.2f  %-+10.2f\n"
		"  RMS Power:                     %-10.2f  %-10.2f  %-10.2f\n",
		iNum,
		dmin, dmax,
		oldvmax, vmax, vmax - oldvmax,
		oldvmin, vmin, vmin - oldvmin,
		oldvsum / iNum, vsum / iNum, vsum / iNum - oldvsum / iNum,
		sqrt(oldvsquaredsum / iNum), sqrt(vsquaredsum / iNum),
		sqrt(vsquaredsum / iNum) - sqrt(oldvsquaredsum / iNum)
	);

	printf("\n");
	iNum = 4;
	kNum = 240;
	for (int i = 0; i < iNum; i++) {
		oldvmax = 0;
		oldvmin = 0;
		vmax = 0;
		vmin = 0;
		dmax = 0;
		dmin = 0;
		oldvsum = 0.0;
		vsum = 0.0;
		oldvsquaredsum = 0.0;
		vsquaredsum = 0.0;
		for (int k = 0; k < kNum; k++) {
			oldvsum += intFSK50bdCarTemplate[i][k];
			vsum += intNewFSK50bdCarTemplate[i][k];
			oldvsquaredsum += pow(intFSK50bdCarTemplate[i][k], 2);
			vsquaredsum += pow(intNewFSK50bdCarTemplate[i][k], 2);
			if (intFSK50bdCarTemplate[i][k] < oldvmin)
				oldvmin = intFSK50bdCarTemplate[i][k];
			if (intFSK50bdCarTemplate[i][k] > oldvmax)
				oldvmax = intFSK50bdCarTemplate[i][k];

			if (intNewFSK50bdCarTemplate[i][k] < vmin)
				vmin = intNewFSK50bdCarTemplate[i][k];
			if (intNewFSK50bdCarTemplate[i][k] > vmax)
				vmax = intNewFSK50bdCarTemplate[i][k];

			delta = intNewFSK50bdCarTemplate[i][k] -
				intFSK50bdCarTemplate[i][k];

			if (delta < dmin)
				dmin = delta;
			if (delta > dmax)
				dmax = delta;
		}
		printf(
			"intFSK50bdCarTemplate[%d][] using %d values"
			" (max single value change: %d %d)\n"
			"  Maximum value:                 %-+10d  %-+10d  %-+10d\n"
			"  Minimum value:                 %-+10d  %-+10d  %-+10d\n"
			"  Mean value (ideally zero):     %-+10.2f  %-+10.2f  %-+10.2f\n"
			"  RMS Power:                     %-10.2f  %-10.2f  %-10.2f\n",
			i, kNum,
			dmin, dmax,
			oldvmax, vmax, vmax - oldvmax,
			oldvmin, vmin, vmin - oldvmin,
			oldvsum / kNum, vsum / kNum, vsum / kNum - oldvsum / kNum,
			sqrt(oldvsquaredsum / kNum), sqrt(vsquaredsum / kNum),
			sqrt(vsquaredsum / kNum) - sqrt(oldvsquaredsum / kNum)
		);
	}

	printf("\n");
	iNum = 4;
	kNum = 20;
	for (int i = 0; i < iNum; i++) {
		oldvmax = 0;
		oldvmin = 0;
		vmax = 0;
		vmin = 0;
		dmax = 0;
		dmin = 0;
		oldvsum = 0.0;
		vsum = 0.0;
		oldvsquaredsum = 0.0;
		vsquaredsum = 0.0;
		for (int k = 0; k < kNum; k++) {
			oldvsum += intFSK600bdCarTemplate[i][k];
			vsum += intNewFSK600bdCarTemplate[i][k];
			oldvsquaredsum += pow(intFSK600bdCarTemplate[i][k], 2);
			vsquaredsum += pow(intNewFSK600bdCarTemplate[i][k], 2);
			if (intFSK600bdCarTemplate[i][k] < oldvmin)
				oldvmin = intFSK600bdCarTemplate[i][k];
			if (intFSK600bdCarTemplate[i][k] > oldvmax)
				oldvmax = intFSK600bdCarTemplate[i][k];

			if (intNewFSK600bdCarTemplate[i][k] < vmin)
				vmin = intNewFSK600bdCarTemplate[i][k];
			if (intNewFSK600bdCarTemplate[i][k] > vmax)
				vmax = intNewFSK600bdCarTemplate[i][k];

			delta = intNewFSK600bdCarTemplate[i][k] -
				intFSK600bdCarTemplate[i][k];

			if (delta < dmin)
				dmin = delta;
			if (delta > dmax)
				dmax = delta;
		}
		printf(
			"intFSK600bdCarTemplate[%d][] using %d values"
			" (max single value change: %d %d)\n"
			"  Maximum value:                 %-+10d  %-+10d  %-+10d\n"
			"  Minimum value:                 %-+10d  %-+10d  %-+10d\n"
			"  Mean value (ideally zero):     %-+10.2f  %-+10.2f  %-+10.2f\n"
			"  RMS Power:                     %-10.2f  %-10.2f  %-10.2f\n",
			i, kNum,
			dmin, dmax,
			oldvmax, vmax, vmax - oldvmax,
			oldvmin, vmin, vmin - oldvmin,
			oldvsum / kNum, vsum / kNum, vsum / kNum - oldvsum / kNum,
			sqrt(oldvsquaredsum / kNum), sqrt(vsquaredsum / kNum),
			sqrt(vsquaredsum / kNum) - sqrt(oldvsquaredsum / kNum)
		);
	}

	printf("\n");
	iNum = 4;
	kNum = 120;
	for (int i = 0; i < iNum; i++) {
		oldvmax = 0;
		oldvmin = 0;
		vmax = 0;
		vmin = 0;
		dmax = 0;
		dmin = 0;
		oldvsum = 0.0;
		vsum = 0.0;
		oldvsquaredsum = 0.0;
		vsquaredsum = 0.0;
		for (int k = 0; k < kNum; k++) {
			oldvsum += intFSK100bdCarTemplate[i][k];
			vsum += intNewFSK100bdCarTemplate[i][k];
			oldvsquaredsum += pow(intFSK100bdCarTemplate[i][k], 2);
			vsquaredsum += pow(intNewFSK100bdCarTemplate[i][k], 2);
			if (intFSK100bdCarTemplate[i][k] < oldvmin)
				oldvmin = intFSK100bdCarTemplate[i][k];
			if (intFSK100bdCarTemplate[i][k] > oldvmax)
				oldvmax = intFSK100bdCarTemplate[i][k];

			if (intNewFSK100bdCarTemplate[i][k] < vmin)
				vmin = intNewFSK100bdCarTemplate[i][k];
			if (intNewFSK100bdCarTemplate[i][k] > vmax)
				vmax = intNewFSK100bdCarTemplate[i][k];

			delta = intNewFSK100bdCarTemplate[i][k] -
				intFSK100bdCarTemplate[i][k];

			if (delta < dmin)
				dmin = delta;
			if (delta > dmax)
				dmax = delta;
		}
		printf(
			"intFSK100bdCarTemplate[%d][] using %d values"
			" (max single value change: %d %d)\n"
			"  Maximum value:                 %-+10d  %-+10d  %-+10d\n"
			"  Minimum value:                 %-+10d  %-+10d  %-+10d\n"
			"  Mean value (ideally zero):     %-+10.2f  %-+10.2f  %-+10.2f\n"
			"  RMS Power:                     %-10.2f  %-10.2f  %-10.2f\n",
			i, kNum,
			dmin, dmax,
			oldvmax, vmax, vmax - oldvmax,
			oldvmin, vmin, vmin - oldvmin,
			oldvsum / kNum, vsum / kNum, vsum / kNum - oldvsum / kNum,
			sqrt(oldvsquaredsum / kNum), sqrt(vsquaredsum / kNum),
			sqrt(vsquaredsum / kNum) - sqrt(oldvsquaredsum / kNum)
		);
	}

	printf("\n");
	iNum = 9;
	jNum = 4;
	kNum = 120;
	for (int i = 0; i < iNum; i++) {
		printf("\n");
		for (int j = 0; j < jNum; j++) {
			oldvmax = 0;
			oldvmin = 0;
			vmax = 0;
			vmin = 0;
			dmax = 0;
			dmin = 0;
			oldvsum = 0.0;
			vsum = 0.0;
			oldvsquaredsum = 0.0;
			vsquaredsum = 0.0;
			for (int k = 0; k < kNum; k++) {
				oldvsum += intPSK100bdCarTemplate[i][j][k];
				vsum += intNewPSK100bdCarTemplate[i][j][k];
				oldvsquaredsum += pow(intPSK100bdCarTemplate[i][j][k], 2);
				vsquaredsum += pow(intNewPSK100bdCarTemplate[i][j][k], 2);
				if (intPSK100bdCarTemplate[i][j][k] < oldvmin)
					oldvmin = intPSK100bdCarTemplate[i][j][k];
				if (intPSK100bdCarTemplate[i][j][k] > oldvmax)
					oldvmax = intPSK100bdCarTemplate[i][j][k];

				if (intNewPSK100bdCarTemplate[i][j][k] < vmin)
					vmin = intNewPSK100bdCarTemplate[i][j][k];
				if (intNewPSK100bdCarTemplate[i][j][k] > vmax)
					vmax = intNewPSK100bdCarTemplate[i][j][k];

				delta = intNewPSK100bdCarTemplate[i][j][k] -
					intPSK100bdCarTemplate[i][j][k];

				if (delta < dmin)
					dmin = delta;
				if (delta > dmax)
					dmax = delta;
			}
			printf(
				"intPSK100bdCarTemplate[%d][%d][] using %d values"
				" (max single value change: %d %d)\n"
				"  Maximum value:                 %-+10d  %-+10d  %-+10d\n"
				"  Minimum value:                 %-+10d  %-+10d  %-+10d\n"
				"  Mean value (ideally zero):     %-+10.2f  %-+10.2f  %-+10.2f\n"
				"  RMS Power:                     %-10.2f  %-10.2f  %-10.2f\n",
				i, j, kNum,
				dmin, dmax,
				oldvmax, vmax, vmax - oldvmax,
				oldvmin, vmin, vmin - oldvmin,
				oldvsum / kNum, vsum / kNum, vsum / kNum - oldvsum / kNum,
				sqrt(oldvsquaredsum / kNum), sqrt(vsquaredsum / kNum),
				sqrt(vsquaredsum / kNum) - sqrt(oldvsquaredsum / kNum)
			);
		}
	}
}

int main() {
	char pathname[] = "newArdopSampleArrays.c";
	fout = fopen(pathname, "wb");
	printf(
		"Writing new sample arrays to %s.\n"
		"  To have ardopcf use these new values, replace the existing\n"
		"  ardopSampleArrays.c with %s and recompile ardopcf.\n"
		"  WARNING:\n"
		"    Before replacing ardopSampleArrays.c with newly calculated\n"
		"    values, printed comparison of Existing to New Values should\n"
		"    be carefully inspected to ensure that no unintentional changes\n"
		"    are being made.  If uncertain about these results, further\n"
		"    evaluation of the new values should be done.\n",
		pathname, pathname
	);
	Generate50BaudTwoToneLeaderTemplate();
	GenerateFSKTemplates();
	GeneratePSKTemplates();
	fclose(fout);
	CompareResults();
}
