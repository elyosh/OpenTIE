/*
 * libimuse — opaque session handle.
 *
 * Defined in its own header so every other public header can include
 * it without circular dependencies. Multiple sub-headers declaring
 * the same `typedef struct imuse imuse_t;` would risk redefinition
 * errors under strict C99.
 */
#ifndef LIBIMUSE_HANDLE_H
#define LIBIMUSE_HANDLE_H

/* Forward-declared opaque handle. Allocated by imuse_create, freed by
 * imuse_destroy. Every public entry point in libimuse takes one as
 * its first argument; sessions are independent. */
typedef struct imuse imuse_t;

#endif /* LIBIMUSE_HANDLE_H */
