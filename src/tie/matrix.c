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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "landru/fourcc.h"
#include "landru/res.h"
#include "tie/matrix.h"

// FUNCTION: TIE 0x89040
Matrix* matrix_Alloc_Matrix(void) {
	Matrix* m = (Matrix*)malloc(sizeof(Matrix));
	if (m)
		matrix_Init_Matrix(m);
	return m;
}

// FUNCTION: TIE 0x8905C
void matrix_Init_Matrix(Matrix* m) {
	m->frame_count = 0;
	m->trans_count = 0;
	m->matrix_count = 0;
	m->data = NULL;
}

// FUNCTION: TIE 0x89074
Matrix* matrix_Res_Matrix(ResFile* rf, const char* name) {
	int offset;
	uint32_t total_size;
	if (!lres_Get_Resource_Offset(rf, FOURCC_MTRX, name, &offset, &total_size))
		return NULL;

	ResFile* stream = lres_Open_Resource_Data(FOURCC_MTRX, name);
	if (!stream)
		return NULL;

	Matrix* m = matrix_Alloc_Matrix();
	if (!m) {
		lres_Close_Resource_Data(rf);
		return NULL;
	}

	m->frame_count = lres_Read_Resource_Word(rf);
	m->trans_count = lres_Read_Resource_Word(rf);
	m->matrix_count = lres_Read_Resource_Word(rf);

	int32_t frame_size = 12 + 6 * m->trans_count + 24 * m->matrix_count;
	int32_t data_size = frame_size * m->frame_count;

	m->data = lres_Read_Resource_Data(rf, data_size);
	lres_Close_Resource_Data(rf);

	return m;
}

// FUNCTION: TIE 0x89154
void matrix_Free_Matrix(Matrix* m) {
	if (m->data)
		free(m->data);
	free(m);
}

/*
 * Copy frame `frame` into the caller's MatrixFrame buffer.
 * NOTE: reproduces the original binary's bug where the trans loop
 * overwrites the same trans_x/y/z fields each iteration (only
 * the last translation entry survives). Real .MTRX assets use
 * trans_count <= 1, so this is harmless in practice.
 */
// FUNCTION: TIE 0x89174
int16_t matrix_Get_Matrix_Frame(Matrix* m, MatrixFrame* dest, int16_t frame) {
	if (frame >= m->frame_count)
		return 0;

	int32_t frame_size = 12 + 6 * m->trans_count + 24 * m->matrix_count;
	const int16_t* src = (const int16_t*)((const uint8_t*)m->data + frame * frame_size);

	/* Camera: 6 WORDs */
	dest->cam_x = *src++;
	dest->cam_y = *src++;
	dest->cam_z = *src++;
	dest->cam_heading = *src++;
	dest->cam_pitch = *src++;
	dest->cam_roll = *src++;

	/* Translations: trans_count * 3 WORDs
	 * Bug-for-bug: always writes to the same dest fields */
	for (int16_t t = 0; t < m->trans_count; t++) {
		dest->trans_x = *src++;
		dest->trans_y = *src++;
		dest->trans_z = *src++;
	}

	/* Joint matrices: matrix_count * (9 rotation + 3 position) WORDs */
	for (int16_t j = 0; j < m->matrix_count; j++) {
		for (int16_t r = 0; r < 9; r++)
			dest->joint_rot[j][r] = *src++;
		for (int16_t p = 0; p < 3; p++)
			dest->joint_pos[j][p] = *src++;
	}

	return 1;
}
