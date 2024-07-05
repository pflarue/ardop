#include "setup.h"

// log level constants
extern int FileLogLevel;
extern int ConsoleLogLevel;

void ardop_test_setup() {
    FileLogLevel = 0;
    ConsoleLogLevel = 0;
}
