#include <stdlib.h>

#include "common/log.h"
#include "rockliff/rrs.h"

extern char DecodeWav[5][256];
extern int host_port;

int processargs(int argc, char * argv[]);
int decode_wav();
int platform_init();
void ardopmain();

/*
 * ardopcf main function
 *
 * Do initial setup before passing control to ardopmain()
 */

int main(int argc, char *argv[])
{
	int ret;

	if ((ret = processargs(argc, argv)) < 0) {
		return ret;
	} else if (ret > 0) {
		return 0;  // exit without indicating an error.
	}

	// Set up the Reed Solomon FEC functions.
	// rslen_set[] must list all of the rslen values used.
	int rslen_set[] = {2, 4, 8, 16, 32, 36, 50, 64};
	init_rs(rslen_set, 8);

	if (DecodeWav[0][0]) {
		decode_wav();
		return 0;
	}

	if ((ret = platform_init()) < 0) {
		return ret;
	} else if (ret > 0) {
		return 0;  // exit without indicating an error.
	}

	// TODO: move other setup features from ardopmain() to here so that
	// ardopmain mostly just handles the main program loop?
	ardopmain();

	return 0;
}
