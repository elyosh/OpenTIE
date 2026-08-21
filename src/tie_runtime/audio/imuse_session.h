/*
 * TIE-side libimuse session handle.
 *
 * libimuse is multi-instance internally, but TIE itself uses exactly
 * one session for the whole process. This header provides the global
 * `im` so every TIE source file that calls into libimuse picks up
 * the same handle without TIE having to thread it through every
 * function.
 *
 * Lifecycle: gamesnd_Open_Pre_iMuse calls imuse_create() and assigns
 * here; gamesnd_Close_Pre_iMuse stops host rendering, calls
 * imuse_destroy(), and clears the handle.
 */
#ifndef TIE_IMUSE_SESSION_H
#define TIE_IMUSE_SESSION_H

#include <imuse.h>

#include "tie_runtime/audio/config.h"
#include "tie_runtime/input/input.h"
#include "tie_runtime/storage/storage.h"

extern imuse_t* im;

#endif /* TIE_IMUSE_SESSION_H */
