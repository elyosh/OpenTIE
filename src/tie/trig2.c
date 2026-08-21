/*
 * TRIG2.C — Fixed-point trigonometry library
 *
 * Provides sine, cosine, arctangent, and coordinate conversion for
 * the 3D flight engine. Uses 16-bit angles (0x10000 = 360°) and
 * lookup tables with linear interpolation.
 *
 * Sine table: 257 entries covering 0-90° (quarter wave), values 0..65534.
 * Arctangent table: 258 entries for atan(i/256), values 0..8192 (= 0-45°).
 * Square root table: 258 entries for sqrt(1 + (i/256)²) * 256.
 *
 * WARNING: many functions use global working variables (xoffset, yoffset,
 * etc.) making them non-reentrant. This matches the original binary.
 */

#include <stdint.h>
#include <stdlib.h>

#include "tie/trig2.h"

/* Half-wave sine table: 514 entries, sin(i * 180°/512) * 65534.
 * Entries 0..256 rise from 0 to 65534 (sin 0°..90°); entries 256..512
 * descend symmetrically back to 0 (sin 90°..180°); entry 513 is a
 * trailing zero for safe sintable[idx+1] access when idx=512. The
 * demo's _sintable at 0xdc44c and retail sintable at 0xcd200 both have
 * the full range. With only 257 entries any angle past 90° read past
 * the array and returned garbage from adjacent globals — visible as
 * rotating axes whose magnitude shrinks mid-turn. */
