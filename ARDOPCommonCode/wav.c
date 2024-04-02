// Write data to single channel 16-bit signed integer WAV file with a sample rate of 12000 Hz

#include <stdio.h>
#include <stdlib.h>
#include "ardopcommon.h"
#include "wav.h"

// PCM WAV file header
char WavHeader[] = {
	0x52, 0x49, 0x46, 0x46, // "RIFF"
	0x00, 0x00, 0x00, 0x00, // DUMMY file size - 8 in bytes = 2 * NumSamples + 36
	0x57, 0x41, 0x56, 0x45, // "WAVE"
	0x66, 0x6d, 0x74, 0x20, // "fmt "
	0x10, 0x00, 0x00, 0x00, // Subchunk size = 16 for PCM
	0x01, 0x00,             // WAVE type format
	0x01, 0x00,             // single channel
	0xE0, 0x2E, 0x00, 0x00, // sample rate = 12000 (Hz)
	0xC0, 0x5D, 0x00, 0x00, // bytes/sec = 24000
	0x02, 0x00,             // block alignment = (num channels * bitsPerChannel / 8) = 2
	0x10, 0x00,             // bits per sample = 16
	0x64, 0x61, 0x74, 0x61, // "data"
	0x00, 0x00, 0x00, 0x00, // DUMMY data size in bytes = 2 * NumSamples for 16-bit samples
};

// Open a WAV for for writing.  Return NULL on failure.
struct WavFile* OpenWavW(const char *pathname)
{
	WriteDebugLog(LOGDEBUG, "Opening WAV file for writing: %s", pathname);
	struct WavFile *wf = (struct WavFile *) malloc(sizeof(struct WavFile));
	if (wf == NULL)
	{
		WriteDebugLog(LOGDEBUG, "Unable to allocate WavFile struct.");
		return NULL;
	}
		
	wf->f = fopen(pathname, "wb");
	if (wf->f == NULL)
	{
		WriteDebugLog(LOGDEBUG, "Unable to open WAV file.");
		return NULL;
	}
		
	wf->NumSamples = 0;
	
	// Write the header for the WAV file.  Use dummy values of 0 for size of file and size of chunk.
	fwrite(WavHeader, 1, sizeof(WavHeader), wf->f);
	return wf;
}

// Close a WAV file after updating the file header
// If ardop is stopped while wf is open, then these required updates will not
// be done, and the WAV file will not be valid.  However, using only the file
// size, they can be fixed later.
int CloseWav(struct WavFile *wf)
{
	int value;
	int retval;
	
	WriteDebugLog(LOGDEBUG, "Finalizing and closing WAV file.");
	// file length should be 44 + 2*wf->NumSamples
	fseek(wf->f, 4, SEEK_SET);
	value = 2 * wf->NumSamples + 36; 
	fwrite(&value, 1, 4, wf->f);
	fseek(wf->f, 40, SEEK_SET);
	value = 2 * wf->NumSamples;
	fwrite(&value, 1, 4, wf->f);
	retval = fclose(wf->f);
	free(wf);
	return retval;
}

// Write data to an open WAV file
int WriteWav(short *ptr, int num, struct WavFile *wf)
{
	// num is the number of 16-bit signed integers from ptr to be written
	fwrite(ptr, 2, num, wf->f);
	wf->NumSamples += num;
	return (num * 2);
}
