#include <stdbool.h>

int init_rs(int *lengths, int count);

int rs_append(unsigned char *data, int datalen, int rslen);

int rs_correct(unsigned char *rxdata, int combinedlen, int rslen, bool quiet, bool test_only);