// GLOBAL: TIE 0xCD200
static const uint16_t sintable[514] = {
	0,     402,   804,   1206,  1608,  2010,  2412,  2814,  3216,  3617,  4019,  4420,  4821,  5222,  5623,
	6023,  6424,  6824,  7224,  7623,  8022,  8421,  8820,  9218,  9616,  10014, 10411, 10808, 11204, 11600,
	11996, 12391, 12785, 13180, 13573, 13966, 14359, 14751, 15143, 15534, 15924, 16314, 16703, 17091, 17479,
	17867, 18253, 18639, 19024, 19409, 19792, 20175, 20557, 20939, 21320, 21699, 22078, 22457, 22834, 23210,
	23586, 23961, 24335, 24708, 25080, 25451, 25821, 26190, 26558, 26925, 27291, 27656, 28020, 28383, 28745,
	29106, 29466, 29824, 30182, 30538, 30893, 31248, 31600, 31952, 32303, 32652, 33000, 33347, 33692, 34037,
	34380, 34721, 35062, 35401, 35738, 36075, 36410, 36744, 37076, 37407, 37736, 38064, 38391, 38716, 39040,
	39362, 39683, 40002, 40320, 40636, 40951, 41264, 41576, 41886, 42194, 42501, 42806, 43110, 43412, 43713,
	44011, 44308, 44604, 44898, 45190, 45480, 45769, 46056, 46341, 46624, 46906, 47186, 47464, 47741, 48015,
	48288, 48559, 48828, 49095, 49361, 49624, 49886, 50146, 50404, 50660, 50914, 51166, 51417, 51665, 51911,
	52156, 52398, 52639, 52878, 53114, 53349, 53581, 53812, 54040, 54267, 54491, 54714, 54934, 55152, 55368,
	55582, 55794, 56004, 56212, 56418, 56621, 56823, 57022, 57219, 57414, 57607, 57798, 57986, 58172, 58356,
	58538, 58718, 58896, 59071, 59244, 59415, 59583, 59750, 59914, 60075, 60235, 60392, 60547, 60700, 60851,
	60999, 61145, 61288, 61429, 61568, 61705, 61839, 61971, 62101, 62228, 62353, 62476, 62596, 62714, 62830,
	62943, 63054, 63162, 63268, 63372, 63473, 63572, 63668, 63763, 63854, 63944, 64031, 64115, 64197, 64277,
	64354, 64429, 64501, 64571, 64639, 64704, 64766, 64827, 64884, 64940, 64993, 65043, 65091, 65137, 65180,
	65220, 65259, 65294, 65328, 65358, 65387, 65413, 65436, 65457, 65476, 65492, 65505, 65516, 65525, 65531,
	65533, 65534, 65533, 65531, 65525, 65516, 65505, 65492, 65476, 65457, 65436, 65413, 65387, 65358, 65328,
	65294, 65259, 65220, 65180, 65137, 65091, 65043, 64993, 64940, 64884, 64827, 64766, 64704, 64639, 64571,
	64501, 64429, 64354, 64277, 64197, 64115, 64031, 63944, 63854, 63763, 63668, 63572, 63473, 63372, 63268,
	63162, 63054, 62943, 62830, 62714, 62596, 62476, 62353, 62228, 62101, 61971, 61839, 61705, 61568, 61429,
	61288, 61145, 60999, 60851, 60700, 60547, 60392, 60235, 60075, 59914, 59750, 59583, 59415, 59244, 59071,
	58896, 58718, 58538, 58356, 58172, 57986, 57798, 57607, 57414, 57219, 57022, 56823, 56621, 56418, 56212,
	56004, 55794, 55582, 55368, 55152, 54934, 54714, 54491, 54267, 54040, 53812, 53581, 53349, 53114, 52878,
	52639, 52398, 52156, 51911, 51665, 51417, 51166, 50914, 50660, 50404, 50146, 49886, 49624, 49361, 49095,
	48828, 48559, 48288, 48015, 47741, 47464, 47186, 46906, 46624, 46341, 46056, 45769, 45480, 45190, 44898,
	44604, 44308, 44011, 43713, 43412, 43110, 42806, 42501, 42194, 41886, 41576, 41264, 40951, 40636, 40320,
	40002, 39683, 39362, 39040, 38716, 38391, 38064, 37736, 37407, 37076, 36744, 36410, 36075, 35738, 35401,
	35062, 34721, 34380, 34037, 33692, 33347, 33000, 32652, 32303, 31952, 31600, 31248, 30893, 30538, 30182,
	29824, 29466, 29106, 28745, 28383, 28020, 27656, 27291, 26925, 26558, 26190, 25821, 25451, 25080, 24708,
	24335, 23961, 23586, 23210, 22834, 22457, 22078, 21699, 21320, 20939, 20557, 20175, 19792, 19409, 19024,
	18639, 18253, 17867, 17479, 17091, 16703, 16314, 15924, 15534, 15143, 14751, 14359, 13966, 13573, 13180,
	12785, 12391, 11996, 11600, 11204, 10808, 10411, 10014, 9616,  9218,  8820,  8421,  8022,  7623,  7224,
	6824,  6424,  6023,  5623,  5222,  4821,  4420,  4019,  3617,  3216,  2814,  2412,  2010,  1608,  1206,
	804,   402,   0,     0,
};

/* Arctangent table: 258 entries, atan(i/256) * 32768/90° */
// GLOBAL: TIE 0xCD904
static const uint16_t arctantable[258] = {
	0,    41,   81,   122,  163,  204,  244,  285,  326,  367,  407,  448,  489,  529,  570,  610,  651,
	692,  732,  773,  813,  854,  894,  935,  975,  1015, 1056, 1096, 1136, 1177, 1217, 1257, 1297, 1337,
	1377, 1417, 1457, 1497, 1537, 1577, 1617, 1656, 1696, 1736, 1775, 1815, 1854, 1894, 1933, 1973, 2012,
	2051, 2090, 2129, 2168, 2207, 2246, 2285, 2324, 2363, 2401, 2440, 2478, 2517, 2555, 2593, 2632, 2670,
	2708, 2746, 2784, 2822, 2860, 2897, 2935, 2973, 3010, 3047, 3085, 3122, 3159, 3196, 3233, 3270, 3307,
	3344, 3380, 3417, 3453, 3490, 3526, 3562, 3598, 3634, 3670, 3706, 3742, 3778, 3813, 3849, 3884, 3920,
	3955, 3990, 4025, 4060, 4095, 4129, 4164, 4199, 4233, 4267, 4302, 4336, 4370, 4404, 4438, 4471, 4505,
	4539, 4572, 4605, 4639, 4672, 4705, 4738, 4771, 4803, 4836, 4869, 4901, 4933, 4966, 4998, 5030, 5062,
	5093, 5125, 5157, 5188, 5220, 5251, 5282, 5313, 5344, 5375, 5406, 5437, 5467, 5498, 5528, 5558, 5589,
	5619, 5649, 5679, 5708, 5738, 5768, 5797, 5826, 5856, 5885, 5914, 5943, 5972, 6000, 6029, 6057, 6086,
	6114, 6142, 6171, 6199, 6226, 6254, 6282, 6310, 6337, 6365, 6392, 6419, 6446, 6473, 6500, 6527, 6554,
	6580, 6607, 6633, 6660, 6686, 6712, 6738, 6764, 6790, 6815, 6841, 6867, 6892, 6917, 6943, 6968, 6993,
	7018, 7043, 7067, 7092, 7117, 7141, 7166, 7190, 7214, 7238, 7262, 7286, 7310, 7334, 7358, 7381, 7405,
	7428, 7451, 7474, 7498, 7521, 7544, 7566, 7589, 7612, 7634, 7657, 7679, 7702, 7724, 7746, 7768, 7790,
	7812, 7834, 7856, 7877, 7899, 7920, 7942, 7963, 7984, 8005, 8026, 8047, 8068, 8089, 8110, 8130, 8151,
	8172, 8192, 8192,
};

