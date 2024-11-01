#include <stdbool.h>

#define FRQCNT 4  // Number of frequencies/tones per carrier
#define MAXDFTLEN 240  // Maximum length (N = number of samples) of DFT to calculate
#define SRATE 12000  // sample rate (Hz)

extern bool blnSdftInitialized;
void init_sdft(int intCenterFrq, short * intSamples, int intDftLen);
int sdft(short * intSamples, int intToneMags[4096], int *intToneMagsIndex, int intDftLen);
