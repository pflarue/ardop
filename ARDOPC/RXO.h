#include "ARDOPC.h"
 
float RxoComputeDecodeDistance(int * intToneMags, UCHAR bytFrameType);
BOOL RxoDecodeSessionID(UCHAR bytFrameType, int * intToneMags, float dblMaxDistance);
int RxoMinimalDistanceFrameType(int * intToneMags);
void ProcessRXOFrame(UCHAR bytFrameType, int frameLen, UCHAR * bytData, BOOL blnFrameDecodedOK);
unsigned char *utf8_check(unsigned char *s);