/* Square root table: sqrt(1 + (i/256)²) * 256, for distance computation */
// GLOBAL: TIE 0xCDB08
static const uint16_t squarerootable[258] = {
	0,     0,     2,     4,     8,     12,    18,    24,    32,    40,    50,    60,    72,    84,    98,
	112,   128,   144,   162,   180,   200,   220,   242,   264,   287,   312,   337,   363,   391,   419,
	448,   479,   510,   542,   575,   610,   645,   681,   718,   756,   795,   835,   876,   918,   961,
	1005,  1050,  1095,  1142,  1190,  1238,  1288,  1338,  1390,  1442,  1495,  1550,  1605,  1661,  1718,
	1776,  1835,  1895,  1955,  2017,  2079,  2143,  2207,  2273,  2339,  2406,  2474,  2543,  2612,  2683,
	2755,  2827,  2900,  2974,  3049,  3125,  3202,  3280,  3358,  3438,  3518,  3599,  3681,  3764,  3847,
	3932,  4017,  4103,  4190,  4278,  4367,  4456,  4547,  4638,  4730,  4822,  4916,  5010,  5105,  5201,
	5298,  5396,  5494,  5593,  5693,  5794,  5895,  5997,  6100,  6204,  6309,  6414,  6520,  6627,  6734,
	6843,  6952,  7061,  7172,  7283,  7395,  7508,  7621,  7735,  7850,  7966,  8082,  8199,  8317,  8435,
	8554,  8674,  8794,  8915,  9037,  9160,  9283,  9407,  9531,  9656,  9782,  9909,  10036, 10164, 10292,
	10421, 10551, 10681, 10812, 10944, 11076, 11209, 11343, 11477, 11612, 11747, 11883, 12019, 12157, 12294,
	12433, 12572, 12711, 12852, 12992, 13134, 13276, 13418, 13561, 13705, 13849, 13994, 14139, 14285, 14431,
	14578, 14726, 14874, 15022, 15171, 15321, 15471, 15622, 15773, 15925, 16077, 16230, 16384, 16537, 16692,
	16847, 17002, 17158, 17314, 17471, 17629, 17786, 17945, 18104, 18263, 18423, 18583, 18744, 18905, 19066,
	19229, 19391, 19554, 19718, 19882, 20046, 20211, 20376, 20542, 20708, 20875, 21042, 21209, 21377, 21546,
	21714, 21884, 22053, 22223, 22394, 22565, 22736, 22908, 23080, 23252, 23425, 23599, 23772, 23946, 24121,
	24296, 24471, 24647, 24823, 24999, 25176, 25353, 25531, 25709, 25887, 26066, 26245, 26424, 26604, 26784,
	26964, 27146, 0,
};

