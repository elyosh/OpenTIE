/*
 * MATRIX.C — 3D animation matrix resource loader
 *
 * Loads FOURCC_MTRX resources containing per-frame camera and joint data
 * for the BPFLIGHT 3D ship viewer animations.
 *
 * .MTRX resource format:
 *   Header: 3 WORDs — frame_count, trans_count, matrix_count
 *   Raw data: frame_count frames, each containing:
 *     6 WORDs: camera (x, y, z, heading, pitch, roll)
 *     trans_count * 3 WORDs: translation (x, y, z)
 *     matrix_count * 12 WORDs: per-joint (9 rotation + 3 position)
 *
 * Frame data size = (12 + 6*trans_count + 24*matrix_count) bytes/frame.
 */

#include "tie/matrix.h"
#include "landru/fourcc.h"
#include "landru/res.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// FUNCTION: TIE95 0x89040
Matrix* matrix_Alloc_Matrix(void) {
	Matrix* m = (Matrix*)malloc(sizeof(Matrix));
	if (m)
		matrix_Init_Matrix(m);
	return m;
}

// FUNCTION: TIE95 0x8905C
void matrix_Init_Matrix(Matrix* m) {
	m->trans_count = 0;
	m->matrix_count = 0;
	m->data = LANDRU_NULL_HANDLE;
	m->frame_count = 0;
}

// FUNCTION: TIE95 0x89074
Matrix* matrix_Res_Matrix(ResFile* rf, const char* name) {
	ResFile* stream;
	Matrix* m;
	int32_t frame_size, data_size;
	int offset;
	uint32_t total_size;
	if (!xres_Get_Resource_Offset(rf, FOURCC_MTRX, name, &offset, &total_size))
		return NULL;

	stream = xres_Open_Resource_Data(FOURCC_MTRX, name);
	if (!stream)
		return NULL;

	m = matrix_Alloc_Matrix();
	if (!m) {
		xres_Close_Resource_Data(rf);
		return NULL;
	}

	m->frame_count = xres_Read_Resource_Word(rf);
	m->trans_count = xres_Read_Resource_Word(rf);
	m->matrix_count = xres_Read_Resource_Word(rf);

	frame_size = 12 + 6 * m->trans_count + 24 * m->matrix_count;
	data_size = frame_size * m->frame_count;

	m->data = xres_Read_Resource_Data(rf, data_size, LANDRU_MEMORY_DEFAULT);
	xres_Close_Resource_Data(rf);

	return m;
}

// FUNCTION: TIE95 0x89154
void matrix_Free_Matrix(Matrix* m) {
	if (m->data)
		xmemhdl_Free_Handle(m->data);
	free(m);
}

/*
 * Copy frame `frame` into the caller's MatrixFrame buffer.
 * NOTE: reproduces the original binary's bug where the trans loop
 * overwrites the same trans_x/y/z fields each iteration (only
 * the last translation entry survives). Real .MTRX assets use
 * trans_count <= 1, so this is harmless in practice.
 */
// FUNCTION: TIE95 0x89174
int16_t matrix_Get_Matrix_Frame(Matrix* m, MatrixFrame* dest, int16_t frame) {
	int16_t offset;
	const int16_t* src;
	int16_t t, j, r, p;
	if (frame < m->frame_count) {
		offset = 6 * m->trans_count;
		offset += 12;
		offset += 24 * m->matrix_count;
		offset *= frame;
		src = (const int16_t*)((const uint8_t*)xmemhdl_Lock_Handle(m->data) + offset);

		/* Camera: 6 WORDs */
		dest->cam_x = *src++;
		dest->cam_y = *src++;
		dest->cam_z = *src++;
		dest->cam_heading = *src++;
		dest->cam_pitch = *src++;
		dest->cam_roll = *src++;

		/* Translations: trans_count * 3 WORDs
		 * Bug-for-bug: always writes to the same dest fields */
		for (t = 0; t < m->trans_count; t++) {
			dest->trans_x = *src++;
			dest->trans_y = *src++;
			dest->trans_z = *src++;
		}

		/* Joint matrices: matrix_count * (9 rotation + 3 position) WORDs */
		for (j = 0; j < m->matrix_count; j++) {
			for (r = 0; r < 9; r++)
				dest->joint_rot[j][r] = *src++;
			for (p = 0; p < 3; p++)
				dest->joint_pos[j][p] = *src++;
		}

		xmemhdl_Unlock_Handle(m->data);
		return 1;
	}
	return 0;
}
