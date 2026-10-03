#include "tie/drawstrm.h"
#include <stdint.h>
#include <string.h>

/* The original TU calls the library memcpy() rather than the inline form. */
#ifdef __WATCOMC__
#pragma function(memcpy)
#endif

/*
 * Decode one frame from the stream.
 *
 * prev_frame: 64000-byte reference buffer (read for reference, overwritten
 *             at the end with the decoded frame)
 * stream_data: encoded frame data from the CD stream
 * cur_frame:   64000-byte output buffer for the decoded frame
 */
// FUNCTION: TIE95 0x89600
void drawstrm_Convert_Frame_To_Palette(void* prev_frame, void* stream_data, void* cur_frame) {
	int y, x;
	int row;
	uint8_t* dst;
	const uint8_t* ref;
	uint32_t value;
	int length;
	int start;
	uint8_t* prev_start;
	uint8_t* cur_start;
	uint8_t* prev;
	uint8_t* cur;
	const uint8_t* stream;

	prev = (uint8_t*)prev_frame;
	stream = (const uint8_t*)stream_data;
	cur = (uint8_t*)cur_frame;
	prev_start = (uint8_t*)prev_frame;
	cur_start = (uint8_t*)cur_frame;

	for (y = 0; y < 200; y += 8) {
		for (x = 0; x < 320; x += 8) {
			int cmd = *(const int16_t*)stream;
			stream += 2;

			if (cmd == 0x7FFD) {
				/* COPY: copy 8x8 from prev_frame at block-relative offset */
				dst = cur;
				ref = prev + *(const int16_t*)stream;
				stream += 2;
				for (row = 0; row < 8; row++) {
					value = *(const uint32_t*)ref;
					*(uint32_t*)dst = value;
					value = *(const uint32_t*)(ref + 4);
					*(uint32_t*)(dst + 4) = value;
					dst += 320;
					ref += 320;
				}
				cur += 8;
				prev += 8;
			} else if (cmd == 0x7FFF) {
				/* DIFF: each clear mask bit copies the reference pixel */
				dst = cur;
				ref = prev + *(const int16_t*)stream;
				stream += 2;
				for (row = 0; row < 8; row++) {
					int mask = *stream++;
					switch (mask & 0x0F) {
						case 0x0F:
							break;
						case 0x0E:
							dst[0] = ref[0];
							break;
						case 0x0D:
							dst[1] = ref[1];
							break;
						case 0x0C:
							dst[1] = ref[1];
							dst[0] = ref[0];
							break;
						case 0x0B:
							dst[2] = ref[2];
							break;
						case 0x0A:
							dst[2] = ref[2];
							dst[0] = ref[0];
							break;
						case 0x09:
							dst[2] = ref[2];
							dst[1] = ref[1];
							break;
						case 0x08:
							dst[2] = ref[2];
							dst[1] = ref[1];
							dst[0] = ref[0];
							break;
						case 0x07:
							dst[3] = ref[3];
							break;
						case 0x06:
							dst[3] = ref[3];
							dst[0] = ref[0];
							break;
						case 0x05:
							dst[3] = ref[3];
							dst[1] = ref[1];
							break;
						case 0x04:
							dst[3] = ref[3];
							dst[1] = ref[1];
							dst[0] = ref[0];
							break;
						case 0x03:
							dst[3] = ref[3];
							dst[2] = ref[2];
							break;
						case 0x02:
							dst[3] = ref[3];
							dst[2] = ref[2];
							dst[0] = ref[0];
							break;
						case 0x01:
							dst[3] = ref[3];
							dst[2] = ref[2];
							dst[1] = ref[1];
							break;
						case 0x00:
							dst[3] = ref[3];
							dst[2] = ref[2];
							dst[1] = ref[1];
							dst[0] = ref[0];
							break;
					}
					switch (mask & 0xF0) {
						case 0xF0:
							break;
						case 0xE0:
							dst[4] = ref[4];
							break;
						case 0xD0:
							dst[5] = ref[5];
							break;
						case 0xC0:
							dst[5] = ref[5];
							dst[4] = ref[4];
							break;
						/* The original tests 0x0B here, so a 0xB0 mask never
						 * copies pixel 6. */
						case 0x0B:
							dst[6] = ref[6];
							break;
						case 0xA0:
							dst[6] = ref[6];
							dst[4] = ref[4];
							break;
						case 0x90:
							dst[6] = ref[6];
							dst[5] = ref[5];
							break;
						case 0x80:
							dst[6] = ref[6];
							dst[5] = ref[5];
							dst[4] = ref[4];
							break;
						case 0x70:
							dst[7] = ref[7];
							break;
						case 0x60:
							dst[7] = ref[7];
							dst[4] = ref[4];
							break;
						case 0x50:
							dst[7] = ref[7];
							dst[5] = ref[5];
							break;
						case 0x40:
							dst[7] = ref[7];
							dst[5] = ref[5];
							dst[4] = ref[4];
							break;
						case 0x30:
							dst[7] = ref[7];
							dst[6] = ref[6];
							break;
						case 0x20:
							dst[7] = ref[7];
							dst[6] = ref[6];
							dst[4] = ref[4];
							break;
						case 0x10:
							dst[7] = ref[7];
							dst[6] = ref[6];
							dst[5] = ref[5];
							break;
						case 0x00:
							dst[7] = ref[7];
							dst[6] = ref[6];
							dst[5] = ref[5];
							dst[4] = ref[4];
							break;
					}
					dst += 320;
					ref += 320;
				}
				cur += 8;
				prev += 8;
			} else if (cmd != 0x7FFE) {
				/* MIXED: run-coded block with the reference at cmd offset */
				uint8_t* dst = cur;
				uint8_t* ref = prev + cmd;
				int pixels = 0;
				int col = 0;

				do {
					int count = *stream;
					int type = count & 3;

					if ((type & 1) == 0) {
						/* Raw pixels follow in the stream */
						count >>= 1;
						pixels += count;
						stream++;
						if (count >= 8 - col) {
							int span = 8 - col;
							int n = span;
							int first;
							count -= span;
							first = col;
							{
								uint8_t* to = dst;
								const uint8_t* from = stream;
								length = span;
								start = first;
								if (length != 0) {
									if (start & 1) {
										if (length == 1) {
											to[0] = from[0];
										} else if (length == 2) {
											to[0] = from[0];
											to[1] = from[1];
										} else if (length == 3) {
											to[0] = from[0];
											to[1] = from[1];
											to[2] = from[2];
										} else if (length == 4) {
											*(uint32_t*)to = *(const uint32_t*)from;
										} else if (length == 5) {
											to[0] = from[0];
											*(uint32_t*)(to + 1) = *(const uint32_t*)(from + 1);
										} else if (length == 6) {
											to[0] = from[0];
											*(uint32_t*)(to + 1) = *(const uint32_t*)(from + 1);
											to[5] = from[5];
										} else {
											to[0] = from[0];
											to[1] = from[1];
											to[2] = from[2];
											*(uint32_t*)(to + 3) = *(const uint32_t*)(from + 3);
										}
									} else {
										if (length == 1) {
											to[0] = from[0];
										} else if (length == 2) {
											to[0] = from[0];
											to[1] = from[1];
										} else if (length == 3) {
											to[0] = from[0];
											to[1] = from[1];
											to[2] = from[2];
										} else if (length == 4) {
											*(uint32_t*)to = *(const uint32_t*)from;
										} else if (length == 5) {
											*(uint32_t*)to = *(const uint32_t*)from;
											to[4] = from[4];
										} else if (length == 6) {
											*(uint32_t*)to = *(const uint32_t*)from;
											to[4] = from[4];
											to[5] = from[5];
										} else if (length == 7) {
											*(uint32_t*)to = *(const uint32_t*)from;
											to[4] = from[4];
											to[5] = from[5];
											to[6] = from[6];
										} else {
											*(uint32_t*)to = *(const uint32_t*)from;
											*(uint32_t*)(to + 4) = *(const uint32_t*)(from + 4);
										}
									}
								}
							}
							dst += 320 - col;
							ref += 320 - col;
							stream += n;
							col = 0;
							while (count >= 8) {
								*(uint32_t*)dst = *(const uint32_t*)stream;
								*(uint32_t*)(dst + 4) = *(const uint32_t*)(stream + 4);
								dst += 320;
								ref += 320;
								stream += 8;
								count -= 8;
							}
						}
						if (count != 0) {
							if (count == 1) {
								dst[0] = stream[0];
							} else if (count == 2) {
								dst[0] = stream[0];
								dst[1] = stream[1];
							} else if (count == 3) {
								dst[0] = stream[0];
								dst[1] = stream[1];
								dst[2] = stream[2];
							} else if (count == 4) {
								*(uint32_t*)dst = *(const uint32_t*)stream;
							} else if (count == 5) {
								*(uint32_t*)dst = *(const uint32_t*)stream;
								dst[4] = stream[4];
							} else if (count == 6) {
								*(uint32_t*)dst = *(const uint32_t*)stream;
								dst[4] = stream[4];
								dst[5] = stream[5];
							} else if (count == 7) {
								*(uint32_t*)dst = *(const uint32_t*)stream;
								dst[4] = stream[4];
								dst[5] = stream[5];
								dst[6] = stream[6];
							} else {
								*(uint32_t*)dst = *(const uint32_t*)stream;
								*(uint32_t*)(dst + 4) = *(const uint32_t*)(stream + 4);
							}
						}
						dst += count;
						stream += count;
						col += count;
						ref += count;
					} else if (type == 3) {
						/* Copy pixels from the reference frame */
						count >>= 2;
						pixels += count;
						stream++;
						if (count >= 8 - col) {
							int span = 8 - col;
							int first;
							count -= span;
							first = col;
							{
								const uint8_t* from = ref;
								uint8_t* to = dst;
								length = span;
								start = first;
								if (length != 0) {
									if (start & 1) {
										if (length == 1) {
											to[0] = from[0];
										} else if (length == 2) {
											to[0] = from[0];
											to[1] = from[1];
										} else if (length == 3) {
											to[0] = from[0];
											to[1] = from[1];
											to[2] = from[2];
										} else if (length == 4) {
											*(uint32_t*)to = *(const uint32_t*)from;
										} else if (length == 5) {
											to[0] = from[0];
											*(uint32_t*)(to + 1) = *(const uint32_t*)(from + 1);
										} else if (length == 6) {
											to[0] = from[0];
											*(uint32_t*)(to + 1) = *(const uint32_t*)(from + 1);
											to[5] = from[5];
										} else {
											to[0] = from[0];
											to[1] = from[1];
											to[2] = from[2];
											*(uint32_t*)(to + 3) = *(const uint32_t*)(from + 3);
										}
									} else {
										if (length == 1) {
											to[0] = from[0];
										} else if (length == 2) {
											to[0] = from[0];
											to[1] = from[1];
										} else if (length == 3) {
											to[0] = from[0];
											to[1] = from[1];
											to[2] = from[2];
										} else if (length == 4) {
											*(uint32_t*)to = *(const uint32_t*)from;
										} else if (length == 5) {
											*(uint32_t*)to = *(const uint32_t*)from;
											to[4] = from[4];
										} else if (length == 6) {
											*(uint32_t*)to = *(const uint32_t*)from;
											to[4] = from[4];
											to[5] = from[5];
										} else if (length == 7) {
											*(uint32_t*)to = *(const uint32_t*)from;
											to[4] = from[4];
											to[5] = from[5];
											to[6] = from[6];
										} else {
											*(uint32_t*)to = *(const uint32_t*)from;
											*(uint32_t*)(to + 4) = *(const uint32_t*)(from + 4);
										}
									}
								}
							}
							dst += 320 - col;
							ref += 320 - col;
							col = 0;
							while (count >= 8) {
								*(uint32_t*)dst = *(const uint32_t*)ref;
								*(uint32_t*)(dst + 4) = *(const uint32_t*)(ref + 4);
								dst += 320;
								ref += 320;
								count -= 8;
							}
						}
						if (count != 0) {
							if (count == 1) {
								dst[0] = ref[0];
							} else if (count == 2) {
								dst[0] = ref[0];
								dst[1] = ref[1];
							} else if (count == 3) {
								dst[0] = ref[0];
								dst[1] = ref[1];
								dst[2] = ref[2];
							} else if (count == 4) {
								*(uint32_t*)dst = *(const uint32_t*)ref;
							} else if (count == 5) {
								*(uint32_t*)dst = *(const uint32_t*)ref;
								dst[4] = ref[4];
							} else if (count == 6) {
								*(uint32_t*)dst = *(const uint32_t*)ref;
								dst[4] = ref[4];
								dst[5] = ref[5];
							} else if (count == 7) {
								*(uint32_t*)dst = *(const uint32_t*)ref;
								dst[4] = ref[4];
								dst[5] = ref[5];
								dst[6] = ref[6];
							} else {
								*(uint32_t*)dst = *(const uint32_t*)ref;
								*(uint32_t*)(dst + 4) = *(const uint32_t*)(ref + 4);
							}
						}
						dst += count;
						col += count;
						ref += count;
					} else if (count != 1) {
						/* Skip pixels, keeping the current frame */
						count >>= 2;
						pixels += count;
						stream++;
						if (count >= 8 - col) {
							count -= 8 - col;
							dst += 320 - col;
							ref += 320 - col;
							col = 0;
							while (count >= 8) {
								dst += 320;
								ref += 320;
								count -= 8;
							}
						}
						dst += count;
						col += count;
						ref += count;
					} else {
						/* Fill: count byte, then color byte */
						count = stream[1];
						value = stream[2];
						pixels += count;
						stream += 3;
						if (count >= 8 - col) {
							int n = 8 - col;
							count -= n;
							{
								uint8_t color = (uint8_t)value;
								if (n != 0) {
									if (col & 1) {
										if (n == 1) {
											dst[0] = color;
										} else if (n == 2) {
											dst[1] = color;
											dst[0] = color;
										} else if (n == 3) {
											dst[1] = color;
											dst[2] = color;
											dst[0] = color;
										} else if (n == 4) {
											*(uint32_t*)dst =
												(color << 8) + color + (((color << 8) + color) << 16);
										} else if (n == 5) {
											*(uint32_t*)(dst + 1) =
												(color << 8) + color + (((color << 8) + color) << 16);
											dst[0] = color;
										} else if (n == 6) {
											*(uint32_t*)(dst + 1) =
												(color << 8) + color + (((color << 8) + color) << 16);
											dst[0] = color;
											dst[5] = color;
										} else {
											*(uint32_t*)(dst + 3) =
												(color << 8) + color + (((color << 8) + color) << 16);
											dst[0] = color;
											dst[1] = color;
											dst[2] = color;
										}
									} else {
										if (n == 1) {
											dst[0] = color;
										} else if (n == 2) {
											dst[1] = color;
											dst[0] = color;
										} else if (n == 3) {
											dst[1] = color;
											dst[2] = color;
											dst[0] = color;
										} else if (n == 4) {
											*(uint32_t*)dst =
												(color << 8) + color + (((color << 8) + color) << 16);
										} else if (n == 5) {
											*(uint32_t*)dst =
												(color << 8) + color + (((color << 8) + color) << 16);
											dst[4] = color;
										} else if (n == 6) {
											*(uint32_t*)dst =
												(color << 8) + color + (((color << 8) + color) << 16);
											dst[4] = color;
											dst[5] = color;
										} else if (n == 7) {
											*(uint32_t*)dst =
												(color << 8) + color + (((color << 8) + color) << 16);
											dst[4] = color;
											dst[5] = color;
											dst[6] = color;
										} else {
											*(uint32_t*)dst =
												(color << 8) + color + (((color << 8) + color) << 16);
											*(uint32_t*)(dst + 4) =
												(color << 8) + color + (((color << 8) + color) << 16);
										}
									}
								}
							}
							dst += 320 - col;
							ref += 320 - col;
							col = 0;
							while (count >= 8) {
								*(uint32_t*)dst = (value << 8) + value + (((value << 8) + value) << 16);
								*(uint32_t*)(dst + 4) = (value << 8) + value + (((value << 8) + value) << 16);
								dst += 320;
								ref += 320;
								count -= 8;
							}
						}
						if (count != 0) {
							if (count == 1) {
								dst[0] = (uint8_t)value;
							} else if (count == 2) {
								dst[1] = (uint8_t)value;
								dst[0] = (uint8_t)value;
							} else if (count == 3) {
								dst[1] = (uint8_t)value;
								dst[2] = (uint8_t)value;
								dst[0] = (uint8_t)value;
							} else if (count == 4) {
								*(uint32_t*)dst = ((uint8_t)value << 8) + (uint8_t)value +
												  ((((uint8_t)value << 8) + (uint8_t)value) << 16);
							} else if (count == 5) {
								*(uint32_t*)dst = ((uint8_t)value << 8) + (uint8_t)value +
												  ((((uint8_t)value << 8) + (uint8_t)value) << 16);
								dst[4] = (uint8_t)value;
							} else if (count == 6) {
								*(uint32_t*)dst = ((uint8_t)value << 8) + (uint8_t)value +
												  ((((uint8_t)value << 8) + (uint8_t)value) << 16);
								dst[4] = (uint8_t)value;
								dst[5] = (uint8_t)value;
							} else if (count == 7) {
								*(uint32_t*)dst = ((uint8_t)value << 8) + (uint8_t)value +
												  ((((uint8_t)value << 8) + (uint8_t)value) << 16);
								dst[4] = (uint8_t)value;
								dst[5] = (uint8_t)value;
								dst[6] = (uint8_t)value;
							} else {
								*(uint32_t*)dst = ((uint8_t)value << 8) + (uint8_t)value +
												  ((((uint8_t)value << 8) + (uint8_t)value) << 16);
								*(uint32_t*)(dst + 4) = ((uint8_t)value << 8) + (uint8_t)value +
														((((uint8_t)value << 8) + (uint8_t)value) << 16);
							}
						}
						dst += count;
						col += count;
						ref += count;
					}
				} while (pixels < 64);
				cur += 8;
				prev += 8;
			} else {
				/* SKIP: block unchanged */
				cur += 8;
				prev += 8;
			}
		}
		/* Move down to the next row of blocks */
		cur += 7 * 320;
		prev += 7 * 320;
	}

	/* Update reference frame for next decode */
	memcpy(prev_start, cur_start, 64000);
}
