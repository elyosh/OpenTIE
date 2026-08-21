#ifndef FILM_UTIL_H
#define FILM_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TieLfdFile TieLfdFile;

void TieFilmUtil_Die(const char* fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
void TieFilmUtil_OpenLfd(TieLfdFile* lfd, const char* path);

#ifdef __cplusplus
}
#endif

#endif
