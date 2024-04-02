//
//	Passes audio samples to the sound interface

//	Windows uses WaveOut

//	Nucleo uses DMA to the internal ADC and DAC

//	Linux will probably use ALSA

//	This is the Nucleo Version

//	Also has some platform specific routines

#include <math.h>
#include "ARDOPC.h"

void SetLED(int blnPTT);
void stopDAC();

// Windows and Linux work with signed samples +- 32767
// STM32 DAC uses unsigned 0 - 4095

unsigned short buffer[2][1200];	// Two Transfer/DMA buffers of 0.1 Sec
unsigned short work;

extern volatile int adc_buffer_mem;

#define SAMPLES_PER_BLOCK 1200

extern uint16_t ADC_Buffer[2][SAMPLES_PER_BLOCK];     // stereo capture buffers

//short audioInputBuffer[SAMPLES_PER_BLOCK]; Reuse DMA buffer

int max, min, tot;

BOOL Loopback = FALSE;
//BOOL Loopback = TRUE;

char CaptureDevice[] = "DMA";
char PlaybackDevice[] = "DMA";

char * CaptureDevices = CaptureDevice;
char * PlaybackDevices = PlaybackDevice;

int LastNow;

int Number = 0;				// Number waiting to be sent

unsigned short * DMABuffer;

#define KISSBufferSize 1024


extern unsigned char rx_buffer[KISSBufferSize];

// Circular buffer pointers
// volatile makes read-modify-write atomic

volatile extern int rx_in;
extern volatile int rx_out;

#define MAX_KISS_LEN 2048	/* Spec calls for at least 1024. */
				/* Might want to make it longer to accomodate */
				/* maximum packet length. */

#define MAX_NOISE_LEN 100
enum kiss_state_e {
	KS_SEARCHING,		/* Looking for FEND to start KISS frame. */
	KS_COLLECTING};		/* In process of collecting KISS frame. */



typedef struct kiss_frame_s {

	enum kiss_state_e state;

	unsigned char kiss_msg[MAX_KISS_LEN];
				/* Leading FEND is optional. */
				/* Contains escapes and ending FEND. */
	int kiss_len;

	unsigned char noise[MAX_NOISE_LEN];
	int noise_len;

} kiss_frame_t;


extern kiss_frame_t kf;		/* Accumulated KISS frame and state of decoder. */


void printtick(char * msg)
{
	Debugprintf("%s %i", msg, Now - LastNow);
	LastNow = Now;
}

extern volatile int ticks;

unsigned int getTicks()
{
	return ticks;
}

extern int kissdebug;
void kiss_send_rec_packet (int chan, unsigned char *fbuf,  int flen);


void txSleep(int mS)
{
	// called while waiting for next TX buffer. Run background processes

	unsigned char ch;

	if (rx_in != rx_out)
	{
		ch = rx_buffer[rx_out];
		rx_out = (rx_out + 1) % KISSBufferSize;
		kiss_rec_byte (&kf, ch, kissdebug, kiss_send_rec_packet);
	}

	recv_process();

//	Sleep(mS);
}


#include <stm32f4xx_dma.h>

int PriorSize = 0;

int Index = 0;				// DMA Buffer being used 0 or 1
int inIndex = 0;				// DMA Buffer being used 0 or 1

BOOL DMARunning = FALSE;		// Used to start DMA on first write

int InitSound()
{
	Config_ADC_DMA();
	Start_ADC_DMA();
	return TRUE;
}

unsigned short * SendtoCard(unsigned short buf, int n)
{
	if (Loopback)
	{
		// Loop back   to decode for testing

//		ProcessNewSamples(buf, 1200);		// signed
	}

	// Start DMA if first call

	if (DMARunning == FALSE)
	{
		StartDAC();
		DMARunning = TRUE;
	}

	// wait for other DMA buffer to finish

	printtick("Start Wait");		// FOr timing tests

	while (1)
	{
		int chan = DMA_GetCurrentMemoryTarget(DMA1_Stream5);

		if (chan == Index) 	// we've started sending current buffer
		{
			Index = !Index;
			printtick("Stop Wait");
			break;
		}
		txSleep(10);				// Run background while waiting
	}
	return &buffer[Index][0];
}

