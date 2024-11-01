/*
 * ardopcf main function
 *
 * Invokes the platform-specific main function from either
 * ALSASound.c (Linux) or Waveout.c (Windows)
 */
int platform_main(int argc, char *argv[]);

int main(int argc, char *argv[])
{
	return platform_main(argc, argv);
}
