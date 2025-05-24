//////////////////////////////////////////////////////////////////////
// VERSION NUMBER ERROR:
// Setting ProductVersion[] = "2.0.3.2.1" for the first release of
// ardopcf was a mistake.  This was due to a bug and misunderstanding
// of the ardop version numbering scheme.  So ardopcf v1.0.4.1.1 comes
// after and replaces v2.0.3.2.1
const char ProductVersion[] = "1.0.4.1.3_develop";

// OSName[] is used as a suffix to ProductVersion[] when written to the log and
// in response to the VERSION host command.
#ifdef WIN32
const char OSName[] = "windows";
#elif __ANDROID__
const char OSName[] = "android";
#else
const char OSName[] = "linux";
#endif
