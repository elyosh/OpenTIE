/* FEINPUT — front-end input: joystick, keyboard, mouse with de-jitter. */

#include "tie/feinput.h"
#include "tie/tie.h"
#include "tie_runtime/display/classic_framebuffer.h"

#include <string.h>

#include "tie_runtime/input/input.h"

/* --- Unimplemented module functions --- */

/* Landru subsystem */
#include "landru/joy.h"
#include "landru/vesa.h"

#include "tie/logbuf2.h"
#include "tie/mouse2.h"
#include "tie/rtsvga2.h"
#include "tie/xtimer.h"

// FUNCTION: TIE98 0x49A2E0
int8_t FlightInput_GetChar(void) {
	/* MODERN ADAPTATION: the application host owns the platform event
	 * queue that replaces the recovered Win32 message pump. */
	return (int8_t)TieInput_ReadKey();
}

/* --- FEINPUT globals --- */

/* Retail-binary mode dispatch table (3 modes x 13 slots) consumed by
 * feinput_SetGraphicsPtrs. In the original the linker populated this
 * at 0xC1EA4 with three backends:
 *   mode 0: VGA  320x200    (RTSVGA2_*VGA)
 *   mode 1: SVGA 640x480    (RTSVGA2_*VGA, slot 8 swapped to outchar32VGA)
 *   mode 2: RGB  640x480x16 (TIE98 packed-pixel routines) */
// GLOBAL: TIE 0xC1EA4
void* graphroutines[39] = {
	/* --- mode 0: VGA 320x200 --- */
	(void*)rtsvga2_initgraphVGA,      /* [0]  initgraph */
	(void*)rtsvga2_blankVGA,          /* [1]  blank */
	(void*)rtsvga2_unblankVGA,        /* [2]  unblank */
	(void*)rtsvga2_buildpaletteVGA,   /* [3]  buildpalette */
	(void*)rtsvga2_savepaletteVGA,    /* [4]  savepalette */
	(void*)rtsvga2_restorepaletteVGA, /* [5]  restorepalette */
	(void*)rtsvga2_calcpositionVGA,   /* [6]  calcposition */
	(void*)rtsvga2_drawshapeVGA,      /* [7]  drawshape */
	(void*)rtsvga2_outcharVGA,        /* [8]  outchar */
	(void*)rtsvga2_clearwindowVGA,    /* [9]  clearwindow */
	(void*)rtsvga2_fillboxVGA,        /* [10] fillbox */
	(void*)rtsvga2_saveboxVGA,        /* [11] savebox */
	(void*)rtsvga2_restoreboxVGA,     /* [12] restorebox */
	/* --- mode 1: SVGA 640x480 (retail layout; slot 8 = outchar32VGA) --- */
	(void*)rtsvga2_initgraphVGA,
	(void*)rtsvga2_blankVGA,
	(void*)rtsvga2_unblankVGA,
	(void*)rtsvga2_buildpaletteVGA,
	(void*)rtsvga2_savepaletteVGA,
	(void*)rtsvga2_restorepaletteVGA,
	(void*)rtsvga2_calcpositionVGA,
	(void*)rtsvga2_drawshapeVGA,
	(void*)rtsvga2_outchar32VGA, /* retail used outchar32 here */
	(void*)rtsvga2_clearwindowVGA,
	(void*)rtsvga2_fillboxVGA,
	(void*)rtsvga2_saveboxVGA,
	(void*)rtsvga2_restoreboxVGA,
	/* --- mode 2: RGB 640x480x16 --- */
	(void*)rtsvga2_initgraphVGA,
	(void*)rtsvga2_blankVGA,
	(void*)rtsvga2_unblankVGA,
	(void*)rtsvga2_buildpaletteVGA_tie98,
	(void*)rtsvga2_savepaletteVGA_tie98,
	(void*)rtsvga2_restorepaletteVGA_tie98,
	(void*)rtsvga2_calcpositionVGA_tie98,
	(void*)rtsvga2_drawshapeVGA_tie98,
	(void*)rtsvga2_outchar32VGA_tie98,
	(void*)rtsvga2_clearwindowVGA_tie98,
	(void*)rtsvga2_fillboxVGA_tie98,
	(void*)rtsvga2_saveboxVGA_tie98,
	(void*)rtsvga2_restoreboxVGA_tie98,
};
// GLOBAL: TIE 0xD41A4
int16_t buffer256flag;
// GLOBAL: TIE 0xD41A6
int16_t thrustmastertopflag;

