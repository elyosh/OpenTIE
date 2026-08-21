#ifndef __MATRIX_H__
#define __MATRIX_H__

#include <stdint.h>

#include "landru/res.h"

#define MATRIX_MAX_JOINTS 4

typedef struct {
	int16_t frame_count;
	int16_t trans_count;
	int16_t matrix_count;
	void* data;
} Matrix;

typedef struct {
	int16_t joint_rot[MATRIX_MAX_JOINTS][9]; /* 3x3 rotation per joint */
	int16_t joint_pos[MATRIX_MAX_JOINTS][3]; /* position per joint */
	int16_t cam_heading;
	int16_t cam_pitch;
	int16_t cam_roll;
	int16_t cam_x;
	int16_t cam_y;
	int16_t cam_z;
	int16_t trans_x;
	int16_t trans_y;
	int16_t trans_z;
} MatrixFrame; /* 114 bytes */

Matrix* matrix_Alloc_Matrix(void);
void matrix_Init_Matrix(Matrix* m);
Matrix* matrix_Res_Matrix(ResFile* rf, const char* name);
void matrix_Free_Matrix(Matrix* m);
int16_t matrix_Get_Matrix_Frame(Matrix* m, MatrixFrame* dest, int16_t frame);

#endif
