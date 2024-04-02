#include <stdio.h>

struct WavFile 
{
	FILE *f;
	int NumSamples;
};

// Open a WAV for for writing.  Return NULL on failure.
struct WavFile* OpenWavW(const char *pathname);

// Close a WAV file after updating the file header
int CloseWav(struct WavFile *wf);

// Write data to WAV file
int WriteWav(short *ptr, int num, struct WavFile *wf);