//		// This generates a nice musical pattern for sound interface testing
//    for (t = 0; t < sizeof(buffer); ++t)
//        buffer[t] =((((t * (t >> 8 | t >> 9) & 46 & t >> 8)) ^ (t & t >> 13 | t >> 6)) & 0xFF);

int min = 0, max = 0, leveltimer = 0;

void multi_modem_process_sample (int chan, int audio_sample) ;

void PollReceivedSamples()
{
	// Process any captured samples
	// Ideally call at least every 100 mS, more than 200 will loose data

 	// Process any received samples

	// convert the saved ADC 12-bit unsigned samples into 16-bit signed samples

    if (adc_buffer_mem >= 0 && adc_buffer_mem <= 1)
    {
    	uint16_t *src = (uint16_t *)&ADC_Buffer[adc_buffer_mem];	// point to the DMA buffer where the ADC samples were saved
//    	int16_t *dst = (int16_t *)&audioInputBuffer;				// point to our own buffer where we are going to copy them too
      	int16_t *dst = (int16_t *)src;				// reuse input buffer
      	short * ptr;

    	// 12-bit unsigned to 16-bit signed

    	int i;

   		for (i = 0; i < SAMPLES_PER_BLOCK; i++)
   		{
    		register int32_t s1 = ((int16_t)(*src++) - 2048) << 4;  // unsigned 12-bit to 16-bit signed .. ADC channel 1
     		*dst++ = (int16_t)s1;
     		tot += s1;
     		if (s1 > max)
     			max = s1;
     		if (s1 < min)
     			min = s1;
    	}

//	serial.printf("Max %d min %d av %d\n", max, min, tot/1200);

   	 printtick("Process Sample Start");
//   	 ProcessNewSamples(audioInputBuffer, SAMPLES_PER_BLOCK);

   	ptr = &ADC_Buffer[adc_buffer_mem];

   	 for (i = 0; i < SAMPLES_PER_BLOCK; i++)
   		 multi_modem_process_sample (0, *(ptr++));

  	 printtick("Process Sample End");

    	 adc_buffer_mem = -1;    // finished with the ADC buffer that the DMA filled

//    	 displayLevel(max);

     	if (leveltimer++ > 100)
     	{
     		leveltimer = 0;
     		Debugprintf("Input peaks = %d, %d", min, max);
     	}
 		min = max = 0;
    }
}


void StopCapture()
{
	Capturing = FALSE;
//	printf("Stop Capture\n");
}

void StartCodec(char * strFault)
{
	strFault[0] = 0;
}

void StopCodec(char * strFault)
{
	strFault[0] = 0;
}

void StartCapture()
{
	Capturing = TRUE;
	DiscardOldSamples();
	ClearAllMixedSamples();
	State = SearchingForLeader;

//	printf("Start Capture\n");
}
void CloseSound()
{ 
}

unsigned short * SoundInit()
{
	Index = 0;
	return &buffer[0][0];
}
	
//	Called at end of transmission

void SoundFlush()
{
	// Append Trailer then send remaining samples

//	AddTrailer();			// add the trailer.

	if (Loopback)
	{
		short loopbuff[1200];		// Temp for testing - loop sent samples to decoder
//		ProcessNewSamples(loopbuff, Number);
	}

	SendtoCard(buffer[Index], Number * 2);

	// Wait for other DMA buffer to empty beofre shutting down DAC

	while (1)
	{
		int chan = DMA_GetCurrentMemoryTarget(DMA1_Stream5);

		if (chan == Index) 	// we've started sending current buffer
		{
			break;
		}
	}

	stopDAC();
	DMARunning = FALSE;


	// I think we should turn round the link here. I dont see the point in
	// waiting for MainPoll

//	SoundIsPlaying = FALSE;

	KeyPTT(FALSE);		 // Unkey the Transmitter

//	StartCapture();

	return;
}

//  Function to Key radio PTT 

const char BoolString[2][6] = {"FALSE", "TRUE"};

BOOL KeyPTT(BOOL blnPTT)
{
	// Returns TRUE if successful False otherwise

	SetLED(blnPTT);
	return TRUE;
}


void audio_put(int a, int b)
{
	int work = (short)(b);
	DMABuffer[Number++] = (work + 32768) >> 4; // 12 bit left justify

	if (Number == SendSize)
	{
		// send this buffer to sound interface

		DMABuffer = SendtoCard(DMABuffer, SendSize);
		Number = 0;
	}
}

int audio_get(int a)
{

}





	
