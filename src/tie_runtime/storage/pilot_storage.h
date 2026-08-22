#ifndef TIE_RUNTIME_PILOT_STORAGE_H
#define TIE_RUNTIME_PILOT_STORAGE_H

/* Shared storage policy: pilots remain selectable after switching frontends. */
#define TIE_PILOT_NAME_MAX 16
#define TIE_PILOT_NAME_CAPACITY (TIE_PILOT_NAME_MAX + 1)
#define TIE_PILOT_FILENAME_CAPACITY (TIE_PILOT_NAME_CAPACITY + sizeof(".tfr") - 1)

#endif
