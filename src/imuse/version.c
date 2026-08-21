#include <imuse/version.h>

/*
 * libimuse — version string accessor.
 *
 * Compiled into the library so a host that links against a
 * different libimuse than its headers were built against can
 * detect the mismatch at runtime by comparing
 * imuse_version_string() to IMUSE_VERSION_STRING (the string
 * baked into the host's headers).
 */

const char* imuse_version_string(void) { return IMUSE_VERSION_STRING; }