/* String table pointers (assigned by loadstringdata, defined here per watdbg) */

/* --- Input polling --- */

// FUNCTION: TIE 0x22DF0
void feinput_checkinput(void) {
	int16_t delta_x = 0;
	int16_t delta_y = 0;
	int16_t delta_roll = 0;

	inputbuttons = 0;

	if (mouseflag) {
		/* Retail: `xor reg,reg; mov reg16, delta; shl reg32, N; mov
		 * dest, reg16` -- bit-pattern shift, low 16 bits stored.
		 * Multiplying gives the same int16/uint16 result for any input
		 * (max product fits in int) without signed-left-shift UB. */
		delta_x = (int16_t)(deltamx * 128);
		delta_y = (int16_t)(deltamy * 64);
		inputbuttons = mousebuttons;
	}

	if (joystickflag) {
		/* joystickx/y/roll already hold logical channels (yaw / pitch
		 * / roll) — feinput_getrawinput applied TieInputMapping when
		 * it sampled raw HID axes. The scale factors below match the
		 * retail binary's gain (120× / 50×). */
		if (!delta_x)
			delta_x = 120 * joystickx;
		if (!delta_y)
			delta_y = 50 * joysticky;
		delta_roll = 120 * joystickroll;
		inputbuttons |= joybuttons;
	}

	/* Thrustmaster top-hat detection (scan codes 222/223) */
	if (keypress == 222)
		thrustmastertopflag = 1;
	if (keypress == 223)
		thrustmastertopflag = 0;
	if (thrustmastertopflag)
		inputbuttons |= 2;

	inputkey = keypress;
	inputdeltax = delta_x;
	inputdeltay = delta_y;
	inputdeltaroll = delta_roll;
}

// FUNCTION: TIE 0x22EB8
void feinput_degitterinput(void) {
	int16_t dx = inputdeltax;
	int16_t dy = inputdeltay;
	int16_t dr = inputdeltaroll;

	int16_t abs_dx = dx < 0 ? -dx : dx;
	if (abs_dx <= 64)
		dx = 0;

	int16_t abs_dy = dy < 0 ? -dy : dy;
	if (abs_dy <= 24)
		dy = 0;

	int16_t abs_dr = dr < 0 ? -dr : dr;
	if (abs_dr <= 64)
		dr = 0;

	inputdeltax = dx;
	inputdeltay = dy;
	inputdeltaroll = dr;
}

// FUNCTION: TIE 0x22F08
void feinput_getinput(void) {
	feinput_getrawinput();
	feinput_checkinput();

	int16_t abs_dx = inputdeltax < 0 ? -inputdeltax : inputdeltax;
	if (abs_dx <= 2048)
		inputdeltax = 0;

	int16_t abs_dy = inputdeltay < 0 ? -inputdeltay : inputdeltay;
	if (abs_dy <= 1536)
		inputdeltay = 0;

	int16_t abs_dr = inputdeltaroll < 0 ? -inputdeltaroll : inputdeltaroll;
	if (abs_dr <= 2048)
		inputdeltaroll = 0;
}

// FUNCTION: TIE 0x22FD8
void feinput_clearinput(void) {
	/* Drain joy+mouse buttons, then wait for 2 timer ticks of "blank" with
	 * no buttons before returning. If buttons re-engage during the wait,
	 * drain again and reset the tick counter. Matches retail FEINPUT_clearinput
	 * which uses XTIMER_Time_Elapsed as a 2-tick debounce window so the next
	 * input poll doesn't see the trailing edge of the prior press. */
	int16_t ticks = 0;
	for (;;) {
		if ((joybuttons & 0xF) || mousebuttons) {
			while ((joybuttons & 0xF) || mousebuttons)
				feinput_getinput();
			xtimer_time_elapsed();
			ticks = 0;
		}
		feinput_getinput();
		ticks += (int16_t)(xtimer_time_elapsed() & 0xFFFF);
		if (ticks >= 2)
			break;
	}
}

