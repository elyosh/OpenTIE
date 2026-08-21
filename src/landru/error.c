#include <stddef.h>

#include <landru/error.h>

#include "host_internal.h"

// GLOBAL: TIE 0xD2F4A
int16_t assertion_level;

// GLOBAL: TIE 0xD2F48
static int16_t landru_exit_gbl = -1;
// GLOBAL: TIE 0xD2F46
static int16_t landru_err_gbl = 1;
// GLOBAL: TIE 0xD2F44
static int16_t landru_escape_gbl;
// GLOBAL: TIE 0xD2F40
static LandruEscapeFunc landru_escfunc_gbl;
// GLOBAL: TIE 0xD2F3C
static LandruBailFunc landru_bailfunc_gbl;
// GLOBAL: TIE 0xD2F4C
static int16_t assertion_fired;

/* --- Bail --- */

void lerror_Do_Landru_Bail(void) {
	if (!assertion_fired) {
		assertion_fired = 1;
		if (landru_bailfunc_gbl)
			landru_bailfunc_gbl();
	}
}

void lerror_Set_Landru_Bail_Function(LandruBailFunc fn) { landru_bailfunc_gbl = fn; }

/* --- Exit --- */

void lerror_Clear_Landru_Exit(void) { landru_exit_gbl = -1; }
int16_t lerror_Get_Landru_Exit(void) { return landru_exit_gbl; }
void lerror_Set_Landru_Exit(int16_t code) { landru_exit_gbl = code; }
bool lerror_Is_Landru_Exit(void) { return landru_exit_gbl != -1; }

bool lerror_Is_Landru_Running(void) { return landru_exit_gbl == -1 && !lerror_Is_Landru_Error(); }

/* --- Escape --- */

void lerror_Clear_Landru_Escape(void) {
	landru_escfunc_gbl = NULL;
	landru_escape_gbl = 0;
}

int16_t lerror_Do_Landru_Escape(void) {
	int16_t result;
	if (landru_escfunc_gbl)
		result = landru_escfunc_gbl();
	else
		result = landru_escape_gbl;
	landru_exit_gbl = result;
	return result;
}

int16_t lerror_Get_Landru_Escape(void) { return landru_escape_gbl; }
void lerror_Set_Landru_Escape(int16_t value) { landru_escape_gbl = value; }

void lerror_Get_Landru_Escape_Function(LandruEscapeFunc* out) { *out = landru_escfunc_gbl; }
void lerror_Set_Landru_Escape_Function(LandruEscapeFunc fn) { landru_escfunc_gbl = fn; }

/* --- Error --- */

void lerror_Clear_Landru_Error(void) { landru_err_gbl = 1; }
int16_t lerror_Get_Landru_Error(void) { return landru_err_gbl; }
void lerror_Set_Landru_Error(int16_t code) { landru_err_gbl = code; }
bool lerror_Is_Landru_Error(void) { return landru_err_gbl != 1; }

/* --- Assertion level --- */

void lerror_Set_Assertion_Level(int16_t level) { assertion_level = level; }
int16_t lerror_Get_Assertion_Level(void) { return assertion_level; }

/* --- Assert --- */

void lerror_Assert_Error(const char* file, uint16_t line, int16_t group, int16_t expr, int level) {
	if (!expr && (int16_t)level <= assertion_level)
		lerror_Display_Assert(file, line, group, NULL);
}

void lerror_Assert_Error_Message(const char* file, uint16_t line, int16_t group, int16_t expr,
								 const char* msg, int level) {
	if (!expr && (int16_t)level <= assertion_level)
		lerror_Display_Assert(file, line, group, msg);
}

void lerror_Display_Assert(const char* file, uint16_t line, int group, const char* msg) {
	lerror_Do_Landru_Bail();

	landru_host_log(LANDRU_LOG_ERROR, "%s(%d): ", file, line);

	switch (group) {
		case 2:
			landru_host_log(LANDRU_LOG_ERROR, "Out of pointer memory\n");
			break;
		case 3:
			landru_host_log(LANDRU_LOG_ERROR, "Out of handle memory\n");
			break;
		case 4:
			landru_host_log(LANDRU_LOG_ERROR, "File error\n");
			break;
		case 5:
			landru_host_log(LANDRU_LOG_ERROR, "LFD error\n");
			break;
		case 6:
			landru_host_log(LANDRU_LOG_ERROR, "Resource error\n");
			break;
		case 7:
			landru_host_log(LANDRU_LOG_ERROR, "Value out of bounds\n");
			break;
		case 8:
			landru_host_log(LANDRU_LOG_ERROR, "String invalid\n");
			break;
		case 9:
			landru_host_log(LANDRU_LOG_ERROR, "Pointer invalid\n");
			break;
		case 10:
			landru_host_log(LANDRU_LOG_ERROR, "Handle invalid\n");
			break;
		case 11:
			landru_host_log(LANDRU_LOG_ERROR, "Item not in list\n");
			break;
		case 12:
			landru_host_log(LANDRU_LOG_ERROR, "Unable to create\n");
			break;
		case 13:
			landru_host_log(LANDRU_LOG_ERROR, "Unable to destroy\n");
			break;
		case 14:
			landru_host_log(LANDRU_LOG_ERROR, "Module not created\n");
			break;
		default:
			landru_host_log(LANDRU_LOG_ERROR, "Assertion failed\n");
			break;
	}

	if (msg && *msg)
		landru_host_log(LANDRU_LOG_ERROR, "%s(%d): %s\n", file, line, msg);

	landru_err_gbl = group > 1 ? (int16_t)group : 0;
	landru_exit_gbl = 0;
}