/* Working globals */
int32_t trig2_xoffset, trig2_yoffset, trig2_zoffset;
int32_t trig2_xmovedist, trig2_ymovedist, trig2_zmovedist;
int32_t trig2_rho, trig2_distanceplane, trig2_polardistance;
int32_t trig2_cartesianxoffset, trig2_cartesianyoffset;
int32_t trig2_divisorhilo;
int16_t trig2_theta, trig2_phi;
int16_t trig2_xyangle, trig2_zangle, trig2_angleplane;
int16_t trig2_signx, trig2_signy, trig2_signz, trig2_signswap;
int16_t trig2_sintheta, trig2_costheta, trig2_sinphi, trig2_cosphi;
int16_t trig2_sinthetasign, trig2_costhetasign, trig2_sinphisign, trig2_cosphisign;
int16_t trig2_divisorhi, trig2_divisorlo;

/* ------------------------------------------------------------------ */

/* Core sine lookup with linear interpolation within the quarter-wave table */
// FUNCTION: TIE 0x5BBE8
uint16_t trig2_calcsineofangle(uint16_t angle) {
	/* The 0x3FE mask covers both halves of the quarter-wave table. `diff`
	 * points from the current sample to the next. */
	uint16_t idx = ((angle >> 5) & 0x3FE) >> 1; /* 0..511 */
	uint16_t base = sintable[idx];
	int16_t diff = (int16_t)(sintable[idx + 1] - sintable[idx]);
	int16_t abs_diff = (diff < 0) ? -diff : diff;
	uint16_t frac = (angle & 0x3F) << 10; /* 6-bit fraction scaled to 16-bit */
	uint16_t interp = (uint16_t)(((uint32_t)frac * abs_diff) >> 16);
	if (diff < 0)
		interp = (uint16_t)(-(int16_t)interp);
	return base + interp;
}

// FUNCTION: TIE 0x5BBE0
uint16_t trig2_getsine(uint16_t angle) { return trig2_calcsineofangle(angle); }

// FUNCTION: TIE 0x5BC58
int16_t trig2_getsignedsin(uint16_t angle) {
	/* Binary does `shr ax,1; or edx,0FFFEh; and eax,edx`. The or/and masks
	 * force bit 0 of the returned value to zero whenever the raw sine
	 * value is EVEN — a Watcom quirk that biases result to 32766 rather
	 * than 32767 for sin(90°). Reproduce it exactly so basis vectors
	 * match the binary bit-for-bit. */
	uint16_t val = trig2_calcsineofangle(angle);
	int16_t result = (int16_t)((val >> 1) & (val | 0xFFFE));
	if (angle & 0x8000)
		result = -result;
	return result;
}

// FUNCTION: TIE 0x5BE94
uint16_t trig2_getcosine(uint16_t angle) { return trig2_calcsineofangle(angle + 0x4000); }

// FUNCTION: TIE 0x5BEA4
int16_t trig2_getsignedcos(int16_t angle) {
	/* Same even-sin bit-clear mask as getsignedsin (see comment there). */
	int16_t shifted = angle + 0x4000;
	uint16_t val = trig2_calcsineofangle((uint16_t)shifted);
	int16_t result = (int16_t)((val >> 1) & (val | 0xFFFE));
	if (shifted < 0)
		result = -result;
	return result;
}

/* ------------------------------------------------------------------ */

/* Multiply 16-bit value by sine of angle, returns 16-bit fixed-point */
// FUNCTION: TIE 0x5BDDC
int16_t trig2_sinewordmult(int16_t val, uint16_t angle) {
	uint16_t abs_val = (val < 0) ? -val : val;
	int16_t sign = (val & 0x8000) ^ (angle & 0x8000);
	uint16_t idx = ((angle >> 5) & 0x3FE) >> 1; /* binary uses 0x3FE to reach sintable[256] at 90° */
	uint32_t result = (uint32_t)sintable[idx] * abs_val + 0x8000;
	if (sign)
		result = -(int32_t)result;
	return (int16_t)(result >> 16);
}

// FUNCTION: TIE 0x5BED0
int16_t trig2_cosinewordmult(int16_t val, uint16_t angle) { return trig2_sinewordmult(val, angle + 0x4000); }