void feinput_waitpress(void) {
	/* Wait for any input (key, joystick button, or mouse button) */
	do {
		if (feinput_getrawinput())
			break;
		if (joybuttons & 0xF)
			break;
	} while (!mousebuttons);

	/* Wait for release */
	if ((joybuttons & 0xF) || mousebuttons) {
		while ((joybuttons & 0xF) || mousebuttons)
			feinput_getinput();
	}
}

void feinput_waitrelease(void) {
	if ((joybuttons & 0xF) || mousebuttons) {
		while ((joybuttons & 0xF) || mousebuttons)
			feinput_getinput();
	}
}

/* --- Device setup --- */

// FUNCTION: TIE 0x23038
void feinput_setupinputdevices(void) {
	joystickflag = 0;
	ngstickflag = 0;
	mouseflag = 0;
	mousebuttons = 0;
	joybuttons = 0;

	joystickcount = ljoy_Joystick_Init();
	joystickflag = (joystickcount != 0);

	mouseflag = mouse2_checkformouse();
}

/* --- Raw input polling --- */

// FUNCTION: TIE 0x233AC
uint16_t feinput_getrawinput(void) {
	/* Retail polled DOS INT 16h / mouse INT 33h directly each call; our
	 * SDL port routes keystrokes through an event queue that only fills
	 * when the application pumps platform events. Some callers (goals_missiongoalsroom,
	 * score/debrief prompts, etc.) sit in a tight do/while loop that
	 * reads raw input without hitting the xtimer path that normally pumps
	 * the queue, so the loop would spin forever without any key making
	 * it through. Input state was latched by the application's pre-tick
	 * pump; tie_core reads via the host vtable.
	 *
	 * Pull model: pixels written to vesa_buff_gbl reach screen at the
	 * next end-of-tick when the application pulls TieClassicFramebuffer_Current().
	 * The "wait for a key" loops that previously needed a mid-tick
	 * flush (goals_missiongoalsroom, etc.) now yield via the task
	 * stack — a fresh TieRuntime_Tick fires, the application pulls and uploads,
	 * then the next tick polls input again. */

	mousebuttons = 0;
	joybuttons = 0;
	joystickx = 0;
	joysticky = 0;
	joystickroll = 0;
	joystickthrottle = 0;

	joystickcount = ljoy_Joystick_Init();
	joystickflag = (joystickcount != 0);
	if (joystickflag) {
		/* Sample the complete host axis range; the mapping picks any 4 as
		 * logical yaw/pitch/roll/throttle. A negative axis index in
		 * the mapping disables that channel. Hosts that publish
		 * fewer physical axes leave the trailing slots at zero. */
		int16_t raw[TIE_INPUT_AXIS_MAX] = { 0 };
		joybuttons = ljoy_Joystick_Read_Axes(raw, TIE_INPUT_AXIS_MAX, 0);
		const TieInputMapping* mapping = TieInput_Mapping();
		joystickx = TieInput_MapAxis(raw, TIE_INPUT_AXIS_MAX, mapping->axes[TIE_INPUT_AXIS_YAW]);
		joysticky = TieInput_MapAxis(raw, TIE_INPUT_AXIS_MAX, mapping->axes[TIE_INPUT_AXIS_PITCH]);
		joystickroll = TieInput_MapAxis(raw, TIE_INPUT_AXIS_MAX, mapping->axes[TIE_INPUT_AXIS_ROLL]);
		joystickthrottle =
			TieInput_MapAxis(raw, TIE_INPUT_AXIS_MAX, mapping->axes[TIE_INPUT_AXIS_THROTTLE_RATE]);
	}

	if (mouseflag) {
		mousebuttons = mouse2_readmouse(&mousex, &mousey);
		mouse2_deltamouse(&deltamx, &deltamy);

		/* The binary was tuned for DOS INT 33h, which returned per-frame
		 * motion in mickeys (~200/inch on the era's hardware) at ~30 FPS.
		 * Our SDL platform returns pixel deltas at the host's frame rate,
		 * which on a 60 FPS / 96 DPI display lands at roughly 1/4 the
		 * per-frame magnitude DOS would have produced for the same hand
		 * motion. Without compensation, the unsigned-cast slew math in
		 * USER_inputforplane gets fed values an order of magnitude too
		 * small and the ship barely banks. The ×4 compensation now lives
		 * in the application's mouse-motion handler, applied
		 * BEFORE the float→int floor so finger jitter below 1/4 px stays
		 * in the fractional carry instead of producing burst pops that
		 * perturb the steering slew. mouse_dx_acc therefore reaches
		 * feinput already in 1/4-pixel units; we only re-clamp here. */

		/* Clamp delta to ±191 / ±127 (matches binary's saturation cap;
		 * inputdeltax = deltamx << 7 must stay within int16). */
		int32_t sx = (int32_t)deltamx;
		int32_t sy = (int32_t)deltamy;
		if (sx < -191)
			sx = -191;
		if (sx > 191)
			sx = 191;
		if (sy < -127)
			sy = -127;
		if (sy > 127)
			sy = 127;
		deltamx = (int16_t)sx;
		deltamy = (int16_t)sy;
	}

	keypress = 0;
	if (TieInput_KeyPending()) {
		keypress = TieInput_ReadKey();
		if (!keypress) {
			/* Extended scan code */
			uint16_t scan = TieInput_ReadKey();

			/* Arrows and ctrl-arrows collapse to 1-4 (left/right/up/down);
			 * any other extended scan code is returned as scan+128. Matches
			 * FEINPUT_getrawinput at retail 0x233ac. */
			if (scan == 0x48 || scan == 0x8D) /* Up / Ctrl-Up */
				keypress = 3;
			else if (scan == 0x4B || scan == 0x73) /* Left / Ctrl-Left */
				keypress = 1;
			else if (scan == 0x4D || scan == 0x74) /* Right / Ctrl-Right */
				keypress = 2;
			else if (scan == 0x50 || scan == 0x91) /* Down / Ctrl-Down */
				keypress = 4;
			else
				keypress = scan + 128;
		}
	}

	return keypress;
}

