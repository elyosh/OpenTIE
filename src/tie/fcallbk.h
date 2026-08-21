/*
 * FCALLBK -- iMUSE trigger callback bridge for the front-end music engine.
 *
 * CbDoCallback runs when a playing track hits its end-marker; it
 * crossfades or defers into the next track and saves/restores per-channel
 * volume state across the transition. CbSetChannels remaps channel
 * volumes from attributes[0] (buildup level). CbInitialize seeds the
 * channel-volume cache.
 */

#ifndef __FCALLBK_H__
#define __FCALLBK_H__

void fcallbk_CbInitialize(void);
int fcallbk_CbDoCallback(int marker_type);
int fcallbk_CbSetChannels(void);

#endif /* __FCALLBK_H__ */
