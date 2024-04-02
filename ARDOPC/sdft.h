#include <stdbool.h>

#define FRQCNT 4  // Number of frequencies/tones per carrier
#define CARCNT 4  // Maximum number of carriers
#define MAXDFTLEN 240  // Maximum length (N = number of samples) of DFT to calculate
#define SRATE 12000  // sample rate (Hz)

extern bool blnSdftInitialized;
void init_sdft(int * intCenterFrqs, short * intSamples, int intDftLen);
int sdft(int * intCenterFrqs, short * intSamples, int intToneMags[CARCNT][4096], int intToneMagsIndex[CARCNT], int intDftLen);