/* Multiply 32-bit value by sine of angle, returns 32-bit */
// FUNCTION: TIE 0x5BE34
int32_t trig2_sinedwordmult(int32_t val, uint16_t angle) {
	int16_t sign = 0;
	if (val < 0) {
		sign = (int16_t)0x8000;
		val = -val;
	}
	sign ^= (angle & 0x8000);
	uint16_t idx = ((angle >> 5) & 0x3FE) >> 1; /* binary uses 0x3FE to reach sintable[256] at 90° */
	uint16_t s = sintable[idx];
	int32_t result =
		(int32_t)((uint16_t)(val >> 16)) * s + (int32_t)(((uint32_t)s * (uint16_t)val + 0x8000) >> 16);
	if (sign)
		result = -result;
	return result;
}

// FUNCTION: TIE 0x5BF2C
int32_t trig2_cosinedwordmult(int32_t val, uint16_t angle) {
	return trig2_sinedwordmult(val, angle + 0x4000);
}

/* ------------------------------------------------------------------ */

/* Inverse sine — binary search through sintable.
 *
 * Note on the binary: TRIG2_arcsin at 0x5BC80 has zero callers in
 * the binary and is structurally buggy:
 *  - anchors at sintable[idx-2] (`mov ax, word_CD1FC[edx]` at
 *    0x5BCB6) instead of the natural sintable[idx-1], producing
 *    results that are one step (= 64 angle units) too high;
 *  - reads sintable[-1] and sintable[-2] for idx<2, which point
 *    at unrelated globals (0xCD1FE = 40960, 0xCD1FC = 256), so
 *    arcsin(0) returns 63 in the binary instead of 0;
 *  - the `mov edi, eax; shr di, 8` pattern drops the carry into
 *    bit 16 of the quotient when target lands on a sintable entry
 *    exactly, costing a full step at the boundary.
 *
 * We don't replicate any of these bugs since the function is
 * unused. This implementation does correct math: anchor at
 * sintable[idx-1] (the lower bound of the bracket
 * sintable[idx-1] < target <= sintable[idx]), span over the
 * current interval, low-byte fraction taken at 32-bit width so
 * the boundary case target == sintable[idx] (quotient = 65536)
 * propagates a full step. */
// FUNCTION: TIE 0x5BC80
int16_t trig2_arcsin(int16_t val) {
	int16_t abs_val = (val < 0) ? -val : val;
	uint16_t target = 2 * abs_val;

	int16_t idx = 0;
	int16_t remaining = 256;
	while (remaining > 0 && target > sintable[idx]) {
		remaining--;
		idx++;
	}

	if (idx == 0)
		return 0; /* arcsin(0) = 0 */

	uint16_t base = sintable[idx - 1];
	uint16_t span = (uint16_t)(sintable[idx] - base);
	uint32_t low_byte = 0;
	if (span && target > base) {
		uint32_t quot = ((uint32_t)(target - base) << 16) / span;
		low_byte = quot >> 8;
	}

	uint32_t pre = ((uint32_t)(255 - remaining) << 8) + low_byte;
	int16_t result = (int16_t)(pre / 4);
	if (val < 0)
		result = -result;
	return result;
}

