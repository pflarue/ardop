#include <stdbool.h>
#include <stddef.h>

#include "common/ardopcommon.h"

extern int WebGuiNumConnected;

// TODO: Add documentation for these functions
int encodeUvint(char *buf, int size, unsigned int uvalue);
void WebguiInit();
void WebguiPoll();
int wg_send_hostdatat(int cnum, char *prefix, unsigned char *data, int datalen);
int wg_send_hostdatab(int cnum, char *prefix, unsigned char *data, int datalen);
int wg_send_hostmsg(int cnum, char msgtype, unsigned char *strText);
int wg_send_alert(int cnum, const char * format, ...);
int wg_send_protocolmode(int cnum);
int wg_send_state(int cnum);
int wg_send_bandwidth(int cnum);
int wg_send_currentlevel(int cnum, unsigned char level);
int wg_send_quality(int cnum, unsigned char quality,
	unsigned int totalRSErrors, unsigned int maxRSErrors);
int wg_send_drivelevel(int cnum);
int wg_send_avglen(int cnum);
int wg_send_rcall(int cnum, const char *call);
int wg_send_mycall(int cnum, char *call);
int wg_send_txframet(int cnum, const char *frame);
int wg_send_rxframet(int cnum, unsigned char state, const char *frame);
int wg_send_pttled(int cnum, bool isOn);
int wg_send_irsled(int cnum, bool isOn);
int wg_send_issled(int cnum, bool isOn);
int wg_send_busy(int cnum, bool isBusy);
int wg_send_wavrx(int cnum, bool isRecording);
int wg_send_pttenabled(int cnum, bool enabled);
int wg_send_rxenabled(int cnum, bool enabled);
int wg_send_rxsilent(int cnum);
int wg_send_txenabled(int cnum, bool enabled);
int wg_send_capturechannel(int cnum);
int wg_send_playbackchannel(int cnum);
int wg_send_devices(int cnum, char **ss, char **cs);
int wg_send_audiodevices(int cnum, DeviceInfo **devices, char *cdevice,
	char *pdevice, bool crestore, bool prestore);
int wg_send_ptton(int cnum, char *hexstr);
int wg_send_pttoff(int cnum, char *hexstr);
int wg_send_pixels(int cnum, unsigned char *data, size_t datalen);
int wg_send_fftdata(float *mags, int magsLen);
