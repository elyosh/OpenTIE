#ifndef TIE_PLAY1_H
#define TIE_PLAY1_H

#include "tie/shellext.h"

#include <landru/bitmap.h>
#include <landru/surface.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int play1_Play1(SceneHeadStruct* the_head);
extern int16_t play1_is_streaming;
extern LandruHandle play1_read_buffer;
extern BitmapStruct play1_last_frame;
extern BitmapStruct play1_current_frame;

/* Scene tables indexed by play1_id (retail data; the modern runtime may
 * replace them with the LecDemos sample-disc tables). */
extern int16_t play1_cur_scene[87];
extern int16_t play1_next_scene[86];
extern int16_t play1_skip_scene[86];
extern char play1_resource_str[86][14];
extern char play1_film_str[86][10];
extern char play1_stream_str[86][24];

#ifdef __cplusplus
}
#endif

#endif