/* Inverse cosine — search sintable from 90° downward */
// FUNCTION: TIE 0x5BD0C
int16_t trig2_arccos(int16_t val) {
	int16_t abs_val = (val < 0) ? -val : val;
	uint16_t target = 2 * abs_val;

	int16_t idx = 256;
	int16_t remaining = 256;
	while (remaining > 0 && target < sintable[idx]) {
		remaining--;
		idx++;
		if (remaining <= 0) {
			/* Extrapolate from the final nonzero table entry and return directly. */
			uint16_t divisor = sintable[idx - 1];
			uint32_t quot = ((uint32_t)target << 16) / divisor;
			uint16_t v = ((uint16_t)quot) >> 8; /* mov edx,eax; shr dx,8 */
			v = (uint16_t)(-(int16_t)v);        /* neg edx (low 16 bits) */
			if (v == 0)
				return 0x4000;                  /* exactly 90° */
			int16_t result = (int16_t)(v >> 2); /* shr dx, 2 (unsigned) */
			if (val < 0)
				result = (int16_t)((uint16_t)(-result) + 0x8000u);
			return result;
		}
	}

	/* Interpolate from the previous interval with a signed delta. */
	remaining--; /* `dec ebx` at 0x5BD8C */
	uint16_t base = sintable[idx - 1];
	int16_t diff = (int16_t)(target - base);

	uint16_t low_byte = 0;
	if (diff != 0) {
		uint16_t span = (uint16_t)(sintable[idx - 2] - base);
		if (span) {
			int32_t signed_dividend = (int32_t)diff * 65536;
			int32_t quot = signed_dividend / (int32_t)span;
			low_byte = ((uint16_t)quot) >> 8; /* `shr di, 8` */
		}
	}

	uint16_t low16 = (uint16_t)(((255 - remaining) << 8) - low_byte);
	int16_t result = (int16_t)(low16 >> 2); /* `shr ax, 2` */
	if (val < 0)
		result = (int16_t)((uint16_t)(-result) + 0x8000u);
	return result;
}

/* ------------------------------------------------------------------ */

/* Core arctangent with table lookup and 8-bit linear interpolation.
 * Sets trig2_angleplane, trig2_divisorhilo, trig2_signswap globals.
 *
 * Quotient layout: bits 8..15 = integer ratio (table index), bits 0..7
 * = fractional ratio (interpolation weight, scaled to 0xFF00 to match
 * retail's `mov ebp, quot; and ebp, 0xFF; shl ebp, 8`). Special cases
 * (a==b, larger==smaller after normalization) use a frac of 0 since
 * arctantable[ratio+1]-arctantable[ratio] near the boundary is small
 * enough that the contribution is sub-tick. */
static void calcarctan_core(int32_t a, int32_t b, int16_t* out_ratio, int16_t* out_angle) {
	trig2_signswap = 0;
	uint32_t frac = 0;

	if ((uint32_t)a == (uint32_t)b) {
		*out_ratio = 256;
		trig2_divisorhilo = a;
	} else {
		uint32_t larger = (uint32_t)a, smaller = (uint32_t)b;
		if (a <= b) {
			larger = (uint32_t)b;
			smaller = (uint32_t)a;
			trig2_signswap = 1;
		}
		trig2_divisorhilo = (int32_t)larger;

		if (!larger) {
			*out_ratio = 0;
		} else {
			if (!(larger & 0xFF000000)) {
				larger <<= 8;
				smaller <<= 8;
				if (!(larger & 0xFF000000)) {
					larger <<= 8;
					smaller <<= 8;
				}
			}
			if (larger == smaller) {
				*out_ratio = 256;
			} else {
				uint32_t quot = smaller / (larger >> 16);
				*out_ratio = (int16_t)((quot & 0xFFFF) >> 8);
				frac = (quot & 0xFF) << 8;
			}
		}
	}

	uint16_t ratio = (uint16_t)*out_ratio;
	int16_t base = (int16_t)arctantable[ratio];
	int16_t next = (int16_t)arctantable[ratio + 1];
	int16_t delta = (int16_t)(next - base);
	int16_t interp = (int16_t)(((int32_t)(uint16_t)delta * (int32_t)frac) >> 16);
	*out_angle = (int16_t)(base + interp);

	if (trig2_signswap) {
		*out_angle = -*out_angle;
		*out_angle += 0x4000;
	}
}

// FUNCTION: TIE 0x5C3B4
int16_t trig2_arctan(int32_t y, int32_t x) {
	trig2_signy = 0;
	if (y < 0) {
		y = -y;
		trig2_signy = 1;
	}
	trig2_signx = 0;
	if (x < 0) {
		x = -x;
		trig2_signx = 1;
	}

	int16_t ratio, angle;
	calcarctan_core(x, y, &ratio, &angle);

	if (trig2_signy)
		angle = -angle;
	if (trig2_signx) {
		angle = -angle;
		angle += (int16_t)0x8000;
	}
	return angle;
}

/* ------------------------------------------------------------------ */

