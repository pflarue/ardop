// Every audio system must provide these function.

#include <stdbool.h>

// Write list of available audio devices (input and output) to log.
// Open CaptureDevice and PlaybackDevice.
// Return true on success or false on failure
bool InitSound();

// Stage for playback the contents (n samples) of the buffer returned by the
// previous call to SendtoCard().  Return a new empty buffer.
// This will block until samples can be staged.
short * SendtoCard(int n);

// Check for available received audio samples.  If samples are available and
// in the Capturing state, call ProcessNewSamples() with them, returning once
// ProcessNewSamples() has returned.  The Capturing state is enabled by
// SoundFlush() which whould be called after all samples to be transmitted have
// been staged with SendtoCard(), and is disabled by StopCapture() which should
// be called when beginning to transmit.
void PollReceivedSamples();

// Disable the Capturing state.
void StopCapture();

// Get an initial empty buffer to be filled before the first call to
// SendtoCard().  Each call to SendtoCard returns the next empty buffer to be
// filled.
unsigned short * SoundInit();

// Adds a trailer to the audio samples that have been staged with SendtoCard,
// and then blocks until all of the staged audio has been played.  Before
// returning, it calls KeyPTT(false) and enables the Capturing state.
void SoundFlush();

extern char **PlaybackDevices;
extern int PlaybackDevicesCount;
extern char **CaptureDevices;
extern int CaptureDevicesCount;
