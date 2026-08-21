#include "host_internal.h"

#include <string.h>

static LandruHost s_host;
static bool s_host_set;

bool landru_set_host(const LandruHost* host) {
	if (!host || !host->file_open || !host->file_read || !host->file_write || !host->file_seek ||
		!host->file_tell || !host->file_close || !host->dir_open || !host->dir_next || !host->dir_close ||
		!host->path_is_dir || !host->key_pending || !host->key_read || !host->modifier_keys ||
		!host->mouse_position || !host->mouse_movement || !host->mouse_set_position || !host->mouse_show ||
		!host->joystick_count || !host->joystick_read || !host->palette_set || !host->now_us)
		return false;

	s_host = *host;
	s_host_set = true;
	return true;
}

void landru_clear_host(void) {
	s_host_set = false;
	memset(&s_host, 0, sizeof s_host);
}

void landru_host_log(LandruLogLevel level, const char* format, ...) {
	if (!s_host_set || !s_host.log)
		return;
	va_list args;
	va_start(args, format);
	s_host.log(s_host.userdata, level, format, args);
	va_end(args);
}

LandruFile* landru_host_file_open(LandruFileRoot root, const char* path, const char* mode) {
	return s_host_set ? s_host.file_open(s_host.userdata, root, path, mode) : NULL;
}

size_t landru_host_file_read(void* buffer, size_t size, size_t count, LandruFile* file) {
	return s_host_set ? s_host.file_read(s_host.userdata, buffer, size, count, file) : 0;
}

size_t landru_host_file_write(const void* buffer, size_t size, size_t count, LandruFile* file) {
	return s_host_set ? s_host.file_write(s_host.userdata, buffer, size, count, file) : 0;
}

int landru_host_file_seek(LandruFile* file, long offset, int origin) {
	return s_host_set ? s_host.file_seek(s_host.userdata, file, offset, origin) : -1;
}

long landru_host_file_tell(LandruFile* file) {
	return s_host_set ? s_host.file_tell(s_host.userdata, file) : -1;
}

int landru_host_file_close(LandruFile* file) {
	return s_host_set ? s_host.file_close(s_host.userdata, file) : -1;
}

LandruDir* landru_host_dir_open(LandruFileRoot root, const char* path) {
	return s_host_set ? s_host.dir_open(s_host.userdata, root, path) : NULL;
}

int landru_host_dir_next(LandruDir* dir, LandruDirEntry* entry) {
	return s_host_set ? s_host.dir_next(s_host.userdata, dir, entry) : 0;
}

void landru_host_dir_close(LandruDir* dir) {
	if (s_host_set)
		s_host.dir_close(s_host.userdata, dir);
}

int landru_host_path_is_dir(LandruFileRoot root, const char* path) {
	return s_host_set ? s_host.path_is_dir(s_host.userdata, root, path) : 0;
}

int landru_host_key_pending(void) { return s_host_set ? s_host.key_pending(s_host.userdata) : 0; }

int landru_host_key_read(void) { return s_host_set ? s_host.key_read(s_host.userdata) : -1; }

int landru_host_modifier_keys(void) { return s_host_set ? s_host.modifier_keys(s_host.userdata) : 0; }

void landru_host_mouse_position(int16_t* buttons, int16_t* x, int16_t* y) {
	if (s_host_set)
		s_host.mouse_position(s_host.userdata, buttons, x, y);
}

void landru_host_mouse_movement(int16_t* x, int16_t* y) {
	if (s_host_set)
		s_host.mouse_movement(s_host.userdata, x, y);
}

void landru_host_mouse_set_position(int16_t x, int16_t y) {
	if (s_host_set)
		s_host.mouse_set_position(s_host.userdata, x, y);
}

void landru_host_mouse_show(bool show) {
	if (s_host_set)
		s_host.mouse_show(s_host.userdata, show);
}

int landru_host_joystick_count(void) { return s_host_set ? s_host.joystick_count(s_host.userdata) : 0; }

void landru_host_joystick_read(int port, int16_t* axes, int axis_count, uint16_t* buttons) {
	if (s_host_set)
		s_host.joystick_read(s_host.userdata, port, axes, axis_count, buttons);
}

void landru_host_palette_set(const uint8_t* rgb, int start, int count) {
	if (s_host_set)
		s_host.palette_set(s_host.userdata, rgb, start, count);
}

void landru_host_frontend_audio_pump(void) {
	if (s_host_set && s_host.frontend_audio_pump)
		s_host.frontend_audio_pump(s_host.userdata);
}

bool landru_host_has_platform_video(void) { return s_host_set && s_host.video.lock && s_host.video.unlock; }

void landru_host_video_set_mode(uint16_t mode) {
	if (s_host_set && s_host.video.set_mode)
		s_host.video.set_mode(s_host.userdata, mode);
}

void landru_host_video_lock(void) {
	if (s_host_set && s_host.video.lock)
		s_host.video.lock(s_host.userdata);
}

void landru_host_video_unlock(void) {
	if (s_host_set && s_host.video.unlock)
		s_host.video.unlock(s_host.userdata);
}

void landru_host_video_copy_to_present_surface(void) {
	if (s_host_set && s_host.video.copy_to_present_surface)
		s_host.video.copy_to_present_surface(s_host.userdata);
}

void landru_host_video_present(void) {
	if (s_host_set && s_host.video.present)
		s_host.video.present(s_host.userdata);
}

uint64_t landru_host_now_us(void) { return s_host_set ? s_host.now_us(s_host.userdata) : 0; }
