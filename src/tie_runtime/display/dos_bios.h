#ifndef TIE_RUNTIME_DISPLAY_DOS_BIOS_H
#define TIE_RUNTIME_DISPLAY_DOS_BIOS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Host stand-in for the Watcom CRT int386 call used by the recovered
 * RTSVGA2 VESA BIOS requests (INT 10h AX=4F05h/4F06h). The host
 * framebuffer is linear, so the BIOS request has no effect. */
int int386(int int_no, const void* inregs, void* outregs);

#ifdef __cplusplus
}
#endif

#endif
