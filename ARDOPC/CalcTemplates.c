//
// This code calculates and writes to files the templates
// used to generate modulation samples.

// Rick's code gererates them dynamically as program start, but
// that measns they have to be in RAM. By pregenerating and 
// compliling them they can be placed in program space
// This is necessary with the small RAM space of embedded CPUs

// This only needs to be run once to generate the source files

// Keep code in case we need to change, but don't compile

#if 0

#include "ARDOPC.h"

#pragma warning(disable : 4244)		// Code does lots of int float to int


static int intAmp = 26000;	   // Selected to have some margin in calculations with 16 bit values (< 32767) this must apply to all filters as well. 

void Generate50BaudTwoToneLeaderTemplate()
{
	int i;
	float x, y, z;
	int line = 0;

	FILE * fp1;

	char msg[80];
	int len;

	fp1 = fopen("s:\\leadercoeffs.txt", "wb");

	for (i = 0; i < 240; i++)
	{
		y = (sin(((1500.0 - 25) / 1500) * (i / 8.0 * 2 * M_PI)));
		z = (sin(((1500.0 + 25) / 1500) * (i / 8.0 * 2 * M_PI)));

		x = intAmp * 0.55 * (y - z);
		int50BaudTwoToneLeaderTemplate[i] = (short)x + 0.5;

		if ((i - line) == 9)
		{
			// print the last 10 values

			len = sprintf(msg, "\t%d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
				int50BaudTwoToneLeaderTemplate[line],
				int50BaudTwoToneLeaderTemplate[line + 1],
				int50BaudTwoToneLeaderTemplate[line + 2],
				int50BaudTwoToneLeaderTemplate[line + 3],
				int50BaudTwoToneLeaderTemplate[line + 4],
				int50BaudTwoToneLeaderTemplate[line + 5],
				int50BaudTwoToneLeaderTemplate[line + 6],
				int50BaudTwoToneLeaderTemplate[line + 7],
				int50BaudTwoToneLeaderTemplate[line + 8],
				int50BaudTwoToneLeaderTemplate[line + 9]);

			line = i + 1;

			fwrite(msg, 1, len, fp1);
		}
	}		
	fclose(fp1);
}

void GenerateTwoToneLeaderTemplate()
{
	// to create leader alternate these template samples reversing sign on each adjacent symbol
    
	int i;
	float x, y, z;
	int line = 0;

	FILE * fp1;

	char msg[80];
	int len;

	fp1 = fopen("s:\\leadercoeffs.txt", "wb");

	for (i = 0; i < 120; i++)
	{
		y = (sin(((1500.0 - 50) / 1500) * (i / 8.0 * 2 * M_PI)));
		z = (sin(((1500.0 + 50) / 1500) * (i / 8.0 * 2 * M_PI)));

		x = intAmp * 0.6 * (y - z);
		intTwoToneLeaderTemplate[i] = (short)x + 0.5;

		if ((i - line) == 9)
		{
			// print the last 10 values

			len = sprintf(msg, "\t%d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
				intTwoToneLeaderTemplate[line],
				intTwoToneLeaderTemplate[line + 1],
				intTwoToneLeaderTemplate[line + 2],
				intTwoToneLeaderTemplate[line + 3],
				intTwoToneLeaderTemplate[line + 4],
				intTwoToneLeaderTemplate[line + 5],
				intTwoToneLeaderTemplate[line + 6],
				intTwoToneLeaderTemplate[line + 7],
				intTwoToneLeaderTemplate[line + 8],
				intTwoToneLeaderTemplate[line + 9]);

			line = i + 1;

			fwrite(msg, 1, len, fp1);
		}
	}		
	fclose(fp1);
}

// Subroutine to create the FSK symbol templates

