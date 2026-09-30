#ifndef TIE_DRAWSTRM_H
#define TIE_DRAWSTRM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void drawstrm_Convert_Frame_To_Palette(void* prev_frame, void* stream_data, void* cur_frame);

#ifdef __cplusplus
}
#endif

#endif
