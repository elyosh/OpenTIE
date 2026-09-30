#ifndef TIE_RUNTIME_FLIGHT_SCREEN_H
#define TIE_RUNTIME_FLIGHT_SCREEN_H

/* Full-screen presentation nested inside a flight scene. The information-room
 * values follow USER_inflightinfo's seven-page carousel, shifted by one so
 * zero remains the normal in-flight view. */
typedef enum TieFlightScreen {
	TIE_FLIGHT_SCREEN_NORMAL = 0,
	TIE_FLIGHT_SCREEN_GOALS = 1,
	TIE_FLIGHT_SCREEN_MAP = 2,
	TIE_FLIGHT_SCREEN_MESSAGES = 3,
	TIE_FLIGHT_SCREEN_DAMAGE = 4,
	TIE_FLIGHT_SCREEN_WINGMEN = 5,
	TIE_FLIGHT_SCREEN_HELP = 6,
	TIE_FLIGHT_SCREEN_OPTIONS = 7,
	TIE_FLIGHT_SCREEN_REPLAY_PROMPT = 8,
	TIE_FLIGHT_SCREEN_REPLAY_VIEWER = 9,
} TieFlightScreen;

#ifdef __cplusplus
extern "C" {
#endif

/* Tracks the currently presented full-screen flight UI. Tasks save the
 * returned value and restore it when they pop so nested screens compose. */
void TieFlightScreen_Reset(void);
TieFlightScreen TieFlightScreen_Active(void);
TieFlightScreen TieFlightScreen_SetActive(TieFlightScreen screen);

#ifdef __cplusplus
}
#endif

#endif /* TIE_RUNTIME_FLIGHT_SCREEN_H */
