#ifndef _TEST_ARDOP_SETUP_H
#define _TEST_ARDOP_SETUP_H

/**
 * @brief Setup routines common to all ARDOP test executables
 *
 * All ARDOP unit test executables should invoke this method
 * once before any tests are executed. This method configures
 * ARDOP logging and any other global state.
 */
void ardop_test_setup();

#endif
