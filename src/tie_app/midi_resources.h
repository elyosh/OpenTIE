#ifndef TIE_APP_MIDI_RESOURCES_H
#define TIE_APP_MIDI_RESOURCES_H

#include <stdbool.h>
#include <stddef.h>

bool TieMidiResources_Soundfontvalidate(const char* path, char* error, size_t error_capacity);
bool TieMidiResources_Sc55RomDirectoryvalidate(const char* path, char* error, size_t error_capacity);

#endif /* TIE_APP_MIDI_RESOURCES_H */
