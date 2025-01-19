#include <stdbool.h>
#include "common/ARDOPC.h"

float RxoComputeDecodeDistance(int * intToneMags, UCHAR bytFrameType);
bool RxoDecodeSessionID(UCHAR bytFrameType, int * intToneMags, float dblMaxDistance);
int RxoMinimalDistanceFrameType(int * intToneMags);
void ProcessRXOFrame(UCHAR bytFrameType, int frameLen, UCHAR * bytData, bool blnFrameDecodedOK);
unsigned char *utf8_check(unsigned char *s, size_t slen);
