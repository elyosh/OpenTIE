#include "util.h"

#include "lfd_file.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void TieFilmUtil_Die(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "filmtool: ");
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

void TieFilmUtil_OpenLfd(TieLfdFile* lfd, const char* path) {
	char error[512];
	if (!TieLfdFile_Open(lfd, path, error, sizeof error))
		TieFilmUtil_Die("%s", error);
}
