#include <stdbool.h>

bool GetTwoToneLeaderWithSync(int intSymLen);
bool Mod4FSKDataAndPlay(int Type, unsigned char * bytEncodedBytes, int Len, int intLeaderLen);
bool Mod4FSK600BdDataAndPlay(int Type, unsigned char * bytEncodedBytes, int Len, int intLeaderLen);
bool ModPSKDataAndPlay(int Type, unsigned char * bytEncodedBytes, int Len, int intLeaderLen);
bool AddTrailer();
bool RemodulateLastFrame();
bool Send5SecTwoTone();
bool sendCWID(const StationId * id);