/* --- Graphics setup --- */

// FUNCTION: TIE 0x23544, TIE98 0x41D340
void feinput_setupgraphics(uint8_t detail_level) {
	uint8_t mode;

	if (flightResolution == TIE_FLIGHT_RES_SVGA)
		mode = 1;
	else if (flightResolution == TIE_FLIGHT_RES_SVGA_16 || flightResolution == TIE_FLIGHT_RES_SVGA_D3D)
		mode = 2;
	else
		mode = 0;

	graphicsmode = mode;
	buffer256flag = 1;
	feinput_SetGraphicsPtrs(mode);
	detaillevel = detail_level;
}

// FUNCTION: TIE 0x2359C
void feinput_SetGraphicsPtrs(uint8_t mode) {
	/* graphroutines holds 13 function pointers per graphics mode.
	 * Mode 0 = VGA 320x200, mode 1 = SVGA 640x480, mode 2 = SVGA 640x480 RGB. */
	void** table = &graphroutines[13 * mode];

	initgraph = table[0];
	blank = (ScreenFunc)table[1];
	unblank = (ScreenFunc)table[2];
	buildpalette = (void (*)(const uint8_t*, uint16_t, uint16_t))table[3];
	savepalette = table[4];
	restorepalette = table[5];
	calcposition = (uint32_t (*)(uint16_t, uint16_t))table[6];
	drawshape = (void (*)(const void*, int16_t, int16_t, int16_t, uint16_t))table[7];
	outchar = (OutCharFunc)table[8];
	clearwindow = (ClearWindowFunc)table[9];
	fillbox = (void (*)(uint16_t, uint16_t, uint16_t, uint16_t))table[10];
	savebox = table[11];
	restorebox = table[12];

	logbuf2_graphsetup();
}
