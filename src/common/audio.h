// Every audio system must provide these functions.

#include <stdbool.h>

#define SendSize 1200  // 100 mS for now
// Two buffers of 0.1 sec duration for TX audio.
extern short txbuffer[2][SendSize];
// index to next txbuffer to be filled..
extern int TxIndex;

// Close the playback audio device if one is open and set TXEnabled=false.
// do_getdevices is passed to updateWebGuiAudioConfig()
void CloseSoundPlayback(bool do_getdevices);
// Close the capture audio device if one is open and set RXEnabled=false.
// do_getdevices is passed to updateWebGuiAudioConfig()
void CloseSoundCapture(bool do_getdevices);


// If devstr matches PlaybackDevice and TXEnabled is true and ch is unchanged,
// then do nothing but write a debug log message.  Otherwise, if TXEnabled is
// true, then CloseSoundPlayback() is called before attempting to open
// devstr.  devstr is first tried as a playback audio device name.  If that
// fails, the first case insensitive substring match in the descriptions of
// playback audio devices is used.  If no match is found, return false with
// TXEnabled=false and PlaybackDevice set to an empty string.  On success,
// return true with TXEnabled=true and PlaybackDevice set to devstr.
bool OpenSoundPlayback(char *devstr, int ch);

// If devstr matches CaptureDevice and RXEnabled is true and ch is unchanged,
// then do nothing but write a debug log message.  Otherwise, if RXEnabled is
// true, then CloseSoundCapture() is called before attempting to open
// devstr.  devstr is first tried as a capture audio device name.  If that
// fails, the first case insensitive substring match in the descriptions of
// capture audio devices is used.  If no match is found, return false with
// RXEnabled=false and CaptureDevice set to an empty string.  On success,
// return true with RXEnabled=true and CaptureDevice set to devstr.
bool OpenSoundCapture(char *devstr, int ch);

// Initialize the audio system.
// If not quiet, write list of available audio devices (input and output) to log.
void InitAudio(bool quiet);

// Stage for playback the contents (n samples) of the txbuffer[TxIndex]
// return true on success and false on failure.
// This will block until samples can be staged.
bool SendtoCard(int n);

// Check for available received audio samples.  If RXEnabled == true, and
// samples are available and in the Capturing state, call ProcessNewSamples()
// with them, returning once ProcessNewSamples() has returned.  The Capturing
// state is enabled by SoundFlush() which is called after all samples to be
// transmitted have been staged with SendtoCard(), and is disabled by
// StopCapture() which is called when beginning to transmit.
void PollReceivedSamples();

// Disable the Capturing state.
void StopCapture();

// Adds a trailer to the audio samples that have been staged with SendtoCard(),
// and then block until all of the staged audio has been played.  Before
// returning, it calls KeyPTT(false) and enables the Capturing state.
// return true on success and false on failure.  On failure, still set
// KeyPTT(false) and enables the Capturing state
bool SoundFlush();

// Populate AudioDevices
void GetDevices();

// Return true if OpenSoundCapture("RESTORE") might succeed, else false
bool crestorable();

// Return true if OpenSoundPlayback("RESTORE") might succeed, else false
bool prestorable();
