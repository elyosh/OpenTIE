#ifndef TIE_FCALLBK_H
#define TIE_FCALLBK_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FCALLBK -- iMUSE trigger callback bridge for the front-end music engine.
 *
 * CbDoCallback runs when a playing track hits its end-marker; it
 * crossfades or defers into the next track and saves/restores per-channel
 * volume state across the transition. CbSetChannels remaps channel
 * volumes from attributes[0] (buildup level). CbInitialize seeds the
 * channel-volume cache.
 */

/* iMUSE MIDI channels whose volumes follow the buildup level. */
#define FCALLBK_NUM_CHANNELS 16

void fcallbk_CbInitialize(void);
int fcallbk_CbDoCallback(int marker_type);
#if !defined(TIE_MODERN) && !defined(TIE98)
/* The iMUSE engine calls trigger callbacks with the marker and the
 * trigger's arguments on the stack. The original takes the marker from the
 * stack and pops it itself, keeping the default name. */
// clang-format off
#pragma aux fcallbk_CbDoCallback parm routine [];
// clang-format on
#endif
void fcallbk_CbSetChannels(void);

#ifdef __cplusplus
}
#endif

#endif
