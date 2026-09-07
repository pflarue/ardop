/*
MIT License

Copyright (c) 2026 Peter LaRue

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// txt2c creates a c source file that defines a single const char string
// from an arbitrary text file.

// Source file may have DOS style \r\n line endings or unix style \n line
// endings, but destination file will always have only unix style line
// endings.  If tab characters are found in source, they will be replaced
// with 2 spaces

#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
	FILE *src, *dst;
	int c;

	if (argc < 4) {
		printf("Usage: txt2c srcpath dstpath variable_name\n");
		return (-1);
	}
	if ((src = fopen(argv[1], "r")) == NULL) {
		printf("Error: Unable to open src file '%s'.\n", argv[1]);
		return (-2);
	}
	if ((dst = fopen(argv[2], "w")) == NULL) {
		printf("Error: Unable to open dst file as '%s'.\n", argv[2]);
		return (-3);
	}
	fprintf(dst,
		"// This file was created from '%s' by txt2c.\n"
		"// Rather than editing this file directly, it may better to edit\n"
		"// that source file, and then use txt2c to rebuild this file.\n\n"
		"const char %s[] = (\n\"", argv[1], argv[3]);
	while ((c = fgetc(src)) != EOF) {
		switch (c) {
		case '"':
			// escape quotes
			fputc('\\', dst);
			fputc('\"', dst);
			break;
		case '\\':
			// escape slash
			fputc('\\', dst);
			fputc('\\', dst);
			break;
		case '\r':
			// Discard \r portion of any dos style line endings
			break;
		case '\n':
			// add quotes at the start and end of every line
			fputc('\\', dst);
			fputc('n', dst);
			fputc('\"', dst);
			fputc('\n', dst);
			fputc('\"', dst);
			break;
		case '\t':
			fputc(' ', dst);
			fputc(' ', dst);
			break;
		default:
			fputc(c, dst);
			break;
		}
	}
	fprintf(dst, "\");\n");
	fclose(src);
	fclose(dst);
	return (0);
}