void GenerateFSKTemplates()
{
	// Generate templates of 240 samples (each symbol template = 20 ms) for each of the 4 possible carriers used in 200 Hz BW FSK modulation.
	// Generate templates of 120 samples (each symbol template = 10 ms) for each of the 20 possible carriers used in 500, 1000 and 2000 Hz BW 4FSK modulation.
	//Used to speed up computation of FSK frames and reduce use of Sin functions.
	//50 baud Tone values 

	// obsolete versions of this code accommodated multi-carrier FSK
	float dblCarFreq[] = {1425, 1475, 1525, 1575};

	float dblAngle;		// Angle in radians
	float dblCarPhaseInc[20]; 
	int i, k;

	char msg[256];
	int len;
	int line = 0;
	FILE * fp1;

	// Compute the phase inc per sample

    for (i = 0; i < 4; i++) 
	{
		dblCarPhaseInc[i] = 2 * M_PI * dblCarFreq[i] / 12000;
	}
	
	// Now compute the templates: (960 32 bit values total) 
	
	for (i = 0; i < 4; i++)			// across the 4 tones for 50 baud frequencies
	{
		dblAngle = 0;
		// 50 baud template

		line = 0;

		for (k = 0; k < 240; k++)	// for 240 samples (one 50 baud symbol)
		{
			intFSK50bdCarTemplate[i][k] = intAmp * 1.1 * sin(dblAngle);  // with no envelope control (factor 1.1 chosen emperically to keep FSK peak amplitude slightly below 2 tone peak)
			dblAngle += dblCarPhaseInc[i];

			if (dblAngle >= 2 * M_PI)
				dblAngle -= 2 * M_PI;
		
			if ((k - line) == 9)
			{
				// print the last 10 values

				len = sprintf(msg, "\t%d, %d, %d, %d, %d, %d, %d, %d, %d, %d,\n",
				intFSK50bdCarTemplate[i][line],
				intFSK50bdCarTemplate[i][line + 1],
				intFSK50bdCarTemplate[i][line + 2],
				intFSK50bdCarTemplate[i][line + 3],
				intFSK50bdCarTemplate[i][line + 4],
				intFSK50bdCarTemplate[i][line + 5],
				intFSK50bdCarTemplate[i][line + 6],
				intFSK50bdCarTemplate[i][line + 7],
				intFSK50bdCarTemplate[i][line + 8],
				intFSK50bdCarTemplate[i][line + 9]);

				line = k + 1;

//				fwrite(msg, 1, len, fp1);
			}
		}
	}


	// 4FSK templates for 600 baud (2 Khz bandwidth) 
	for (i = 0; i < 4; i++)		 // across the 4 tones for 600 baud frequencies
	{
		dblAngle = 0;
		//600 baud template
		for (k = 0; k < 20; k++)	 // for 20 samples (one 600 baud symbol)
		{
			int xx = intAmp * 1.1 * sin(dblAngle); // with no envelope control (factor 1.1 chosen emperically to keep FSK peak amplitude slightly below 2 tone peak)
			if (intFSK600bdCarTemplate[i][k] != xx)
				printf("Duff\n");

			dblAngle += (2 * M_PI / 12000) * (600 + i * 600);
			if (dblAngle >= 2 * M_PI)
				dblAngle -= 2 * M_PI;
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

	// Now compute the templates: (2400 32 bit values total)  

	for (i = 0; i < 4; i++)	 // across 20 tones
	{
		dblAngle = 0;
		//'100 baud template
		for (k = 0; k < 120; k++)		// for 120 samples (one 100 baud symbol)
		{
			short work = intAmp * 1.1 * sin(dblAngle);
			intFSK100bdCarTemplate[i][k] = work; // with no envelope control (factor 1.1 chosen emperically to keep FSK peak amplitude slightly below 2 tone peak)
				
			dblAngle += dblCarPhaseInc[i];
			if (dblAngle >= 2 * M_PI)
				dblAngle -= 2 * M_PI;
		}
	}


	// Now print them


	fp1 = fopen("s:\\fskcoeffs100.txt", "wb");

	len = sprintf(msg, "short intFSK100bdCarTemplate[4][120] = \r\n");
	fwrite(msg, 1, len, fp1);

	len = sprintf(msg, "\t{{\r\n");
	fwrite(msg, 1, len, fp1);

	for (i = 0; i < 4; i++)		// across 9 tones
	{
			line = 0;

			for (k = 0; k <= 119; k++) // for 120 samples (one 100 baud symbol, 200 baud modes will just use half of the data)
			{
				if ((k - line) == 9)
				{
					// print 10 to line

					len = sprintf(msg, "\t%d, %d, %d, %d, %d, %d, %d, %d, %d, %d,\n",
					intFSK100bdCarTemplate[i][line],
					intFSK100bdCarTemplate[i][line + 1],
					intFSK100bdCarTemplate[i][line + 2],
					intFSK100bdCarTemplate[i][line + 3],
					intFSK100bdCarTemplate[i][line + 4],
					intFSK100bdCarTemplate[i][line + 5],
					intFSK100bdCarTemplate[i][line + 6],
					intFSK100bdCarTemplate[i][line + 7],
					intFSK100bdCarTemplate[i][line + 8],
					intFSK100bdCarTemplate[i][line + 9]);

					line = k + 1;

					if (k == 119)
					{
						len += sprintf(&msg[len-2], "},\r\n\r\n\t{");
						len -=2;
					}
					fwrite(msg, 1, len, fp1);
				}
	
		}
	}

	len = sprintf(msg, "\t}};\r\n");
	fwrite(msg, 1, len, fp1);

	fclose(fp1);

	fp1 = fopen("s:\\fskcoeffs600.txt", "wb");

	len = sprintf(msg, "short intFSK600bdCarTemplate[4][20] = {\r\n");
	fwrite(msg, 1, len, fp1);

	len = sprintf(msg, "\t{\r\n");
	fwrite(msg, 1, len, fp1);

	for (i = 0; i < 4; i++)		// across 4 tones
	{
			line = 0;
			for (k = 0; k < 20; k++) // for 20 samples (one 600 baud symbol)
			{
				if ((k - line) == 9)
				{
					// print 10 to line

					len = sprintf(msg, "\t%d, %d, %d, %d, %d, %d, %d, %d, %d, %d,\n",
					intFSK600bdCarTemplate[i][line],
					intFSK600bdCarTemplate[i][line + 1],
					intFSK600bdCarTemplate[i][line + 2],
					intFSK600bdCarTemplate[i][line + 3],
					intFSK600bdCarTemplate[i][line + 4],
					intFSK600bdCarTemplate[i][line + 5],
					intFSK600bdCarTemplate[i][line + 6],
					intFSK600bdCarTemplate[i][line + 7],
					intFSK600bdCarTemplate[i][line + 8],
					intFSK600bdCarTemplate[i][line + 9]);

					line = k + 1;

					if (k == 19)
					{
						len += sprintf(&msg[len-2], "},\r\n\r\n\t{");
						len -=2;
					}
					fwrite(msg, 1, len, fp1);
				}
	
		}
	}

	len = sprintf(msg, "\t}};\r\n");
	fwrite(msg, 1, len, fp1);

	fclose(fp1);



}

//	 Subroutine to initialize valid frame types 


//	Subroutine to create the PSK symbol templates for 8 tones and 8 phases at 200 baud
float round(float x);

// obsolete versions of this code partially accommodated intBaud of 200 and 167 as well as 100.
VOID GeneratePSKTemplates()
{
	// Generate templates of 120 samples (each template = 10 ms) for each of the 9 possible carriers used in PSK modulation. 
	// Used to speed up computation of PSK frames and reduce use of Sin functions.
	// Amplitude values will have to be scaled based on the number of Active Carriers (1, 2, 4 or 8) initial values should be OK for 1 carrier
	// Tone values 
	// the carrier frequencies in Hz

	int i, j ,k;
	float dblCarFreq[]  = {800, 1000, 1200, 1400, 1500, 1600, 1800, 2000, 2200};
	FILE * fp1;
	char msg[256];
	int len;
	int line = 0;

	//  for 1 carrier modes use index 4 (1500)
	//  for 2 carrier modes use indexes 3, 5 (1400 and 1600 Hz)
	//  for 4 carrier modes use indexes 2, 3, 5, 6 (1200, 1400, 1600, 1800Hz) 
	//  for 8 carrier modes use indexes 0,1,2,3,5,6,7,8 (800, 1000, 1200, 1400, 1600, 1800, 2000, 2200 Hz) 

	float dblCarPhaseInc[9] ;	// the phase inc per sample

	float dblAngle;			 // Angle in radians

        //Dim dblPeakAmp As Double = intAmp * 0.5 ' may need to adjust 
	
	fp1 = fopen("d:\\PSKcoeffs.txt", "wb");

		// Compute the phase inc per sample

	for (i = 0; i <= 8; i++)
	{
		dblCarPhaseInc[i] = 2 * M_PI * dblCarFreq[i] / 12000;
	}

	// Now compute the templates: (4320 32 bit values total) 

	for (i = 0; i <= 8; i++)		// across 9 tones
	{
		for (j = 0; j <= 3; j++)	// ( using only half the values and sign compliment for the opposit phases) 
		{
			dblAngle = 2 * M_PI * j / 8;
			for (k = 0; k <= 119; k++) // for 120 samples (one 100 baud symbol)
			{
	//			float xx = intAmp * sin(M_PI * k / 119) * sin(dblAngle);
	//			float xx2 = round(xx);
	//			int xxi= (int)(xx2);
				
	//			if (intPSK100bdCarTemplate[i][j][k] != xxi)
	////			{
	//				k++;
	//				k--;
	//			}
		
				intPSK100bdCarTemplate[i][j][k] = (short)round(intAmp * sin(dblAngle));  // with no envelope control
	          // intPSK100bdCarTemplate(i, j, k) = intAmp * Sin(PI * k / 119) * Sin(dblAngle) ' with envelope control using Sin
 		
				dblAngle += dblCarPhaseInc[i];
				
				if (dblAngle >= 2 * M_PI)
					dblAngle -= 2 * M_PI;
			}
		}
	}

// Now print them

	len = sprintf(msg, "\tshort intPSK100bdCarTemplate[9][4][120] = \r\n");
	fwrite(msg, 1, len, fp1);

	len = sprintf(msg, "\t{{{\r\n");
	fwrite(msg, 1, len, fp1);

	for (i = 0; i <= 8; i++)		// across 9 tones
	{
		for (j = 0; j <= 3; j++)	// ( using only half the values and sign compliment for the opposit phases) 
		{
			line = 0;

			for (k = 0; k <= 119; k++) // for 120 samples (one 100 baud symbol, 200 baud modes will just use half of the data)
			{
				if ((k - line) == 9)
				{
					// print 10 to line

					len = sprintf(msg, "\t%d, %d, %d, %d, %d, %d, %d, %d, %d, %d,\n",
					intPSK100bdCarTemplate[i][j][line],
					intPSK100bdCarTemplate[i][j][line + 1],
					intPSK100bdCarTemplate[i][j][line + 2],
					intPSK100bdCarTemplate[i][j][line + 3],
					intPSK100bdCarTemplate[i][j][line + 4],
					intPSK100bdCarTemplate[i][j][line + 5],
					intPSK100bdCarTemplate[i][j][line + 6],
					intPSK100bdCarTemplate[i][j][line + 7],
					intPSK100bdCarTemplate[i][j][line + 8],
					intPSK100bdCarTemplate[i][j][line + 9]);

					line = k + 1;

					if (k == 119)
					{
						len += sprintf(&msg[len-2], "},\r\n\t{");
						len -=2;
					}
					fwrite(msg, 1, len, fp1);
				}
			}
//			len = sprintf(msg, "\t}{\r\n");
//			fwrite(msg, 1, len, fp1);
		}
		len = sprintf(msg, "\t}}{{\r\n");
		fwrite(msg, 1, len, fp1);
	}

	len = sprintf(msg, "\t}}};\r\n");
	fwrite(msg, 1, len, fp1);
	fclose(fp1);
	
}


#endif


