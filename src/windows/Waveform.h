// Every audio system must provide these function.

#include <stdbool.h>

bool InitSound();
short * SendtoCard(int n);
void PollReceivedSamples();
void StopCapture();
unsigned short * SoundInit();
void SoundFlush();

extern char **PlaybackDevices;
extern int PlaybackDevicesCount;
extern char **CaptureDevices;
extern int CaptureDevicesCount;
