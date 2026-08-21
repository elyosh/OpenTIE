#include "tie_app/midi_resources.h"

#include <stdio.h>

#include <imuse/midi_fluidsynth.h>
#include <imuse/midi_nuked_sc55.h>

bool TieMidiResources_Soundfontvalidate(const char* path, char* error, size_t error_capacity) {
	if (!path || !path[0]) {
		if (error && error_capacity)
			snprintf(error, error_capacity, "Select a SoundFont file.");
		return false;
	}
	FILE* file = fopen(path, "rb");
	if (!file) {
		if (error && error_capacity)
			snprintf(error, error_capacity, "The SoundFont file is missing or unreadable:\n%s", path);
		return false;
	}
	fclose(file);
	if (!imuse_fluidsynth_soundfont_is_valid(path)) {
		if (error && error_capacity)
			snprintf(error, error_capacity, "The selected file is not a valid SoundFont:\n%s", path);
		return false;
	}
	if (error && error_capacity)
		error[0] = '\0';
	return true;
}

bool TieMidiResources_Sc55RomDirectoryvalidate(const char* path, char* error, size_t error_capacity) {
	ImuseNukedSc55Romset* romset = imuse_nuked_sc55_romset_load(path, error, error_capacity);
	if (!romset)
		return false;
	imuse_nuked_sc55_romset_release(romset);
	return true;
}
