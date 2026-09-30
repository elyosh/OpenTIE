#include "tie_runtime/display/dos_bios.h"

/* The linear host framebuffer has no VESA banking or logical scan-line
 * registers; accept the request and report success. */
int int386(int int_no, const void* inregs, void* outregs) {
	(void)int_no;
	(void)inregs;
	(void)outregs;
	return 0;
}
