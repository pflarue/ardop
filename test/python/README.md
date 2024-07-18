This directory contains supplemental test scripts for ardopcf.  Unlike the tests that are compiled and run using `make test`, these test scripts use Python to automatically run the ardopcf compiled executable file with suitable command line arguments and then parse the data written to the console to determine what occured.  They may also write and then read WAV files containing audio that would normally be transmitted.

Temporary WAV files are written to the test/python/tmp directory.  Normally, these WAV files are deleted after each test.  However, when a failure to correctly decode a WAV file fails, that file is not deleted.  This allows further evaluation of that file for debugging purposes.

Debug log files created during these tests are also written to the test/python/tmp directory.  Minimal data is written to these logs to limit their size, but they are never automatically deleted.

Anyone running these tests regularly may want to delete the contents of the test/python/tmp directory periodically unless some of the files retained there are being used for debugging purposes.

Also, unlike the tests that are compiled and run using `make test`, these tests are not intended to provide carefully targeted testing of an isolated section of the ardopcf code.  Instead, these tests are intended to evaluate the performance of the extensive amount of ardopcf code associated with encoding, modulation, demodulation, and decoding that converts data to be transmitted into to audio and audio received back to usable data.  As a result, poor results from these test may be indicative of a problem that deserves further evaluation, but the tests will not typically identify where that problem might be located in the code.  These tests will be used as a part of the evaluation of future ardopcf release candidates.  This may help identify whether unintended consequences of changes to ardopcf have degraded its reliability.

Running these tests with ardopcf which has been compiled with ASAN/UBSAN may be useful.  This will cause the tests to run more slowly, but may help identify bugs that might otherwise not be noticed.  See https://github.com/pflarue/ardop/issues/37 for additional details.

Each file whose filename has the form `test_*.py` is a python script that will perform one or more tests.  On any computer where `python` is installed, running `python test_wav_io.py` from within the `test/python` directory should run the tests, where `test_wav_io.py` is the test file to be run.  On Linux machines where `/usr/bin/python` exists and `test_wav_io.py` is set to be an executable file, it should also be possible to simply type './test_wav_io.py' in the `test/python` directory.  These test scripts require that the ardopcf executable be available in the root directory of the repository.

Some test scripts may include `--verbose` and `-v` options.  In those cases `-v 0` will reduce the amount of data printed, `-v 1` will have no effect because this is the default level, `-v 2` (which is also equivalent to `-v`) and possibly `-v 3` will increase the amount of data printed.

These test scripts may write quite a bit of text to the console while running, but will normally print a more compact summary of the results at the end.