/* Public calcarctan — called by MATH2_getradarcoord */
// FUNCTION: TIE 0x5C2C0
void trig2_calcarctan(int32_t a, int32_t b) {
	int16_t ratio, angle;
	calcarctan_core(a, b, &ratio, &angle);
	trig2_angleplane = angle;
}

/* Internal: 2D cartesian to polar using calcarctan + square root table */
static void trig2_ctoptwodim_internal(int32_t a, int32_t b) {
	int16_t ratio, angle;
	calcarctan_core(a, b, &ratio, &angle);
	trig2_angleplane = angle;

	/* Distance = divisorhilo * sqrt(1 + (ratio/256)²) */
	uint16_t sqrt_val = squarerootable[(uint16_t)ratio];
	trig2_polardistance = trig2_divisorhilo +
						  (int32_t)((uint32_t)sqrt_val * (uint16_t)trig2_divisorhilo + 0x8000) / 65536 +
						  (int32_t)sqrt_val * (trig2_divisorhilo >> 16);
}

void trig2_ptoc3dim(void) {
	trig2_zoffset = trig2_sinedwordmult(trig2_rho, trig2_phi);
	trig2_xoffset = trig2_cosinedwordmult(trig2_zoffset, trig2_theta);
	trig2_yoffset = trig2_sinedwordmult(trig2_zoffset, trig2_theta);
	trig2_zoffset = trig2_cosinedwordmult(trig2_rho, trig2_phi);
}

void trig2_ptoc2dim(void) {
	trig2_cartesianxoffset = trig2_cosinedwordmult(trig2_distanceplane, trig2_angleplane);
	trig2_cartesianyoffset = trig2_sinedwordmult(trig2_distanceplane, trig2_angleplane);
}

// FUNCTION: TIE 0x5C024
void trig2_movexyz(uint16_t distance, int16_t pitch, uint16_t heading) {
	trig2_phi = heading;
	trig2_theta = 0x4000 - pitch;
	trig2_rho = distance;
	trig2_ptoc3dim();
	trig2_xmovedist = trig2_xoffset;
	trig2_ymovedist = trig2_yoffset;
	trig2_zmovedist = trig2_zoffset;
}

// FUNCTION: TIE 0x5C1D0
void trig2_ctop2dim(int32_t x, int32_t y) {
	trig2_signx = 0;
	if (x < 0) {
		x = -x;
		trig2_signx = 1;
	}
	trig2_signy = 0;
	if (y < 0) {
		y = -y;
		trig2_signy = 1;
	}

	trig2_ctoptwodim_internal(x, y);

	int16_t angle = trig2_angleplane;
	if (trig2_signy)
		angle = -angle;
	if (trig2_signx) {
		angle = -angle;
		angle += (int16_t)0x8000;
	}
	angle = -angle + 0x4000;
	trig2_xyangle = angle;
}

// FUNCTION: TIE 0x5C0BC
void trig2_ctop(int32_t x, int32_t y, int32_t z) {
	trig2_signx = (x < 0) ? 1 : 0;
	if (x < 0)
		x = -x;
	trig2_xoffset = x;

	trig2_signy = (y < 0) ? 1 : 0;
	if (y < 0)
		y = -y;
	trig2_yoffset = y;

	trig2_signz = (z < 0) ? 1 : 0;
	if (z < 0)
		z = -z;
	trig2_zoffset = z;

	/* XY-plane angle */
	trig2_ctoptwodim_internal(trig2_xoffset, trig2_yoffset);
	trig2_xyangle = trig2_angleplane;
	if (trig2_signy)
		trig2_xyangle = -trig2_angleplane;
	if (trig2_signx) {
		trig2_xyangle = -trig2_xyangle;
		trig2_xyangle += (int16_t)0x8000;
	}
	trig2_xyangle = -trig2_xyangle + 0x4000;

	/* Z-elevation angle */
	trig2_ctoptwodim_internal(trig2_polardistance, trig2_zoffset);
	trig2_zangle = trig2_angleplane;
	if (trig2_signz)
		trig2_zangle = -trig2_angleplane;
	trig2_zangle = -trig2_zangle + 0x4000;
}
